/*
 * ==============================================================================================
 * Project   : HE THONG BAI DO XE THONG MINH ESP32 (6 O DO / 3 TANG x 2 COT)
 * Bo mach   : Altium PCB Bai_Do_Xe_Thong_Minh_ESP32 (ESP32 DevKit V1 30-Pin)
 * Dong co   : 2 x Stepper NEMA 17 qua Driver A4988 (Trục X ngang, Trục Z nâng tầng)
 * Co cau    : Servo 360 do (quay lien tuc) keo/day thanh rang - banh rang
 * Dinh vi   : 2 Cong tac hanh trinh Home X (GPIO 16) & Min Z (GPIO 32) = Diem lay/tra xe (0,0)
 *             + 1 Cong tac bao ve dinh Max Z (GPIO 33)
 * Luu tru   : Preferences Flash NVS (Luu vinh vien toa do 6 o do & thoi gian quay Servo 360)
 * Ngoai vi  : RFID RC522, LCD I2C 1602/2004, Cam bien IR cong vao (GPIO 35), Buzzer, LED
 * Giao tiep : Web Dashboard Responsive + REST API (/api/status, /api/plate, /api/jog, ...)
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
#define PIN_STEP_X        14  // Chân 11 ESP32 - Step Truc X
#define PIN_DIR_X         12  // Chân 12 ESP32 - Dir Truc X
#define PIN_LIMIT_X_HOME  16  // Chân 21 ESP32 - Cong tac hanh trinh Home X (Diem lay xe X=0)

#define PIN_STEP_Z        25  // Chân 8 ESP32  - Step Truc Z
#define PIN_DIR_Z         26  // Chân 9 ESP32  - Dir Truc Z
#define PIN_LIMIT_Z_MIN   32  // Chân 6 ESP32  - Cong tac hanh trinh Min Z (Diem lay xe Z=0)
#define PIN_LIMIT_Z_MAX   33  // Chân 7 ESP32  - Cong tac hanh trinh Max Z (Bao ve dinh)

#define PIN_MOTORS_EN     27  // Chân 10 ESP32 - Enable Driver A4988 (LOW = Bat)
#define PIN_SERVO_PWM     13  // Chân 13 ESP32 - Tin hieu Servo 360 do
#define PIN_IR_GATE       35  // Chân 5 ESP32  - Cam bien hong ngoai cong vao
#define PIN_LED_STATUS    2   // Chân 19 ESP32 - LED trang thai
#define PIN_BUZZER        4   // Chân 20 ESP32 - Coi (Active LOW)

#define BUZZER_ACTIVE_LOW false // SS8050 la transistor NPN: HIGH = Keu, LOW = Tat
void buzzerOn()  { digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_LOW ? LOW : HIGH); }
void buzzerOff() { digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_LOW ? HIGH : LOW); }
void beep(int count = 1, int durationMs = 80) {
  for (int i = 0; i < count; i++) {
    buzzerOn(); delay(durationMs); buzzerOff();
    if (i < count - 1) delay(80);
  }
}

// RFID RC522 (SPI)
#define PIN_RFID_SS       5
#define PIN_RFID_RST      17
#define PIN_RFID_SCK      18
#define PIN_RFID_MISO     19
#define PIN_RFID_MOSI     23

LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo trayServo;
WebServer server(80);
Preferences prefs;

// --------------------------- 2. CAU HINH WI-FI ------------------------------------------------
const char* WIFI_SSID     = "VIETSET_TECH";
const char* WIFI_PASS     = "vs68686868";
const char* AP_SSID       = "BaiDoXe_ESP32";
const char* AP_PASS       = "12345678";

// --------------------------- 3. TOA DO 6 O DO & CAU HINH SERVO 360 DO -------------------------
#define TOTAL_SLOTS 6

struct SlotInfo {
  bool occupied;
  String uid;
  String plate;
  String parkTime;
  long stepX; // So xung truc X tinh tu cong tac hanh trinh Home (0,0)
  long stepZ; // So xung truc Z tinh tu cong tac hanh trinh Home (0,0)
};

SlotInfo slots[TOTAL_SLOTS] = {
  {false, "", "", "",  2000,  1500}, // Slot 1: Tang 1, Cot 1
  {false, "", "", "",  6000,  1500}, // Slot 2: Tang 1, Cot 2
  {false, "", "", "",  2000,  4500}, // Slot 3: Tang 2, Cot 1
  {false, "", "", "",  6000,  4500}, // Slot 4: Tang 2, Cot 2
  {false, "", "", "",  2000,  7500}, // Slot 5: Tang 3, Cot 1
  {false, "", "", "",  6000,  7500}  // Slot 6: Tang 3, Cot 2
};

long currentX = 0;
long currentZ = 0;
bool isHomed  = false;

// --- CAU HINH CHIEU QUAY DONG CO BUOC (DOI true/false NEU BI NGUOC CHIEU) ---
#define INVERT_DIR_X  false   // false = Ve Home X chay sang Trai | true = Ve Home X chay sang Phai
#define INVERT_DIR_Z  true    // true = Da dao chieu Z de khi ve Home (0) ban nang ha XUONG DUOI

// --- CAU HINH SERVO 360 DO (QUAY LIEN TUC) ---
// Neu chieu quay bi nguoc (bam Day lai thanh Rut), chi can dao 2 so 30 va 150 cho nhau!
#define SERVO_PWM_STOP     90   // Dung im
#define SERVO_PWM_EXTEND   30   // Quay thuan: Tho thanh rang ra (do khay / day vao o)
#define SERVO_PWM_RETRACT  150  // Quay nguoc: Rut thanh rang ve

long servoExtendMs  = 800; // Thoi gian quay de tho ra het co (ms) - Luu trong Flash NVS
long servoRetractMs = 800; // Thoi gian quay de rut ve het co (ms)  - Luu trong Flash NVS

bool waitingForPlate = false;
int pendingSlot = -1;
String pendingUID = "";
int sessionId = 100;

// --------------------------- 4. DOC/GHI FLASH NVS (TOA DO + SERVO 360) ------------------------
void loadConfigFromNVS() {
  prefs.begin("baidoxe", false);
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    String keyX = "sx" + String(i);
    String keyZ = "sz" + String(i);
    slots[i].stepX = prefs.getLong(keyX.c_str(), slots[i].stepX);
    slots[i].stepZ = prefs.getLong(keyZ.c_str(), slots[i].stepZ);
    Serial.printf("[NVS] Slot %d -> X = %ld, Z = %ld\n", i + 1, slots[i].stepX, slots[i].stepZ);
  }
  servoExtendMs  = prefs.getLong("srvExt", 800);
  servoRetractMs = prefs.getLong("srvRet", 800);
  Serial.printf("[NVS] Servo 360 -> Extend = %ld ms | Retract = %ld ms\n", servoExtendMs, servoRetractMs);
  prefs.end();
}

void saveSlotCoordinateToNVS(int slotIdx, long x, long z) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;
  slots[slotIdx].stepX = x;
  slots[slotIdx].stepZ = z;
  prefs.begin("baidoxe", false);
  prefs.putLong(("sx" + String(slotIdx)).c_str(), x);
  prefs.putLong(("sz" + String(slotIdx)).c_str(), z);
  prefs.end();
  Serial.printf("[NVS-SAVE] Slot %d: X = %ld, Z = %ld\n", slotIdx + 1, x, z);
  beep(2, 60);
}

void saveServoTimingToNVS(long extMs, long retMs) {
  servoExtendMs  = constrain(extMs, 100, 5000);
  servoRetractMs = constrain(retMs, 100, 5000);
  prefs.begin("baidoxe", false);
  prefs.putLong("srvExt", servoExtendMs);
  prefs.putLong("srvRet", servoRetractMs);
  prefs.end();
  Serial.printf("[NVS-SAVE] Servo 360: Extend = %ld ms, Retract = %ld ms\n", servoExtendMs, servoRetractMs);
  beep(1, 60);
}

// --------------------------- 5. DIEU KHIEN SERVO 360 DO ---------------------------------------
void servoStop() {
  trayServo.write(SERVO_PWM_STOP);
}

void servoRunTime(int pwmVal, long durationMs) {
  trayServo.write(pwmVal);
  delay(durationMs);
  servoStop();
}

void servoExtend() {
  Serial.printf("[SERVO 360] Quay tho ra trong %ld ms...\n", servoExtendMs);
  servoRunTime(SERVO_PWM_EXTEND, servoExtendMs);
}

void servoRetract() {
  Serial.printf("[SERVO 360] Quay rut ve trong %ld ms...\n", servoRetractMs);
  servoRunTime(SERVO_PWM_RETRACT, servoRetractMs);
}

// --------------------------- 6. DIEU KHIEN STEPPER & CONG TAC HANH TRINH ----------------------
void stepPulse(int stepPin, int delayUs = 450) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(delayUs);
}

bool isLimitXHomeHit() { return digitalRead(PIN_LIMIT_X_HOME) == LOW; }
bool isLimitZMinHit()  { return digitalRead(PIN_LIMIT_Z_MIN) == LOW; }
bool isLimitZMaxHit()  { return digitalRead(PIN_LIMIT_Z_MAX) == LOW; }

void setDirX(bool movePositive) {
  digitalWrite(PIN_DIR_X, (movePositive ^ INVERT_DIR_X) ? HIGH : LOW);
}

void setDirZ(bool movePositive) {
  digitalWrite(PIN_DIR_Z, (movePositive ^ INVERT_DIR_Z) ? HIGH : LOW);
}

// Ve diem lay xe (0,0) bang 2 cong tac hanh trinh X_HOME va Z_MIN
void homeAxes() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DANG VE HOME...");
  lcd.setCursor(0, 1); lcd.print("Cho cong tac HT");
  Serial.println("\n>>> [HOMING] VE DIEM LAY XE (X=0, Z=0) BANG CONG TAC HANH TRINH <<<");

  digitalWrite(PIN_MOTORS_EN, LOW);
  servoStop();

  // 1. Ha Z cham LIMIT_Z_MIN (dong thoi bao ve neu cham nham Z_MAX)
  setDirZ(false);
  int timeoutZ = 15000;
  bool hitWrongZ = false;
  while (!isLimitZMinHit() && timeoutZ-- > 0) {
    if (isLimitZMaxHit()) {
      hitWrongZ = true;
      Serial.println("[LOI HOMING Z] Cham nham LIMIT_Z_MAX! Hay kiem tra chieu quay Z hoac day B1/B4!");
      beep(3, 120);
      break;
    }
    stepPulse(PIN_STEP_Z, 600);
    if (timeoutZ % 200 == 0) yield();
  }
  if (!hitWrongZ && isLimitZMinHit()) {
    setDirZ(true);
    for (int i = 0; i < 150; i++) stepPulse(PIN_STEP_Z, 600);
    delay(50);
    setDirZ(false);
    timeoutZ = 600;
    while (!isLimitZMinHit() && timeoutZ-- > 0) stepPulse(PIN_STEP_Z, 900);
  }
  currentZ = 0;
  Serial.println("[HOMING Z OK] Z = 0");

  // 2. Keo X cham LIMIT_X_HOME
  setDirX(false);
  int timeoutX = 15000;
  while (!isLimitXHomeHit() && timeoutX-- > 0) {
    stepPulse(PIN_STEP_X, 500);
    if (timeoutX % 200 == 0) yield();
  }
  if (isLimitXHomeHit()) {
    setDirX(true);
    for (int i = 0; i < 150; i++) stepPulse(PIN_STEP_X, 500);
    delay(50);
    setDirX(false);
    timeoutX = 600;
    while (!isLimitXHomeHit() && timeoutX-- > 0) stepPulse(PIN_STEP_X, 800);
  }
  currentX = 0;
  Serial.println("[HOMING X OK] X = 0");

  isHomed = true;
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("HOME OK (0, 0)");
  lcd.setCursor(0, 1); lcd.print("San sang don xe");
  beep(1, 100);
}

void jogX(long steps) {
  if (steps == 0) return;
  digitalWrite(PIN_MOTORS_EN, LOW);
  bool dir = (steps > 0);
  setDirX(dir);
  long count = abs(steps);
  // Cho phep thoat ra neu dang de san tren CTHT X khi chay duong (+)
  bool allowLeaveHome = (dir && isLimitXHomeHit());

  for (long i = 0; i < count; i++) {
    bool hitX = isLimitXHomeHit();
    if (!hitX) allowLeaveHome = false;
    if (hitX && !allowLeaveHome) {
      currentX = 0;
      Serial.println("\n[DUNG KHAN CAP] Cham CTHT HOME X! Dat X = 0");
      beep(1, 120);
      return;
    }
    stepPulse(PIN_STEP_X, 450);
    currentX += (dir ? 1 : -1);
    if (i % 200 == 0) yield();
  }
  Serial.printf("[JOG X] X = %ld | Z = %ld\n", currentX, currentZ);
}

void jogZ(long steps) {
  if (steps == 0) return;
  digitalWrite(PIN_MOTORS_EN, LOW);
  bool dir = (steps > 0);
  setDirZ(dir);
  long count = abs(steps);
  bool allowLeaveMin = (dir && isLimitZMinHit());
  bool allowLeaveMax = (!dir && isLimitZMaxHit());

  for (long i = 0; i < count; i++) {
    bool hitMin = isLimitZMinHit();
    bool hitMax = isLimitZMaxHit();
    if (!hitMin) allowLeaveMin = false;
    if (!hitMax) allowLeaveMax = false;

    if (hitMin && !allowLeaveMin) {
      currentZ = 0;
      Serial.println("\n[DUNG KHAN CAP] Cham CTHT MIN Z! Dat Z = 0");
      beep(1, 120);
      return;
    }
    if (hitMax && !allowLeaveMax) {
      Serial.println("\n[DUNG KHAN CAP] Cham CTHT MAX Z (Dinh)! Dung!");
      beep(3, 100);
      return;
    }
    stepPulse(PIN_STEP_Z, 550);
    currentZ += (dir ? 1 : -1);
    if (i % 200 == 0) yield();
  }
  Serial.printf("[JOG Z] X = %ld | Z = %ld\n", currentX, currentZ);
}

void moveToAbsolute(long targetX, long targetZ) {
  if (!isHomed) homeAxes();
  digitalWrite(PIN_MOTORS_EN, LOW);
  Serial.printf("[MOVE] (%ld, %ld) -> (%ld, %ld)\n", currentX, currentZ, targetX, targetZ);
  jogZ(targetZ - currentZ);
  delay(150);
  jogX(targetX - currentX);
  delay(150);
}

// --------------------------- 7. CHU TRINH GUI XE & LAY XE -------------------------------------
void storeCarToSlot(int slotIdx) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;
  long targetX = slots[slotIdx].stepX;
  long targetZ = slots[slotIdx].stepZ;

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Gui xe -> O "); lcd.print(slotIdx + 1);
  lcd.setCursor(0, 1); lcd.print("X:" + String(targetX) + " Z:" + String(targetZ));

  // 1. Phat xung tu diem lay xe (0,0) den dung toa do O
  moveToAbsolute(targetX, targetZ);
  delay(300);

  // 2. Servo 360 quay tho thanh rang dua khay xe vao o
  lcd.setCursor(0, 1); lcd.print("Day khay vao o  ");
  servoExtend();
  delay(400);

  // 3. Servo 360 quay nguoc rut thanh rang ra khoi khay
  lcd.setCursor(0, 1); lcd.print("Rut thanh rang  ");
  servoRetract();
  delay(300);

  // 4. Quay ve diem lay xe (0,0)
  lcd.setCursor(0, 1); lcd.print("Ve diem lay xe  ");
  moveToAbsolute(0, 0);

  slots[slotIdx].occupied = true;
  beep(1, 200);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DA CAT XE XONG!");
  lcd.setCursor(0, 1); lcd.print("O " + String(slotIdx + 1) + " da co xe");
}

void retrieveCarFromSlot(int slotIdx) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;
  long targetX = slots[slotIdx].stepX;
  long targetZ = slots[slotIdx].stepZ;

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Lay xe -> O "); lcd.print(slotIdx + 1);
  lcd.setCursor(0, 1); lcd.print("X:" + String(targetX) + " Z:" + String(targetZ));

  // 1. Di chuyen den dung toa do O can lay
  moveToAbsolute(targetX, targetZ);
  delay(300);

  // 2. Servo 360 tho thanh rang vao do khay xe
  lcd.setCursor(0, 1); lcd.print("Do lay khay xe  ");
  servoExtend();
  delay(400);

  // 3. Servo 360 rut khay xe ra khoi o
  lcd.setCursor(0, 1); lcd.print("Keo khay xe ra  ");
  servoRetract();
  delay(300);

  // 4. Mang xe ve diem tra xe (0,0)
  lcd.setCursor(0, 1); lcd.print("Ve diem tra xe  ");
  moveToAbsolute(0, 0);

  slots[slotIdx].occupied = false;
  slots[slotIdx].uid = "";
  slots[slotIdx].plate = "";
  beep(2, 100);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MOI NHAN LAI XE!");
  lcd.setCursor(0, 1); lcd.print("Cam on quy khach");
}

// --------------------------- 8. WEB DASHBOARD & REST API --------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Bãi Đỗ Xe Thông Minh ESP32 (6 Ô)</title>
  <style>
    :root { --bg: #0f172a; --panel: #1e293b; --card: #334155; --text: #f8fafc; --accent: #38bdf8; --success: #22c55e; --danger: #ef4444; --warning: #f59e0b; }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', sans-serif; background: var(--bg); color: var(--text); padding: 14px; }
    .container { max-width: 960px; margin: auto; }
    header { text-align: center; margin-bottom: 16px; }
    h1 { font-size: 1.5rem; color: var(--accent); margin-bottom: 4px; }
    .badge { display: inline-block; padding: 4px 10px; border-radius: 999px; font-size: 0.78rem; background: var(--card); }
    .grid-2 { display: grid; grid-template-columns: 1.15fr 0.85fr; gap: 14px; margin-bottom: 14px; }
    @media(max-width: 700px) { .grid-2 { grid-template-columns: 1fr; } }
    .card { background: var(--panel); padding: 14px; border-radius: 10px; border: 1px solid rgba(255,255,255,0.08); margin-bottom: 14px; }
    .card h2 { font-size: 1.05rem; color: var(--accent); margin-bottom: 10px; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 5px; }
    .slots-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
    .slot-box { background: var(--card); padding: 9px; border-radius: 8px; text-align: center; border-left: 4px solid var(--success); }
    .slot-box.occupied { border-left-color: var(--danger); background: #451a1a; }
    .slot-title { font-weight: bold; font-size: 0.9rem; }
    .slot-plate { font-size: 1rem; color: #fde047; font-weight: bold; margin: 2px 0; }
    .slot-coord { font-size: 0.7rem; color: #94a3b8; }
    .status-row { display: flex; justify-content: space-between; padding: 5px 0; border-bottom: 1px solid rgba(255,255,255,0.05); font-size: 0.88rem; }
    .sw-dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; margin-right: 5px; }
    .sw-active { background: var(--danger); box-shadow: 0 0 6px var(--danger); }
    .sw-idle { background: #64748b; }
    .btn-group { display: grid; grid-template-columns: repeat(4, 1fr); gap: 7px; margin-top: 8px; }
    @media(max-width: 550px) { .btn-group { grid-template-columns: repeat(2, 1fr); } }
    button { background: var(--card); color: var(--text); border: none; padding: 9px; border-radius: 6px; font-weight: bold; cursor: pointer; font-size: 0.82rem; }
    button:hover { background: var(--accent); color: #000; }
    .btn-primary { background: #0284c7; }
    .btn-success { background: #16a34a; }
    .btn-danger  { background: #dc2626; }
    .btn-warn    { background: #d97706; }
    .save-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-top: 10px; }
    .srv-row { display: flex; gap: 8px; align-items: center; margin-top: 8px; flex-wrap: wrap; }
    input[type=number] { background: #0f172a; color: #fff; border: 1px solid #475569; padding: 7px; border-radius: 5px; width: 90px; text-align: center; }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>BÃI ĐỖ XE THÔNG MINH ESP32</h1>
      <p class="badge">6 Ô Đỗ &bull; Công Tắc Hành Trình &bull; Servo 360° &bull; Lưu Flash NVS</p>
    </header>

    <div class="grid-2">
      <div class="card">
        <h2>Trạng Thái 6 Ô Đỗ (3 Tầng x 2 Cột)</h2>
        <div class="slots-grid" id="slotsView">Đang tải...</div>
      </div>

      <div class="card">
        <h2>Công Tắc Hành Trình (Điểm Lấy Xe 0,0)</h2>
        <div class="status-row"><span>Tọa độ hiện tại:</span><strong id="curCoord" style="color:var(--accent);">X=0 | Z=0</strong></div>
        <div class="status-row"><span>CTHT Home X (GPIO 16):</span><span id="swX"><span class="sw-dot sw-idle"></span>Hở</span></div>
        <div class="status-row"><span>CTHT Min Z (GPIO 32):</span><span id="swZMin"><span class="sw-dot sw-idle"></span>Hở</span></div>
        <div class="status-row"><span>CTHT Max Z (GPIO 33):</span><span id="swZMax"><span class="sw-dot sw-idle"></span>Hở</span></div>
        <div class="status-row"><span>Homing:</span><strong id="homeStatus" style="color:var(--warning);">Chưa Home</strong></div>
        <button class="btn-primary" style="width:100%; margin-top:10px;" onclick="fetch('/api/home')">&#x21bb; Về Điểm Lấy Xe (0, 0)</button>
      </div>
    </div>

    <div class="card">
      <h2>1. Hiệu Chỉnh Tọa Độ Xung 6 Ô Đỗ (Tính Từ CTHT)</h2>
      <div class="btn-group">
        <button onclick="jog('z', 1000)">&#x25b2; Z Lên +1000</button>
        <button onclick="jog('z', 200)">&#x25b2; Z Lên +200</button>
        <button onclick="jog('z', -200)">&#x25bc; Z Xuống -200</button>
        <button onclick="jog('z', -1000)">&#x25bc; Z Xuống -1000</button>
        <button onclick="jog('x', -1000)">&laquo; X Trái -1000</button>
        <button onclick="jog('x', -200)">&lsaquo; X Trái -200</button>
        <button onclick="jog('x', 200)">X Phải &rsaquo; +200</button>
        <button onclick="jog('x', 1000)">X Phải &raquo; +1000</button>
      </div>
      <div class="save-grid">
        <button class="btn-success" onclick="saveSlot(1)">LƯU Ô 1</button>
        <button class="btn-success" onclick="saveSlot(2)">LƯU Ô 2</button>
        <button class="btn-success" onclick="saveSlot(3)">LƯU Ô 3</button>
        <button class="btn-success" onclick="saveSlot(4)">LƯU Ô 4</button>
        <button class="btn-success" onclick="saveSlot(5)">LƯU Ô 5</button>
        <button class="btn-success" onclick="saveSlot(6)">LƯU Ô 6</button>
      </div>
    </div>

    <div class="card">
      <h2>2. Hiệu Chỉnh Servo 360° (Đỡ Khay & Rút Khay)</h2>
      <p style="font-size:0.82rem; color:#94a3b8;">Chỉnh thời gian quay (ms) để thanh răng thò ra đỡ được khay và rút ra khỏi khay vừa khít:</p>
      <div class="srv-row">
        <label>Thời gian Đẩy Ra (ms):</label>
        <input type="number" id="extMs" value="800" step="50">
        <label>Thời gian Rút Về (ms):</label>
        <input type="number" id="retMs" value="800" step="50">
        <button class="btn-primary" onclick="saveServo()">Lưu Thời Gian Vào Flash</button>
      </div>
      <div class="btn-group">
        <button class="btn-warn" onclick="fetch('/api/servo?cmd=extend')">Test Đẩy Ra (Full)</button>
        <button class="btn-warn" onclick="fetch('/api/servo?cmd=retract')">Test Rút Về (Full)</button>
        <button onclick="fetch('/api/servo?cmd=nudge_ext')">Nhích Ra +100ms</button>
        <button onclick="fetch('/api/servo?cmd=nudge_ret')">Nhích Về +100ms</button>
      </div>
    </div>
  </div>

  <script>
    let srvLoaded = false;
    function jog(axis, steps) { fetch('/api/jog?axis='+axis+'&steps='+steps).then(r=>r.json()).then(updateUI); }
    function saveSlot(s) { if(confirm('Lưu tọa độ hiện tại vào Ô '+s+'?')) fetch('/api/save_slot?slot='+s).then(r=>r.text()).then(alert); }
    function saveServo() {
      let e = document.getElementById('extMs').value, r = document.getElementById('retMs').value;
      fetch('/api/servo_save?ext='+e+'&ret='+r).then(res=>res.text()).then(alert);
    }
    function updateUI(d) {
      if(!d) return;
      document.getElementById('curCoord').innerText = 'X = '+d.currentX+' | Z = '+d.currentZ;
      document.getElementById('homeStatus').innerText = d.isHomed ? 'Đã Home (0,0)' : 'Chưa Home';
      document.getElementById('homeStatus').style.color = d.isHomed ? 'var(--success)' : 'var(--warning)';
      if(!srvLoaded && d.servoExtendMs) {
        document.getElementById('extMs').value = d.servoExtendMs;
        document.getElementById('retMs').value = d.servoRetractMs;
        srvLoaded = true;
      }
      const rSw = (id, a) => document.getElementById(id).innerHTML = a ? '<span class="sw-dot sw-active"></span>ĐANG CHẠM' : '<span class="sw-dot sw-idle"></span>Hở';
      rSw('swX', d.swX); rSw('swZMin', d.swZMin); rSw('swZMax', d.swZMax);
      let html = '';
      d.slots.forEach(s => {
        html += `<div class="slot-box ${s.occupied?'occupied':''}">
          <div class="slot-title">Ô ĐỖ ${s.slot}</div>
          <div class="slot-plate">${s.occupied?(s.plate||'Có Xe ('+s.uid+')'):'TRỐNG'}</div>
          <div class="slot-coord">X=${s.stepX} | Z=${s.stepZ}</div></div>`;
      });
      document.getElementById('slotsView').innerHTML = html;
    }
    setInterval(()=>fetch('/api/status').then(r=>r.json()).then(updateUI).catch(()=>{}), 1000);
  </script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handleApiStatus() {
  String json = "{";
  json += "\"connected\":true,";
  json += "\"isHomed\":" + String(isHomed ? "true" : "false") + ",";
  json += "\"currentX\":" + String(currentX) + ",";
  json += "\"currentZ\":" + String(currentZ) + ",";
  json += "\"servoExtendMs\":" + String(servoExtendMs) + ",";
  json += "\"servoRetractMs\":" + String(servoRetractMs) + ",";
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
      lcd.setCursor(0, 1); lcd.print("Luu O " + String(pendingSlot + 1));
      beep(1, 150);
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

void handleApiHome() { homeAxes(); server.send(200, "text/plain", "HOME_OK"); }

void handleApiServo() {
  String cmd = server.arg("cmd");
  if (cmd == "extend")         servoExtend();
  else if (cmd == "retract")   servoRetract();
  else if (cmd == "nudge_ext") servoRunTime(SERVO_PWM_EXTEND, 100);
  else if (cmd == "nudge_ret") servoRunTime(SERVO_PWM_RETRACT, 100);
  else servoStop();
  server.send(200, "text/plain", "SERVO_OK");
}

void handleApiServoSave() {
  if (server.hasArg("ext") && server.hasArg("ret")) {
    saveServoTimingToNVS(server.arg("ext").toInt(), server.arg("ret").toInt());
    server.send(200, "text/plain", "OK: Da luu Servo 360 (Day=" + String(servoExtendMs) + "ms, Rut=" + String(servoRetractMs) + "ms) vao Flash!");
    return;
  }
  server.send(400, "text/plain", "MISSING_PARAMS");
}

// --------------------------- 9. GIAO TIEP SERIAL MONITOR --------------------------------------
void printSerialHelp() {
  Serial.println("\n=======================================================================");
  Serial.println("   HIEU CHINH XUNG 6 O DO & SERVO 360 DO (LUU FLASH NVS VINH VIEN)     ");
  Serial.println("=======================================================================");
  Serial.println(" [w]/[s] : Truc Z Len/Xuong (+/-200 xung) | [W]/[S] : (+/-1000 xung)");
  Serial.println(" [d]/[a] : Truc X Phai/Trai (+/-200 xung) | [D]/[A] : (+/-1000 xung)");
  Serial.println(" [h]     : Chay ve DIEM LAY XE (0, 0) bang cong tac hanh trinh");
  Serial.println(" [t]     : Kiem tra trang thai 3 cong tac hanh trinh");
  Serial.println(" [1]..[6]: Luu so xung hien tai vao O 1..6 (Flash NVS)");
  Serial.println(" [p]     : In bang toa do 6 o & thong so Servo 360");
  Serial.println("-----------------------------------------------------------------------");
  Serial.println(" SERVO 360 DO:");
  Serial.println(" [e]     : Quay DAY khay ra (" + String(servoExtendMs) + " ms)");
  Serial.println(" [r]     : Quay RUT thanh rang ve (" + String(servoRetractMs) + " ms)");
  Serial.println(" [q]/[z] : Nhich thanh rang Ra / Ve (+100 ms) de can khoang cach");
  Serial.println(" [+]/[-] : Tang / Giam thoi gian Servo (+/- 100 ms) va luu Flash");
  Serial.println("=======================================================================");
  Serial.printf("-> Hien tai: X=%ld | Z=%ld | Extend=%ldms | Retract=%ldms\n\n", currentX, currentZ, servoExtendMs, servoRetractMs);
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
      case 'e': servoExtend(); break;
      case 'r': servoRetract(); break;
      case 'q': servoRunTime(SERVO_PWM_EXTEND, 100); break;
      case 'z': servoRunTime(SERVO_PWM_RETRACT, 100); break;
      case '+': saveServoTimingToNVS(servoExtendMs + 100, servoRetractMs + 100); break;
      case '-': saveServoTimingToNVS(servoExtendMs - 100, servoRetractMs - 100); break;
      case 't':
        Serial.printf("[SWITCH] X_HOME(16): %s | Z_MIN(32): %s | Z_MAX(33): %s\n",
          isLimitXHomeHit() ? "CHAM" : "HO", isLimitZMinHit() ? "CHAM" : "HO", isLimitZMaxHit() ? "CHAM" : "HO");
        break;
      case '1': case '2': case '3': case '4': case '5': case '6':
        saveSlotCoordinateToNVS(c - '1', currentX, currentZ);
        break;
      case 'p':
        Serial.println("\n>>> BANG THONG SO HIEN TAI TRONG FLASH NVS: <<<");
        for (int i = 0; i < TOTAL_SLOTS; i++)
          Serial.printf("  O %d: X = %ld xung | Z = %ld xung\n", i + 1, slots[i].stepX, slots[i].stepZ);
        Serial.printf("  Servo 360: Day ra = %ld ms | Rut ve = %ld ms\n\n", servoExtendMs, servoRetractMs);
        break;
      default: printSerialHelp(); break;
    }
  }
}

// --------------------------- 10. SETUP & LOOP -------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  pinMode(PIN_STEP_X, OUTPUT); pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT); pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT); digitalWrite(PIN_MOTORS_EN, LOW);

  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);
  pinMode(PIN_IR_GATE, INPUT);
  pinMode(PIN_LED_STATUS, OUTPUT); digitalWrite(PIN_LED_STATUS, LOW);

  buzzerOff(); pinMode(PIN_BUZZER, OUTPUT); buzzerOff();

  trayServo.attach(PIN_SERVO_PWM);
  servoStop();

  lcd.init(); lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("BAI DO XE 6 O");
  lcd.setCursor(0, 1); lcd.print("Dang khoi tao...");
  beep(1, 100); // Test coi 1 tieng khi khoi dong he thong

  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  loadConfigFromNVS();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Ket noi WiFi...");
  lcd.setCursor(0, 1); lcd.print(WIFI_SSID);
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 16) { delay(500); retry++; }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    Serial.printf("\n[WIFI OK] http://%s\n", WiFi.localIP().toString().c_str());
    MDNS.begin("baidoxe");
    delay(1500);
  } else {
    WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID, AP_PASS);
    lcd.clear(); lcd.setCursor(0, 0); lcd.print("AP: BaiDoXe_ESP");
    lcd.setCursor(0, 1); lcd.print(WiFi.softAPIP().toString());
    Serial.printf("\n[AP MODE] http://%s\n", WiFi.softAPIP().toString().c_str());
    delay(1500);
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/plate", HTTP_POST, handleApiPlate);
  server.on("/api/jog", HTTP_GET, handleApiJog);
  server.on("/api/save_slot", HTTP_GET, handleApiSaveSlot);
  server.on("/api/home", HTTP_GET, handleApiHome);
  server.on("/api/servo", HTTP_GET, handleApiServo);
  server.on("/api/servo_save", HTTP_GET, handleApiServoSave);
  server.begin();

  homeAxes();
  printSerialHelp();
}

void loop() {
  server.handleClient();
  handleSerialCommands();

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String scannedUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) scannedUID += "0";
      scannedUID += String(rfid.uid.uidByte[i], HEX);
    }
    scannedUID.toUpperCase();
    rfid.PICC_HaltA(); rfid.PCD_StopCrypto1();
    Serial.println("\n[RFID] UID: " + scannedUID);
    beep(1, 100);

    int foundSlot = -1;
    for (int i = 0; i < TOTAL_SLOTS; i++) {
      if (slots[i].occupied && slots[i].uid == scannedUID) { foundSlot = i; break; }
    }

    if (foundSlot >= 0) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("The: " + scannedUID);
      lcd.setCursor(0, 1); lcd.print("Tra xe O " + String(foundSlot + 1));
      retrieveCarFromSlot(foundSlot);
    } else {
      int emptySlot = -1;
      for (int i = 0; i < TOTAL_SLOTS; i++) {
        if (!slots[i].occupied) { emptySlot = i; break; }
      }
      if (emptySlot == -1) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("BAI XE DA DAY!");
        lcd.setCursor(0, 1); lcd.print("Vui long quay lai");
        beep(3, 150);
      } else {
        pendingSlot = emptySlot;
        pendingUID = scannedUID;
        slots[emptySlot].uid = scannedUID;
        waitingForPlate = true;
        sessionId++;
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Vao O " + String(emptySlot + 1));
        lcd.setCursor(0, 1); lcd.print("Cho quet bien so");
      }
    }
  }
  delay(10);
}
