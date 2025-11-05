// Rotary Encoder test (ESP32 + KY-040)
// CLK=32, DT=33, SW=26  (부저는 GPIO25)

#define CLK 32
#define DT 33
#define SW 26

int lastCLKState;
int counter = 0;
unsigned long lastTime = 0;

void IRAM_ATTR handleEncoder() {
  unsigned long now = millis();
  if (now - lastTime < 3) return;  // 3 ms 디바운스
  lastTime = now;

  int currentCLK = digitalRead(CLK);
  int currentDT  = digitalRead(DT);

  if (currentCLK != lastCLKState) {
    if (currentDT != currentCLK) {
      counter++;  // 시계 방향
      Serial.printf("Clockwise → %d\n", counter);
    } else {
      counter--;  // 반시계 방향
      Serial.printf("Counterclockwise → %d\n", counter);
    }
  }
  lastCLKState = currentCLK;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Rotary Encoder Test Start");

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  lastCLKState = digitalRead(CLK);
  attachInterrupt(digitalPinToInterrupt(CLK), handleEncoder, CHANGE);
}

void loop() {
  if (digitalRead(SW) == LOW) {
    Serial.println("Button pressed!");
    delay(250);  // 버튼 디바운스
  }
}
