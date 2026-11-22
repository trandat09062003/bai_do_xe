/*
 * ==============================================================================================
 * CONG CU HIEU CHINH TOA DO DONG CO BUOC THONG MINH QUA BAN PHIM (SERIAL MONITOR)
 * ==============================================================================================
 * Huong dan:
 * 1. Nap code nay vao ESP32, mo Serial Monitor (Toc do 115200 baud).
 * 2. Dung ban phim may tinh bam cac phim sau de dieu khien xe den dung vi tri:
 *    - Phim 'w' : Truc Z di LEN (+200 buoc)   | 'W' : di LEN nhanh (+1000 buoc)
 *    - Phim 's' : Truc Z di XUONG (-200 buoc) | 'S' : di XUONG nhanh (-1000 buoc)
 *    - Phim 'd' : Truc X sang PHAI (+200 buoc)| 'D' : sang PHAI nhanh (+1000 buoc)
 *    - Phim 'a' : Truc X sang TRAI (-200 buoc)| 'A' : sang TRAI nhanh (-1000 buoc)
 *    - Phim 'h' : Chay ve HOME (Goc chuan cong tac hanh trinh)
 *    - Phim 'e' : Test Servo DAY khay xe ra
 *    - Phim 'r' : Test Servo THU khay xe ve
 *
 * 3. Khi xe da dung dung ngay ngan truoc o do, bam phim de LUU TOA DO:
 *    - Bam '1' : Luu vi tri hien tai vao O 1 (Tang 1, Cot 1)
 *    - Bam '2' : Luu vi tri hien tai vao O 2 (Tang 1, Cot 2)
 *    - Bam '3' : Luu vi tri hien tai vao O 3 (Tang 2, Cot 1)
 *    - Bam '4' : Luu vi tri hien tai vao O 4 (Tang 2, Cot 2)
 *    - Bam 'p' : IN TOAN BO BANG TOA DO DE COPY-PASTE VAO CODE CHINH!
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

Servo trayServo;

long currentX = 0;
long currentZ = 0;

long slotX[4] = {2000, 6000, 2000, 6000};
long slotZ[4] = {1500, 1500, 5500, 5500};

void stepPulse(int stepPin, int delayUs = 500) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(delayUs);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(delayUs);
}

void moveRelX(long steps) {
  if (steps == 0) return;
  bool dir = (steps > 0);
  digitalWrite(PIN_DIR_X, dir ? HIGH : LOW);
  long count = abs(steps);
  for (long i = 0; i < count; i++) {
    if (!dir && digitalRead(PIN_LIMIT_X_HOME) == LOW) {
      currentX = 0;
      Serial.println("\n[CANH BAO] Cham cong tac HOME X! Dat X = 0");
      return;
    }
    stepPulse(PIN_STEP_X, 450);
  }
  currentX += steps;
  Serial.printf("-> Toa do hien tai: X = %ld | Z = %ld\n", currentX, currentZ);
}

void moveRelZ(long steps) {
  if (steps == 0) return;
  bool dir = (steps > 0);
  digitalWrite(PIN_DIR_Z, dir ? HIGH : LOW);
  long count = abs(steps);
  for (long i = 0; i < count; i++) {
    if (!dir && digitalRead(PIN_LIMIT_Z_MIN) == LOW) {
      currentZ = 0;
      Serial.println("\n[CANH BAO] Cham cong tac DAY Z (MIN)! Dat Z = 0");
      return;
    }
    if (dir && digitalRead(PIN_LIMIT_Z_MAX) == LOW) {
      Serial.println("\n[CANH BAO] Cham cong tac DINH Z (MAX)!");
      return;
    }
    stepPulse(PIN_STEP_Z, 550);
  }
  currentZ += steps;
  Serial.printf("-> Toa do hien tai: X = %ld | Z = %ld\n", currentX, currentZ);
}

void goHome() {
  Serial.println("\n>>> DANG CHAY VE HOME X VA Z...");
  digitalWrite(PIN_DIR_Z, LOW);
  int tZ = 8000;
  while (digitalRead(PIN_LIMIT_Z_MIN) == HIGH && tZ-- > 0) {
    stepPulse(PIN_STEP_Z, 600);
  }
  currentZ = 0;

  digitalWrite(PIN_DIR_X, LOW);
  int tX = 8000;
  while (digitalRead(PIN_LIMIT_X_HOME) == HIGH && tX-- > 0) {
    stepPulse(PIN_STEP_X, 500);
  }
  currentX = 0;
  Serial.println("[OK] Da ve HOME thanh cong! X = 0, Z = 0");
}

void printMenu() {
  Serial.println("\n=======================================================");
  Serial.println("     BANG DIEU KHIEN HIEU CHINH TOA DO BAI DO XE       ");
  Serial.println("=======================================================");
  Serial.println(" [w] / [s] : Truc Z Len / Xuong (+/- 200 buoc)");
  Serial.println(" [W] / [S] : Truc Z Len / Xuong nhanh (+/- 1000 buoc)");
  Serial.println(" [d] / [a] : Truc X Phai / Trai (+/- 200 buoc)");
  Serial.println(" [D] / [A] : Truc X Phai / Trai nhanh (+/- 1000 buoc)");
  Serial.println(" [h]       : Chay ve vi tri HOME (X=0, Z=0)");
  Serial.println(" [e] / [r] : Test Servo Day / Thu khay xe");
  Serial.println("-------------------------------------------------------");
  Serial.println(" [1], [2], [3], [4] : Luu toa do vi tri hien tai vao O 1..4");
  Serial.println(" [p]       : IN BANG TOA DO CHUAN DE COPY VAO CODE CHINH");
  Serial.println("=======================================================");
  Serial.printf("Toa do hien tai: X = %ld | Z = %ld\n\n", currentX, currentZ);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_STEP_X, OUTPUT);
  pinMode(PIN_DIR_X, OUTPUT);
  pinMode(PIN_STEP_Z, OUTPUT);
  pinMode(PIN_DIR_Z, OUTPUT);
  pinMode(PIN_MOTORS_EN, OUTPUT);

  pinMode(PIN_LIMIT_X_HOME, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MIN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_Z_MAX, INPUT_PULLUP);

  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, HIGH); // Tat coi (Active LOW)

  // Bat Driver
  digitalWrite(PIN_MOTORS_EN, LOW);

  trayServo.attach(PIN_SERVO_PWM);
  trayServo.write(10); // Thu ve

  printMenu();
}

void loop() {
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return;

    switch (c) {
      case 'w': moveRelZ(200); break;
      case 'W': moveRelZ(1000); break;
      case 's': moveRelZ(-200); break;
      case 'S': moveRelZ(-1000); break;

      case 'd': moveRelX(200); break;
      case 'D': moveRelX(1000); break;
      case 'a': moveRelX(-200); break;
      case 'A': moveRelX(-1000); break;

      case 'h': goHome(); break;

      case 'e':
        Serial.println("-> Servo: DAY KHAY XE RA (Goc 95 do)...");
        trayServo.write(95);
        break;
      case 'r':
        Serial.println("-> Servo: THU KHAY XE VE (Goc 10 do)...");
        trayServo.write(10);
        break;

      case '1':
        slotX[0] = currentX; slotZ[0] = currentZ;
        Serial.printf("\n>>> [DA LUU O 1]: X = %ld, Z = %ld <<<\n", currentX, currentZ);
        break;
      case '2':
        slotX[1] = currentX; slotZ[1] = currentZ;
        Serial.printf("\n>>> [DA LUU O 2]: X = %ld, Z = %ld <<<\n", currentX, currentZ);
        break;
      case '3':
        slotX[2] = currentX; slotZ[2] = currentZ;
        Serial.printf("\n>>> [DA LUU O 3]: X = %ld, Z = %ld <<<\n", currentX, currentZ);
        break;
      case '4':
        slotX[3] = currentX; slotZ[3] = currentZ;
        Serial.printf("\n>>> [DA LUU O 4]: X = %ld, Z = %ld <<<\n", currentX, currentZ);
        break;

      case 'p':
        Serial.println("\n=======================================================");
        Serial.println(">>> BANG TOA DO CHUAN 100% CUA BAN (COPY VAO CODE CHINH): <<<");
        Serial.println("=======================================================");
        Serial.printf("SlotInfo slots[TOTAL_SLOTS] = {\n");
        Serial.printf("  {false, \"\", \"\", \"\",  %5ld,  %5ld},  // O 1: Tang 1, Cot 1\n", slotX[0], slotZ[0]);
        Serial.printf("  {false, \"\", \"\", \"\",  %5ld,  %5ld},  // O 2: Tang 1, Cot 2\n", slotX[1], slotZ[1]);
        Serial.printf("  {false, \"\", \"\", \"\",  %5ld,  %5ld},  // O 3: Tang 2, Cot 1\n", slotX[2], slotZ[2]);
        Serial.printf("  {false, \"\", \"\", \"\",  %5ld,  %5ld}   // O 4: Tang 2, Cot 2\n", slotX[3], slotZ[3]);
        Serial.printf("};\n");
        Serial.println("=======================================================\n");
        break;

      default:
        printMenu();
        break;
    }
  }
}

