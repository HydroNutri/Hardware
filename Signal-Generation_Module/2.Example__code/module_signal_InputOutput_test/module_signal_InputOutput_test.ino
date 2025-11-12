#include <SPI.h>
#include <mcp_can.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- OLED ----------------
#define OLED_SDA 5
#define OLED_SCL 4
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ---------------- MCP2515 CAN ----------------
#define CAN_CS 7
#define CAN_INT 1
MCP_CAN CAN(CAN_CS);

// ---------------- BUTTON ----------------
#define BTN_PIN 6
int mode = 1;   // MODE1: 수조 / MODE2: 재배기

// ------- MODE 1(수조) 기본값 -------
float wt_temp = 23.5;
float wt_ph   = 7.1;
int   wt_tds  = 700;
int   wt_tur  = 50;
int   wt_do   = 85;
int   wt_dist = 300;
bool  wt_light = false;

// ------- MODE 2(재배기) 기본값 -------
float gr_temp = 25.5;
float gr_hum  = 60.0;
int   gr_r = 50, gr_b = 50, gr_w = 50, gr_uv = 50;
bool  leak1 = false, leak2 = false, leak3 = false, leak4 = false;

// ---------------- 랜덤 변화 (float, int 오버로드) ----------------
float changeValueFloat(float current, float minV, float maxV, float delta) {
  float next = current + ((float)random(-100, 101) / 100.0f) * delta;
  return constrain(next, minV, maxV);
}

int changeValueInt(int current, int minV, int maxV, int delta) {
  int next = current + random(-delta, delta + 1);
  return constrain(next, minV, maxV);
}

// ---------------- OLED 출력 ----------------
void showOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (mode == 1) {
    display.print("MODE1 WaterTank\n");
    display.printf("T: %.1fC  pH: %.2f\n", wt_temp, wt_ph);
    display.printf("TDS:%dppm DO:%d%%\n", wt_tds, wt_do);
    display.printf("Turb:%d NTU Dist:%dmm\n", wt_tur, wt_dist);
    display.printf("Light: %s\n", wt_light ? "ON" : "OFF");
  }
  else {
    display.print("MODE2 GrowBed\n");
    display.printf("T:%.1fC H:%.1f%%\n", gr_temp, gr_hum);
    display.printf("R:%d B:%d W:%d UV:%d\n", gr_r, gr_b, gr_w, gr_uv);
    display.printf("Leak:%d%d%d%d\n", leak1, leak2, leak3, leak4);
  }

  display.display();
}

// ---------------- CAN 송신 ----------------
void sendCAN() {
  byte buf[8];

  if (mode == 1) {
    wt_temp = changeValueFloat(wt_temp, 20.0, 30.0, 1.0);
    wt_ph   = changeValueFloat(wt_ph, 6.5, 7.5, 0.05);
    wt_tds  = changeValueInt(wt_tds, 600, 900, 5);
    wt_tur  = changeValueInt(wt_tur, 30, 70, 2);
    wt_do   = changeValueInt(wt_do, 80, 95, 1);

    buf[0] = (int)wt_temp;
    buf[1] = (int)wt_dist;
    buf[2] = (int)(wt_ph * 10);
    buf[3] = wt_do;
    buf[4] = highByte(wt_tds);
    buf[5] = lowByte(wt_tds);
    buf[6] = wt_tur;
    buf[7] = wt_light;

    CAN.sendMsgBuf(0x101, 0, sizeof(buf), buf);
  }
  else {
    gr_temp = changeValueFloat(gr_temp, 20.0, 35.0, 1.0);
    gr_hum  = changeValueFloat(gr_hum, 40.0, 80.0, 2.0);

    buf[0] = (int)gr_temp;
    buf[1] = (int)gr_hum;
    buf[2] = gr_r;
    buf[3] = gr_b;
    buf[4] = gr_w;
    buf[5] = gr_uv;
    buf[6] = leak1 | (leak2 << 1) | (leak3 << 2) | (leak4 << 3);
    buf[7] = 0;

    CAN.sendMsgBuf(0x102, 0, sizeof(buf), buf);
  }

  Serial.printf("\n[TX] MODE%d ", mode);
  for (int i = 0; i < 8; i++) Serial.printf("%02X ", buf[i]);
  Serial.println();

  showOLED();
}

// ---------------- CAN 수신 ----------------
void receiveCAN() {
  if (!digitalRead(CAN_INT)) {
    unsigned long rxId;
    byte len;
    byte buf[8];
    CAN.readMsgBuf(&rxId, &len, buf);

    Serial.printf("[RX] ID:0x%03lX Data:", rxId);
    for (byte i = 0; i < len; i++) Serial.printf(" %02X", buf[i]);
    Serial.println();

    if (rxId == 0x201)  wt_light = buf[0];
    if (rxId == 0x202) {
      gr_r  = buf[0];
      gr_b  = buf[1];
      gr_w  = buf[2];
      gr_uv = buf[3];
    }

    showOLED();
  }
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK)
    Serial.println("CAN INIT OK");
  else
    Serial.println("CAN INIT FAILED");

  CAN.setMode(MCP_NORMAL);
  showOLED();
}

// ---------------- LOOP ----------------
void loop() {
  static unsigned long prev = 0;

  if (digitalRead(BTN_PIN) == LOW) {
    mode = (mode == 1) ? 2 : 1;
    Serial.printf("\n===== MODE %d =====\n", mode);
    delay(300);
  }

  if (millis() - prev > 1000) {
    prev = millis();
    sendCAN();
  }

  receiveCAN();
}
