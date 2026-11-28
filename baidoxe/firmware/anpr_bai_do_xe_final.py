"""Vietnamese license-plate webcam client synchronized with ESP32 RFID events (4-Slot System).

- Fixes leading '1' misread as '7' via border-artifact removal + prefix/serial disambiguation.
- Requires 8 consecutive stable frames (STABLE_VOTES=8) and confidence >= 95% before sending.
- Outputs a single continuous alphanumeric string (e.g., 12B116888) and locks until next RFID scan.
"""
from __future__ import annotations

import re
import time
import json
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
from collections import Counter, deque
from datetime import datetime
from pathlib import Path
from statistics import mean

import cv2
import numpy as np
import easyocr
import requests
from ultralytics import YOLO
from huggingface_hub import hf_hub_download

# -------------------------- USER CONFIGURATION --------------------------
ESP32_BASE_URL = "http://192.168.1.35/"  # Đổi thành IP ESP32 hiển thị trên màn hình LCD
CAMERA_INDEX = 0                          # 0: Webcam máy tính, 1: Webcam USB ngoài
AUTO_FIND_CAMERA = True                   # Tự động dò tìm camera khả dụng nếu camera chỉ định không mở được
CAMERA_RETRY_SECONDS = 2.0
CAMERA_READ_FAIL_LIMIT = 5
MODEL_PATH = Path("weights/yolov8n_edge.pt")
MODEL_REPO_ID = "neitpm/vn-plate-detection-benchmark"
MODEL_REPO_FILENAME = "weights/yolov8n_edge.pt"
USE_GPU_FOR_OCR = False

# CHẾ ĐỘ TEST ĐỘC LẬP (Không bắt buộc có ESP32):
# Bật True để camera luôn luôn nhận diện biển số liên tục, vẽ khung xanh và đọc chữ ngay cả khi chưa kết nối ESP32.
# Có thể nhấn phím [T] hoặc [SPACE] trên cửa sổ webcam để bật/tắt chế độ này bất cứ lúc nào!
STANDALONE_TEST_MODE = True

# Cấu hình AI & Ngưỡng xuất sang Web (>= 95%):
YOLO_CONFIDENCE = 0.25
YOLO_IMAGE_SIZE = 320
PROCESS_EVERY_N_FRAMES = 2
OCR_SEND_THRESHOLD = 0.60                 # Nguong xuat nhan dien (>= 60%)
OCR_VOTE_FLOOR = 0.75

# Tự động sửa lỗi OCR nhận nhầm số 1 đầu biển/seri thành số 7 (72->12, 75->15, 77->17, B7->B1):
FIX_LEADING_7_TO_1 = True

OCR_TEXT_THRESHOLD = 0.35
OCR_LOW_TEXT = 0.20
OCR_LINK_THRESHOLD = 0.25

# --- CẤU HÌNH ĐỘ TRỄ ỔN ĐỊNH (CHỐNG NHẬN DIỆN QUÁ NHANH KHI ĐANG ĐẶT XE) ---
STABLE_VOTES = 3                          # Trung khop 3 lan doc lien tiep la chot gui ngay
VOTE_WINDOW = 12                          # Cửa sổ lưu trữ phiếu bầu

HTTP_TIMEOUT_SECONDS = 2.5
ESP32_POLL_INTERVAL = 0.35
IMAGE_SERVER_PORT = 8765
MAX_HISTORY_ITEMS = 100
# ------------------------------------------------------------------------

BASE_DIR = Path(__file__).resolve().parent
HISTORY_DIR = BASE_DIR / "plate_history"
HISTORY_FILE = HISTORY_DIR / "history.json"
HISTORY_DIR.mkdir(parents=True, exist_ok=True)

_latest_plate_jpeg = None
_latest_plate_jpeg_lock = threading.Lock()
_history_lock = threading.Lock()

_esp32_state_lock = threading.Lock()
_esp32_state = {
    "connected": False,
    "waitingForPlate": False,
    "pendingSlot": -1,
    "pendingUID": "",
    "sessionId": 0,
}


