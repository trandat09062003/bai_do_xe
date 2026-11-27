/*
 * ==============================================================================================
 * Project   : HE THONG QUAN LY BAI DO XE THONG MINH ESP32 (6 O DO / 3 TANG x 2 COT)
 * Bo mach   : Altium PCB Bai_Do_Xe_Thong_Minh_ESP32 (ESP32 DevKit V1 30-Pin)
 * Dong co   : 2 x Stepper NEMA 17 qua Driver A4988 (Trục X ngang, Trục Z nâng tầng)
 * Co cau    : Servo 360 do (quay lien tuc) keo/day thanh rang - banh rang
 * Dinh vi   : 2 Cong tac hanh trinh Home X (GPIO 16) & Min Z (GPIO 32) = Diem lay/tra xe (0,0)
 *             + 1 Cong tac bao ve dinh Max Z (GPIO 33)
 * Luu tru   : Preferences Flash NVS (Luu vinh vien toa do 6 o do & thoi gian quay Servo 360)
 * Ngoai vi  : RFID RC522, LCD I2C 1602/2004, Cam bien IR cong vao (GPIO 35), Buzzer SS8050, LED
 * Giao dien : Web Quan Ly Chuyen Nghiep 5 Tab (Tong quan, Dieu khien, Vi tri do, Quan ly xe, Lich su)
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
#define PIN_LIMIT_X_HOME  16  // Chân 21 ESP32 - SW1: Cong tac hanh trinh Home X (0)

#define PIN_STEP_Z        25  // Chân 8 ESP32  - Step Truc Z
#define PIN_DIR_Z         26  // Chân 9 ESP32  - Dir Truc Z
#define PIN_LIMIT_Z_MIN   32  // Chân 6 ESP32  - SW3: Cong tac hanh trinh Min Z (0)
#define PIN_LIMIT_Z_MAX   33  // Chân 7 ESP32  - SW2: Cong tac hanh trinh Max Z (Bao ve dinh)

#define PIN_MOTORS_EN     27  // Chân 10 ESP32 - Enable Driver A4988 (LOW = Bat)
#define PIN_SERVO_PWM     13  // Chân 13 ESP32 - Tin hieu Servo 360 do
#define PIN_IR_GATE       35  // Chân 5 ESP32  - Cam bien hong ngoai cong vao
#define PIN_LED_STATUS    2   // Chân 19 ESP32 - LED trang thai
#define PIN_BUZZER        4   // Chân 20 ESP32 - Coi

#define BUZZER_ACTIVE_LOW false // Transistor NPN (C1815 / SS8050): HIGH = Keu, LOW = Tat
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

// --------------------------- 3. TOA DO CHUAN 6 O DO & CAU HINH SERVO 360 ----------------------
#define TOTAL_SLOTS 6

struct SlotInfo {
  bool occupied;
  String uid;
  String plate;
  String parkTime;
  long stepX; // So xung truc X tinh tu goc (0,0)
  long stepZ; // So xung truc Z tinh tu goc (0,0)
};

// Toa do chuan da hieu chinh thuc te cho mo hinh 3 tang x 2 cot:
SlotInfo slots[TOTAL_SLOTS] = {
  {false, "", "", "",  14000,      0}, // O 1: Tang 1, Cot 1
  {false, "", "", "",  25400,      0}, // O 2: Tang 1, Cot 2
  {false, "", "", "",  14000,  10000}, // O 3: Tang 2, Cot 1
  {false, "", "", "",  25400,  10000}, // O 4: Tang 2, Cot 2
  {false, "", "", "",  14000,  19000}, // O 5: Tang 3, Cot 1
  {false, "", "", "",  25400,  19000}  // O 6: Tang 3, Cot 2
};

// Lich su vao/ra gan day (luu toi da 20 luot)
#define MAX_HISTORY 20
struct HistoryItem {
  String timeStr;
  String action; // "VAO" hoac "RA"
  int slot;
  String uid;
  String plate;
};
HistoryItem historyLog[MAX_HISTORY];
int historyCount = 0;

long currentX = 0;
long currentZ = 0;
bool isHomed  = true;

// Chieu quay dong co buoc
#define INVERT_DIR_X  false   // false = Ve Home X chay sang Trai | true = Chay sang Phai
#define INVERT_DIR_Z  true    // true = Ha xuong cham MIN Z khi ve Home (0)

// Servo 360 do (quay lien tuc)
#define SERVO_PWM_STOP     90
#define SERVO_PWM_EXTEND   30
#define SERVO_PWM_RETRACT  150

long servoExtendMs  = 800;
long servoRetractMs = 800;

// Che do van hanh: true = Tu dong (RFID + Camera ANPR), false = Thu cong / RFID nhanh khong can doi Camera
bool requireCameraAI = true;
String systemStatusText = "San sang don xe";

bool waitingForPlate = false;
int pendingSlot = -1;
String pendingUID = "";
int sessionId = 100;

String formatUptime() {
  unsigned long s = millis() / 1000;
  unsigned long h = s / 3600;
  unsigned long m = (s % 3600) / 60;
  unsigned long sec = s % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, sec);
  return String(buf);
}

void addHistory(String action, int slotNum, String uid, String plate) {
  if (historyCount < MAX_HISTORY) {
    for (int i = historyCount; i > 0; i--) historyLog[i] = historyLog[i - 1];
    historyCount++;
  } else {
    for (int i = MAX_HISTORY - 1; i > 0; i--) historyLog[i] = historyLog[i - 1];
  }
  historyLog[0] = {formatUptime(), action, slotNum, uid, plate};
}

// --------------------------- 4. DOC/GHI FLASH NVS (TOA DO + SERVO 360) ------------------------
void loadConfigFromNVS() {
  prefs.begin("baidoxe", false);
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    slots[i].stepX = prefs.getLong(("sx" + String(i)).c_str(), slots[i].stepX);
    slots[i].stepZ = prefs.getLong(("sz" + String(i)).c_str(), slots[i].stepZ);
  }
  servoExtendMs  = prefs.getLong("srvExt", 800);
  servoRetractMs = prefs.getLong("srvRet", 800);
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
  beep(2, 60);
}

void saveServoTimingToNVS(long extMs, long retMs) {
  servoExtendMs  = constrain(extMs, 100, 5000);
  servoRetractMs = constrain(retMs, 100, 5000);
  prefs.begin("baidoxe", false);
  prefs.putLong("srvExt", servoExtendMs);
  prefs.putLong("srvRet", servoRetractMs);
  prefs.end();
  beep(1, 60);
}

// --------------------------- 5. DIEU KHIEN SERVO 360 DO ---------------------------------------
void servoStop() { trayServo.write(SERVO_PWM_STOP); }
void servoRunTime(int pwmVal, long durationMs) {
  trayServo.write(pwmVal);
  delay(durationMs);
  servoStop();
}
void servoExtend()  { servoRunTime(SERVO_PWM_EXTEND, servoExtendMs); }
void servoRetract() { servoRunTime(SERVO_PWM_RETRACT, servoRetractMs); }

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

void setDirX(bool movePositive) { digitalWrite(PIN_DIR_X, (movePositive ^ INVERT_DIR_X) ? HIGH : LOW); }
void setDirZ(bool movePositive) { digitalWrite(PIN_DIR_Z, (movePositive ^ INVERT_DIR_Z) ? HIGH : LOW); }

void homeAxes() {
  systemStatusText = "Dang chay ve Home (0,0)...";
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DANG VE HOME...");
  lcd.setCursor(0, 1); lcd.print("Cho cong tac HT");

  digitalWrite(PIN_MOTORS_EN, LOW);
  servoStop();

  // 1. Ha Z cham LIMIT_Z_MIN
  setDirZ(false);
  long timeoutZ = 50000;
  bool hitWrongZ = false;
  while (!isLimitZMinHit() && timeoutZ-- > 0) {
    if (isLimitZMaxHit()) { hitWrongZ = true; beep(3, 120); break; }
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

  // 2. Keo X cham LIMIT_X_HOME
  setDirX(false);
  long timeoutX = 50000;
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

  isHomed = true;
  systemStatusText = "San sang don xe";
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
  bool allowLeaveHome = (dir && isLimitXHomeHit());

  for (long i = 0; i < count; i++) {
    bool hitX = isLimitXHomeHit();
    if (!hitX) allowLeaveHome = false;
    if (hitX && !allowLeaveHome) {
      currentX = 0;
      beep(1, 120);
      return;
    }
    stepPulse(PIN_STEP_X, 450);
    currentX += (dir ? 1 : -1);
    if (i % 200 == 0) yield();
  }
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
      beep(1, 120);
      return;
    }
    if (hitMax && !allowLeaveMax) {
      beep(3, 100);
      return;
    }
    stepPulse(PIN_STEP_Z, 550);
    currentZ += (dir ? 1 : -1);
    if (i % 200 == 0) yield();
  }
}

void moveToAbsolute(long targetX, long targetZ) {
  if (!isHomed) homeAxes();
  digitalWrite(PIN_MOTORS_EN, LOW);
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

  systemStatusText = "Dang cat xe vao Vi tri " + String(slotIdx + 1) + "...";
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Gui xe -> O "); lcd.print(slotIdx + 1);
  lcd.setCursor(0, 1); lcd.print("X:" + String(targetX) + " Z:" + String(targetZ));

  moveToAbsolute(targetX, targetZ);
  delay(300);

  lcd.setCursor(0, 1); lcd.print("Day khay vao o  ");
  servoExtend();
  delay(400);

  lcd.setCursor(0, 1); lcd.print("Rut thanh rang  ");
  servoRetract();
  delay(300);

  lcd.setCursor(0, 1); lcd.print("Ve diem lay xe  ");
  moveToAbsolute(0, 0);

  slots[slotIdx].occupied = true;
  slots[slotIdx].parkTime = formatUptime();
  addHistory("VAO", slotIdx + 1, slots[slotIdx].uid, slots[slotIdx].plate);

  systemStatusText = "Da cat xe vao Vi tri " + String(slotIdx + 1);
  beep(1, 180);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("DA CAT XE XONG!");
  lcd.setCursor(0, 1); lcd.print("O " + String(slotIdx + 1) + " da co xe");
}

void retrieveCarFromSlot(int slotIdx) {
  if (slotIdx < 0 || slotIdx >= TOTAL_SLOTS) return;
  long targetX = slots[slotIdx].stepX;
  long targetZ = slots[slotIdx].stepZ;

  String oldUid = slots[slotIdx].uid;
  String oldPlate = slots[slotIdx].plate;

  systemStatusText = "Dang lay xe tu Vi tri " + String(slotIdx + 1) + "...";
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Lay xe -> O "); lcd.print(slotIdx + 1);
  lcd.setCursor(0, 1); lcd.print("X:" + String(targetX) + " Z:" + String(targetZ));

  moveToAbsolute(targetX, targetZ);
  delay(300);

  lcd.setCursor(0, 1); lcd.print("Do lay khay xe  ");
  servoExtend();
  delay(400);

  lcd.setCursor(0, 1); lcd.print("Keo khay xe ra  ");
  servoRetract();
  delay(300);

  lcd.setCursor(0, 1); lcd.print("Ve diem tra xe  ");
  moveToAbsolute(0, 0);

  addHistory("RA", slotIdx + 1, oldUid, oldPlate);
  slots[slotIdx].occupied = false;
  slots[slotIdx].uid = "";
  slots[slotIdx].plate = "";
  slots[slotIdx].parkTime = "";

  systemStatusText = "Da tra xe tu Vi tri " + String(slotIdx + 1);
  beep(2, 100);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MOI NHAN LAI XE!");
  lcd.setCursor(0, 1); lcd.print("Cam on quy khach");
}

// --------------------------- 8. GIAO DIEN WEB QUAN LY CHUYEN NGHIEP ----------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Hệ Thống Quản Lý Bãi Đỗ Xe Thông Minh</title>
  <style>
    :root {
      --navy: #1e3a8a; --bg: #f1f5f9; --white: #ffffff; --border: #e2e8f0;
      --text: #0f172a; --muted: #64748b; --blue: #2563eb; --blue-light: #eff6ff;
      --green: #16a34a; --green-bg: #f0fdf4; --green-border: #86efac;
      --red: #dc2626; --red-bg: #fef2f2; --red-border: #fca5a5;
      --amber: #d97706;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); min-height: 100vh; display: flex; flex-direction: column; }

    /* Top Header */
    .topbar { background: var(--navy); color: #fff; padding: 12px 20px; display: flex; justify-content: space-between; align-items: center; box-shadow: 0 2px 6px rgba(0,0,0,0.12); position: sticky; top: 0; z-index: 50; }
    .brand { display: flex; align-items: center; gap: 10px; font-weight: 700; font-size: 1.05rem; letter-spacing: 0.3px; }
    .top-badges { display: flex; gap: 10px; align-items: center; font-size: 0.8rem; }
    .pill { background: rgba(255,255,255,0.16); padding: 4px 10px; border-radius: 999px; font-weight: 600; }
    .pill.online { background: #16a34a; }

    /* Main Layout */
    .layout { display: flex; flex: 1; }
    .sidebar { width: 225px; background: var(--white); border-right: 1px solid var(--border); padding: 16px 10px; flex-shrink: 0; }
    .menu-label { font-size: 0.72rem; font-weight: 700; color: #94a3b8; padding: 0 12px 8px; letter-spacing: 0.5px; }
    .nav-item { display: flex; align-items: center; gap: 10px; padding: 11px 14px; border-radius: 8px; cursor: pointer; font-weight: 600; font-size: 0.9rem; color: #334155; margin-bottom: 4px; transition: 0.15s; }
    .nav-item:hover { background: #f8fafc; color: var(--navy); }
    .nav-item.active { background: #dbeafe; color: var(--navy); }

    .content { flex: 1; padding: 20px; max-width: 1180px; }
    @media(max-width: 780px) {
      .layout { flex-direction: column; }
      .sidebar { width: 100%; display: flex; overflow-x: auto; padding: 8px; border-right: none; border-bottom: 1px solid var(--border); }
      .menu-label { display: none; }
      .nav-item { white-space: nowrap; margin-bottom: 0; margin-right: 4px; padding: 8px 12px; font-size: 0.85rem; }
    }

    .page-title { font-size: 1.3rem; font-weight: 700; margin-bottom: 4px; color: var(--text); }
    .page-sub { font-size: 0.86rem; color: var(--muted); margin-bottom: 16px; }
    .tab-pane { display: none; }
    .tab-pane.active { display: block; }

    /* KPI Cards */
    .kpi-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 14px; margin-bottom: 14px; }
    @media(max-width: 650px) { .kpi-grid { grid-template-columns: 1fr; } }
    .card { background: var(--white); border: 1px solid var(--border); border-radius: 10px; padding: 16px; box-shadow: 0 1px 2px rgba(0,0,0,0.03); margin-bottom: 14px; }
    .kpi-title { font-size: 0.82rem; color: var(--muted); font-weight: 600; margin-bottom: 6px; }
    .kpi-val { font-size: 1.8rem; font-weight: 700; color: var(--text); }
    .kpi-val.green { color: var(--green); }
    .kpi-val.red { color: var(--red); }

    /* Floor Diagrams */
    .floor-header { background: var(--navy); color: #fff; padding: 7px 14px; border-radius: 6px; font-weight: 700; font-size: 0.84rem; margin: 12px 0 8px; }
    .floor-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; }
    @media(max-width: 560px) { .floor-grid { grid-template-columns: 1fr; } }
    .slot-card { background: var(--green-bg); border: 1.5px solid var(--green-border); border-radius: 8px; padding: 14px; text-align: center; transition: 0.15s; }
    .slot-card.occupied { background: var(--red-bg); border-color: var(--red-border); }
    .slot-name { font-weight: 700; font-size: 0.95rem; margin-bottom: 4px; }
    .slot-meta { font-size: 0.8rem; color: var(--muted); margin-bottom: 6px; }
    .slot-plate-txt { font-size: 1.05rem; font-weight: 700; color: var(--red); margin: 4px 0; }
    .badge-status { display: inline-block; padding: 3px 10px; border-radius: 999px; font-size: 0.72rem; font-weight: 700; }
    .badge-free { background: #dcfce7; color: #15803d; }
    .badge-busy { background: #fee2e2; color: #b91c1c; }

    /* Buttons & Inputs */
    button { border: none; border-radius: 6px; padding: 8px 13px; font-weight: 600; font-size: 0.84rem; cursor: pointer; transition: 0.15s; background: #e2e8f0; color: var(--text); }
    button:hover { filter: brightness(0.95); }
    button:active { transform: scale(0.97); }
    .btn-blue { background: var(--blue); color: #fff; }
    .btn-green { background: var(--green); color: #fff; }
    .btn-red { background: var(--red); color: #fff; }
    .btn-amber { background: var(--amber); color: #fff; }
    .btn-navy { background: var(--navy); color: #fff; }

    /* Control Grid */
    .ctrl-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; }
    @media(max-width: 780px) { .ctrl-grid { grid-template-columns: 1fr; } }
    .step-bar { display: flex; gap: 6px; margin-bottom: 12px; flex-wrap: wrap; }
    .step-btn { background: #f8fafc; border: 1px solid #cbd5e1; padding: 6px 11px; font-size: 0.8rem; }
    .step-btn.active { background: var(--navy); color: #fff; border-color: var(--navy); }
    .dpad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; max-width: 320px; margin: auto; }
    .dpad button { padding: 13px 6px; font-size: 0.9rem; background: #f1f5f9; border: 1px solid #cbd5e1; }
    .dpad .dpad-center { background: var(--blue); color: #fff; border: none; font-size: 0.8rem; }

    .status-line { display: flex; justify-content: space-between; padding: 7px 0; border-bottom: 1px solid var(--border); font-size: 0.88rem; }
    .dot { display: inline-block; width: 9px; height: 9px; border-radius: 50%; margin-right: 6px; background: #94a3b8; }
    .dot.hit { background: var(--red); box-shadow: 0 0 6px var(--red); }

    /* Tables */
    table { width: 100%; border-collapse: collapse; font-size: 0.86rem; }
    th, td { padding: 10px 12px; text-align: left; border-bottom: 1px solid var(--border); }
    th { background: #f8fafc; color: var(--muted); font-weight: 700; font-size: 0.8rem; text-transform: uppercase; }
    input[type=number], input[type=text] { border: 1px solid #cbd5e1; border-radius: 6px; padding: 7px 10px; font-size: 0.86rem; }

    #toast { position: fixed; bottom: 20px; right: 20px; background: var(--navy); color: #fff; padding: 11px 18px; border-radius: 8px; font-weight: 600; font-size: 0.86rem; box-shadow: 0 6px 16px rgba(0,0,0,0.2); opacity: 0; pointer-events: none; transition: 0.25s; z-index: 999; }
    #toast.show { opacity: 1; }
  </style>
</head>
<body>
  <header class="topbar">
    <div class="brand">&#x1F697; HỆ THỐNG QUẢN LÝ BÃI ĐỖ XE THÔNG MINH</div>
    <div class="top-badges">
      <span class="pill" id="topCoord">X: 0 | Z: 0</span>
      <span class="pill online" id="topMode">Trực tuyến</span>
    </div>
  </header>

  <div class="layout">
    <aside class="sidebar">
      <div class="menu-label">MENU CHÍNH</div>
      <div class="nav-item active" onclick="showTab('overview', this)">&#x1F4CA; Tổng quan</div>
      <div class="nav-item" onclick="showTab('control', this)">&#x2699;&#xFE0F; Điều khiển</div>
      <div class="nav-item" onclick="showTab('slots', this)">&#x1F17F;&#xFE0F; Vị trí đỗ</div>
      <div class="nav-item" onclick="showTab('vehicles', this)">&#x1F698; Quản lý xe</div>
      <div class="nav-item" onclick="showTab('history', this)">&#x1F4CB; Lịch sử</div>
    </aside>

    <main class="content">
      <!-- TAB 1: TỔNG QUAN -->
      <div id="tab-overview" class="tab-pane active">
        <div class="page-title">Tổng quan hệ thống</div>
        <div class="page-sub">Theo dõi tình trạng hoạt động thời gian thực của bãi đỗ xe 3 tầng (6 vị trí).</div>

        <div class="kpi-grid">
          <div class="card" style="margin-bottom:0;"><div class="kpi-title">Tổng số vị trí</div><div class="kpi-val">6</div></div>
          <div class="card" style="margin-bottom:0;"><div class="kpi-title">Vị trí đang sử dụng</div><div class="kpi-val red" id="kpiBusy">0</div></div>
          <div class="card" style="margin-bottom:0;"><div class="kpi-title">Vị trí còn trống</div><div class="kpi-val green" id="kpiFree">6</div></div>
        </div>

        <div class="card" style="margin-top:14px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px;">
          <div>
            <div style="font-weight:700; font-size:0.95rem;">Trạng thái hệ thống: <span id="sysStatusTxt" style="color:var(--blue);">Sẵn sàng đón xe</span></div>
            <div style="font-size:0.82rem; color:var(--muted); margin-top:3px;" id="camModeTxt">Chế độ: Tự động (RFID + Camera ANPR)</div>
          </div>
          <div style="display:flex; gap:8px; flex-wrap:wrap;">
            <button class="btn-navy" onclick="goHome()">&#x21bb; Về Home (0,0)</button>
            <button class="btn-amber" onclick="toggleCamMode()" id="btnCamToggle">Chế độ: Chờ Camera AI</button>
          </div>
        </div>

        <div class="card">
          <div style="font-weight:700; font-size:1rem; margin-bottom:6px;">Sơ đồ bãi đỗ xe 3 tầng</div>
          <div id="floorDiagram">Đang tải sơ đồ...</div>
        </div>
      </div>

      <!-- TAB 2: ĐIỀU KHIỂN -->
      <div id="tab-control" class="tab-pane">
        <div class="page-title">Điều khiển & Hiệu chỉnh</div>
        <div class="page-sub">Điều khiển bàn nâng trục X-Z (hỗ trợ phím W/A/S/D), công tắc hành trình và Servo 360°.</div>

        <div class="ctrl-grid">
          <div class="card">
            <h3 style="margin-bottom:10px; font-size:1rem;">Tay cầm điều hướng X - Z</h3>
            <div class="step-bar">
              <button class="step-btn" onclick="setStep(50, this)">Mịn (50)</button>
              <button class="step-btn active" onclick="setStep(200, this)">Vừa (200)</button>
              <button class="step-btn" onclick="setStep(1000, this)">Nhanh (1000)</button>
              <button class="step-btn" onclick="setStep(2000, this)">Xa (2000)</button>
            </div>
            <div class="dpad">
              <div></div><button onclick="jog('z', stepSize)">&#x25B2; Z Lên</button><div></div>
              <button onclick="jog('x', -stepSize)">&#x25C0; X Trái</button>
              <button class="dpad-center" onclick="setZero()">Đặt Đây Là<br>(0, 0)</button>
              <button onclick="jog('x', stepSize)">X Phải &#x25B6;</button>
              <div></div><button onclick="jog('z', -stepSize)">&#x25BC; Z Xuống</button><div></div>
            </div>
            <button class="btn-blue" style="width:100%; margin-top:12px;" onclick="goHome()">&#x21bb; Tự Chạy Về CTHT Home (0, 0)</button>
          </div>

          <div class="card">
            <h3 style="margin-bottom:10px; font-size:1rem;">Công tắc hành trình & Servo 360°</h3>
            <div class="status-line"><span>Tọa độ hiện tại:</span><strong id="ctrlCoord" style="color:var(--blue);">X=0 | Z=0</strong></div>
            <div class="status-line"><span>CTHT Home X (SW1 - GPIO 16):</span><span id="swX"><span class="dot"></span>Hở</span></div>
            <div class="status-line"><span>CTHT Min Z (SW3 - GPIO 32):</span><span id="swZMin"><span class="dot"></span>Hở</span></div>
            <div class="status-line"><span>CTHT Max Z (SW2 - GPIO 33):</span><span id="swZMax"><span class="dot"></span>Hở</span></div>

            <div style="margin-top:14px; font-weight:700; font-size:0.88rem;">Hiệu chỉnh thời gian Servo 360° (ms):</div>
            <div style="display:flex; gap:8px; align-items:center; margin-top:8px; flex-wrap:wrap;">
              <label style="font-size:0.82rem;">Đẩy:</label><input type="number" id="extMs" value="800" step="50" style="width:75px;">
              <label style="font-size:0.82rem;">Rút:</label><input type="number" id="retMs" value="800" step="50" style="width:75px;">
              <button class="btn-blue" onclick="saveServo()">Lưu Flash</button>
            </div>
            <div style="display:grid; grid-template-columns:1fr 1fr; gap:8px; margin-top:10px;">
              <button class="btn-amber" onclick="srv('extend')">Test Đẩy Ra</button>
              <button class="btn-amber" onclick="srv('retract')">Test Rút Về</button>
              <button onclick="srv('nudge_ext')">Nhích Ra +100ms</button>
              <button onclick="srv('nudge_ret')">Nhích Về +100ms</button>
            </div>
          </div>
        </div>
      </div>

      <!-- TAB 3: VỊ TRÍ ĐỖ -->
      <div id="tab-slots" class="tab-pane">
        <div class="page-title">Quản lý 6 Vị trí đỗ & Tọa độ</div>
        <div class="page-sub">Thao tác gửi xe / lấy xe thủ công từng ô hoặc cập nhật tọa độ xung từ điểm hiện tại.</div>
        <div class="card" style="overflow-x:auto;">
          <table>
            <thead><tr><th>Vị trí</th><th>Tầng / Cột</th><th>Tọa độ (X, Z)</th><th>Trạng thái</th><th>Thao tác vận hành</th><th>Hiệu chỉnh</th></tr></thead>
            <tbody id="slotsTable"></tbody>
          </table>
        </div>
      </div>

      <!-- TAB 4: QUẢN LÝ XE -->
      <div id="tab-vehicles" class="tab-pane">
        <div class="page-title">Quản lý xe đang gửi trong bãi</div>
        <div class="page-sub">Danh sách xe đang đỗ kèm biển số ANPR và mã thẻ RFID.</div>
        <div class="card" style="overflow-x:auto;">
          <table>
            <thead><tr><th>Vị trí</th><th>Biển số xe</th><th>Mã thẻ RFID</th><th>Thời điểm vào</th><th>Thao tác</th></tr></thead>
            <tbody id="vehiclesTable"></tbody>
          </table>
        </div>
      </div>

      <!-- TAB 5: LỊCH SỬ -->
      <div id="tab-history" class="tab-pane">
        <div class="page-title">Lịch sử xe vào / ra</div>
        <div class="page-sub">Nhật ký hoạt động gần nhất của hệ thống bãi đỗ xe.</div>
        <div class="card" style="overflow-x:auto;">
          <table>
            <thead><tr><th>Thời gian</th><th>Hành động</th><th>Vị trí</th><th>Mã thẻ RFID</th><th>Biển số</th></tr></thead>
            <tbody id="historyTable"></tbody>
          </table>
        </div>
      </div>
    </main>
  </div>
  <div id="toast">Thông báo</div>

  <script>
    let stepSize = 200;
    let srvLoaded = false;

    function showTab(name, el) {
      document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
      document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
      document.getElementById('tab-' + name).classList.add('active');
      el.classList.add('active');
    }
    function showToast(msg) {
      const t = document.getElementById('toast');
      t.innerText = msg;
      t.classList.add('show');
      setTimeout(() => t.classList.remove('show'), 2300);
    }
    function setStep(val, el) {
      stepSize = val;
      document.querySelectorAll('.step-btn').forEach(b => b.classList.remove('active'));
      el.classList.add('active');
    }
    function jog(axis, steps) { fetch('/api/jog?axis='+axis+'&steps='+steps).then(r=>r.json()).then(updateUI); }
    function setZero() { fetch('/api/zero').then(r=>r.text()).then(m=>{ showToast(m); fetchStatus(); }); }
    function goHome() { showToast('Đang chạy về Home (0,0)...'); fetch('/api/home').then(()=>{ showToast('Đã về Home (0,0)!'); fetchStatus(); }); }
    function saveSlot(s) { fetch('/api/save_slot?slot='+s).then(r=>r.text()).then(m=>{ showToast(m); fetchStatus(); }); }
    function gotoSlot(s) { showToast('Đang chạy thử tới Vị trí '+s+'...'); fetch('/api/goto_slot?slot='+s).then(r=>r.text()).then(m=>showToast(m)); }
    function parkManual(s) { showToast('Đang đưa xe vào Vị trí '+s+'...'); fetch('/api/park_manual?slot='+s).then(r=>r.text()).then(m=>{ showToast(m); fetchStatus(); }); }
    function retrieveManual(s) { showToast('Đang lấy xe từ Vị trí '+s+'...'); fetch('/api/retrieve_manual?slot='+s).then(r=>r.text()).then(m=>{ showToast(m); fetchStatus(); }); }
    function toggleCamMode() { fetch('/api/toggle_cam').then(r=>r.text()).then(m=>{ showToast(m); fetchStatus(); }); }
    function srv(cmd) { fetch('/api/servo?cmd='+cmd); }
    function saveServo() {
      let e = document.getElementById('extMs').value, r = document.getElementById('retMs').value;
      fetch('/api/servo_save?ext='+e+'&ret='+r).then(res=>res.text()).then(m=>showToast(m));
    }

    document.addEventListener('keydown', e => {
      if(e.target.tagName === 'INPUT') return;
      const k = e.key.toLowerCase();
      if(k==='w' || e.key==='ArrowUp')    { e.preventDefault(); jog('z', stepSize); }
      if(k==='s' || e.key==='ArrowDown')  { e.preventDefault(); jog('z', -stepSize); }
      if(k==='a' || e.key==='ArrowLeft')  { e.preventDefault(); jog('x', -stepSize); }
      if(k==='d' || e.key==='ArrowRight') { e.preventDefault(); jog('x', stepSize); }
      if(k==='h') goHome();
    });

    function updateUI(d) {
      if(!d) return;
      document.getElementById('topCoord').innerText = `X: ${d.currentX} | Z: ${d.currentZ}`;
      document.getElementById('ctrlCoord').innerText = `X=${d.currentX} | Z=${d.currentZ}`;
      document.getElementById('sysStatusTxt').innerText = d.statusText || 'Sẵn sàng';
      document.getElementById('camModeTxt').innerText = d.requireCameraAI ? 'Chế độ: Tự động (RFID + Camera AI)' : 'Chế độ: RFID Nhanh (Không cần chờ Camera)';
      document.getElementById('btnCamToggle').innerText = d.requireCameraAI ? 'Đang bật: Chờ Camera AI' : 'Đang bật: Quẹt thẻ cất ngay';

      if(!srvLoaded && d.servoExtendMs) {
        document.getElementById('extMs').value = d.servoExtendMs;
        document.getElementById('retMs').value = d.servoRetractMs;
        srvLoaded = true;
      }

      const rSw = (id, a) => document.getElementById(id).innerHTML = a ? '<span class="dot hit"></span><strong style="color:#dc2626">ĐANG CHẠM</strong>' : '<span class="dot"></span>Hở';
      rSw('swX', d.swX); rSw('swZMin', d.swZMin); rSw('swZMax', d.swZMax);

      let busy = d.slots.filter(s => s.occupied).length;
      document.getElementById('kpiBusy').innerText = busy;
      document.getElementById('kpiFree').innerText = d.slots.length - busy;

      // Render Sơ đồ 3 tầng
      let diag = '';
      for(let f = 0; f < 3; f++) {
        diag += `<div class="floor-header">TẦNG ${f+1}</div><div class="floor-grid">`;
        for(let c = 0; c < 2; c++) {
          let s = d.slots[f*2 + c];
          diag += `<div class="slot-card ${s.occupied?'occupied':''}">
            <div class="slot-name">VỊ TRÍ ${s.slot}</div>
            <div class="slot-meta">Tầng ${f+1} &bull; Cột ${c+1}</div>
            ${s.occupied ? `<div class="slot-plate-txt">${s.plate || 'Thẻ: ' + s.uid}</div>` : `<div class="slot-meta">Chưa có xe</div>`}
            <span class="badge-status ${s.occupied?'badge-busy':'badge-free'}">${s.occupied?'ĐANG ĐỖ XE':'CÒN TRỐNG'}</span>
          </div>`;
        }
        diag += `</div>`;
      }
      document.getElementById('floorDiagram').innerHTML = diag;

      // Render Bảng Vị trí đỗ
      let sTbl = '';
      d.slots.forEach((s, idx) => {
        let tang = Math.floor(idx/2) + 1, cot = (idx%2) + 1;
        sTbl += `<tr>
          <td><strong>Vị trí ${s.slot}</strong></td>
          <td>Tầng ${tang} - Cột ${cot}</td>
          <td>X=${s.stepX} | Z=${s.stepZ}</td>
          <td><span class="badge-status ${s.occupied?'badge-busy':'badge-free'}">${s.occupied?'Có xe':'Trống'}</span></td>
          <td>${s.occupied ? `<button class="btn-red" onclick="retrieveManual(${s.slot})">Lấy xe ra</button>` : `<button class="btn-green" onclick="parkManual(${s.slot})">Gửi xe vào</button>`}</td>
          <td><button onclick="gotoSlot(${s.slot})">Chạy thử</button> <button class="btn-blue" onclick="saveSlot(${s.slot})">Lưu tọa độ</button></td>
        </tr>`;
      });
      document.getElementById('slotsTable').innerHTML = sTbl;

      // Render Bảng Quản lý xe
      let vTbl = '';
      d.slots.filter(s => s.occupied).forEach(s => {
        vTbl += `<tr>
          <td><strong>Vị trí ${s.slot}</strong></td>
          <td><strong style="color:var(--navy)">${s.plate || 'Chưa có biển số'}</strong></td>
          <td>${s.uid || 'Thủ công'}</td>
          <td>${s.parkTime || '--:--:--'}</td>
          <td><button class="btn-red" onclick="retrieveManual(${s.slot})">Trả xe ngay</button></td>
        </tr>`;
      });
      document.getElementById('vehiclesTable').innerHTML = vTbl || '<tr><td colspan="5" style="text-align:center;color:var(--muted);">Hiện chưa có xe nào đang gửi trong bãi.</td></tr>';

      // Render Bảng Lịch sử
      let hTbl = '';
      if(d.history) {
        d.history.forEach(h => {
          hTbl += `<tr>
            <td>${h.time}</td>
            <td><span class="badge-status ${h.action==='VAO'?'badge-free':'badge-busy'}">${h.action==='VAO'?'XE VÀO':'XE RA'}</span></td>
            <td>Vị trí ${h.slot}</td>
            <td>${h.uid || '--'}</td>
            <td><strong>${h.plate || '--'}</strong></td>
          </tr>`;
        });
      }
      document.getElementById('historyTable').innerHTML = hTbl || '<tr><td colspan="5" style="text-align:center;color:var(--muted);">Chưa có lịch sử vào/ra.</td></tr>';
    }

    function fetchStatus() { fetch('/api/status').then(r=>r.json()).then(updateUI).catch(()=>{}); }
    setInterval(fetchStatus, 1000);
    fetchStatus();
  </script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handleApiStatus() {
  String json = "{";
  json += "\"connected\":true,";
  json += "\"isHomed\":" + String(isHomed ? "true" : "false") + ",";
  json += "\"requireCameraAI\":" + String(requireCameraAI ? "true" : "false") + ",";
  json += "\"statusText\":\"" + systemStatusText + "\",";
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
    json += "\"parkTime\":\"" + slots[i].parkTime + "\",";
    json += "\"stepX\":" + String(slots[i].stepX) + ",";
    json += "\"stepZ\":" + String(slots[i].stepZ) + "}";
    if (i < TOTAL_SLOTS - 1) json += ",";
  }
  json += "],\"history\":[";
  for (int i = 0; i < historyCount; i++) {
    json += "{\"time\":\"" + historyLog[i].timeStr + "\",";
    json += "\"action\":\"" + historyLog[i].action + "\",";
    json += "\"slot\":" + String(historyLog[i].slot) + ",";
    json += "\"uid\":\"" + historyLog[i].uid + "\",";
    json += "\"plate\":\"" + historyLog[i].plate + "\"}";
    if (i < historyCount - 1) json += ",";
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
      server.send(200, "text/plain", "Da luu Vi tri " + String(s + 1) + " (X=" + String(currentX) + ", Z=" + String(currentZ) + ")");
      return;
    }
  }
  server.send(400, "text/plain", "INVALID_SLOT");
}

void handleApiGotoSlot() {
  if (server.hasArg("slot")) {
    int s = server.arg("slot").toInt() - 1;
    if (s >= 0 && s < TOTAL_SLOTS) {
      moveToAbsolute(slots[s].stepX, slots[s].stepZ);
      server.send(200, "text/plain", "Da chay toi Vi tri " + String(s + 1));
      return;
    }
  }
  server.send(400, "text/plain", "INVALID_SLOT");
}

void handleApiParkManual() {
  if (server.hasArg("slot")) {
    int s = server.arg("slot").toInt() - 1;
    if (s >= 0 && s < TOTAL_SLOTS && !slots[s].occupied) {
      slots[s].uid = "THUCONG";
      slots[s].plate = "XE-VAO-" + String(s + 1);
      storeCarToSlot(s);
      server.send(200, "text/plain", "Da gui xe vao Vi tri " + String(s + 1));
      return;
    }
  }
  server.send(400, "text/plain", "KHONG_HOP_LE");
}

void handleApiRetrieveManual() {
  if (server.hasArg("slot")) {
    int s = server.arg("slot").toInt() - 1;
    if (s >= 0 && s < TOTAL_SLOTS && slots[s].occupied) {
      retrieveCarFromSlot(s);
      server.send(200, "text/plain", "Da lay xe tu Vi tri " + String(s + 1));
      return;
    }
  }
  server.send(400, "text/plain", "KHONG_HOP_LE");
}

void handleApiToggleCam() {
  requireCameraAI = !requireCameraAI;
  waitingForPlate = false;
  server.send(200, "text/plain", requireCameraAI ? "Da bat cho Camera AI" : "Da bat che do quet the cat xe ngay");
}

void handleApiHome() { homeAxes(); server.send(200, "text/plain", "HOME_OK"); }

void handleApiZero() {
  currentX = 0; currentZ = 0; isHomed = true;
  beep(1, 80);
  server.send(200, "text/plain", "Da chot cho nay lam goc (0, 0)!");
}

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
    server.send(200, "text/plain", "Da luu Servo 360 (" + String(servoExtendMs) + "ms / " + String(servoRetractMs) + "ms)");
    return;
  }
  server.send(400, "text/plain", "MISSING_PARAMS");
}

// --------------------------- 9. GIAO TIEP SERIAL MONITOR --------------------------------------
void printSerialHelp() {
  Serial.println("\n=== HE THONG BAI DO XE THONG MINH ESP32 (6 VI TRI) ===");
  Serial.println(" [w/s/a/d] (+/-200) | [W/S/A/D] (+/-1000) | [h] Home | [1..6] Luu O | [p] In");
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;
    switch (c) {
      case 'w': jogZ(200); break; case 'W': jogZ(1000); break;
      case 's': jogZ(-200); break; case 'S': jogZ(-1000); break;
      case 'd': jogX(200); break; case 'D': jogX(1000); break;
      case 'a': jogX(-200); break; case 'A': jogX(-1000); break;
      case 'h': homeAxes(); break;
      case 'e': servoExtend(); break; case 'r': servoRetract(); break;
      case 'q': servoRunTime(SERVO_PWM_EXTEND, 100); break;
      case 'z': servoRunTime(SERVO_PWM_RETRACT, 100); break;
      case '1': case '2': case '3': case '4': case '5': case '6':
        saveSlotCoordinateToNVS(c - '1', currentX, currentZ); break;
      case 'p':
        for (int i = 0; i < TOTAL_SLOTS; i++)
          Serial.printf("Vi tri %d: X=%ld | Z=%ld\n", i + 1, slots[i].stepX, slots[i].stepZ);
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
  beep(1, 100);

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
  server.on("/api/goto_slot", HTTP_GET, handleApiGotoSlot);
  server.on("/api/park_manual", HTTP_GET, handleApiParkManual);
  server.on("/api/retrieve_manual", HTTP_GET, handleApiRetrieveManual);
  server.on("/api/toggle_cam", HTTP_GET, handleApiToggleCam);
  server.on("/api/home", HTTP_GET, handleApiHome);
  server.on("/api/zero", HTTP_GET, handleApiZero);
  server.on("/api/servo", HTTP_GET, handleApiServo);
  server.on("/api/servo_save", HTTP_GET, handleApiServoSave);
  server.begin();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SAN SANG! QUET");
  lcd.setCursor(0, 1); lcd.print("THE RFID DE GUI");
  printSerialHelp();
}

void loop() {
  if (isLimitXHomeHit()) currentX = 0;
  if (isLimitZMinHit())  currentZ = 0;

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
        systemStatusText = "Bai xe da day!";
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("BAI XE DA DAY!");
        lcd.setCursor(0, 1); lcd.print("Vui long quay lai");
        beep(3, 150);
      } else {
        slots[emptySlot].uid = scannedUID;
        if (requireCameraAI) {
          pendingSlot = emptySlot;
          pendingUID = scannedUID;
          waitingForPlate = true;
          sessionId++;
          systemStatusText = "Cho Camera ANPR quet bien so cho Vi tri " + String(emptySlot + 1);
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Vao O " + String(emptySlot + 1));
          lcd.setCursor(0, 1); lcd.print("Cho quet bien so");
        } else {
          slots[emptySlot].plate = "THE-" + scannedUID.substring(0, 4);
          storeCarToSlot(emptySlot);
        }
      }
    }
  }
  delay(10);
}
