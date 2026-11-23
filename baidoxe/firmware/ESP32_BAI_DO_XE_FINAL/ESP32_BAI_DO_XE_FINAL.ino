/*
 * ==============================================================================================
 * Project   : HE THONG BAI DO XE THONG MINH ESP32 (6 O DO / 3 TANG x 2 COT)
 * Bo mach   : Altium PCB Bai_Do_Xe_Thong_Minh_ESP32 (ESP32 DevKit V1 30-Pin)
 * Dong co   : 2 x Stepper NEMA 17 qua Driver A4988 (Trục X ngang, Trục Z nâng tầng)
 * Co cau    : Servo MG90S keo/day khay xe (pallet) - 180 do tieu chuan (Thu 10 do, Day 95 do)
 * Dinh vi   : 3 Cong tac hanh trinh (Home X, Min Z, Max Z) - Toa do tuyet doi tham chieu tu Home
 * Luu tru   : Preferences Flash NVS (Luu vinh vien toa do 6 o do, khong mat khi mat dien)
 * Ngoai vi  : RFID RC522, LCD I2C 1602/2004, Cam bien IR cong vao (GPIO 35), Buzzer, LED
 * Giao tiep : Web Dashboard Responsive + REST API (/api/status, /api/plate, /api/jog, /api/save_slot)
 * ==============================================================================================
 */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <Preferences.h>

// --------------------------- 1. CAU HINH CHAN GPIO (CHUAN ALTIUM PCB) -------------------------
// Khoi Dong co buoc Truc X (Nguyên ly U3 - Driver A4988)
#define PIN_STEP_X        14  // Chân 11 ESP32
#define PIN_DIR_X         12  // Chân 12 ESP32
#define PIN_LIMIT_X_HOME  16  // Chân 21 ESP32 (RX2) - Cong tac hanh trinh Home X (Goc 0 truc X)

// Khoi Dong co buoc Truc Z - Nang ha (Nguyên ly U2 - Driver A4988)
#define PIN_STEP_Z        25  // Chân 8 ESP32
#define PIN_DIR_Z         26  // Chân 9 ESP32
#define PIN_LIMIT_Z_MIN   32  // Chân 6 ESP32 - Cong tac hanh trinh day Z (Goc 0 truc Z)
#define PIN_LIMIT_Z_MAX   33  // Chân 7 ESP32 - Cong tac hanh trinh dinh Z (Gioi han an toan tren)

// Chan Enable chung cho ca 2 Driver A4988 (LOW = Bat dong co, HIGH = Tha dong co)
#define PIN_MOTORS_EN     27  // Chân 10 ESP32

// Co cau tay gat / trượt khay xe: Servo MG90S
#define PIN_SERVO_PWM     13  // Chân 13 ESP32

// Cam bien hong ngoai cong vao (IR Gate LM393) - Chốt chân 5 (GPIO 35 Input-Only)
#define PIN_IR_GATE       35  // Chân 5 ESP32

// Den bao & Coi chip
#define PIN_LED_STATUS    2   // Chân 19 ESP32
#define PIN_BUZZER        4   // Chân 20 ESP32 (Active LOW: LOW = On, HIGH = Off)

#define BUZZER_ACTIVE_LOW true

void buzzerOn() {
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_LOW ? LOW : HIGH);
}

void buzzerOff() {
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_LOW ? HIGH : LOW);
}

void beep(int count = 1, int durationMs = 80) {
  for (int i = 0; i < count; i++) {
    buzzerOn();
    delay(durationMs);
    buzzerOff();
    if (i < count - 1) delay(80);
  }
}

// RFID RC522 (Giao tiep SPI phan cung)
#define PIN_RFID_SS       5   // Chân 23 ESP32
#define PIN_RFID_RST      17  // Chân 22 ESP32 (TX2)
#define PIN_RFID_SCK      18  // Chân 24 ESP32
#define PIN_RFID_MISO     19  // Chân 25 ESP32
#define PIN_RFID_MOSI     23  // Chân 30 ESP32

// Ngoai vi
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo trayServo;
WebServer server(80);
Preferences prefs;

// --------------------------- 2. CAU HINH WI-FI ------------------------------------------------
const char* WIFI_SSID     = "VIETSET_TECH";      // WiFi của bạn
const char* WIFI_PASS     = "vs68686868";        // Mật khẩu WiFi
const char* AP_SSID       = "BaiDoXe_ESP32";     // Phát WiFi Access Point nếu WiFi chính mất
const char* AP_PASS       = "12345678";

// --------------------------- 3. TOA DO VA CAU HINH HE THONG 6 O DO XE ------------------------
#define TOTAL_SLOTS 6  // He thong 6 o do xe (3 tang x 2 cot)