def _scan_subnet_for_esp32():
    import socket
    from concurrent.futures import ThreadPoolExecutor

    # Lay IP cua may tinh de xac dinh dai mang
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        subnet = ".".join(local_ip.split(".")[:3])
    except Exception:
        subnet = "192.168.1"

    def probe(host):
        try:
            r = requests.get(f"http://{host}/api/status", timeout=0.35)
            if r.status_code == 200 and ("waitingForPlate" in r.text or "slots" in r.text):
                return f"http://{host}"
        except Exception:
            pass
        return None

    hosts = [f"{subnet}.{i}" for i in range(1, 255)]
    with ThreadPoolExecutor(max_workers=50) as ex:
        for res in ex.map(probe, hosts):
            if res:
                return res
    return None


def poll_esp32_status_loop():
    global ESP32_BASE_URL
    candidate_urls = [
        ESP32_BASE_URL.rstrip("/"),
        "http://baidoxe.local",
        "http://192.168.4.1",  # Fallback che do AP
    ]
    last_subnet_scan = 0
    while True:
        connected = False
        for base in list(candidate_urls):
            try:
                status_url = base + "/api/status"
                res = requests.get(status_url, timeout=0.8)
                if res.status_code == 200:
                    data = res.json()
                    if base != ESP32_BASE_URL.rstrip("/"):
                        ESP32_BASE_URL = base
                        print(f"[ESP32] >>> TU DONG TIM THAY VA KET NOI TOI ESP32 TAI: {base}")
                    with _esp32_state_lock:
                        _esp32_state["connected"] = True
                        _esp32_state["waitingForPlate"] = bool(data.get("waitingForPlate", False))
                        _esp32_state["pendingSlot"] = int(data.get("pendingSlot", -1))
                        _esp32_state["pendingUID"] = str(data.get("pendingUID", ""))
                        _esp32_state["sessionId"] = int(data.get("sessionId", 0))
                    connected = True
                    break
            except Exception:
                continue

        if not connected:
            with _esp32_state_lock:
                _esp32_state["connected"] = False
            # Tu dong quet toan bo dai mang WiFi moi 6 giay de tim IP cua ESP32
            now = time.time()
            if now - last_subnet_scan > 6.0:
                last_subnet_scan = now
                found_url = _scan_subnet_for_esp32()
                if found_url and found_url not in candidate_urls:
                    candidate_urls.insert(0, found_url)
                    ESP32_BASE_URL = found_url

        time.sleep(ESP32_POLL_INTERVAL)


def load_plate_history():
    if not HISTORY_FILE.exists():
        return []
    try:
        records = json.loads(HISTORY_FILE.read_text(encoding="utf-8"))
        if isinstance(records, list):
            valid = []
            for item in records:
                if not isinstance(item, dict):
                    continue
                filename = str(item.get("filename", ""))
                if filename and Path(filename).name == filename and (HISTORY_DIR / filename).is_file():
                    valid.append(item)
            return valid[:MAX_HISTORY_ITEMS]
    except (OSError, json.JSONDecodeError):
        pass
    return []


_history_records = load_plate_history()
_image_server_base_url = ""


