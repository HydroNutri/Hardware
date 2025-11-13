#pragma once

// ===== Timing requirements from the spec =====
// CAN: 100ms, UI: 200ms, UART: 200ms
#define CAN_PERIOD_MS   100
#define UI_PERIOD_MS    200
#define UART_PERIOD_MS  200

// ===== Modes =====
#define SIM_MODE 1  // 1: simulate CAN/UART in software, 0: use TWAI & real UART

// ===== GPIO (adjust to your board) =====
#define PIN_LED_BLUE   GPIO_NUM_2
#define PIN_LED_GREEN  GPIO_NUM_4
#define PIN_LED_RED    GPIO_NUM_5
// Optional buzzer pin (not used by default)
#define PIN_BUZZER     GPIO_NUM_15

// UART settings (server uplink). When SIM_MODE==1, we just print framed packets.
#define UART_PORT      UART_NUM_1
#define UART_TX_PIN    GPIO_NUM_17
#define UART_RX_PIN    GPIO_NUM_16
#define UART_BAUD      115200

// UART frame (STX/LEN/TYPE/DATA/CRC/ETX)
#define UART_STX 0x02
#define UART_ETX 0x03