struct SlotInfo {
  bool occupied;
  String uid;
  String plate;
  String parkTime;
  long stepX;                  // Toa do X tinh tu cong tac hanh trinh (so buoc)
  long stepZ;                  // Toa do Z tinh tu cong tac hanh trinh (so buoc)
};

// Toa do mac dinh ban dau (se duoc ghi de boi gia tri luu trong Preferences Flash neu co)
SlotInfo slots[TOTAL_SLOTS] = {
  {false, "", "", "",  2000,  1500}, // Slot 1: Tang 1, Cot 1
  {false, "", "", "",  6000,  1500}, // Slot 2: Tang 1, Cot 2
  {false, "", "", "",  2000,  4500}, // Slot 3: Tang 2, Cot 1
  {false, "", "", "",  6000,  4500}, // Slot 4: Tang 2, Cot 2
  {false, "", "", "",  2000,  7500}, // Slot 5: Tang 3, Cot 1
  {false, "", "", "",  6000,  7500}  // Slot 6: Tang 3, Cot 2
};

// Toa do thuc te hien tai cua ban nang (so buoc tinh tu Home cong tac hanh trinh)
long currentX = 0;
long currentZ = 0;
bool isHomed  = false;

// Goc quay Servo MG90S (180 do tieu chuan, KHONG PHAI 315 DO!)
const int SERVO_RETRACT = 10;  // Thu tay gat ve (10 do)
const int SERVO_EXTEND  = 95;  // Day khay ra / keo khay ve (95 do)

// Bien quan ly trang thai voi Camera ANPR AI
bool waitingForPlate = false;
int pendingSlot = -1;
String pendingUID = "";
int sessionId = 100;

// --------------------------- 4. DOC/GHI TOA DO VAO BO NHO FLASH (NVS) -------------------------
void loadSlotCoordinatesFromNVS() {
  prefs.begin("baidoxe", false);
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    String keyX = "sx" + String(i);
    String keyZ = "sz" + String(i);
    long defX = slots[i].stepX;
    long defZ = slots[i].stepZ;
    slots[i].stepX = prefs.getLong(keyX.c_str(), defX);
    slots[i].stepZ = prefs.getLong(keyZ.c_str(), defZ);
    Serial.printf("[NVS] Slot %d -> Toa do Flash: X = %ld, Z = %ld\n", i + 1, slots[i].stepX, slots[i].stepZ);
  }
  prefs.end();
}

void saveSlotCoordinateToNVS(int slotIdx, long x, long z) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;
  slots[slotIdx].stepX = x;
  slots[slotIdx].stepZ = z;

  prefs.begin("baidoxe", false);
  String keyX = "sx" + String(slotIdx);
  String keyZ = "sz" + String(slotIdx);
  prefs.putLong(keyX.c_str(), x);
  prefs.putLong(keyZ.c_str(), z);
  prefs.end();

  Serial.printf("[NVS-SAVE] Da luu Slot %d vao Flash: X = %ld, Z = %ld\n", slotIdx + 1, x, z);
  beep(2, 60);
}

// --------------------------- 5. DIEU KHIEN DONG CO BUOC & CONG TAC HANH TRINH ------------------
void stepPulse(int stepPin, int delayUs = 450) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(delayUs);
}

// Kiem tra trang thai cong tac hanh trinh (Active LOW voi INPUT_PULLUP)
bool isLimitXHomeHit() { return digitalRead(PIN_LIMIT_X_HOME) == LOW; }
bool isLimitZMinHit()  { return digitalRead(PIN_LIMIT_Z_MIN) == LOW; }
bool isLimitZMaxHit()  { return digitalRead(PIN_LIMIT_Z_MAX) == LOW; }

