/*
 * ==============================================================================
 * CODE TEST SERVO 180 DO QUAY MUOT (CHONG SUT AP & GIAT KHAY)
 *
 * NOI CHAN:
 *   - Day Cam / Vang (Signal) -> D13 (GPIO 13)
 *   - Day Do (+5V)            -> Nguon 5V >= 2A
 *   - Day Nau / Den (GND)     -> GND chung voi ESP32
 * ==============================================================================
 */

#define PIN_SERVO_PWM 13
#define PIN_LED       2
#define SERVO_FREQ    50
#define SERVO_RES     16

void setServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  long us = map(angle, 0, 180, 500, 2500);
  uint32_t duty = (uint32_t)((us * 65535UL) / 20000UL);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO_PWM, duty);
#else
  ledcWrite(0, duty);
#endif
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_SERVO_PWM, SERVO_FREQ, SERVO_RES);
#else
  ledcSetup(0, SERVO_FREQ, SERVO_RES);
  ledcAttachPin(PIN_SERVO_PWM, 0);
#endif
  setServoAngle(180);
  delay(500);
}

void loop() {
  // Quay muot tu 180 do ve 0 do (Day xe vao)
  digitalWrite(PIN_LED, HIGH);
  for (int a = 180; a >= 0; a -= 2) {
    setServoAngle(a);
    delay(10);
  }
  delay(1000);

  // Quay muot tu 0 do len 180 do (Rut ve)
  digitalWrite(PIN_LED, LOW);
  for (int a = 0; a <= 180; a += 2) {
    setServoAngle(a);
    delay(10);
  }
  delay(1000);
}
