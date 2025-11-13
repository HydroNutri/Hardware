#pragma once
// ====== Build-time config (Arduino IDE) ======
// Spec timing: CAN 100ms, UI 200ms, UART 200ms
#define CAN_PERIOD_MS    100
#define UI_PERIOD_MS     200
#define UART_PERIOD_MS   200

// Simulation first. Set to 0 to integrate real hardware later.
#define SIM_MODE         1

// LED pins (adjust if needed)
#define PIN_LED_BLUE     2   // Built-in LED on many ESP32 boards
#define PIN_LED_GREEN    4
#define PIN_LED_RED      5

// UART server link framing
#define UART_STX 0x02
#define UART_ETX 0x03