// Ham chay ve goc Home bang 2 cong tac hanh trinh (Z_MIN va X_HOME)
void homeAxes() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DANG VE HOME...");
  lcd.setCursor(0, 1); lcd.print("Cho cong tac HT");
  Serial.println("\n>>> [HOMING] BAT DAU VE GOC CHUAN BANG CONG TAC HANH TRINH <<<");

  digitalWrite(PIN_MOTORS_EN, LOW); // Bat nguon dong co

  // 1. Thu tay gat Servo ve an toan
  trayServo.write(SERVO_RETRACT);
  delay(250);

  // 2. VE HOME TRUC Z (Ha xuong cham LIMIT_Z_MIN)
  Serial.println("[HOMING Z] Ha truc Z ve LIMIT_Z_MIN...");
  digitalWrite(PIN_DIR_Z, LOW); // LOW = chieu ha xuong
  int timeoutZ = 15000;
  while (!isLimitZMinHit() && timeoutZ-- > 0) {
    stepPulse(PIN_STEP_Z, 600);
  }

  // Lui nhe len 150 buoc roi cham cham lai de tang do chinh xac
  digitalWrite(PIN_DIR_Z, HIGH);
  for (int i = 0; i < 150; i++) stepPulse(PIN_STEP_Z, 600);
  delay(50);
  digitalWrite(PIN_DIR_Z, LOW);
  timeoutZ = 600;
  while (!isLimitZMinHit() && timeoutZ-- > 0) {
    stepPulse(PIN_STEP_Z, 900); // Buoc cham
  }
  currentZ = 0;
  Serial.println("[HOMING Z OK] Z = 0 (Da cham cong tac LIMIT_Z_MIN)");

  // 3. VE HOME TRUC X (Chay sang trai cham LIMIT_X_HOME)
  Serial.println("[HOMING X] Chay truc X sang trai ve LIMIT_X_HOME...");
  digitalWrite(PIN_DIR_X, LOW); // LOW = sang trai
  int timeoutX = 15000;
  while (!isLimitXHomeHit() && timeoutX-- > 0) {
    stepPulse(PIN_STEP_X, 500);
  }

  // Lui nhe sang phai 150 buoc roi cham cham lai
  digitalWrite(PIN_DIR_X, HIGH);
  for (int i = 0; i < 150; i++) stepPulse(PIN_STEP_X, 500);
  delay(50);
  digitalWrite(PIN_DIR_X, LOW);
  timeoutX = 600;
  while (!isLimitXHomeHit() && timeoutX-- > 0) {
    stepPulse(PIN_STEP_X, 800); // Buoc cham
  }
  currentX = 0;
  Serial.println("[HOMING X OK] X = 0 (Da cham cong tac LIMIT_X_HOME)");

  isHomed = true;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("HOME THANH CONG!");
  lcd.setCursor(0, 1); lcd.print("X:0 | Z:0 San sang");
  Serial.println("[HOMING HOAN TAT] Toa do may da thiet lap: X = 0, Z = 0\n");
  beep(2, 80);
}

// Di chuyen truc X tuong doi voi bao ve an toan cong tac hanh trinh
void jogX(long steps) {
  if (steps == 0) return;
  digitalWrite(PIN_MOTORS_EN, LOW);
  bool dir = (steps > 0); // true = sang phai, false = sang trai
  digitalWrite(PIN_DIR_X, dir ? HIGH : LOW);
  long count = abs(steps);

  for (long i = 0; i < count; i++) {
    if (!dir && isLimitXHomeHit()) {
      currentX = 0;
      Serial.println("\n[CANH BAO] Cham cong tac LIMIT_X_HOME! Dat currentX = 0");
      beep(1, 150);
      return;
    }
    stepPulse(PIN_STEP_X, 450);
    currentX += (dir ? 1 : -1);
  }
  Serial.printf("[JOG X] Toa do hien tai -> X = %ld | Z = %ld\n", currentX, currentZ);
}

// Di chuyen truc Z tuong doi voi bao ve an toan cong tac hanh trinh
void jogZ(long steps) {
  if (steps == 0) return;
  digitalWrite(PIN_MOTORS_EN, LOW);
  bool dir = (steps > 0); // true = di len, false = ha xuong
  digitalWrite(PIN_DIR_Z, dir ? HIGH : LOW);
  long count = abs(steps);

  for (long i = 0; i < count; i++) {
    if (!dir && isLimitZMinHit()) {
      currentZ = 0;
      Serial.println("\n[CANH BAO] Cham cong tac LIMIT_Z_MIN! Dat currentZ = 0");
      beep(1, 150);
      return;
    }
    if (dir && isLimitZMaxHit()) {
      Serial.println("\n[CANH BAO NGUY HIEM] Cham cong tac LIMIT_Z_MAX (Dinh hanh trinh)! Dung!");
      beep(3, 100);
      return;
    }
    stepPulse(PIN_STEP_Z, 550);
    currentZ += (dir ? 1 : -1);
  }
  Serial.printf("[JOG Z] Toa do hien tai -> X = %ld | Z = %ld\n", currentX, currentZ);
}

