#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config.h"

static const char* TAG = "MAIN";

// ---------------------- Data & Enums (from spec) ---------------------- //
enum ModuleID : uint8_t { MAIN=0x01, TANK=0x10, GROW=0x20, NUTRI=0x30, FEED=0x40 };
enum Cmd : uint8_t { SENS=0x01, STAT=0x02, CMD=0x10, ACK=0x11, ERR=0x12 };

struct CANMsg {
    uint8_t id;
    uint8_t cmd;
    uint8_t flags;
    uint32_t ts_ms;
    uint8_t payload[32];
    size_t len;
};

// State snapshot
struct TankState { float temp=0, level=0, ph=7.0f, tds=0, turb=0, do_pct=0; };
struct GrowState { float temp=0, hum=0; uint8_t leak_bits=0; int led=0; };
struct NutriState { uint8_t ratio[4]={0,0,0,0}; uint16_t remain[4]={0,0,0,0}; };
struct FeedState { uint16_t remain_g=0; };

struct LEDState { bool blue=false, green=false, red=false; };

// ---------------------- Global ---------------------- //
static QueueHandle_t g_can_rxq;
static LEDState g_led;
static TankState g_tank;
static GrowState g_grow;
static NutriState g_nutri;
static FeedState g_feed;

// ---------------------- CRC16-CCITT ---------------------- //
static uint16_t crc16_ccitt(const uint8_t* data, size_t len, uint16_t seed=0xFFFF) {
    uint16_t crc = seed;
    for (size_t i=0; i<len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j=0; j<8; ++j) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

// ---------------------- NVS minimal settings ---------------------- //
static int load_led_brightness() {
    nvs_handle_t h;
    if (nvs_open("aqua", NVS_READONLY, &h) != ESP_OK) return 40;
    int32_t v = 40;
    nvs_get_i32(h, "g_led", &v);
    nvs_close(h);
    return (int)v;
}

static void save_led_brightness(int v) {
    nvs_handle_t h;
    if (nvs_open("aqua", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "g_led", v);
    nvs_commit(h);
    nvs_close(h);
}

// ---------------------- Utils ---------------------- //
static void set_leds() {
    gpio_set_level(PIN_LED_BLUE,  g_led.blue  ? 1 : 0);
    gpio_set_level(PIN_LED_GREEN, g_led.green ? 1 : 0);
    gpio_set_level(PIN_LED_RED,   g_led.red   ? 1 : 0);
}

static uint32_t now_ms() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// ---------------------- Simulated modules (SIM_MODE) ---------------------- //
#if SIM_MODE
static void sim_tank_task(void*) {
    while (true) {
        CANMsg m{};
        m.id = TANK; m.cmd = SENS; m.flags = 0; m.ts_ms = now_ms();
        float temp = 24.0f + ((float)(rand()%600-300))/1000.0f; // ±0.3
        float level= 60.0f + ((float)(rand()%200-100))/100.0f;  // ±1.0
        float ph   = 7.2f + ((float)(rand()%400-200))/1000.0f;  // ±0.2
        float tds  = 350.0f + ((float)(rand()%200-100))/10.0f;
        float turb = std::max(0.0f, ((float)(rand()%500))/100.0f);
        float dop  = 85.0f + ((float)(rand()%400-200))/100.0f;
        memcpy(m.payload, &temp, sizeof(float));
        memcpy(m.payload+4, &level, sizeof(float));
        memcpy(m.payload+8, &ph, sizeof(float));
        memcpy(m.payload+12, &tds, sizeof(float));
        memcpy(m.payload+16, &turb, sizeof(float));
        memcpy(m.payload+20, &dop, sizeof(float));
        m.len = 24;
        xQueueSend(g_can_rxq, &m, 0);
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
    }
}

static void sim_grow_task(void*) {
    while (true) {
        CANMsg m{};
        m.id = GROW; m.cmd = SENS; m.flags = 0; m.ts_ms = now_ms();
        float temp = 23.0f + ((float)(rand()%1000-500))/1000.0f;
        float hum  = 55.0f + ((float)(rand()%400-200))/100.0f;
        uint8_t leak = (rand()%1000==0) ? (1u << (rand()%4)) : 0;
        uint8_t led  = (uint8_t)g_grow.led;
        memcpy(m.payload, &temp, sizeof(float));
        memcpy(m.payload+4, &hum, sizeof(float));
        m.payload[8] = leak;
        m.payload[9] = led;
        m.len = 10;
        xQueueSend(g_can_rxq, &m, 0);
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
    }
}

static void sim_nutri_task(void*) {
    uint16_t remain[4] = {3000,3000,3000,3000};
    uint8_t ratio[4] = {10,10,0,0};
    while (true) {
        CANMsg m{};
        m.id = NUTRI; m.cmd = SENS; m.flags = 0; m.ts_ms = now_ms();
        memcpy(m.payload, ratio, 4);
        memcpy(m.payload+4, remain, 8);
        m.len = 12;
        // slow consumption
        if (rand()%10==0) for (int i=0;i<4;i++) if (remain[i]>0) remain[i]--;
        xQueueSend(g_can_rxq, &m, 0);
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
    }
}

static void sim_feed_task(void*) {
    uint16_t remain = 500;
    while (true) {
        CANMsg m{};
        m.id = FEED; m.cmd = SENS; m.flags = 0; m.ts_ms = now_ms();
        if (rand()%100==0 && remain>0) remain--;
        memcpy(m.payload, &remain, 2);
        m.len = 2;
        xQueueSend(g_can_rxq, &m, 0);
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
    }
}
#endif

// ---------------------- CAN receive & watchdog ---------------------- //
static uint32_t last_seen[256] = {0};

static void can_rx_task(void*) {
    CANMsg m{};
    while (true) {
        if (xQueueReceive(g_can_rxq, &m, pdMS_TO_TICKS(200)) == pdTRUE) {
            last_seen[m.id] = now_ms();
            if (m.id==TANK && m.cmd==SENS && m.len>=24) {
                memcpy(&g_tank.temp,  m.payload, 4);
                memcpy(&g_tank.level, m.payload+4, 4);
                memcpy(&g_tank.ph,    m.payload+8, 4);
                memcpy(&g_tank.tds,   m.payload+12,4);
                memcpy(&g_tank.turb,  m.payload+16,4);
                memcpy(&g_tank.do_pct,m.payload+20,4);
            } else if (m.id==GROW && m.cmd==SENS && m.len>=10) {
                memcpy(&g_grow.temp, m.payload, 4);
                memcpy(&g_grow.hum,  m.payload+4, 4);
                g_grow.leak_bits = m.payload[8];
                g_grow.led = m.payload[9];
            } else if (m.id==NUTRI && m.cmd==SENS && m.len>=12) {
                memcpy(g_nutri.ratio, m.payload, 4);
                memcpy(g_nutri.remain, m.payload+4, 8);
            } else if (m.id==FEED && m.cmd==SENS && m.len>=2) {
                memcpy(&g_feed.remain_g, m.payload, 2);
            }
        }
    }
}

static void can_watchdog_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(CAN_PERIOD_MS));
        uint32_t now = now_ms();
        bool all_ok = true;
        ModuleID ids[4] = {TANK, GROW, NUTRI, FEED};
        for (ModuleID id : ids) {
            bool ok = (now - last_seen[id]) < 500; // 0.5s window
            all_ok = all_ok && ok;
        }
        g_led.green = all_ok;
        // Red LED on leak, nutrient low, feed empty
        bool red = false;
        if (g_grow.leak_bits) red = true;
        for (int i=0;i<4;i++) if (g_nutri.remain[i] < 200) red = true;
        if (g_feed.remain_g == 0) red = true;
        g_led.red = red;
        set_leds();
    }
}