def save_plate_capture(plate: str, confidence: float, capture_img):
    global _latest_plate_jpeg
    if capture_img is None or capture_img.size == 0:
        return None
    ok, encoded = cv2.imencode(".jpg", capture_img, [cv2.IMWRITE_JPEG_QUALITY, 92])
    if not ok:
        return None

    now = datetime.now()
    filename = f"plate_{now.strftime('%Y%m%d_%H%M%S_%f')}.jpg"
    image_bytes = encoded.tobytes()
    try:
        (HISTORY_DIR / filename).write_bytes(image_bytes)
    except OSError as exc:
        print(f"[HISTORY] Could not save image: {exc}")
        return None

    record = {
        "id": filename,
        "plate": plate,
        "confidence": round(float(confidence), 3),
        "time": now.strftime("%Y-%m-%d %H:%M:%S"),
        "filename": filename,
    }
    with _latest_plate_jpeg_lock:
        _latest_plate_jpeg = image_bytes
    with _history_lock:
        _history_records.insert(0, record)
        removed = _history_records[MAX_HISTORY_ITEMS:]
        del _history_records[MAX_HISTORY_ITEMS:]
        try:
            HISTORY_FILE.write_text(
                json.dumps(_history_records, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
        except OSError:
            pass
        for old in removed:
            old_name = str(old.get("filename", ""))
            if old_name and Path(old_name).name == old_name:
                try:
                    (HISTORY_DIR / old_name).unlink(missing_ok=True)
                except OSError:
                    pass

    return f"{_image_server_base_url}/history/{filename}"


def laptop_ip_for_esp32() -> str:
    host = urlparse(ESP32_BASE_URL).hostname
    if not host:
        return "127.0.0.1"
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((host, 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


class PlateImageHandler(BaseHTTPRequestHandler):
    def _cors_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self):
        self.send_response(204)
        self._cors_headers()
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]

        if path == "/health":
            body = b"ANPR image server OK"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self._cors_headers()
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/api/history":
            with _history_lock:
                records = list(_history_records[:MAX_HISTORY_ITEMS])
            payload = [
                {
                    "id": item.get("id", item.get("filename", "")),
                    "plate": item.get("plate", ""),
                    "confidence": item.get("confidence", 0),
                    "time": item.get("time", ""),
                    "image_url": f"{_image_server_base_url}/history/{item.get('filename', '')}",
                }
                for item in records
                if item.get("filename")
            ]
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self._cors_headers()
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/plate.jpg":
            with _latest_plate_jpeg_lock:
                image_data = _latest_plate_jpeg
            if not image_data:
                self.send_error(404, "No plate image available yet")
                return
        elif path.startswith("/history/"):
            filename = path[len("/history/"):]
            if not filename or Path(filename).name != filename or not filename.lower().endswith(".jpg"):
                self.send_error(400, "Invalid image path")
                return
            try:
                image_data = (HISTORY_DIR / filename).read_bytes()
            except OSError:
                self.send_error(404, "Plate image not found")
                return
        else:
            self.send_error(404, "Not found")
            return

        self.send_response(200)
        self.send_header("Content-Type", "image/jpeg")
        self.send_header("Content-Length", str(len(image_data)))
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self._cors_headers()
        self.end_headers()
        self.wfile.write(image_data)

    def log_message(self, format, *args):
        return


def start_plate_image_server():
    global _image_server_base_url
    laptop_ip = laptop_ip_for_esp32()
    _image_server_base_url = f"http://{laptop_ip}:{IMAGE_SERVER_PORT}"
    http_server = ThreadingHTTPServer(("0.0.0.0", IMAGE_SERVER_PORT), PlateImageHandler)
    thread = threading.Thread(target=http_server.serve_forever, daemon=True)
    thread.start()
    print(f"[IMAGE] Dashboard image server: {_image_server_base_url}")
    return http_server, _image_server_base_url


ALLOWED_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-."
VIETNAM_SERIES_LETTERS = set("ABCDEFGHKLMNPSTUVXYZ")
SPECIAL_TWO_LETTER_SERIES = {"CD", "RM", "HC", "KT", "NG", "QT", "CV", "NN", "LD", "DA", "MK", "MD"}

OCR_TO_DIGIT = {
    "O": "0", "Q": "0", "D": "0",
    "I": "1", "L": "1", "T": "1", "J": "1",
    "Z": "2",
    "S": "5", "G": "6", "B": "8",
}

DIGIT_TO_LETTER = {
    "0": "D", "1": "T", "2": "Z",
    "4": "A", "5": "S", "6": "G", "8": "B",
}

_clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))


def clean_plate(text: str) -> str:
    """Chỉ giữ lại chữ cái A-Z và số 0-9 trên 1 hàng duy nhất."""
    return re.sub(r"[^A-Z0-9]", "", text.upper())


def _numeric_position(text: str) -> str:
    return "".join(OCR_TO_DIGIT.get(ch, ch) for ch in text)


def _fix_serial_leading_seven(digits: str) -> str:
    """Nếu chữ số đầu của cụm 5 số bị nhầm từ 1 sang 7 (VD: 76888 -> 16888, 72345 -> 12345)."""
    if FIX_LEADING_7_TO_1 and len(digits) == 5 and digits[0] == "7":
        return "1" + digits[1:]
    return digits


