/*
 * ==============================================================================================
 * CONG CU HIEU CHINH TOA DO DONG CO BUOC THONG MINH QUA BAN PHIM & LUU FLASH NVS
 * ==============================================================================================
 * Bo mach: Altium Bai_Do_Xe_Thong_Minh_ESP32
 * Huong dan:
 * 1. Nap code nay vao ESP32, mo Serial Monitor (Toc do 115200 baud).
 * 2. Dung ban phim may tinh bam cac phim sau de dieu khien xe den dung vi tri:
 *    - Phim 'w' : Truc Z di LEN (+200 buoc)   | 'W' : di LEN nhanh (+1000 buoc)
 *    - Phim 's' : Truc Z di XUONG (-200 buoc) | 'S' : di XUONG nhanh (-1000 buoc)
 *    - Phim 'd' : Truc X sang PHAI (+200 buoc)| 'D' : sang PHAI nhanh (+1000 buoc)
 *    - Phim 'a' : Truc X sang TRAI (-200 buoc)| 'A' : sang TRAI nhanh (-1000 buoc)
 *    - Phim 'h' : Chay ve HOME (Goc chuan cong tac hanh trinh: Z_MIN va X_HOME)
 *    - Phim 'e' : Test Servo DAY khay xe ra (95 do)
 *    - Phim 'r' : Test Servo THU khay xe ve (10 do)
 *    - Phim 't' : Kiem tra nhanh trang thai 3 cong tac hanh trinh (X_HOME, Z_MIN, Z_MAX)
 *
 * 3. Khi ban nang da dung ngay ngan truoc o do, bam phim de LUU TOA DO TRUC TIEP VAO FLASH NVS:
 *    - Bam '1' : Luu vi tri hien tai vao O 1 (Tang 1, Cot 1)
 *    - Bam '2' : Luu vi tri hien tai vao O 2 (Tang 1, Cot 2)
 *    - Bam '3' : Luu vi tri hien tai vao O 3 (Tang 2, Cot 1)
 *    - Bam '4' : Luu vi tri hien tai vao O 4 (Tang 2, Cot 2)
 *    - Bam 'p' : In toan bo bang toa do hien tai de kiem tra!
 *
 * GHI CHU QUAN TRONG VE SERVO:
 * - Servo MG90S la loai quay 180 do tieu chuan, KHONG PHAI 315 DO!
 * - Co cau gat khay chi can quay 10 do (thu ve) va 95 do (day ra).
 * ==============================================================================================
 */

#include <ESP32Servo.h>
#include <Preferences.h>

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
Preferences prefs;

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

void loadFromNVS() {
  prefs.begin("baidoxe", false);
  for (int i = 0; i < 4; i++) {
    String kX = "sx" + String(i);
    String kZ = "sz" + String(i);
    slotX[i] = prefs.getLong(kX.c_str(), slotX[i]);
    slotZ[i] = prefs.getLong(kZ.c_str(), slotZ[i]);
  }
  prefs.end();
  Serial.println("[NVS] Da doc toa do tu bo nho Flash ESP32!");
}

void saveToNVS(int idx, long x, long z) {
  if (idx < 0 || idx >= 4) return;
  slotX[idx] = x;
  slotZ[idx] = z;
  prefs.begin("baidoxe", false);
  String kX = "sx" + String(idx);
  String kZ = "sz" + String(idx);
  prefs.putLong(kX.c_str(), x);
  prefs.putLong(kZ.c_str(), z);
  prefs.end();
  Serial.printf("\n>>> [DA GHI VAO FLASH] O %d: X = %ld, Z = %ld <<<\n", idx + 1, x, z);
  // Bip 2 tieng
  digitalWrite(PIN_BUZZER, LOW); delay(50); digitalWrite(PIN_BUZZER, HIGH); delay(50);
  digitalWrite(PIN_BUZZER, LOW); delay(50); digitalWrite(PIN_BUZZER, HIGH);
}