// ---------------------- UART Tx (framed JSON) ---------------------- //
static bool uart_connected = true;

static void uart_init() {
#if SIM_MODE
    // No actual UART when sim; console prints are enough.
#else
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    uart_driver_install(UART_PORT, 2048, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#endif
}

static void uart_send_frame(uint8_t type, const std::string& data) {
    std::vector<uint8_t> buf;
    buf.reserve(1 + 2 + 1 + data.size() + 2 + 1);
    buf.push_back(UART_STX);
    uint16_t length = (uint16_t)(1 + data.size());
    buf.push_back(length & 0xFF);
    buf.push_back((length >> 8) & 0xFF);
    buf.push_back(type);
    buf.insert(buf.end(), data.begin(), data.end());
    uint16_t crc = crc16_ccitt((const uint8_t*)&buf[3], length);
    buf.push_back(crc & 0xFF);
    buf.push_back((crc >> 8) & 0xFF);
    buf.push_back(UART_ETX);
#if SIM_MODE
    // Print base64-like hex for visibility
    printf("[UART TX] %zu bytes: ", buf.size());
    for (auto b : buf) printf("%02X ", b);
    printf("\n");
#else
    uart_write_bytes(UART_PORT, (const char*)buf.data(), buf.size());
#endif
}

static void uart_tx_task(void*) {
    uart_init();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(UART_PERIOD_MS));
        if (!uart_connected) {
            g_led.blue = false;
            set_leds();
            continue;
        }
        g_led.blue = true;
        set_leds();
        // minimal JSON
        char json[512];
        snprintf(json, sizeof(json),
            "{\"ts\":%u,\"tank\":{\"t\":%.2f,\"lvl\":%.1f,\"ph\":%.2f,\"tds\":%.0f,\"turb\":%.2f,\"do\":%.1f},"
            "\"grow\":{\"t\":%.2f,\"h\":%.1f,\"leak\":%u,\"led\":%d},"
            "\"nutri\":{\"ratio\":[%u,%u,%u,%u],\"remain\":[%u,%u,%u,%u]},"
            "\"feed\":{\"remain\":%u}}",
            now_ms(),
            g_tank.temp, g_tank.level, g_tank.ph, g_tank.tds, g_tank.turb, g_tank.do_pct,
            g_grow.temp, g_grow.hum, g_grow.leak_bits, g_grow.led,
            g_nutri.ratio[0], g_nutri.ratio[1], g_nutri.ratio[2], g_nutri.ratio[3],
            g_nutri.remain[0], g_nutri.remain[1], g_nutri.remain[2], g_nutri.remain[3],
            g_feed.remain_g);
        uart_send_frame(0x01, std::string(json));
    }
}

