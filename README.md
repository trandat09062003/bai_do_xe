# HỆ THỐNG BÃI ĐỖ XE TỰ ĐỘNG THÔNG MINH 2 TẦNG (ESP32 + ANPR AI)

Dự án thiết kế và chế tạo mô hình bãi đỗ xe ô tô tự động 4 vị trí (2 tầng x 2 cột), kết hợp hệ thống thị giác máy tính nhận diện biển số xe (ANPR) và cơ cấu vận chuyển 2 trục X - Z.

* **Tác giả:** Trần Đạt ([@trandat09062003](https://github.com/trandat09062003))
* **Vi điều khiển chính:** ESP32-WROOM-32 (DevKit V1 - 30 pins)
* **Xử lý ảnh & AI:** Python 3 + YOLOv8 Plate Detection + EasyOCR
* **Thiết kế phần cứng:** Altium Designer (Schematic & PCB 2 lớp)

---

## 1. Giới thiệu tổng quan

Hệ thống mô phỏng bãi đỗ xe thông minh dạng thang nâng - xếp hình (Puzzle / Elevator Parking System) với các tính năng:
- **Kiểm soát xe vào/ra bằng thẻ từ RFID (RC522):** Lưu mã định danh (UID) thẻ tương ứng với từng vị trí khay đỗ.
- **Tự động nhận diện biển số xe (ANPR):** Camera/Webcam giám sát cổng vào bắt khung hình, mô hình deep learning YOLOv8 định vị vùng biển số, sau đó EasyOCR trích xuất ký tự chữ số và tự động gửi dữ liệu về ESP32 qua giao thức HTTP REST API.
- **Cơ cấu định vị 2 trục X - Z chính xác:** Sử dụng 2 động cơ bước NEMA 17 điều khiển qua 2 driver A4988, kết hợp 3 công tắc hành trình để định vị điểm chuẩn (Home X, Min Z, Max Z).
- **Cơ cấu cấp/nhả xe:** Động cơ RC Servo MG90S bánh răng kim loại thực hiện kéo khay xe ra và đẩy khay xe vào slot đỗ.
- **Giám sát trực quan:** Màn hình LCD 1602 I2C báo số lượng chỗ trống và thông tin quẹt thẻ; Web Dashboard máy chủ nhúng trên ESP32 theo dõi trực quan vị trí 4 ô đỗ, thời gian gửi xe và ảnh chụp biển số qua mạng Wi-Fi cục bộ.

---

## 2. Cấu trúc phần cứng & Linh kiện

| STT | Tên linh kiện / Module | Thông số kỹ thuật | Vai trò trong hệ thống |
|:---:|:---|:---|:---|
| 1 | **ESP32 DevKit V1** | 30 chân, 240MHz Dual-Core, Wi-Fi 2.4GHz | Vi điều khiển trung tâm, xử lý logic, Web Server |
| 2 | **Động cơ bước NEMA 17** | 2 pha 4 dây, góc bước 1.8°, 1.5A | Điều khiển cơ cấu trượt trục X và nâng hạ trục Z |
| 3 | **Driver A4988** | Hỗ trợ vi bước 1/16, tản nhiệt nhôm | Mạch đệm công suất điều khiển động cơ bước |
| 4 | **Servo MG90S** | Bánh răng kim loại, góc xoay 180° | Đẩy/thu ngàm khay xe vào/ra slot đỗ |
| 5 | **Module RFID RC522** | Tần số 13.56MHz (Chuẩn Mifare) | Đọc thẻ từ người dùng gửi/lấy xe |
| 6 | **Cảm biến hồng ngoại IR (LM393)** | Ngõ ra Digital, tầm phát hiện 2 - 30cm | Cảm biến ngắt phát hiện xe dừng trước barrier |
| 7 | **Màn hình LCD 1602 + I2C** | 16 ký tự x 2 dòng, giao tiếp PCF8574 | Hiển thị hướng dẫn, chỗ trống, mã thẻ, biển số |
| 8 | **Công tắc hành trình (Limit Switch)** | 3 chân microswitch có cần gạt | Xác định gốc tọa độ Home X, Min Z và Max Z |
| 9 | **Còi chip (Buzzer 5V)** | Active LOW qua transistor đệm | Bíp xác nhận thao tác quẹt thẻ và cảnh báo lỗi |
| 10 | **Nguồn tổ ong / Adapter** | 12V - 5A & Hạ áp LM2596 / AMS1117 5V | Cấp nguồn động lực cho motor và nguồn điều khiển |

---

## 3. Sơ đồ kết nối chân (GPIO Pinout ESP32)

Bảng phân bổ chân chuẩn theo file thiết kế mạch in Altium (`baidoxe.PcbDoc`):

| Khối chức năng | Tên tín hiệu | Chân ESP32 | Ghi chú kỹ thuật |
|:---|:---|:---:|:---|
| **Trục X (Ngang)** | `STEP_X` | **GPIO 14** | Xung bước điều khiển vị trí X |
| | `DIR_X` | **GPIO 12** | Chiều quay động cơ X |
| | `LIMIT_X_HOME` | **GPIO 16** | Công tắc hành trình gốc trục X (Pull-up) |
| **Trục Z (Nâng hạ)** | `STEP_Z` | **GPIO 25** | Xung bước điều khiển thang nâng Z |
| | `DIR_Z` | **GPIO 26** | Chiều nâng/hạ động cơ Z |
| | `LIMIT_Z_MIN` | **GPIO 32** | Công tắc hành trình đáy tầng 1 (Pull-up) |
| | `LIMIT_Z_MAX` | **GPIO 33** | Công tắc hành trình đỉnh tầng 2 (Pull-up) |
| **Bật/Tắt Động cơ** | `MOTORS_EN` | **GPIO 27** | Kích hoạt cả 2 Driver A4988 (Mức 0 = Bật, Mức 1 = Nhả) |
| **Tay gạt khay xe** | `SERVO_PWM` | **GPIO 13** | Xung PWM 50Hz điều khiển góc quay Servo |
| **Cảm biến cổng** | `IR_GATE` | **GPIO 35** | Cảm biến hồng ngoại cổng vào (Input-Only) |
| **Thẻ từ RFID RC522** | `SS (SDA)` | **GPIO 5** | Giao tiếp chuẩn phần cứng SPI |
| | `SCK` | **GPIO 18** | SPI Clock |
| | `MOSI` | **GPIO 23** | SPI Master Out Slave In |
| | `MISO` | **GPIO 19** | SPI Master In Slave Out |
| | `RST` | **GPIO 17** | Chân Reset module RC522 |
| **Màn hình LCD 1602** | `SDA` | **GPIO 21** | Giao tiếp I2C Data (Địa chỉ 0x27) |
| | `SCL` | **GPIO 22** | Giao tiếp I2C Clock |
| **Cảnh báo & Đèn** | `BUZZER` | **GPIO 4** | Kích mức 0 (Active LOW) |
| | `LED_STATUS` | **GPIO 2** | Đèn báo trạng thái hoạt động |
| **Nút bấm trên mạch (Tactile)** | `B3` | **GPIO 16** | Nút test / giả lập công tắc hành trình Home X |
| | `B1` | **GPIO 32** | Nút test / giả lập công tắc hành trình Min Z |
| | `B4` | **GPIO 33** | Nút test / giả lập công tắc hành trình Max Z |

> [!NOTE]
> **Vai trò của các nút bấm B1, B3, B4 trên bo mạch Altium:**
> - Trên bo mạch được thiết kế sẵn 3 nút nhấn tactile SMD (B1, B3, B4) kèm tụ dập dội (104) và điện trở kéo.
> - **Khi test mạch (Bench Test):** Có thể nhấn trực tiếp 3 nút này để giả lập trạng thái chạm công tắc hành trình mà không cần gắn khung cơ khí.
> - **Khi lắp mô hình hoàn chỉnh:** Có thể hàn song song 2 dây của công tắc hành trình cơ khí (Microswitch gắn ngoài ray) vào chân của các nút bấm này trên mạch.
> - **Các nút trên board ESP32:** Nút `EN` dùng để Reset/khởi động lại vi điều khiển; nút `BOOT` dùng để kích hoạt chế độ nạp Firmware khi cần.

---

## 4. Cài đặt dòng Driver A4988 (Vref Tuning)

Để động cơ bước hoạt động êm ái, kéo tải khỏe và không bị phát nhiệt làm nóng chảy chi tiết in 3D/mica, cần chỉnh điện áp tham chiếu $V_{ref}$ trên chiết áp của A4988:

1. Chuyển đồng hồ vạn năng (VOM) sang thang đo điện áp một chiều **DC 2V** (hoặc **20V**).
2. Que **ĐEN (-)** kẹp vào chân **GND** của mạch.
3. Que **ĐỎ (+)** chạm vào **đỉnh ốc kim loại của biến trở xoay** trên module A4988.
4. Dùng tua vít 2 cạnh nhỏ xoay nhẹ chiết áp để đạt giá trị:
   $$\mathbf{V_{ref} \approx 0.58\text{V} - 0.65\text{V}} \quad (\text{Khuyến nghị: } 0.60\text{V})$$
   *Dòng đỉnh tương ứng: $I_{max} = \frac{V_{ref}}{8 \times 0.1\Omega} \approx 0.75\text{A}$ (phù hợp tải mô hình bãi xe).*

---

## 5. Cấu trúc thư mục dự án

```text
bai_do_xe/
├── baidoxe/                              # Thư mục thiết kế phần cứng Altium
│   ├── baidoxe.PrjPcb                    # File quản lý đồ án Altium
│   ├── baidoxe.PcbDoc                    # Bản vẽ mạch in PCB 2 lớp
│   ├── baidoxe.SchDoc                    # Sơ đồ nguyên lý điện tử
│   ├── AutoPlace_Baidoxe.pas             # Script sắp xếp linh kiện tự động
│   ├── Libraries/                        # Thư viện footprint & ký hiệu (PcbLib, SchLib)
│   └── firmware/                         # Toàn bộ mã nguồn nhúng và phần mềm AI
│       ├── ESP32_BAI_DO_XE_FINAL/        # Firmware chính thức cho ESP32
│       │   └── ESP32_BAI_DO_XE_FINAL.ino
│       ├── CONG_CU_HIEU_CHINH_DONG_CO/   # Tool căn chỉnh tọa độ qua bàn phím Serial
│       │   └── CONG_CU_HIEU_CHINH_DONG_CO.ino
│       ├── TEST_2_DONG_CO_BUOC/          # Code test nhanh chiều quay & kiểm tra mạch driver
│       │   └── TEST_2_DONG_CO_BUOC.ino
│       ├── anpr_bai_do_xe_final.py       # Script Python AI nhận diện biển số & gửi ESP32
│       ├── requirements_anpr.txt         # Danh mục thư viện Python cần cài đặt
│       └── CHAY_ANPR.bat                 # Script chạy nhanh camera ANPR trên Windows
├── .gitignore                            # Bộ lọc loại trừ file rác, file tạm và history
└── README.md                             # Tài liệu kỹ thuật chi tiết của dự án
```

---

## 6. Hướng dẫn nạp Code & Vận hành

### Bước 1: Nạp Firmware cho ESP32
1. Cài đặt **Arduino IDE** (phiên bản 1.8.x hoặc 2.x).
2. Cài đặt board ESP32 trong *Boards Manager* (tìm `esp32` của *Espressif Systems*).
3. Cài đặt các thư viện cần thiết qua *Library Manager*:
   - `MFRC522` by GithubCommunity
   - `LiquidCrystal_I2C` by Frank de Brabander
   - `ESP32Servo` by Kevin Harrington
4. Mở file [ESP32_BAI_DO_XE_FINAL.ino](baidoxe/firmware/ESP32_BAI_DO_XE_FINAL/ESP32_BAI_DO_XE_FINAL.ino).
5. Kiểm tra thông tin Wi-Fi:
   ```cpp
   const char* WIFI_SSID = "TEN_WIFI_CUA_BAN";
   const char* WIFI_PASS = "MAT_KHAU_WIFI";
   ```
6. Chọn board **DOIT ESP32 DEVKIT V1**, chọn đúng cổng COM và nhấn **Upload**.

### Bước 2: Hiệu chỉnh tọa độ các ô đỗ (Tùy chọn theo khung cơ khí)
Nếu kích thước khung thực tế của bạn khác với thông số mặc định, sử dụng công cụ hiệu chỉnh:
1. Nạp file [CONG_CU_HIEU_CHINH_DONG_CO.ino](baidoxe/firmware/CONG_CU_HIEU_CHINH_DONG_CO/CONG_CU_HIEU_CHINH_DONG_CO.ino).
2. Mở **Serial Monitor** với tốc độ **115200 baud**.
3. Dùng bàn phím máy tính gõ các phím điều hướng để lái khay xe:
   - `W` / `S`: Trục Z lên / xuống (1000 bước).
   - `D` / `A`: Trục X sang phải / trái (1000 bước).
   - `w` / `s` / `d` / `a`: Nhích tinh chỉnh từng 200 bước vào mép ô.
   - `e` / `r`: Thử đẩy khay ra / thu khay về.
4. Khi khay xe đã đối diện chuẩn trước ô:
   - Gõ `1` -> Lưu Ô 1 (Tầng 1, cột 1).
   - Lái sang tiếp theo, gõ `2` -> Lưu Ô 2 (Tầng 1, cột 2).
   - Lái lên tầng 2, gõ `3` -> Lưu Ô 3 (Tầng 2, cột 1).
   - Gõ `4` -> Lưu Ô 4 (Tầng 2, cột 2).
5. Gõ phím `p` để in toàn bộ bảng tọa độ, sau đó dán vào dòng 93 của file code chính.

### Bước 3: Cài đặt và chạy hệ thống nhận diện biển số (ANPR)
1. Cài đặt Python 3.10 hoặc 3.11 trên máy tính Windows.
2. Mở PowerShell hoặc Command Prompt tại thư mục `baidoxe/firmware`:
   ```bash
   pip install -r requirements_anpr.txt
   pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
   ```
3. Chạy chương trình nhận diện:
   ```bash
   python anpr_bai_do_xe_final.py
   ```
   *(hoặc nhấp đúp trực tiếp vào file **`CHAY_ANPR.bat`**)*.
4. Ứng dụng tự động bật Webcam cổng 0, tự dò tìm địa chỉ IP của ESP32 trong mạng LAN nội bộ và gửi dữ liệu biển số nhận dạng được khi xe vào bãi.

---

## 7. Nguyên lý hoạt động của hệ thống

```text
[Xe vào cổng] ---> [Cảm biến IR phát hiện] ---> [Quẹt thẻ RFID]
                                                      |
                                                      v
[Lưu UID vào khay] <--- [Gửi POST /api/plate] <--- [Camera ANPR chụp biển số]
         |
         v
[Động cơ X + Z chạy đưa khay tới ô trống]
         |
         v
[Servo MG90S đẩy xe vào slot] ---> [Cập nhật Web Dashboard & LCD]
```

* **Gửi xe (Check-in):** Khi xe vào, cảm biến IR phát hiện xe. Người dùng quẹt thẻ RFID lên đầu đọc RC522. ESP32 phát tín hiệu yêu cầu nhận diện biển số. Camera ghi nhận và AI đọc biển số gửi qua Wi-Fi. Bàn nâng hạ X-Z di chuyển khay xe tới ô đỗ còn trống gần nhất, servo đẩy xe vào vị trí và hệ thống lưu trữ trạng thái.
* **Lấy xe (Check-out):** Người dùng quẹt lại đúng thẻ RFID đã dùng lúc vào. Hệ thống tra cứu vị trí ô đỗ tương ứng, điều khiển bàn nâng đến đúng slot, servo kéo khay xe ra và bàn nâng đưa xe xuống vị trí cổng trả xe. Ô đỗ được giải phóng về trạng thái sẵn sàng đón lượt xe mới.