void moveRelX(long steps) {
  if (steps == 0) return;
  bool dir = (steps > 0);
  digitalWrite(PIN_DIR_X, dir ? HIGH : LOW);
  long count = abs(steps);
  for (long i = 0; i < count; i++) {
    if (!dir && digitalRead(PIN_LIMIT_X_HOME) == LOW) {
      currentX = 0;
      Serial.println("\n[CANH BAO] Cham cong tac HOME X (GPIO 16)! Dat X = 0");
      return;
    }
    stepPulse(PIN_STEP_X, 450);
    currentX += (dir ? 1 : -1);
  }
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
      Serial.println("\n[CANH BAO] Cham cong tac DAY Z (MIN GPIO 32)! Dat Z = 0");
      return;
    }
    if (dir && digitalRead(PIN_LIMIT_Z_MAX) == LOW) {
      Serial.println("\n[CANH BAO NGUY HIEM] Cham cong tac DINH Z (MAX GPIO 33)!");
      return;
    }
    stepPulse(PIN_STEP_Z, 550);
    currentZ += (dir ? 1 : -1);
  }
  Serial.printf("-> Toa do hien tai: X = %ld | Z = %ld\n", currentX, currentZ);
}

void goHome() {
  Serial.println("\n>>> DANG CHAY VE HOME X VA Z BANG CONG TAC HANH TRINH...");
  trayServo.write(10);
  delay(200);

  // 1. Z ha xuong
  digitalWrite(PIN_DIR_Z, LOW);
  int tZ = 12000;
  while (digitalRead(PIN_LIMIT_Z_MIN) == HIGH && tZ-- > 0) {
    stepPulse(PIN_STEP_Z, 600);
  }
  // Lui nhe roi cham cham
  digitalWrite(PIN_DIR_Z, HIGH);
  for (int i = 0; i < 150; i++) stepPulse(PIN_STEP_Z, 600);
  delay(50);
  digitalWrite(PIN_DIR_Z, LOW);
  tZ = 600;
  while (digitalRead(PIN_LIMIT_Z_MIN) == HIGH && tZ-- > 0) {
    stepPulse(PIN_STEP_Z, 900);
  }
  currentZ = 0;

  // 2. X sang trai
  digitalWrite(PIN_DIR_X, LOW);
  int tX = 12000;
  while (digitalRead(PIN_LIMIT_X_HOME) == HIGH && tX-- > 0) {
    stepPulse(PIN_STEP_X, 500);
  }
  // Lui nhe roi cham cham
  digitalWrite(PIN_DIR_X, HIGH);
  for (int i = 0; i < 150; i++) stepPulse(PIN_STEP_X, 500);
  delay(50);
  digitalWrite(PIN_DIR_X, LOW);
  tX = 600;
  while (digitalRead(PIN_LIMIT_X_HOME) == HIGH && tX-- > 0) {
    stepPulse(PIN_STEP_X, 800);
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
  Serial.println(" [h]       : Chay ve vi tri HOME (X=0, Z=0) bang switch");
  Serial.println(" [e] / [r] : Test Servo Day khay (95 do) / Thu ve (10 do)");
  Serial.println(" [t]       : Kiem tra trang thai 3 cong tac hanh trinh");
  Serial.println("-------------------------------------------------------");
  Serial.println(" [1], [2], [3], [4] : LUU TRUC TIEP TOA DO VAO FLASH NVS");
  Serial.println(" [p]       : IN BANG TOA DO 4 O DO");
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

  loadFromNVS();
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

      case 't':
        Serial.printf("[TEST SWITCH] Home X (16): %s | Min Z (32): %s | Max Z (33): %s\n",
          digitalRead(PIN_LIMIT_X_HOME) == LOW ? "CHAM (LOW)" : "HO (HIGH)",
          digitalRead(PIN_LIMIT_Z_MIN) == LOW ? "CHAM (LOW)" : "HO (HIGH)",
          digitalRead(PIN_LIMIT_Z_MAX) == LOW ? "CHAM (LOW)" : "HO (HIGH)");
        break;

      case '1': saveToNVS(0, currentX, currentZ); break;
      case '2': saveToNVS(1, currentX, currentZ); break;
      case '3': saveToNVS(2, currentX, currentZ); break;
      case '4': saveToNVS(3, currentX, currentZ); break;

      case 'p':
        Serial.println("\n=======================================================");
        Serial.println(">>> BANG TOA DO HIEN TAI TRONG FLASH NVS: <<<");
        for (int i = 0; i < 4; i++) {
          Serial.printf("  O %d: X = %ld, Z = %ld\n", i + 1, slotX[i], slotZ[i]);
        }
        Serial.println("=======================================================\n");
        break;

      default:
        printMenu();
        break;
    }
  }
}
