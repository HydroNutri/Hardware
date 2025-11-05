#include <driver/twai.h>  // ESP32 내장 CAN(TWAI) 드라이버

// 송수신 핀 지정 (SN65HVD230 연결)
const gpio_num_t TX_PIN = GPIO_NUM_21;
const gpio_num_t RX_PIN = GPIO_NUM_22;

// 송신 모드(1) / 수신 모드(0)
#define IS_SENDER 1

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 CAN (SN65HVD230) Test Start");

  // TWAI 설정 구조체
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();   // 속도 500 kbps
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // 모든 메시지 수신 허용

  // 드라이버 설치 및 시작
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("Driver installed");
  } else {
    Serial.println("Driver install failed");
    while (1);
  }

  if (twai_start() == ESP_OK) {
    Serial.println("CAN started successfully");
  } else {
    Serial.println("CAN start failed");
    while (1);
  }
}

void loop() {
#if IS_SENDER
  // === 송신 모드 ===
  twai_message_t message;
  message.identifier = 0x100;   // CAN ID
  message.extd = 0;             // 표준 ID
  message.data_length_code = 4; // 데이터 길이 (1~8)
  message.data[0] = 0xAA;
  message.data[1] = 0xBB;
  message.data[2] = 0xCC;
  message.data[3] = 0xDD;

  if (twai_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
    Serial.println("Message sent!");
  } else {
    Serial.println("Send failed");
  }
  delay(1000);

#else
  // === 수신 모드 ===
  twai_message_t rx_msg;
  if (twai_receive(&rx_msg, pdMS_TO_TICKS(1000)) == ESP_OK) {
    Serial.printf("Received CAN ID: 0x%03X, DLC: %d, Data: ", rx_msg.identifier, rx_msg.data_length_code);
    for (int i = 0; i < rx_msg.data_length_code; i++) {
      Serial.printf("%02X ", rx_msg.data[i]);
    }
    Serial.println();
  } else {
    Serial.println("No message received");
  }
#endif
}