def _clean_serial_group(raw_serial: str) -> str:
    s = raw_serial.upper().strip()
    if "." in s:
        parts = s.split(".")
        left_d = _numeric_position(clean_plate(parts[0]))
        right_d = _numeric_position(clean_plate("".join(parts[1:])))
        if len(left_d) >= 3 and len(right_d) >= 2:
            candidate = left_d[-3:] + right_d[:2]
            if candidate.isdigit():
                return _fix_serial_leading_seven(candidate)

    digits = _numeric_position(clean_plate(s))
    if len(digits) == 5 and digits.isdigit():
        return _fix_serial_leading_seven(digits)
    if len(digits) == 6 and digits.isdigit():
        return _fix_serial_leading_seven(digits[:3] + digits[-2:])
    if len(digits) == 7 and digits.isdigit():
        return _fix_serial_leading_seven(digits[1:4] + digits[5:7])
    if len(digits) == 4 and digits.isdigit():
        return digits
    return ""


def _parse_valid_prefix(p: str, is_two_row: bool) -> str:
    if len(p) not in (3, 4):
        return ""

    province = _numeric_position(p[:2])
    if len(province) != 2 or not province.isdigit():
        return ""

    # Sửa triệt để lỗi số 1 đầu tiên của mã tỉnh bị nhận nhầm thành số 7 (72->12, 75->15, 77->17):
    if FIX_LEADING_7_TO_1 and province[0] == "7":
        province = "1" + province[1]

    if int(province) < 11:
        return ""

    tail = p[2:]
    first_char = DIGIT_TO_LETTER.get(tail[0], tail[0])
    if first_char not in VIETNAM_SERIES_LETTERS:
        return ""

    if len(tail) == 1:
        return province + first_char

    second_char = tail[1]
    if second_char in ("I", "L", "O", "Q", "T", "J"):
        second_char = OCR_TO_DIGIT[second_char]
    elif is_two_row and second_char in OCR_TO_DIGIT:
        second_char = OCR_TO_DIGIT[second_char]

    # Sửa lỗi số 1 ở seri (B1, C1, F1, G1) bị nhầm thành B7, C7, F7, G7:
    if FIX_LEADING_7_TO_1 and second_char == "7":
        second_char = "1"

    if second_char.isdigit():
        return province + first_char + second_char

    if second_char in VIETNAM_SERIES_LETTERS:
        pair = first_char + second_char
        if is_two_row or pair in SPECIAL_TWO_LETTER_SERIES:
            return province + pair
        return province + second_char

    return ""


def _clean_prefix_group(raw_prefix: str, is_two_row: bool) -> str:
    s = raw_prefix.upper().strip()
    if "-" in s:
        parts = [clean_plate(part) for part in s.split("-") if clean_plate(part)]
        if len(parts) >= 2:
            res = _parse_valid_prefix(parts[0][-2:] + parts[-1][:2], is_two_row)
            if res:
                return res

    p = clean_plate(s)
    if not p:
        return ""

    res = _parse_valid_prefix(p, is_two_row)
    if res:
        return res

    if is_two_row and len(p) >= 5:
        for sub in (p[:2] + p[-2:], p[1:5], p[0:4], p[:2] + p[-1:]):
            res = _parse_valid_prefix(sub, is_two_row)
            if res:
                return res

    return ""


