HUONG DAN - BO CODE CHOT BAI DO XE THONG MINH
================================================

1. KIEN TRUC
- ESP32: TB6600, dong co buoc, cam bien E18-D80NK, RFID RC522, LCD I2C, buzzer, Web Dashboard.
- Laptop Windows: webcam USB + Python + YOLO + EasyOCR; laptop luu anh bien so va phuc vu anh qua HTTP port 8765.
- ESP32 nhan bien so + OCR confidence + thoi gian + URL anh qua POST /api/plate.
- Trinh duyet tai anh truc tiep tu laptop; ESP32 khong luu file anh trong bo nho.

2. CHAN GPIO DA CHOT (THEO CHUONG TRINH THU NGHIEM DONG CO DA QUAY)
- TB6600 ENA: GPIO4 (GPIO LOW = driver enable/giu truc; GPIO HIGH = driver offline/nha truc theo cach dau C1815 da mo ta).
- TB6600 DIR: GPIO16.
- TB6600 PUL: GPIO17; xung HIGH 600 us, LOW 600 us.
- E18-D80NK: GPIO34; SENSOR_ACTIVE_LEVEL = HIGH.
- Buzzer GPIO25.
- RC522: SS27, RST26, SCK18, MISO19, MOSI23.
- LCD I2C: SDA21, SCL22.

CANH BAO PHAN CUNG:
- GPIO34 la input-only va khong co pull-up/pull-down noi bo. Tin hieu cam bien phai co muc 0/3.3 V ro rang, khong dua 5 V truc tiep vao GPIO34.
- Code dang gia dinh HIGH = phat hien khay theo sketch thu nghiem. Neu do thuc te cho thay LOW khi co khay, doi SENSOR_ACTIVE_LEVEL thanh LOW.
- Kieu ENA TB6600 clone co the khac nhau. Hay thu khoa/nha truc khong tai truoc khi lap co cau.
- E18 phai nhin thay mot dau/moc tren moi khay va co khoang khong phat hien giua hai moc. Neu khay khong co moc rieng, cam bien khong the dem duoc 6 khay.

3. DIEU KHIEN BAI XE
- TOTAL_SLOTS = 6.
- Nut "Do khay va dat moc Khay 1" tren Web phai duoc nhan sau khi khoi dong hoac sau khi JOG.
- Khay dau tien duoc cam bien tim thay se duoc quy uoc la Khay 1.
- RFID moi: tim khay trong gan nhat, dua khay ve cong va gan UID vao khay.
- RFID da co: dua dung khay cua UID ra cong va giai phong khay.
- Trang thai chiem dung khay hien dang nam trong RAM; mat nguon/reset ESP32 se mat bang ghi. Hay khoi dong/dong bo lai va nhap lai trang thai neu can.

4. NHAN DIEN BIEN SO
- Webcam luon duoc giu mo khi chuong trinh Python dang chay; neu camera mat frame, app se thu mo lai.
- YOLO confidence = 0.15; image size = 640.
- EasyOCR candidate min = 0.22; text_threshold = 0.40; low_text = 0.20; link_threshold = 0.30.
- Chap nhan khi dinh dang bien so duoc ho tro, co it nhat 3 ket qua trung nhau trong cua so 7 frame va OCR trung binh >= 0.55.
- OCR confidence la diem cua model, khong phai xac suat dung da hieu chuan.
- He thong chuan hoa dinh dang DD[A-Z/so seri]-XXX.XX cho cac dang bien so noi dia thong dung. Sua mot so loi OCR chen ky tu sau ma tinh tren crop bien ngang; quy tac ti le anh la heuristic, khong bao phu moi loai bien so dac biet.
- Moi bien so duoc chap nhan se luu JPEG vao thu muc plate_history; giu toi da 100 anh; Web hien 12 anh gan nhat.
- Laptop mo HTTP image server cong 8765. Cho phep Python qua Windows Firewall tren mang Private.
- Python gui toi ESP32 IP 192.168.0.101 mac dinh; doi ESP32_BASE_URL neu IP thay doi.

5. CAI PYTHON (CHI CAN MOT LAN TREN MOI LAPTOP)
- Dung Python 3.11 x64. Khong dung Python 3.15 cho bo phu thuoc nay.
- Mo CMD tai thu muc nay.
- Tao moi truong: python -m venv .venv
- Kich hoat: .venv\Scripts\activate
- Cai pip: python -m pip install --upgrade pip setuptools wheel
- Cai PyTorch CPU:
  python -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
- Cai cac thu vien:
  python -m pip install -r requirements_anpr.txt
- Chay: python anpr_bai_do_xe_final.py hoac nhan dup CHAY_ANPR.bat

6. CAU HINH TRUOC KHI CHAY
- Trong ESP32_BAI_DO_XE_FINAL.ino: dien SSID va password Wi-Fi router.
- Trong anpr_bai_do_xe_final.py: kiem tra ESP32_BASE_URL (mac dinh http://192.168.0.101) va CAMERA_INDEX (mac dinh 0).
- ESP32 va laptop phai cung mang LAN. Neu ESP32 chay AP 192.168.4.1, laptop ket noi Wi-Fi BaiDoXe_ESP32 va doi ESP32_BASE_URL thanh http://192.168.4.1.
- Nhan nut Home tren Web truoc khi su dung RFID.

7. GIOI HAN DA BIET
- Webcam chi nhan dang bien so xuat hien trong khung hinh. Ban nay chua phan loai chieu VAO/RA.
- Bien so ANPR hien chua lien ket tu dong voi UID RFID/so khay; RFID van la co che quan ly vao/ra khay.
- Canh webcam va thuc te bien so co the lam thay doi ti le crop; can thu nghiem va hieu chinh.
- Chua the xac nhan compile/upload tren bo ESP32 cua ban trong moi truong nay. Test co khi khong tai truoc khi van hanh.
