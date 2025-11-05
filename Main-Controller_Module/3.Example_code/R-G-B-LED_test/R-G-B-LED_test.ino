// ESP32 RGB LED Test
// R=16, G=17, B=14  (B: TFT 회로 피해서 이동)

#define LED_R 16
#define LED_G 17
#define LED_B 14

void setup() {
  Serial.begin(115200);
  Serial.println("RGB LED Test Start (B on GPIO14)");

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  // 초기 상태: 모두 OFF
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
}

void loop() {
  // 순차 점등
  Serial.println("Pattern 1: Sequential Blink");
  digitalWrite(LED_R, HIGH); delay(500);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, HIGH); delay(500);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, HIGH); delay(500);
  digitalWrite(LED_B, LOW);

  // 전체 깜박임
  Serial.println("Pattern 2: All Blink");
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_R, HIGH);
    digitalWrite(LED_G, HIGH);
    digitalWrite(LED_B, HIGH);
    delay(300);
    digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, LOW);
    delay(300);
  }

  // 상태 표시 시뮬레이션
  Serial.println("Pattern 3: Status Simulation");
  digitalWrite(LED_R, HIGH); delay(1000); digitalWrite(LED_R, LOW); // 오류
  digitalWrite(LED_G, HIGH); delay(1000); digitalWrite(LED_G, LOW); // 정상
  for (int i = 0; i < 6; i++) { // 통신 중
    digitalWrite(LED_B, !digitalRead(LED_B));
    delay(150);
  }
  digitalWrite(LED_B, LOW);

  delay(1000);
}
