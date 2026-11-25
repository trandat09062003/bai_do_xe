/*
 * ==============================================================================================
 * CHUONG TRINH TEST SERVO 360 DO + CONG TAC HANH TRINH + DONG CO BUOC (KHONG CAN WIFI)
 * ==============================================================================================
 * Muc dich:
 * 1. Phan biet nhanh Servo 360 do (quay lien tuc) va Servo 180 do (quay theo goc).
 * 2. Test nhanh thoi gian quay tho ra / rut vao cua thanh rang.
 * 3. Test 3 cong tac hanh trinh (B1, B3, B4) va 2 dong co buoc X, Z.
 *
 * Huong dan su dung:
 * - Nap code vao ESP32 -> Mo Serial Monitor (Toc do 115200 baud).
 * - Ngay khi bat nguon, Servo se tu dong test 1 lan: Quay Thuan 0.8s -> Dung 1s -> Quay Nguoc 0.8s -> Dung.
 *   + Neu thay Servo QUAY VONG TRON LIEN TUC -> Do la SERVO 360 DO!
 *   + Neu thay Servo CHI NHICH DEN GOC ROI DUNG -> Do la SERVO 180 DO!
 * ==============================================================================================
 */

#include <ESP32Servo.h>

#define PIN_STEP_X        14
#define PIN_DIR_X         12
#define PIN_LIMIT_X_HOME  16

#define PIN_STEP_Z        25
#define PIN_DIR_Z         26
#define PIN_LIMIT_Z_MIN   32
#define PIN_LIMIT_Z_MAX   33

#define PIN_MOTORS_EN     27
#define PIN_SERVO_PWM     13
#define PIN_LED_STATUS    2
#define PIN_BUZZER        4

Servo testServo;

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
  Serial.println("         MENU TEST SERVO 360 & PHAN CUNG ESP32         ");
  Serial.println("=======================================================");
  Serial.println(" [1] : Servo quay THUAN 500ms (write 30) -> Dung");
  Serial.println(" [2] : Servo quay NGUOC 500ms (write 150) -> Dung");
  Serial.println(" [3] : Servo quay THUAN LIEN TUC (de xem co quay 360 ko)");
  Serial.println(" [4] : Servo quay NGUOC LIEN TUC");
  Serial.println(" [0] : DUNG SERVO NGAY LAP TUC (write 90)");
  Serial.println("-------------------------------------------------------");
  Serial.println(" [t] : Kiem tra 3 Cong tac hanh trinh (X_HOME, Z_MIN, Z_MAX)");
  Serial.println(" [x] / [X] : Nhich Truc X Trai / Phai (400 buoc)");
  Serial.println(" [z] / [Z] : Nhich Truc Z Xuong / Len (400 buoc)");
  Serial.println("=======================================================\n");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_STEP_X, OUTPUT);
  pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT);
  pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT);
  digitalWrite(PIN_MOTORS_EN, LOW);

  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH); // Tat coi (Active LOW)

  // Test coi 1 tieng khi khoi dong he thong
  digitalWrite(PIN_BUZZER, LOW);
  delay(100);
  digitalWrite(PIN_BUZZER, HIGH);

  testServo.attach(PIN_SERVO_PWM);
  testServo.write(90); // Dung yen ban dau

  Serial.println("\n>>> BAT DAU TU DONG TEST SERVO (Kiem tra 360 hay 180) <<<");
  Serial.println("-> Quay THUAN 800ms (write 30)...");
  testServo.write(30);
  delay(800);

  Serial.println("-> DUNG (write 90) trong 1 giay...");
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
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;

    switch (c) {
      case '1':
        Serial.println("-> Servo THUAN 500ms...");
        testServo.write(30);
        delay(500);
        testServo.write(90);
        Serial.println("-> Da dung.");
        break;

      case '2':
        Serial.println("-> Servo NGUOC 500ms...");
        testServo.write(150);
        delay(500);
        testServo.write(90);
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
        Serial.printf("[CTHT] X_HOME(16): %s | Z_MIN(32): %s | Z_MAX(33): %s\n",
          digitalRead(PIN_LIMIT_X_HOME) == LOW ? "CHAM (LOW)" : "HO (HIGH)",
          digitalRead(PIN_LIMIT_Z_MIN) == LOW ? "CHAM (LOW)" : "HO (HIGH)",
          digitalRead(PIN_LIMIT_Z_MAX) == LOW ? "CHAM (LOW)" : "HO (HIGH)");
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
