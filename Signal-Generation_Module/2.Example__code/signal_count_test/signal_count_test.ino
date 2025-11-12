//아래의 라이브러리 설치 필요 (라이브러리 메니저 이용)
  //"MCP_CAN_lib by Cory Fowler"
  //"Adafruit SSD1306"
  //"Adafruit GFX Library"

#include <SPI.h>
#include <mcp_can.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_ADDR 0x3C
#define OLED_SDA 5
#define OLED_SCL 4
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// MCP2515
#define CAN_CS 7
#define CAN_INT 1
MCP_CAN CAN(CAN_CS);

// Button
#define BTN_PIN 6

unsigned long msgCount = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  pinMode(BTN_PIN, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.setCursor(0, 0);
  display.println("CAN Signal Generator");
  display.println("Init MCP2515...");
  display.display();

  // CAN Initialize, 500 kbps
  if (CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ) == CAN_OK) {
    Serial.println("CAN init OK!");
    display.println("CAN init OK!");
  } else {
    Serial.println("CAN init FAIL!");
    display.println("CAN init FAIL!");
  }
  display.display();

  CAN.setMode(MCP_NORMAL);  // CAN BUS 정상 모드
}

void loop() {
  if (digitalRead(BTN_PIN) == LOW) {
    sendCANMessage();
    delay(250);  // 버튼 디바운스
  }
}

void sendCANMessage() {
  byte data[8] = {0};

  msgCount++;

  data[0] = (msgCount >> 24) & 0xFF;
  data[1] = (msgCount >> 16) & 0xFF;
  data[2] = (msgCount >> 8) & 0xFF;
  data[3] = msgCount & 0xFF;

  CAN.sendMsgBuf(0x123, 0, 4, data);  // CAN ID 0x123, DLC=4 바이트

  Serial.printf("Sent CAN message #%lu\n", msgCount);

  // OLED 표시
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("CAN SIGNAL TX");
  display.print("ID: 0x123\nCOUNT: ");
  display.println(msgCount);
  display.display();
}
