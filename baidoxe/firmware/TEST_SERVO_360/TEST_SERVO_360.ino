/*
 * ==============================================================================================
 * CHUONG TRINH TEST SERVO 360 + THEO DOI NUT BAM & CONG TAC HANH TRINH REAL-TIME
 * ==============================================================================================
 * Tinh nang:
 * 1. Khoi dong: Test coi SS8050 keu bip dung 1 tieng (100ms) roi tat han (KHONG bi keu lien tuc).
 * 2. Tu dong in LOG ngay lap tuc khi ban AN NUT BAM (B1, B3, B4) hoac CHAM CONG TAC HANH TRINH!
 * 3. Test Servo 360 do (quay thuan / nguoc / dung) qua Serial Monitor (115200 baud).
 * 4. Test nhich nhanh 2 dong co buoc truc X va truc Z.
 * ==============================================================================================
 */

#include <ESP32Servo.h>

#define PIN_STEP_X        14
#define PIN_DIR_X         12
#define PIN_LIMIT_X_HOME  16  // Nut B3 / CTHT Home X

#define PIN_STEP_Z        25
#define PIN_DIR_Z         26
#define PIN_LIMIT_Z_MIN   32  // Nut B1 / CTHT Min Z
#define PIN_LIMIT_Z_MAX   33  // Nut B4 / CTHT Max Z

#define PIN_MOTORS_EN     27
#define PIN_SERVO_PWM     13
#define PIN_IR_GATE       35
#define PIN_LED_STATUS    2
#define PIN_BUZZER        4   // SS8050 NPN: HIGH = Keu, LOW = Tat

Servo testServo;

// Luu trang thai truoc do de phat hien nhan/nha ngay lap tuc
bool lastX  = HIGH;
bool lastZ1 = HIGH;
bool lastZ2 = HIGH;
bool lastIR = HIGH;

void beepOnce(int ms = 60) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}

void stepMotor(int stepPin, int dirPin, bool dir, int steps, int delayUs = 500) {
  digitalWrite(PIN_MOTORS_EN, LOW);
  digitalWrite(dirPin, dir ? HIGH : LOW);
  for (int i = 0; i < steps; i++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(delayUs);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(delayUs);
  }
}

void printMenu() {
  Serial.println("\n=======================================================");
  Serial.println("   TEST SERVO 360 + NUT BAM & CONG TAC HANH TRINH      ");
  Serial.println("=======================================================");
  Serial.println(" -> Hay thu AN NUT (B1, B3, B4) hoac CHAM CTHT de xem LOG!");
  Serial.println("-------------------------------------------------------");
  Serial.println(" [1] : Servo quay THUAN 500ms (write 30) -> Dung");
  Serial.println(" [2] : Servo quay NGUOC 500ms (write 150) -> Dung");
  Serial.println(" [3] : Servo quay THUAN LIEN TUC (xem co quay 360 ko)");
  Serial.println(" [4] : Servo quay NGUOC LIEN TUC");
  Serial.println(" [0] : DUNG SERVO NGAY LAP TUC (write 90)");
  Serial.println(" [t] : In trang thai hien tai cua ca 3 CTHT");
  Serial.println(" [x]/[X] : Nhich Truc X Trai / Phai (400 buoc)");
  Serial.println(" [z]/[Z] : Nhich Truc Z Xuong / Len (400 buoc)");
  Serial.println("=======================================================\n");
}

