//수동 부저 테스트 코드
#define BUZZER_PIN 25

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
  tone(BUZZER_PIN, 1000);  // 1kHz 삐
  delay(200);
  noTone(BUZZER_PIN);
  delay(200);
}