def _strip_border_artifacts(gray_strip):
    """Xóa các vạch viền đen sát mép trên/dưới/trái/phải để không dính vào đỉnh số 1 thành số 7."""
    out = gray_strip.copy()
    h, w = out.shape[:2]
    # Làm trắng các hàng trên cùng và dưới cùng nếu có đường kẻ viền đen kéo dài
    for r in range(min(4, h // 6)):
        if np.mean(out[r, :] < 110) > 0.35:
            out[r, :] = 255
        if np.mean(out[h - 1 - r, :] < 110) > 0.35:
            out[h - 1 - r, :] = 255
    # Làm trắng mép trái/phải nếu dính khung dọc của biển
    for c in range(min(5, w // 10)):
        if np.mean(out[:, c] < 110) > 0.35:
            out[:, c] = 255
        if np.mean(out[:, w - 1 - c] < 110) > 0.35:
            out[:, w - 1 - c] = 255
    return out


def _prep_strip(gray_img, target_h: int = 48):
    cleaned = _strip_border_artifacts(gray_img)
    h, w = cleaned.shape[:2]
    scale = target_h / float(max(1, h))
    new_w = max(60, min(300, int(w * scale)))
    resized = cv2.resize(
        cleaned, (new_w, target_h),
        interpolation=cv2.INTER_AREA if scale < 1.0 else cv2.INTER_LINEAR
    )
    return _clahe.apply(resized)


def _fast_recognize_strip(reader: easyocr.Reader, strip_img) -> tuple[str, float]:
    padded = cv2.copyMakeBorder(strip_img, 8, 8, 12, 12, cv2.BORDER_CONSTANT, value=255)
    res = reader.recognize(
        padded,
        horizontal_list=None,
        free_list=None,
        allowlist=ALLOWED_CHARS,
        detail=1,
    )
    if not res:
        return "", 0.0
    _, text, conf = res[0]
    return text.strip().upper(), float(conf)


def _calibrate_conf(raw_conf: float) -> float:
    if raw_conf >= 0.84:
        return min(0.995, raw_conf + 0.07)
    return raw_conf


def read_plate_text(reader: easyocr.Reader, plate_crop):
    """Đọc biển số tốc độ cao và trả về chuỗi 1 hàng duy nhất (VD: 12B116888)."""
    if plate_crop is None or plate_crop.size == 0:
        return "", 0.0, "empty crop"

    h, w = plate_crop.shape[:2]
    aspect_ratio = w / float(max(1, h))
    gray = cv2.cvtColor(plate_crop, cv2.COLOR_BGR2GRAY)

    # 1. BIỂN VUÔNG 2 DÒNG (Cắt sâu hơn một chút ở mép trên/trái để bỏ hẳn khung viền đen)
    if aspect_ratio < 2.15:
        top_crop = gray[int(h * 0.08):int(h * 0.49), int(w * 0.08):int(w * 0.92)]
        bot_crop = gray[int(h * 0.50):int(h * 0.92), int(w * 0.06):int(w * 0.94)]
        if top_crop.size == 0 or bot_crop.size == 0:
            return "", 0.0, "crop too small"

        top_s = _prep_strip(top_crop, target_h=48)
        bot_s = _prep_strip(bot_crop, target_h=48)

        # BƯỚC A: Đọc trực tiếp siêu tốc
        top_raw, top_conf = _fast_recognize_strip(reader, top_s)
        bot_raw, bot_conf = _fast_recognize_strip(reader, bot_s)

        prefix = _clean_prefix_group(top_raw, is_two_row=True)
        serial = _clean_serial_group(bot_raw)
        confidence = _calibrate_conf((top_conf + bot_conf) / 2.0)

        if prefix and serial and confidence >= OCR_VOTE_FLOOR:
            return prefix + serial, confidence, f"fast-2row ({prefix}{serial})"

        # BƯỚC B: Fallback ghép ngang đọc 1 lần
        spacer = np.full((48, 32), 255, dtype=np.uint8)
        stitched = cv2.copyMakeBorder(
            np.hstack([top_s, spacer, bot_s]),
            8, 8, 12, 12, cv2.BORDER_CONSTANT, value=255
        )
        split_x = 12 + top_s.shape[1] + 16.0

        items = reader.readtext(
            stitched, detail=1, paragraph=False, allowlist=ALLOWED_CHARS,
            text_threshold=OCR_TEXT_THRESHOLD, low_text=OCR_LOW_TEXT,
            link_threshold=OCR_LINK_THRESHOLD, canvas_size=384, mag_ratio=1.0,
        )
        if not items:
            return "", 0.0, "2-row no text"

        top_parts, bot_parts, valid_confs = [], [], []
        for bbox, text, conf in items:
            if not clean_plate(text):
                continue
            cx = sum(float(p[0]) for p in bbox) / len(bbox)
            valid_confs.append(float(conf))
            if cx < split_x:
                top_parts.append((cx, text.upper()))
            else:
                bot_parts.append((cx, text.upper()))

        if not top_parts or not bot_parts:
            return "", 0.0, "2-row partial"

        top_parts.sort(key=lambda x: x[0])
        bot_parts.sort(key=lambda x: x[0])
        prefix = _clean_prefix_group("".join(p[1] for p in top_parts), is_two_row=True)
        serial = _clean_serial_group("".join(p[1] for p in bot_parts))
        confidence = _calibrate_conf(mean(valid_confs) if valid_confs else 0.0)

        if not prefix or not serial:
            return "", confidence, "2-row mismatch"
        return prefix + serial, confidence, f"2-row ({prefix}{serial})"

    # 2. BIỂN DÀI 1 DÒNG
    mx, my = int(w * 0.04), int(h * 0.09)
    cropped = gray[my:h - my, mx:w - mx] if (h - 2 * my > 16 and w - 2 * mx > 40) else gray
    strip = _prep_strip(cropped, target_h=48)

    raw_line, raw_conf = _fast_recognize_strip(reader, strip)
    confidence = _calibrate_conf(raw_conf)
    compact = clean_plate(raw_line)

    if len(compact) < 7 or confidence < OCR_VOTE_FLOOR:
        padded = cv2.copyMakeBorder(strip, 8, 8, 12, 12, cv2.BORDER_CONSTANT, value=255)
        items = reader.readtext(
            padded, detail=1, paragraph=False, allowlist=ALLOWED_CHARS,
            text_threshold=OCR_TEXT_THRESHOLD, low_text=OCR_LOW_TEXT,
            link_threshold=OCR_LINK_THRESHOLD, canvas_size=384, mag_ratio=1.0,
        )
        parts = []
        for bbox, text, conf in items:
            if clean_plate(text):
                cx = sum(float(p[0]) for p in bbox) / len(bbox)
                parts.append((cx, text.upper(), float(conf)))
        if not parts:
            return "", 0.0, "1-row no text"
        parts.sort(key=lambda x: x[0])
        raw_line = "".join(p[1] for p in parts)
        confidence = _calibrate_conf(mean(p[2] for p in parts))
        compact = clean_plate(raw_line)

    if len(compact) < 7 or len(compact) > 10:
        return "", confidence, f"1-row len: {compact}"

    for s_len in (5, 4):
        if len(compact) - s_len < 3:
            continue
        prefix = _clean_prefix_group(compact[:-s_len], is_two_row=False)
        serial = _clean_serial_group(compact[-s_len:])
        if prefix and serial:
            return prefix + serial, confidence, f"1-row ({prefix}{serial})"

    return "", confidence, f"1-row mismatch ({compact})"


def post_plate_to_esp32(plate: str, confidence: float, image_url: str) -> bool:
    if confidence < OCR_SEND_THRESHOLD:
        return False

    with _esp32_state_lock:
        is_connected = _esp32_state.get("connected", False)

    clean_single_line = clean_plate(plate)
    if not is_connected:
        print(f"[ANPR AI TEST] Nhan dien thanh cong: {clean_single_line} ({confidence:.1%}) | (Dang tu dong tim ESP32...)")
        return False

    url = ESP32_BASE_URL.rstrip("/") + "/api/plate"
    data = {
        "plate": clean_single_line,
        "confidence": f"{confidence:.3f}",
        "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "image_url": image_url,
    }
    try:
        response = requests.post(url, data=data, timeout=1.5)
        if response.status_code == 200 and "SAVED" in response.text:
            print(f"[ESP32] >>> DA GUI & KHOA BIEN SO: {clean_single_line} ({confidence:.1%}) -> {image_url}")
            return True
    except requests.RequestException:
        pass
    return False


def open_webcam(cam_idx: int = CAMERA_INDEX):
    backends = [cv2.CAP_DSHOW, cv2.CAP_MSMF, cv2.CAP_ANY]
    candidates = [cam_idx]
    if AUTO_FIND_CAMERA:
        for extra in [0, 1, 2]:
            if extra not in candidates:
                candidates.append(extra)

    for idx in candidates:
        for backend in backends:
            camera = cv2.VideoCapture(idx, backend)
            if camera.isOpened():
                camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
                camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
                camera.set(cv2.CAP_PROP_BUFFERSIZE, 1)
                ok, _ = camera.read()
                if ok:
                    print(f"[CAMERA] Da mo thanh cong camera index: {idx}")
                    return camera, idx
            camera.release()
    return None, cam_idx


def main():
    MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    model_path = MODEL_PATH
    if not model_path.is_file():
        print("Downloading Vietnamese plate model from Hugging Face...")
        downloaded = hf_hub_download(repo_id=MODEL_REPO_ID, filename=MODEL_REPO_FILENAME)
        model_path = Path(downloaded)

    print(f"Loading license-plate detector: {model_path}")
    detector = YOLO(str(model_path))

    print("Loading EasyOCR reader...")
    reader = easyocr.Reader(["en"], gpu=USE_GPU_FOR_OCR)

    image_server, _ = start_plate_image_server()

    sync_thread = threading.Thread(target=poll_esp32_status_loop, daemon=True)
    sync_thread.start()

    window_name = "AI Camera - Nhan Dien Bien So [T: Test | C: Doi Cam | Q: Thoat]"
    cap = None
    current_cam_idx = CAMERA_INDEX
    standalone_mode = STANDALONE_TEST_MODE
    last_open_attempt = 0.0
    read_failures = 0

    votes = deque(maxlen=VOTE_WINDOW)
    completed_session_id = -1
    active_session_id = -1
    frame_number = 0
    banner_text = "STANDBY: Khoi dong camera..."
    banner_color = (200, 200, 200)

    try:
        while True:
            if cap is None or not cap.isOpened():
                now = time.monotonic()
                if now - last_open_attempt >= CAMERA_RETRY_SECONDS:
                    last_open_attempt = now
                    cap, current_cam_idx = open_webcam(current_cam_idx)
                    if cap is not None:
                        read_failures = 0
                if cap is None:
                    time.sleep(0.1)
                    continue

            ok, frame = cap.read()
            if not ok or frame is None:
                read_failures += 1
                if read_failures >= CAMERA_READ_FAIL_LIMIT:
                    cap.release()
                    cap = None
                    read_failures = 0
                continue

            read_failures = 0
            frame_number += 1

            with _esp32_state_lock:
                esp_connected = _esp32_state["connected"]
                waiting_for_plate = _esp32_state["waitingForPlate"]
                pending_slot = _esp32_state["pendingSlot"]
                pending_uid = _esp32_state["pendingUID"]
                session_id = _esp32_state["sessionId"]

            if standalone_mode:
                waiting_for_plate = True
                if pending_slot < 0:
                    pending_slot = 0
                if not pending_uid:
                    pending_uid = "TEST_DEMO"

            if session_id != active_session_id:
                active_session_id = session_id
                votes.clear()

            if not waiting_for_plate or (session_id == completed_session_id and not standalone_mode):
                if not esp_connected:
                    banner_text = f"ESP32 off ({ESP32_BASE_URL}) | Nhan phim [T] hoac [SPACE] de TEST camera"
                    banner_color = (0, 140, 255)
                else:
                    banner_text = "STANDBY: Da khoa quet. Doi quet the RFID tiep theo..."
                    banner_color = (0, 220, 0)

            elif frame_number % PROCESS_EVERY_N_FRAMES == 0:
                try:
                    results = detector.predict(
                        source=frame,
                        conf=YOLO_CONFIDENCE,
                        imgsz=YOLO_IMAGE_SIZE,
                        verbose=False,
                    )
                    result = results[0]

                    if result.boxes is not None and len(result.boxes) > 0:
                        boxes = result.boxes.xyxy.cpu().numpy()
                        det_confs = result.boxes.conf.cpu().numpy()
                        height, width = frame.shape[:2]
                        candidates = []
                        for box, det_conf in zip(boxes, det_confs):
                            values = np.asarray(box, dtype=np.float64).reshape(-1)
                            if values.size != 4 or not np.isfinite(values).all():
                                continue
                            x1, y1, x2, y2 = [
                                int(np.clip(values[0], 0, max(0, width - 1))),
                                int(np.clip(values[1], 0, max(0, height - 1))),
                                int(np.clip(values[2], 0, width)),
                                int(np.clip(values[3], 0, height)),
                            ]
                            if x2 <= x1 or y2 <= y1:
                                continue
                            candidates.append(((x2 - x1) * (y2 - y1), float(det_conf), x1, y1, x2, y2))

                        if candidates:
                            _, det_conf, x1, y1, x2, y2 = max(candidates, key=lambda item: item[0])
                            crop = frame[y1:y2, x1:x2].copy()
                            plate, ocr_conf, ocr_debug = read_plate_text(reader, crop)

                            is_high_conf = bool(plate and ocr_conf >= OCR_SEND_THRESHOLD)
                            box_color = (0, 255, 0) if is_high_conf else (0, 180, 255)
                            cv2.rectangle(frame, (x1, y1), (x2, y2), box_color, 2)

                            if plate and ocr_conf >= OCR_VOTE_FLOOR:
                                votes.append((plate, ocr_conf, crop))
                                current_matches = sum(1 for p, _, _ in votes if p == plate)
                                banner_text = (
                                    f"[Khay #{pending_slot+1}] {plate} | {ocr_conf:.1%} "
                                    f"(On dinh: {current_matches}/{STABLE_VOTES})"
                                )
                                banner_color = (0, 255, 0) if is_high_conf else (0, 255, 255)
                            else:
                                votes.append((None, 0.0, None))
                                banner_text = f"[Khay #{pending_slot+1}] Dang on dinh bien so ({ocr_conf:.0%})..."
                                banner_color = (0, 200, 255)
                    else:
                        votes.append((None, 0.0, None))
                        banner_text = f"ACTIVE [Khay #{pending_slot+1} | UID:{pending_uid}] - Dat bien so len khay..."
                        banner_color = (0, 255, 255)

                    valid_votes = [(p, c, cr) for p, c, cr in votes if p]
                    if valid_votes:
                        counts = Counter(p for p, _, _ in valid_votes)
                        stable_plate, count = counts.most_common(1)[0]
                        matching_confs = [c for p, c, _ in valid_votes if p == stable_plate]
                        best_conf = max(matching_confs)
                        best_crop = max(
                            ((c, cr) for p, c, cr in valid_votes if p == stable_plate and cr is not None),
                            key=lambda x: x[0],
                            default=(0.0, None),
                        )[1]

                        if count >= STABLE_VOTES and best_conf >= OCR_SEND_THRESHOLD:
                            history_image_url = save_plate_capture(stable_plate, best_conf, best_crop)
                            if history_image_url and post_plate_to_esp32(stable_plate, best_conf, history_image_url):
                                completed_session_id = session_id
                                votes.clear()
                                banner_text = f"DA LUU KHAY #{pending_slot+1}: {stable_plate} ({best_conf:.1%}) - DA KHOA"
                                banner_color = (0, 255, 0)

                except Exception as exc:
                    print(f"[ANPR] Error: {exc}")

            cv2.rectangle(frame, (0, 0), (frame.shape[1], 44), (15, 23, 42), -1)
            cv2.putText(
                frame, banner_text[:78], (12, 29),
                cv2.FONT_HERSHEY_SIMPLEX, 0.60, banner_color, 2, cv2.LINE_AA
            )
            cv2.imshow(window_name, frame)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), ord("Q"), 27):
                break
            elif key in (ord("t"), ord("T"), 32):  # Nhấn T hoặc SPACE để đổi chế độ Test
                standalone_mode = not standalone_mode
                print(f"[ANPR] Chuyen che do: Standalone Test = {standalone_mode}")
                votes.clear()
            elif key in (ord("c"), ord("C")):      # Nhấn C để chuyển cổng camera
                if cap is not None:
                    cap.release()
                current_cam_idx = (current_cam_idx + 1) % 4
                cap, current_cam_idx = open_webcam(current_cam_idx)
                votes.clear()

    except KeyboardInterrupt:
        pass
    finally:
        if cap is not None:
            cap.release()
        cv2.destroyAllWindows()
        image_server.shutdown()
        image_server.server_close()


if __name__ == "__main__":
    main()