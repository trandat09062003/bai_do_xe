/*
 * ==============================================================================================
 * CHUONG TRINH TEST SERVO (PHAN BIET 360 DO / 180 DO) + CTHT + DONG CO BUOC
 * ==============================================================================================
 * - Khoi dong: Bip coi 1 tieng (100ms).
 * - Doi 1.5 giay cho nguon on dinh, sau do tu dong test Servo nhe nhang (giam dong dinh tranh sut ap):
 *     + Quay thuan em (write 65) 700ms -> Dung (write 90) 1.5s -> Quay nguoc em (write 115) 700ms -> Dung.
 * - Nhan Serial Monitor (115200 baud) de dieu khien thu cong hoac bat/tat tu dong lap.
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
bool autoLoopTest = true;
unsigned long lastCycleMs = 0;

void beepOne() {
  digitalWrite(PIN_BUZZER, LOW);
  delay(100);
  digitalWrite(PIN_BUZZER, HIGH);
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

void runServoTestCycle() {
  Serial.println("\n[TEST] Quay THUAN em (write 65) 700ms...");
  digitalWrite(PIN_LED_STATUS, HIGH);
  testServo.write(65);
  delay(700);

  Serial.println("[TEST] DUNG (write 90) 1.5s...");
  digitalWrite(PIN_LED_STATUS, LOW);
  testServo.write(90);
  delay(1500);

  Serial.println("[TEST] Quay NGUOC em (write 115) 700ms...");
  digitalWrite(PIN_LED_STATUS, HIGH);
  testServo.write(115);
  delay(700);

  Serial.println("[TEST] DUNG HAN (write 90)!");
  digitalWrite(PIN_LED_STATUS, LOW);
  testServo.write(90);
}

void printMenu() {
  Serial.println("\n=======================================================");
  Serial.println("         MENU TEST SERVO & PHAN CUNG ESP32             ");
  Serial.println("=======================================================");
  Serial.println(" [1] : Servo quay THUAN 500ms (write 30) -> Dung");
  Serial.println(" [2] : Servo quay NGUOC 500ms (write 150) -> Dung");
  Serial.println(" [3] : Servo quay THUAN LIEN TUC (write 60)");
  Serial.println(" [4] : Servo quay NGUOC LIEN TUC (write 120)");
  Serial.println(" [0] : DUNG SERVO NGAY (write 90) & Tat tu dong lap");
  Serial.println(" [a] : Bat / Tat che do tu dong lap test Servo moi 4s");
  Serial.println("-------------------------------------------------------");
  Serial.println(" [t] : Kiem tra 3 Cong tac hanh trinh (X_HOME, Z_MIN, Z_MAX)");
  Serial.println(" [x] / [X] : Nhich Truc X Trai / Phai (400 buoc)");
  Serial.println(" [z] / [Z] : Nhich Truc Z Xuong / Len (400 buoc)");
  Serial.println("=======================================================\n");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_STEP_X, OUTPUT);
  pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT);
  pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT);
  digitalWrite(PIN_MOTORS_EN, HIGH); // Tam tha dong co buoc khi test servo

  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH); // Tat coi (Active LOW)
  beepOne(); // Test coi 1 tieng khi khoi dong

  testServo.attach(PIN_SERVO_PWM);
  testServo.write(90); // Dung yen

  delay(1500); // Cho nguon on dinh truoc khi quay servo
  printMenu();
  runServoTestCycle();
  lastCycleMs = millis();
}

void loop() {
  if (autoLoopTest && (millis() - lastCycleMs >= 4000)) {
    runServoTestCycle();
    lastCycleMs = millis();
  }

  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;

    switch (c) {
      case '1':
        autoLoopTest = false;
        Serial.println("-> Servo THUAN 500ms (write 30)...");
        testServo.write(30);
        delay(500);
        testServo.write(90);
        Serial.println("-> Da dung.");
        break;

      case '2':
        autoLoopTest = false;
        Serial.println("-> Servo NGUOC 500ms (write 150)...");
        testServo.write(150);
        delay(500);
        testServo.write(90);
        Serial.println("-> Da dung.");
        break;

      case '3':
        autoLoopTest = false;
        Serial.println("-> Servo QUAY THUAN LIEN TUC (Bam [0] de dung)...");
        testServo.write(60);
        break;

      case '4':
        autoLoopTest = false;
        Serial.println("-> Servo QUAY NGUOC LIEN TUC (Bam [0] de dung)...");
        testServo.write(120);
        break;

      case '0':
      case 's':
        autoLoopTest = false;
        testServo.write(90);
        Serial.println("-> DA DUNG SERVO (write 90) & TAT TU DONG LAP!");
        break;

      case 'a':
        autoLoopTest = !autoLoopTest;
        Serial.printf("-> Che do tu dong lap test Servo: %s\n", autoLoopTest ? "BAT" : "TAT");
        lastCycleMs = millis();
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