// Di chuyen den TOA DO TUYET DOI (targetX, targetZ) tinh tu goc Cong tac hanh trinh
void moveToAbsolute(long targetX, long targetZ) {
  if (!isHomed) {
    Serial.println("[CHU Y] May chua ve Home! Tu dong ve Home truoc...");
    homeAxes();
  }

  digitalWrite(PIN_MOTORS_EN, LOW);
  Serial.printf("[MOVE] Di chuyen tu (X=%ld, Z=%ld) -> (X=%ld, Z=%ld)\n", currentX, currentZ, targetX, targetZ);

  // 1. Chay truc Z
  long deltaZ = targetZ - currentZ;
  jogZ(deltaZ);
  delay(150);

  // 2. Chay truc X
  long deltaX = targetX - currentX;
  jogX(deltaX);
  delay(150);
}

// --------------------------- 6. THAO TAC CAT / LAY XE QUA SERVO & TOA DO -----------------------
void storeCarToSlot(int slotIdx) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;

  long targetX = slots[slotIdx].stepX;
  long targetZ = slots[slotIdx].stepZ;

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Dua xe -> Slot "); lcd.print(slotIdx + 1);
  lcd.setCursor(0, 1); lcd.print("X:" + String(targetX) + " Z:" + String(targetZ));

  // 1. Di chuyen ban nang den dung vi tri o do
  moveToAbsolute(targetX, targetZ);
  delay(300);

  // 2. Servo day khay xe vao o do (Goc 95 do)
  lcd.setCursor(0, 1); lcd.print("Day khay xe vao ");
  trayServo.write(SERVO_EXTEND);
  delay(900);

  // 3. Servo thu tay gat ve (Goc 10 do)
  trayServo.write(SERVO_RETRACT);
  delay(600);

  // 4. Tro ve vi tri Home don xe tiep theo
  lcd.setCursor(0, 1); lcd.print("Ve cong don xe  ");
  moveToAbsolute(0, 0);

  slots[slotIdx].occupied = true;
  beep(1, 200);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DA CAT XE XONG!");
  lcd.setCursor(0, 1); lcd.print("Slot " + String(slotIdx + 1) + " san sang");
}

void retrieveCarFromSlot(int slotIdx) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;

  long targetX = slots[slotIdx].stepX;
  long targetZ = slots[slotIdx].stepZ;

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Lay xe -> Slot "); lcd.print(slotIdx + 1);
  lcd.setCursor(0, 1); lcd.print("X:" + String(targetX) + " Z:" + String(targetZ));

  // 1. Di chuyen den o do xe can lay
  moveToAbsolute(targetX, targetZ);
  delay(300);

  // 2. Keo khay xe ra ban nang
  lcd.setCursor(0, 1); lcd.print("Keo khay xe ra  ");
  trayServo.write(SERVO_EXTEND);
  delay(900);
  trayServo.write(SERVO_RETRACT);
  delay(600);

  // 3. Mang xe xuong cong tra xe (Tang tret / Home)
  lcd.setCursor(0, 1); lcd.print("Mang ve cong tra");
  moveToAbsolute(0, 0);

  // Giai phong o do
  slots[slotIdx].occupied = false;
  slots[slotIdx].uid = "";
  slots[slotIdx].plate = "";
  beep(2, 100);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MOI NHAN LAI XE!");
  lcd.setCursor(0, 1); lcd.print("Cam on quy khach");
}

