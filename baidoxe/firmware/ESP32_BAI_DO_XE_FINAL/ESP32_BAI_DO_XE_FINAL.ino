/*
 * ==============================================================================================
 * Project   : HE THONG BAI DO XE THONG MINH ESP32 (2 TRUC X-Z + SERVO + RFID + CAMERA AI ANPR)
 * Bo mach   : Altium PCB Bai_Do_Xe_Thong_Minh_ESP32 (ESP32 DevKit V1 30-Pin)
 * Dong co   : 2 x Stepper NEMA 17 qua Driver A4988 (Trục X ngang, Trục Z nâng tầng)
 * Co cau    : Servo MG90S keo/day khay xe (pallet)
 * Ngoai vi  : RFID RC522, LCD I2C 1602/2004, Cam bien IR cong vao (Chân 5 GPIO 35), Buzzer, LED
 * Dong bo   : Web Server REST API (/api/status, /api/plate) ket noi voi Python Camera AI
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

// --------------------------- 1. CAU HINH CHAN GPIO (CHUAN THEO ALTIUM PCB) -------------------
// Khoi Dong co buoc Truc X (Nguyên ly U3 - Driver A4988)
#define PIN_STEP_X        14  // Chân 11 ESP32
#define PIN_DIR_X         12  // Chân 12 ESP32
#define PIN_LIMIT_X_HOME  16  // Chân 21 ESP32 (RX2) - Cong tac hanh trinh Home X

// Khoi Dong co buoc Truc Z - Nang ha (Nguyên ly U2 - Driver A4988)
#define PIN_STEP_Z        25  // Chân 8 ESP32
#define PIN_DIR_Z         26  // Chân 9 ESP32
#define PIN_LIMIT_Z_MIN   32  // Chân 6 ESP32 - Cong tac hanh trinh day Z (Min)
#define PIN_LIMIT_Z_MAX   33  // Chân 7 ESP32 - Cong tac hanh trinh dinh Z (Max)

// Chan Enable chung cho ca 2 Driver A4988 (LOW = Bat dong co, HIGH = Tha dong co)
#define PIN_MOTORS_EN     27  // Chân 10 ESP32

// Co cau tay gat / trượt khay xe: Servo MG90S
#define PIN_SERVO_PWM     13  // Chân 13 ESP32

// Cam bien hong ngoai cong vao (IR Gate LM393) - Chốt chân 5 (GPIO 35)
#define PIN_IR_GATE       35  // Chân 5 ESP32 (Input-Only)

// Den bao & Coi chip
#define PIN_LED_STATUS    2   // Chân 19 ESP32
#define PIN_BUZZER        4   // Chân 20 ESP32

// Cau hinh muc kich Coi (Buzzer):
// true  = Active LOW (Coi keu khi chan muc 0, giup FIX LOI COI LUON KEU)
// false = Active HIGH (Coi keu khi chan muc 1)
#define BUZZER_ACTIVE_LOW true

void buzzerOn() {
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_LOW ? LOW : HIGH);
}

void buzzerOff() {
  digitalWrite(PIN_BUZZER, BUZZER_ACTIVE_LOW ? HIGH : LOW);
}

// RFID RC522 (Giao tiep SPI)
#define PIN_RFID_SS       5   // Chân 23 ESP32
#define PIN_RFID_RST      17  // Chân 22 ESP32 (TX2)
#define PIN_RFID_SCK      18  // Chân 24 ESP32
#define PIN_RFID_MISO     19  // Chân 25 ESP32
#define PIN_RFID_MOSI     23  // Chân 30 ESP32

// LCD I2C: SDA=GPIO21, SCL=GPIO22
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo trayServo;
WebServer server(80);

// --------------------------- 2. CAU HINH WI-FI & WEB SERVER ----------------------------------
const char* WIFI_SSID     = "VIETSET_TECH";      // WiFi của bạn
const char* WIFI_PASS     = "vs68686868";        // Mật khẩu WiFi
const char* AP_SSID       = "BaiDoXe_ESP32";     // Phát WiFi nếu không kết nối được Router
const char* AP_PASS       = "12345678";

// --------------------------- 3. CAU HINH TAI CHO DO XE (4 HOAC 6 SLOTS) -----------------------
#define TOTAL_SLOTS       4    // So o do xe (2 cot x 2 tang = 4 o)

struct SlotInfo {
  bool occupied;
  String uid;
  String plate;
  String parkTime;
  long stepX;                  // Toa do truc X (so buoc)
  long stepZ;                  // Toa do truc Z (so buoc)
};

// Bang toa do buoc cho tung Slot (Ban co the hieu chinh theo khoang cach thuc te):
// Slot 0: Tang 1, Cot 1 | Slot 1: Tang 1, Cot 2
// Slot 2: Tang 2, Cot 1 | Slot 3: Tang 2, Cot 2
SlotInfo slots[TOTAL_SLOTS] = {
  {false, "", "", "",  2000,  1500},
  {false, "", "", "",  6000,  1500},
  {false, "", "", "",  2000,  5500},
  {false, "", "", "",  6000,  5500}
};

// Vi tri goc ban nang / cong don tra xe
const long HOME_X = 0;
const long HOME_Z = 0;

// Goc Servo: 0 = Thu tay ve, 90 = Day khay vao / Keo khay ra
const int SERVO_RETRACT = 10;
const int SERVO_EXTEND  = 95;

// Bien trang thai dong bo voi Camera AI Python
bool waitingForPlate = false;
int pendingSlot = -1;
String pendingUID = "";
int sessionId = 100;

// --------------------------- 4. HAM DIEU KHIEN DONG CO BUOC -----------------------------------
void stepPulse(int stepPin, int delayUs = 400) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(delayUs);
}

// Chay truc X den toa do tuong doi
void moveX(long steps, bool dir) {
  digitalWrite(PIN_DIR_X, dir ? HIGH : LOW);
  for (long i = 0; i < steps; i++) {
    // Kiem tra cong tac hanh trinh neu dang di ve Home
    if (!dir && digitalRead(PIN_LIMIT_X_HOME) == LOW) break;
    stepPulse(PIN_STEP_X, 350);
  }
}

// Chay truc Z den toa do tuong doi
void moveZ(long steps, bool dir) {
  digitalWrite(PIN_DIR_Z, dir ? HIGH : LOW);
  for (long i = 0; i < steps; i++) {
    // Kiem tra an toan hanh trinh
    if (!dir && digitalRead(PIN_LIMIT_Z_MIN) == LOW) break;
    if (dir && digitalRead(PIN_LIMIT_Z_MAX) == LOW) break;
    stepPulse(PIN_STEP_Z, 450);
  }
}

// Chay ve vi tri Home ban dau
void homeAxes() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Dang ve Home X-Z");
  digitalWrite(PIN_MOTORS_EN, LOW); // Bat nguon dong co

  // 1. Ve Home Z (ha xuong cham cong tac hanh trinh MIN)
  digitalWrite(PIN_DIR_Z, LOW);
  int timeoutZ = 8000; // Gioi han an toan neu chua cam cong tac hanh trinh
  while (digitalRead(PIN_LIMIT_Z_MIN) == HIGH && timeoutZ-- > 0) {
    stepPulse(PIN_STEP_Z, 600);
  }
  delay(100);

  // 2. Ve Home X (chay sang trai cham cong tac hanh trinh HOME X)
  digitalWrite(PIN_DIR_X, LOW);
  int timeoutX = 6000; // Gioi han an toan neu chua cam cong tac hanh trinh
  while (digitalRead(PIN_LIMIT_X_HOME) == HIGH && timeoutX-- > 0) {
    stepPulse(PIN_STEP_X, 500);
  }
  delay(100);

  // 3. Servo ve vi tri thu
  trayServo.write(SERVO_RETRACT);
  delay(300);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Home OK! San sang");
}

// Cat xe vao Slot
void storeCarToSlot(int slotIdx) {
  digitalWrite(PIN_MOTORS_EN, LOW);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Dua xe -> Slot "); lcd.print(slotIdx + 1);

  // Di chuyen truc Z len tang tuong ung
  moveZ(slots[slotIdx].stepZ, true);
  delay(200);

  // Di chuyen truc X den khoang cot tuong ung
  moveX(slots[slotIdx].stepX, true);
  delay(200);

  // Day khay xe vao o do
  trayServo.write(SERVO_EXTEND);
  delay(800);
  trayServo.write(SERVO_RETRACT);
  delay(500);

  // Tro ve vi tri Home don xe
  moveX(slots[slotIdx].stepX, false);
  delay(200);
  moveZ(slots[slotIdx].stepZ, false);
  delay(200);

  slots[slotIdx].occupied = true;
  beep(1, 200);
}

// Lay xe tu Slot ra tra cho khach
void retrieveCarFromSlot(int slotIdx) {
  digitalWrite(PIN_MOTORS_EN, LOW);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Lay xe tu Slot "); lcd.print(slotIdx + 1);

  // Di chuyen den o do xe
  moveZ(slots[slotIdx].stepZ, true);
  delay(200);
  moveX(slots[slotIdx].stepX, true);
  delay(200);

  // Keo khay xe ra ban nang
  trayServo.write(SERVO_EXTEND);
  delay(800);
  trayServo.write(SERVO_RETRACT);
  delay(500);

  // Mang xe ve tang tret / cong tra xe
  moveX(slots[slotIdx].stepX, false);
  delay(200);
  moveZ(slots[slotIdx].stepZ, false);
  delay(200);

  // Giai phong Slot
  slots[slotIdx].occupied = false;
  slots[slotIdx].uid = "";
  slots[slotIdx].plate = "";
  beep(2, 100);
}

void beep(int count, int durationMs) {
  for (int i = 0; i < count; i++) {
    buzzerOn();
    delay(durationMs);
    buzzerOff();
    if (i < count - 1) delay(100);
  }
}

// --------------------------- 5. REST API DONG BO CAMERA AI PYTHON ----------------------------
void handleApiStatus() {
  String json = "{";
  json += "\"connected\":true,";
  json += "\"waitingForPlate\":" + String(waitingForPlate ? "true" : "false") + ",";
  json += "\"pendingSlot\":" + String(pendingSlot) + ",";
  json += "\"pendingUID\":\"" + pendingUID + "\",";
  json += "\"sessionId\":" + String(sessionId) + ",";
  json += "\"slots\":[";
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    json += "{\"slot\":" + String(i + 1) + ",";
    json += "\"occupied\":" + String(slots[i].occupied ? "true" : "false") + ",";
    json += "\"uid\":\"" + slots[i].uid + "\",";
    json += "\"plate\":\"" + slots[i].plate + "\"}";
    if (i < TOTAL_SLOTS - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleApiPlate() {
  if (server.hasArg("plate")) {
    String plate = server.arg("plate");
    String conf = server.arg("confidence");
    String imgUrl = server.arg("image_url");

    if (waitingForPlate && pendingSlot >= 0 && pendingSlot < TOTAL_SLOTS) {
      slots[pendingSlot].plate = plate;
      waitingForPlate = false;
      sessionId++;

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Bien so: " + plate);
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

// --------------------------- 6. SETUP & LOOP CHINH --------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Khoi tao cac chan I/O
  pinMode(PIN_STEP_X, OUTPUT);
  pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT);
  pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT);
  digitalWrite(PIN_MOTORS_EN, LOW); // Bat dong co

  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);

  pinMode(PIN_IR_GATE, INPUT); // GPIO 35 la input-only
  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);

  // Khoi tao Coi & Tat ngay lap tuc de tranh bi luon keu
  buzzerOff();
  pinMode(PIN_BUZZER, OUTPUT);
  buzzerOff();

  trayServo.attach(PIN_SERVO_PWM);
  trayServo.write(SERVO_RETRACT);

  // Khoi tao LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("BAI DO XE ESP32");
  lcd.setCursor(0, 1); lcd.print("Khoi dong...");

  // Test coi khi khoi dong (bip bip 2 tieng ngan roi tat han)
  beep(2, 80);

  // Khoi tao RFID RC522
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();

  // Ket noi WiFi
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Ket noi WiFi...");
  lcd.setCursor(0, 1); lcd.print(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());
    MDNS.begin("baidoxe");
    delay(2000);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("AP: BaiDoXe_ESP");
    lcd.setCursor(0, 1); lcd.print(WiFi.softAPIP().toString());
    delay(2000);
  }

  // Dang ky REST API
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/plate", HTTP_POST, handleApiPlate);
  server.begin();

  // Ve Home
  homeAxes();
  beep(2, 80);
}

void loop() {
  server.handleClient();

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

    beep(1, 100);

    // Kiem tra the nay dang gui xe hay den lay xe
    int foundSlot = -1;
    for (int i = 0; i < TOTAL_SLOTS; i++) {
      if (slots[i].occupied && slots[i].uid == scannedUID) {
        foundSlot = i;
        break;
      }
    }

    if (foundSlot >= 0) {
      // 1. THE DA GUI XE -> TIEN HANH TRA XE
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("The: " + scannedUID);
      lcd.setCursor(0, 1); lcd.print("Tra xe Slot " + String(foundSlot + 1));
      retrieveCarFromSlot(foundSlot);
    } else {
      // 2. THE MOI -> TIM O TRONG DE GUI XE
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
        beep(3, 200);
      } else {
        // Kich hoat cho Camera AI chup bien so
        pendingSlot = emptySlot;
        pendingUID = scannedUID;
        slots[emptySlot].uid = scannedUID;
        waitingForPlate = true;
        sessionId++;

        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Vao Slot " + String(emptySlot + 1));
        lcd.setCursor(0, 1); lcd.print("Cho quet bien so");
      }
    }
  }

  delay(20);
}

