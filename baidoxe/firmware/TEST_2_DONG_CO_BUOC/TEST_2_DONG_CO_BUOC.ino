/*
 * ==============================================================================================
 * CHUONG TRINH TEST NHANH 2 DONG CO BUOC (TRUC X VA TRUC Z) - MACH ALTIUM ESP32
 * ==============================================================================================
 * Muc dich: Test truc tiep xem 2 dong co buoc da nhan tin hieu va quay duoc chua.
 * Khong can WiFi, khong can cong tac hanh trinh, khong can man hinh.
 * ==============================================================================================
 */

// Cau hinh chan chuan theo mach Altium
#define PIN_STEP_X    14  // Chân 11 ESP32 - Step Trục X (Driver U3)
#define PIN_DIR_X     12  // Chân 12 ESP32 - Dir Trục X
#define PIN_STEP_Z    25  // Chân 8 ESP32  - Step Trục Z (Driver U2)
#define PIN_DIR_Z     26  // Chân 9 ESP32  - Dir Trục Z
#define PIN_MOTORS_EN 27  // Chân 10 ESP32 - Enable ca 2 driver (LOW = Bat, HIGH = Tat)

#define PIN_LED_STATUS 2  // LED bao tren bo mach
#define PIN_BUZZER     4  // Coi bao

// Ham tao xung buoc (Step Pulse)
void stepMotor(int stepPin, int delayMicrosec) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(delayMicrosec);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(delayMicrosec);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=======================================================");
  Serial.println(">>> BAT DAU CHUONG TRINH TEST 2 DONG CO BUOC ALTIUM <<<");
  Serial.println("=======================================================");

  // Khoi tao chan
  pinMode(PIN_STEP_X, OUTPUT);
  pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT);
  pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT);
  
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Tat coi
  digitalWrite(PIN_BUZZER, HIGH); // Active LOW

  // BAT DRIVER A4988 (MUC LOW LA BAT)
  digitalWrite(PIN_MOTORS_EN, LOW);
  Serial.println("[OK] Da kich hoat chan MOTORS_EN = LOW (Bat nguon 2 Driver)");

  // Bip 2 tieng test
  digitalWrite(PIN_BUZZER, LOW); delay(80); digitalWrite(PIN_BUZZER, HIGH); delay(80);
  digitalWrite(PIN_BUZZER, LOW); delay(80); digitalWrite(PIN_BUZZER, HIGH);
}

void loop() {
  // -------------------------------------------------------------
  // TEST TRUC X (DRIVER U3 - HEADER H2)
  // -------------------------------------------------------------
  Serial.println("-> TRUC X: Quay Thuan (800 buoc)...");
  digitalWrite(PIN_LED_STATUS, HIGH);
  digitalWrite(PIN_DIR_X, HIGH);
  for (int i = 0; i < 800; i++) {
    stepMotor(PIN_STEP_X, 600); // 600us = toc do vua phai, de keo
  }
  delay(400);

  Serial.println("-> TRUC X: Quay Nguoc (800 buoc)...");
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_DIR_X, LOW);
  for (int i = 0; i < 800; i++) {
    stepMotor(PIN_STEP_X, 600);
  }
  delay(600);

  // -------------------------------------------------------------
  // TEST TRUC Z (DRIVER U2 - HEADER H1)
  // -------------------------------------------------------------
  Serial.println("-> TRUC Z: Quay Thuan (800 buoc)...");
  digitalWrite(PIN_LED_STATUS, HIGH);
  digitalWrite(PIN_DIR_Z, HIGH);
  for (int i = 0; i < 800; i++) {
    stepMotor(PIN_STEP_Z, 700);
  }
  delay(400);

  Serial.println("-> TRUC Z: Quay Nguoc (800 buoc)...");
  digitalWrite(PIN_LED_STATUS, LOW);
  digitalWrite(PIN_DIR_Z, LOW);
  for (int i = 0; i < 800; i++) {
    stepMotor(PIN_STEP_Z, 700);
  }
  delay(600);

  Serial.println("--- Het 1 chu ky test, lap lai sau 1 giay ---\n");
  delay(1000);
}

