//CAN 통신을 이용한 양뱡향 채팅 

#include <driver/twai.h>

const gpio_num_t TX_PIN = GPIO_NUM_21;
const gpio_num_t RX_PIN = GPIO_NUM_22;

String inputBuffer = "";

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 CAN Chat Example (SN65HVD230)");

  twai_general_config_t g_config =
      TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK ||
      twai_start() != ESP_OK) {
    Serial.println("❌ CAN init failed!");
    while (1) delay(1000);
  }
  Serial.println("✅ CAN started. Type message and press Enter to send.");
}

void loop() {
  // ---------- ① 수신 처리 ----------
  twai_message_t rx_msg;
  if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
    Serial.print("\n📩 Received: ");
    for (int i = 0; i < rx_msg.data_length_code; i++) {
      Serial.print((char)rx_msg.data[i]);
    }
    Serial.println();
  }

  // ---------- ② 시리얼 입력 처리 ----------
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        sendCANMessage(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }
}

void sendCANMessage(String msg) {
  twai_message_t tx_msg = {};
  tx_msg.identifier = 0x123;
  tx_msg.extd = 0;

  // 메시지를 8바이트씩 나눠서 전송
  int totalLen = msg.length();
  for (int i = 0; i < totalLen; i += 8) {
    int chunkLen = min(8, totalLen - i);
    tx_msg.data_length_code = chunkLen;
    for (int j = 0; j < chunkLen; j++) {
      tx_msg.data[j] = msg[i + j];
    }
    if (twai_transmit(&tx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
      Serial.print("📤 Sent: ");
      for (int j = 0; j < chunkLen; j++) Serial.print((char)tx_msg.data[j]);
      Serial.println();
    } else {
      Serial.println("⚠️ Send failed");
    }
    delay(10);
  }
}