// --------------------------- 7. GIAO DIEN WEB DASHBOARD & REST API ---------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Bãi Đỗ Xe Thông Minh ESP32 (6 Ô Đỗ)</title>
  <style>
    :root {
      --bg: #0f172a; --panel: #1e293b; --card: #334155; --text: #f8fafc;
      --accent: #38bdf8; --success: #22c55e; --danger: #ef4444; --warning: #f59e0b;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: var(--bg); color: var(--text); padding: 16px; }
    .container { max-width: 950px; margin: auto; }
    header { text-align: center; margin-bottom: 20px; }
    h1 { font-size: 1.6rem; color: var(--accent); margin-bottom: 6px; }
    .badge { display: inline-block; padding: 4px 10px; border-radius: 999px; font-size: 0.8rem; background: var(--card); }
    .grid-2 { display: grid; grid-template-columns: 1.2fr 0.8fr; gap: 16px; margin-bottom: 16px; }
    @media(max-width: 700px) { .grid-2 { grid-template-columns: 1fr; } }
    .card { background: var(--panel); padding: 16px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.08); }
    .card h2 { font-size: 1.1rem; color: var(--accent); margin-bottom: 12px; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 6px; }
    .slots-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
    .slot-box { background: var(--card); padding: 10px; border-radius: 8px; text-align: center; border-left: 4px solid var(--success); transition: all 0.2s; }
    .slot-box.occupied { border-left-color: var(--danger); background: #451a1a; }
    .slot-title { font-weight: bold; font-size: 0.95rem; }
    .slot-plate { font-size: 1.05rem; color: #fde047; font-weight: bold; margin: 3px 0; }
    .slot-coord { font-size: 0.72rem; color: #94a3b8; }
    .status-row { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid rgba(255,255,255,0.05); font-size: 0.9rem; }
    .sw-dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 6px; }
    .sw-active { background: var(--danger); box-shadow: 0 0 6px var(--danger); }
    .sw-idle { background: #64748b; }
    .btn-group { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    button { background: var(--card); color: var(--text); border: none; padding: 10px; border-radius: 6px; font-weight: bold; cursor: pointer; transition: 0.2s; font-size: 0.85rem; }
    button:hover { background: var(--accent); color: #000; }
    button.btn-primary { background: #0284c7; }
    button.btn-success { background: #16a34a; }
    button.btn-danger { background: #dc2626; }
    .save-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    @media(max-width: 600px) { .save-grid { grid-template-columns: repeat(2, 1fr); } }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>BÃI ĐỖ XE THÔNG MINH ESP32</h1>
      <p class="badge">Hệ Thống 6 Ô Đỗ &bull; 3 Tầng x 2 Cột &bull; RFID &bull; Camera AI ANPR</p>
    </header>

    <div class="grid-2">
      <!-- SO DO 6 O DO XE -->
      <div class="card">
        <h2>Trạng Thái 6 Ô Đỗ (3 Tầng x 2 Cột)</h2>
        <div class="slots-grid" id="slotsView">Đang tải dữ liệu...</div>
      </div>

      <!-- TOA DO & CONG TAC HANH TRINH -->
      <div class="card">
        <h2>Công Tắc Hành Trình & Tọa Độ Máy</h2>
        <div class="status-row">
          <span>Tọa độ hiện tại:</span>
          <strong id="curCoord" style="color:var(--accent);">X = 0 | Z = 0</strong>
        </div>
        <div class="status-row">
          <span>Công tắc Home X (GPIO 16):</span>
          <span id="swX"><span class="sw-dot sw-idle"></span>Hở</span>
        </div>
        <div class="status-row">
          <span>Công tắc Min Z (GPIO 32):</span>
          <span id="swZMin"><span class="sw-dot sw-idle"></span>Hở</span>
        </div>
        <div class="status-row">
          <span>Công tắc Max Z (GPIO 33):</span>
          <span id="swZMax"><span class="sw-dot sw-idle"></span>Hở</span>
        </div>
        <div class="status-row">
          <span>Trạng thái Homing:</span>
          <strong id="homeStatus" style="color:var(--warning);">Chưa Home</strong>
        </div>

        <button class="btn-primary" style="width:100%; margin-top:12px;" onclick="fetch('/api/home')">
          &#x21bb; Chạy Về Home (Xác Lập Gốc 0, 0)
        </button>
      </div>
    </div>

    <!-- BANG DIEU KHIEN CAN CHINH TOA DO -->
    <div class="card">
      <h2>Công Cụ Lái & Lưu Tọa Độ 6 Ô Đỗ Vào Flash NVS</h2>
      <p style="font-size:0.85rem; color:#94a3b8; margin-bottom:8px;">
        1. Bấm "Chạy Về Home" để xác lập gốc chuẩn 0, 0.<br>
        2. Dùng các nút bên dưới để lái bàn nâng đến ngay ngắn miệng từng ô đỗ 1 đến 6.<br>
        3. Nhấn nút "LƯU VÀO Ô 1..6" để lưu vĩnh viễn tọa độ vào bộ nhớ Flash (tắt nguồn không mất)!
      </p>

      <div class="btn-group">
        <button onclick="jog('z', 1000)">&#x25b2; Z Lên Nhanh (+1000)</button>
        <button onclick="jog('z', 200)">&#x25b2; Z Lên (+200)</button>
        <button class="btn-danger" onclick="fetch('/api/servo?action=test')">Test Servo Đẩy/Thu</button>
        <button onclick="jog('z', -1000)">&#x25bc; Z Xuống Nhanh (-1000)</button>
        <button onclick="jog('z', -200)">&#x25bc; Z Xuống (-200)</button>
        <button onclick="fetch('/api/home')">Về Gốc Home</button>
      </div>

      <div class="btn-group" style="margin-top:8px;">
        <button onclick="jog('x', -1000)">&laquo; X Trái Nhanh (-1000)</button>
        <button onclick="jog('x', -200)">&lsaquo; X Trái (-200)</button>
        <button onclick="jog('x', 200)">X Phải (&rsaquo;) (+200)</button>
        <button onclick="jog('x', 1000)">X Phải (&raquo;) (+1000)</button>
      </div>

      <div class="save-grid">
        <button class="btn-success" onclick="saveSlot(1)">LƯU VÀO Ô 1</button>
        <button class="btn-success" onclick="saveSlot(2)">LƯU VÀO Ô 2</button>
        <button class="btn-success" onclick="saveSlot(3)">LƯU VÀO Ô 3</button>
        <button class="btn-success" onclick="saveSlot(4)">LƯU VÀO Ô 4</button>
        <button class="btn-success" onclick="saveSlot(5)">LƯU VÀO Ô 5</button>
        <button class="btn-success" onclick="saveSlot(6)">LƯU VÀO Ô 6</button>
      </div>
    </div>
  </div>

  <script>
    function jog(axis, steps) {
      fetch('/api/jog?axis=' + axis + '&steps=' + steps).then(r => r.json()).then(updateUI);
    }
    function saveSlot(slot) {
      if(confirm('Bạn có chắc muốn lưu tọa độ hiện tại vào Ô ' + slot + '?')) {
        fetch('/api/save_slot?slot=' + slot).then(r => r.text()).then(alert);
      }
    }
    function updateUI(data) {
      if(!data) return;
      document.getElementById('curCoord').innerText = 'X = ' + data.currentX + ' | Z = ' + data.currentZ;
      document.getElementById('homeStatus').innerText = data.isHomed ? 'Đã Home Chuẩn' : 'Chưa Home';
      document.getElementById('homeStatus').style.color = data.isHomed ? 'var(--success)' : 'var(--warning)';

      function renderSw(id, active) {
        document.getElementById(id).innerHTML = active ?
          '<span class="sw-dot sw-active"></span>ĐANG CHẠM' :
          '<span class="sw-dot sw-idle"></span>Hở';
      }
      renderSw('swX', data.swX);
      renderSw('swZMin', data.swZMin);
      renderSw('swZMax', data.swZMax);

      let html = '';
      data.slots.forEach(s => {
        html += `<div class="slot-box ${s.occupied ? 'occupied' : ''}">
          <div class="slot-title">Ô ĐỖ ${s.slot}</div>
          <div class="slot-plate">${s.occupied ? (s.plate || 'Có Xe (Thẻ ' + s.uid + ')') : 'TRỐNG'}</div>
          <div class="slot-coord">Toạ độ: X=${s.stepX} | Z=${s.stepZ}</div>
        </div>`;
      });
      document.getElementById('slotsView').innerHTML = html;
    }

    setInterval(() => {
      fetch('/api/status').then(r => r.json()).then(updateUI).catch(()=>{});
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

// REST API cho Camera AI Python & Web Dashboard
void handleApiStatus() {
  String json = "{";
  json += "\"connected\":true,";
  json += "\"isHomed\":" + String(isHomed ? "true" : "false") + ",";
  json += "\"currentX\":" + String(currentX) + ",";
  json += "\"currentZ\":" + String(currentZ) + ",";
  json += "\"swX\":" + String(isLimitXHomeHit() ? "true" : "false") + ",";
  json += "\"swZMin\":" + String(isLimitZMinHit() ? "true" : "false") + ",";
  json += "\"swZMax\":" + String(isLimitZMaxHit() ? "true" : "false") + ",";
  json += "\"waitingForPlate\":" + String(waitingForPlate ? "true" : "false") + ",";
  json += "\"pendingSlot\":" + String(pendingSlot) + ",";
  json += "\"pendingUID\":\"" + pendingUID + "\",";
  json += "\"sessionId\":" + String(sessionId) + ",";
  json += "\"slots\":[";
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    json += "{\"slot\":" + String(i + 1) + ",";
    json += "\"occupied\":" + String(slots[i].occupied ? "true" : "false") + ",";
    json += "\"uid\":\"" + slots[i].uid + "\",";
    json += "\"plate\":\"" + slots[i].plate + "\",";
    json += "\"stepX\":" + String(slots[i].stepX) + ",";
    json += "\"stepZ\":" + String(slots[i].stepZ) + "}";
    if (i < TOTAL_SLOTS - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleApiPlate() {
  if (server.hasArg("plate")) {
    String plate = server.arg("plate");
    if (waitingForPlate && pendingSlot >= 0 && pendingSlot < TOTAL_SLOTS) {
      slots[pendingSlot].plate = plate;
      waitingForPlate = false;
      sessionId++;

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("BIEN SO: " + plate);
      lcd.setCursor(0, 1); lcd.print("Luu Slot " + String(pendingSlot + 1));
      beep(1, 150);

      // Kich hoat dong co dua xe vao slot
      storeCarToSlot(pendingSlot);

      server.send(200, "text/plain", "SAVED: " + plate);
      return;
    }
  }
  server.send(400, "text/plain", "BAD_REQUEST");
}

void handleApiJog() {
  if (server.hasArg("axis") && server.hasArg("steps")) {
    String axis = server.arg("axis");
    long steps = server.arg("steps").toInt();
    if (axis == "x" || axis == "X") jogX(steps);
    else if (axis == "z" || axis == "Z") jogZ(steps);
    handleApiStatus();
    return;
  }
  server.send(400, "text/plain", "MISSING_PARAMS");
}

void handleApiSaveSlot() {
  if (server.hasArg("slot")) {
    int s = server.arg("slot").toInt() - 1;
    if (s >= 0 && s < TOTAL_SLOTS) {
      saveSlotCoordinateToNVS(s, currentX, currentZ);
      server.send(200, "text/plain", "OK: Da luu O " + String(s + 1) + " (X=" + String(currentX) + ", Z=" + String(currentZ) + ") vao Flash!");
      return;
    }
  }
  server.send(400, "text/plain", "INVALID_SLOT");
}

void handleApiHome() {
  homeAxes();
  server.send(200, "text/plain", "HOME_OK");
}

void handleApiServo() {
  Serial.println("[SERVO TEST] Day khay xe (95 do)...");
  trayServo.write(SERVO_EXTEND);
  delay(1000);
  Serial.println("[SERVO TEST] Thu khay xe (10 do)...");
  trayServo.write(SERVO_RETRACT);
  server.send(200, "text/plain", "SERVO_TEST_OK");
}

// --------------------------- 8. GIAO TIEP BAN PHIM SERIAL (CALIBRATION TUC THOI) ---------------
void printSerialHelp() {
  Serial.println("\n=======================================================================");
  Serial.println("   CONG CU HIEU CHINH TOA DO BAI DO XE QUA SERIAL & CONG TAC HANH TRINH  ");
  Serial.println("=======================================================================");
  Serial.println(" [w] / [s] : Truc Z Len / Xuong (+/- 200 buoc)");
  Serial.println(" [W] / [S] : Truc Z Len / Xuong nhanh (+/- 1000 buoc)");
  Serial.println(" [d] / [a] : Truc X Phai / Trai (+/- 200 buoc)");
  Serial.println(" [D] / [A] : Truc X Phai / Trai nhanh (+/- 1000 buoc)");
  Serial.println(" [h]       : Chay ve GOC HOME bang cong tac hanh trinh (X=0, Z=0)");
  Serial.println(" [e] / [r] : Test Servo DAY khay (95 do) / THU khay ve (10 do)");
  Serial.println(" [t]       : Kiem tra trang thai 3 cong tac hanh trinh (Hometest)");
  Serial.println("-----------------------------------------------------------------------");
  Serial.println(" [1], [2], [3], [4], [5], [6] : Luu toa do hien tai vao O 1..6 (FLASH NVS)");
  Serial.println(" [p]       : In toan bo bang toa do 6 o do");
  Serial.println("=======================================================================");
  Serial.printf("-> Toa do hien tai: X = %ld | Z = %ld | Homed = %s\n\n", currentX, currentZ, isHomed ? "YES" : "NO");
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;

    switch (c) {
      case 'w': jogZ(200); break;
      case 'W': jogZ(1000); break;
      case 's': jogZ(-200); break;
      case 'S': jogZ(-1000); break;

      case 'd': jogX(200); break;
      case 'D': jogX(1000); break;
      case 'a': jogX(-200); break;
      case 'A': jogX(-1000); break;

      case 'h': homeAxes(); break;

      case 'e':
        Serial.println("-> Servo: DAY KHAY XE RA (Goc 95 do)...");
        trayServo.write(SERVO_EXTEND);
        break;
      case 'r':
        Serial.println("-> Servo: THU KHAY XE VE (Goc 10 do)...");
        trayServo.write(SERVO_RETRACT);
        break;

      case 't':
        Serial.printf("[KIEM TRA SWITCH] LIMIT_X_HOME(16): %s | LIMIT_Z_MIN(32): %s | LIMIT_Z_MAX(33): %s\n",
          isLimitXHomeHit() ? "CHAM (LOW)" : "HO (HIGH)",
          isLimitZMinHit() ? "CHAM (LOW)" : "HO (HIGH)",
          isLimitZMaxHit() ? "CHAM (LOW)" : "HO (HIGH)");
        break;

      case '1': case '2': case '3': case '4': case '5': case '6': {
        int idx = c - '1';
        saveSlotCoordinateToNVS(idx, currentX, currentZ);
        break;
      }

      case 'p':
        Serial.println("\n>>> BANG TOA DO 6 O DO HIEN TAI (TINH TU CONG TAC HANH TRINH): <<<");
        for (int i = 0; i < TOTAL_SLOTS; i++) {
          Serial.printf("  Slot %d: X = %ld | Z = %ld\n", i + 1, slots[i].stepX, slots[i].stepZ);
        }
        Serial.println();
        break;

      default:
        printSerialHelp();
        break;
    }
  }
}

// --------------------------- 9. SETUP & LOOP --------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Khoi tao cac chan I/O Dong co
  pinMode(PIN_STEP_X, OUTPUT);
  pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT);
  pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT);
  digitalWrite(PIN_MOTORS_EN, LOW); // Bat dong co buoc

  // Khoi tao Cong tac hanh trinh (INPUT_PULLUP: Chạm = LOW, Hở = HIGH)
  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);

  // Cam bien cong vao (GPIO 35 la input-only)
  pinMode(PIN_IR_GATE, INPUT);

  // LED & Coi
  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);
  buzzerOff();
  pinMode(PIN_BUZZER, OUTPUT);
  buzzerOff();

  // Khoi tao Servo MG90S
  trayServo.attach(PIN_SERVO_PWM);
  trayServo.write(SERVO_RETRACT); // 10 do

  // Khoi tao LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("BAI DO XE 6 O");
  lcd.setCursor(0, 1); lcd.print("Dang khoi tao...");

  // Coi bip test khoi dong
  beep(2, 60);

  // Khoi tao RFID RC522
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();

  // Load toa do da luu tu Flash NVS
  loadSlotCoordinatesFromNVS();

  // Ket noi WiFi
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Ket noi WiFi...");
  lcd.setCursor(0, 1); lcd.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 16) {
    delay(500);
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    Serial.printf("\n[WIFI OK] IP: http://%s\n", WiFi.localIP().toString().c_str());
    MDNS.begin("baidoxe");
    delay(1500);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AP: BaiDoXe_ESP");
    lcd.setCursor(0, 1); lcd.print(WiFi.softAPIP().toString());
    Serial.printf("\n[AP MODE] IP: http://%s\n", WiFi.softAPIP().toString().c_str());
    delay(1500);
  }

  // Dang ky Routing Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/plate", HTTP_POST, handleApiPlate);
  server.on("/api/jog", HTTP_GET, handleApiJog);
  server.on("/api/save_slot", HTTP_GET, handleApiSaveSlot);
  server.on("/api/home", HTTP_GET, handleApiHome);
  server.on("/api/servo", HTTP_GET, handleApiServo);
  server.begin();

  // Chay ve Home goc chuan ngay khi khoi dong
  homeAxes();

  printSerialHelp();
}

void loop() {
  server.handleClient();
  handleSerialCommands();

  // Kiem tra the RFID
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String scannedUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) scannedUID += "0";
      scannedUID += String(rfid.uid.uidByte[i], HEX);
    }
    scannedUID.toUpperCase();
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    Serial.println("\n[RFID] Phat hien the UID: " + scannedUID);
    beep(1, 100);

    // Kiem tra the da gui xe trong bai chua
    int foundSlot = -1;
    for (int i = 0; i < TOTAL_SLOTS; i++) {
      if (slots[i].occupied && slots[i].uid == scannedUID) {
        foundSlot = i;
        break;
      }
    }

    if (foundSlot >= 0) {
      // 1. THE DA GUI XE -> TRA XE
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("The: " + scannedUID);
      lcd.setCursor(0, 1); lcd.print("Tra xe Slot " + String(foundSlot + 1));
      retrieveCarFromSlot(foundSlot);
    } else {
      // 2. THE MOI -> TIM O TRONG
      int emptySlot = -1;
      for (int i = 0; i < TOTAL_SLOTS; i++) {
        if (!slots[i].occupied) {
          emptySlot = i;
          break;
        }
      }

      if (emptySlot == -1) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("BAI XE DA DAY!");
        lcd.setCursor(0, 1); lcd.print("Vui long quay lai");
        beep(3, 150);
      } else {
        // Cho Camera ANPR chup bien so
        pendingSlot = emptySlot;
        pendingUID = scannedUID;
        slots[emptySlot].uid = scannedUID;
        waitingForPlate = true;
        sessionId++;

        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Vao Slot " + String(emptySlot + 1));
        lcd.setCursor(0, 1); lcd.print("Cho quet bien so");
        Serial.printf("[CHECK-IN] Dang cho camera ANPR chup bien so cho Slot %d...\n", emptySlot + 1);
      }
    }
  }

  delay(10);
}