// ---------------------- UI/Console & Commands ---------------------- //
static void ui_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(UI_PERIOD_MS));
        printf("\n=== Dashboard ===\n");
        printf("[LED] Blue:%s Green:%s Red:%s\n", g_led.blue?"ON":"OFF", g_led.green?"ON":"OFF", g_led.red?"ON":"OFF");
        printf("Tank  T=%.2fC L=%.1fmm pH=%.2f TDS=%.0f Turb=%.2f DO=%.1f%%\n",
               g_tank.temp, g_tank.level, g_tank.ph, g_tank.tds, g_tank.turb, g_tank.do_pct);
        printf("Grow  T=%.2fC H=%.1f%% Leak=0b%04u LED=%d%%\n", g_grow.temp, g_grow.hum, g_grow.leak_bits, g_grow.led);
        printf("Nutri Ratio=%u/%u/%u/%u Remain=%u/%u/%u/%u ml\n",
               g_nutri.ratio[0], g_nutri.ratio[1], g_nutri.ratio[2], g_nutri.ratio[3],
               g_nutri.remain[0], g_nutri.remain[1], g_nutri.remain[2], g_nutri.remain[3]);
        printf("Feed  Remain=%u g\n", g_feed.remain_g);
        printf("Commands: help | feed <g> | led <0-100> | srvdown | srvup\n");
    }
}

static void command_apply(const char* line) {
    if (strncmp(line,"help",4)==0) {
        printf("help: feed <g>, led <0-100>, srvdown, srvup\n");
    } else if (strncmp(line,"feed",4)==0) {
        int g=5; sscanf(line+4, "%d", &g);
        if (g<0) g=0;
        if (g_feed.remain_g >= g) g_feed.remain_g -= g;
        else g_feed.remain_g = 0;
        printf("Dispense feed: %d g\n", g);
    } else if (strncmp(line,"led",3)==0) {
        int v=50; sscanf(line+3, "%d", &v);
        v = std::max(0,std::min(100,v));
        g_grow.led = v;
        save_led_brightness(v);
        printf("Set grow LED: %d%%\n", v);
    } else if (strncmp(line,"srvdown",7)==0) {
        uart_connected = false; printf("UART link -> DOWN\n");
    } else if (strncmp(line,"srvup",5)==0) {
        uart_connected = true; printf("UART link -> UP\n");
    } else {
        printf("Unknown command\n");
    }
}

static void input_task(void*) {
    // Read from stdin via monitor (IDF monitor forwards keystrokes)
    char line[64];
    while (true) {
        int n = scanf("%63s", line);
        if (n>0) command_apply(line);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ---------------------- Scheduler (basic) ---------------------- //
static void scheduler_task(void*) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Example: if time-based schedules are needed, use sntp + localtime
        // For brevity, we do not implement wall-clock matching here.
    }
}

// ---------------------- App entry ---------------------- //
extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());

    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<PIN_LED_BLUE) | (1ULL<<PIN_LED_GREEN) | (1ULL<<PIN_LED_RED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
    g_grow.led = load_led_brightness();

    g_can_rxq = xQueueCreate(16, sizeof(CANMsg));

#if SIM_MODE
    xTaskCreate(sim_tank_task, "sim_tank", 4096, nullptr, 5, nullptr);
    xTaskCreate(sim_grow_task, "sim_grow", 4096, nullptr, 5, nullptr);
    xTaskCreate(sim_nutri_task,"sim_nutri",4096, nullptr, 5, nullptr);
    xTaskCreate(sim_feed_task, "sim_feed", 4096, nullptr, 5, nullptr);
#else
    // TODO: Initialize TWAI(CAN) driver and create a receiver task that pushes CAN frames to g_can_rxq.
#endif

    xTaskCreate(can_rx_task, "can_rx", 4096, nullptr, 9, nullptr);
    xTaskCreate(can_watchdog_task, "can_watch", 4096, nullptr, 8, nullptr);
    xTaskCreate(uart_tx_task, "uart_tx", 4096, nullptr, 8, nullptr);
    xTaskCreate(ui_task, "ui", 4096, nullptr, 7, nullptr);
    xTaskCreate(input_task, "input", 4096, nullptr, 6, nullptr);
    xTaskCreate(scheduler_task, "sched", 4096, nullptr, 5, nullptr);

    ESP_LOGI(TAG, "Aquaponics Main Controller (ESP-IDF, SIM_MODE=%d) started", SIM_MODE);
}