void checkSwitchesRealtime() {
  bool curX  = digitalRead(PIN_LIMIT_X_HOME);
  bool curZ1 = digitalRead(PIN_LIMIT_Z_MIN);
  bool curZ2 = digitalRead(PIN_LIMIT_Z_MAX);
  bool curIR = digitalRead(PIN_IR_GATE);

  if (curX != lastX) {
    delay(20); // Chong doi phim
    curX = digitalRead(PIN_LIMIT_X_HOME);
    if (curX != lastX) {
      lastX = curX;
      if (curX == LOW) {
        Serial.println(">>> [CHAM!] Nut B3 / CTHT HOME X (GPIO 16) = LOW");
        beepOnce(40);
      } else {
        Serial.println("--- [NHA!]  Nut B3 / CTHT HOME X (GPIO 16) = HIGH");
      }
    }
  }

  if (curZ1 != lastZ1) {
    delay(20);
    curZ1 = digitalRead(PIN_LIMIT_Z_MIN);
    if (curZ1 != lastZ1) {
      lastZ1 = curZ1;
      if (curZ1 == LOW) {
        Serial.println(">>> [CHAM!] Nut B1 / CTHT MIN Z (GPIO 32) = LOW");
        beepOnce(40);
      } else {
        Serial.println("--- [NHA!]  Nut B1 / CTHT MIN Z (GPIO 32) = HIGH");
      }
    }
  }

  if (curZ2 != lastZ2) {
    delay(20);
    curZ2 = digitalRead(PIN_LIMIT_Z_MAX);
    if (curZ2 != lastZ2) {
      lastZ2 = curZ2;
      if (curZ2 == LOW) {
        Serial.println(">>> [CHAM!] Nut B4 / CTHT MAX Z (GPIO 33) = LOW");
        beepOnce(40);
      } else {
        Serial.println("--- [NHA!]  Nut B4 / CTHT MAX Z (GPIO 33) = HIGH");
      }
    }
  }

  if (curIR != lastIR) {
    delay(20);
    curIR = digitalRead(PIN_IR_GATE);
    if (curIR != lastIR) {
      lastIR = curIR;
      Serial.println(curIR == LOW ? ">>> [IR CONG] Co vat can / xe truoc cong!" : "--- [IR CONG] Trong");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(PIN_STEP_X, OUTPUT); pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT); pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT); digitalWrite(PIN_MOTORS_EN, LOW);

  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);
  pinMode(PIN_IR_GATE, INPUT);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Tat coi (SS8050 NPN: LOW = Tat)

  // Test coi 1 tieng khi khoi dong he thong
  beepOnce(100);

  testServo.attach(PIN_SERVO_PWM);
  testServo.write(90);

  lastX  = digitalRead(PIN_LIMIT_X_HOME);
  lastZ1 = digitalRead(PIN_LIMIT_Z_MIN);
  lastZ2 = digitalRead(PIN_LIMIT_Z_MAX);
  lastIR = digitalRead(PIN_IR_GATE);

  Serial.println("\n>>> TU DONG TEST SERVO 1 LAN KHI KHOI DONG <<<");
  Serial.println("-> Quay THUAN 800ms (write 30)...");
  testServo.write(30);
  delay(800);
  Serial.println("-> DUNG (write 90) 1 giay...");
  testServo.write(90);
  delay(1000);
  Serial.println("-> Quay NGUOC 800ms (write 150)...");
  testServo.write(150);
  delay(800);
  Serial.println("-> DUNG HAN (write 90)!");
  testServo.write(90);

  printMenu();
}

void loop() {
  checkSwitchesRealtime();

  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;

    switch (c) {
      case '1':
        Serial.println("-> Servo THUAN 500ms...");
        testServo.write(30); delay(500); testServo.write(90);
        Serial.println("-> Da dung.");
        break;
      case '2':
        Serial.println("-> Servo NGUOC 500ms...");
        testServo.write(150); delay(500); testServo.write(90);
        Serial.println("-> Da dung.");
        break;
      case '3':
        Serial.println("-> Servo QUAY THUAN LIEN TUC (Bam [0] de dung)...");
        testServo.write(30);
        break;
      case '4':
        Serial.println("-> Servo QUAY NGUOC LIEN TUC (Bam [0] de dung)...");
        testServo.write(150);
        break;
      case '0':
      case 's':
        testServo.write(90);
        Serial.println("-> DA DUNG SERVO (write 90)!");
        break;
      case 't':
        Serial.printf("[TRANG THAI] B3/X_HOME(16): %s | B1/Z_MIN(32): %s | B4/Z_MAX(33): %s\n",
          digitalRead(PIN_LIMIT_X_HOME) == LOW ? "CHAM (LOW)" : "HO (HIGH)",
          digitalRead(PIN_LIMIT_Z_MIN)  == LOW ? "CHAM (LOW)" : "HO (HIGH)",
          digitalRead(PIN_LIMIT_Z_MAX)  == LOW ? "CHAM (LOW)" : "HO (HIGH)");
        break;
      case 'x':
        Serial.println("-> Truc X: Trai 400 buoc");
        stepMotor(PIN_STEP_X, PIN_DIR_X, false, 400, 500);
        break;
      case 'X':
        Serial.println("-> Truc X: Phai 400 buoc");
        stepMotor(PIN_STEP_X, PIN_DIR_X, true, 400, 500);
        break;
      case 'z':
        Serial.println("-> Truc Z: Xuong 400 buoc");
        stepMotor(PIN_STEP_Z, PIN_DIR_Z, false, 400, 600);
        break;
      case 'Z':
        Serial.println("-> Truc Z: Len 400 buoc");
        stepMotor(PIN_STEP_Z, PIN_DIR_Z, true, 400, 600);
        break;
      default:
        printMenu();
        break;
    }
  }
}
