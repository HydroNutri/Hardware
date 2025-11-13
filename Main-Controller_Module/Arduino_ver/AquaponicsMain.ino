/*
  Aquaponics Main Controller – Arduino IDE Sketch
  ------------------------------------------------
  Implements the core firmware behavior per 요구사항 정의서 v0.1.0:
  - CAN collection: 100 ms (simulated when SIM_MODE=1)
  - UART uplink:    200 ms (framed JSON)
  - UI refresh:     200 ms (Serial console dashboard)
  - LED/Alarm rules, basic persistence (Preferences)
  - Fail-safe: Blue LED off when server link lost

  Board: ESP32 Dev Module (Arduino core)
*/

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// ---------------------- Data & Enums ---------------------- //
enum ModuleID : uint8_t { MAIN_ID=0x01, TANK=0x10, GROW=0x20, NUTRI=0x30, FEED=0x40 };
enum Cmd : uint8_t { SENS=0x01, STAT=0x02, CMD=0x10, ACK=0x11, ERR=0x12 };

struct TankState { float temp=0, level=0, ph=7.0f, tds=0, turb=0, do_pct=0; };
struct GrowState { float temp=0, hum=0; uint8_t leak_bits=0; int led=0; };
struct NutriState { uint8_t ratio[4]={0,0,0,0}; uint16_t remain[4]={0,0,0,0}; };
struct FeedState { uint16_t remain_g=0; };
struct LEDState { bool blue=false, green=false, red=false; };

// ---------------------- Globals ---------------------- //
Preferences prefs;
LEDState ledState;
TankState tank;
GrowState grow;
NutriState nutri;
FeedState feed;

bool uartConnected = true;
uint32_t lastSeen[256] = {0};

// timers
unsigned long tCan = 0, tUart = 0, tUI = 0;

// input buffer
String inbuf;

// ---------------------- Utils ---------------------- //
static inline uint32_t nowMs() { return millis(); }

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

static void applyLeds() {
  digitalWrite(PIN_LED_BLUE,  ledState.blue  ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, ledState.green ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,   ledState.red   ? HIGH : LOW);
}

// ---------------------- Simulation (SIM_MODE) ---------------------- //
#if SIM_MODE
static float frand(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 10000) / 10000.0f);
}
static void simTick() {
  // Tank
  tank.temp  = 24.0f + frand(-0.3f, 0.3f);
  tank.level = 60.0f + frand(-1.0f, 1.0f);
  tank.ph    = 7.2f  + frand(-0.2f, 0.2f);
  tank.tds   = 350.0f + frand(-10.0f, 10.0f);
  tank.turb  = max(0.0f, frand(0.0f, 5.0f));
  tank.do_pct= 85.0f + frand(-2.0f, 2.0f);
  lastSeen[TANK] = nowMs();

  // Grow
  grow.temp = 23.0f + frand(-0.5f, 0.5f);
  grow.hum  = 55.0f + frand(-2.0f, 2.0f);
  if (random(0, 1000) == 0) {
    uint8_t bit = 1u << random(0,4);
    grow.leak_bits ^= bit; // toggle
  }
  lastSeen[GROW] = nowMs();

  // Nutri
  // ratios static demo
  nutri.ratio[0]=10; nutri.ratio[1]=10; nutri.ratio[2]=0; nutri.ratio[3]=0;
  for (int i=0;i<4;i++) {
    if (random(0,10)==0 && nutri.remain[i]>0) nutri.remain[i]--;
  }
  lastSeen[NUTRI] = nowMs();

  // Feed
  if (random(0,100)==0 && feed.remain_g>0) feed.remain_g--;
  lastSeen[FEED] = nowMs();
}
#else
// TODO: integrate real CAN receive (TWAI) and update tank/grow/nutri/feed + lastSeen[]
static void simTick() {}
#endif

// ---------------------- Alarms & Watch ---------------------- //
static void updateWatchAndAlarms() {
  uint32_t now = nowMs();
  bool all_ok = true;
  ModuleID ids[4] = {TANK, GROW, NUTRI, FEED};
  for (ModuleID id : ids) {
    bool ok = (now - lastSeen[id]) < 500; // 0.5s window
    all_ok = all_ok && ok;
  }
  ledState.green = all_ok;

  bool red = false;
  if (grow.leak_bits) red = true;
  for (int i=0;i<4;i++) if (nutri.remain[i] < 200) red = true;
  if (feed.remain_g == 0) red = true;
  ledState.red = red;
  applyLeds();
}

// ---------------------- UART framing ---------------------- //
static void uartSendFrame(uint8_t type, const String& data) {
  if (!uartConnected) return;
  // STX, LEN(LE), TYPE, DATA..., CRC(LE), ETX
  uint16_t length = (uint16_t)(1 + data.length()); // TYPE + DATA
  Serial.write(UART_STX);
  Serial.write((uint8_t)(length & 0xFF));
  Serial.write((uint8_t)((length >> 8) & 0xFF));
  Serial.write(type);
  for (size_t i=0;i<data.length();++i) Serial.write((uint8_t)data[i]);
  // CRC over [TYPE][DATA]
  // assemble temporary buffer for CRC
  static uint8_t buf[512];
  size_t n = 0;
  buf[n++] = type;
  for (size_t i=0;i<data.length() && n<sizeof(buf); ++i) buf[n++] = (uint8_t)data[i];
  uint16_t crc = crc16_ccitt(buf, n);
  Serial.write((uint8_t)(crc & 0xFF));
  Serial.write((uint8_t)((crc >> 8) & 0xFF));
  Serial.write(UART_ETX);
}

static void sendStatusPacket() {
  String json = "{";
  json += "\"ts\":" + String(nowMs());
  json += ",\"tank\":{\"t\":" + String(tank.temp,2) + ",\"lvl\":" + String(tank.level,1) +
          ",\"ph\":" + String(tank.ph,2) + ",\"tds\":" + String(tank.tds,0) +
          ",\"turb\":" + String(tank.turb,2) + ",\"do\":" + String(tank.do_pct,1) + "}";
  json += ",\"grow\":{\"t\":" + String(grow.temp,2) + ",\"h\":" + String(grow.hum,1) +
          ",\"leak\":" + String(grow.leak_bits) + ",\"led\":" + String(grow.led) + "}";
  json += ",\"nutri\":{\"ratio\":[" + String(nutri.ratio[0]) + "," + String(nutri.ratio[1]) + "," +
          String(nutri.ratio[2]) + "," + String(nutri.ratio[3]) + "],\"remain\":[" +
          String(nutri.remain[0]) + "," + String(nutri.remain[1]) + "," +
          String(nutri.remain[2]) + "," + String(nutri.remain[3]) + "]}";
  json += ",\"feed\":{\"remain\":" + String(feed.remain_g) + "}";
  json += "}";

  ledState.blue = uartConnected;
  applyLeds();
  uartSendFrame(0x01, json);
}

// ---------------------- UI / Console ---------------------- //
static void drawDashboard() {
  Serial.println();
  Serial.println(F("=== Dashboard (Arduino, 200ms) ==="));
  Serial.print(F("[LED] Blue:"));  Serial.print(ledState.blue?"ON ":"OFF ");
  Serial.print(F("Green:"));       Serial.print(ledState.green?"ON ":"OFF ");
  Serial.print(F("Red:"));         Serial.println(ledState.red?"ON ":"OFF ");
  Serial.print(F("Tank  T=")); Serial.print(tank.temp,2);
  Serial.print(F("C L="));  Serial.print(tank.level,1);
  Serial.print(F("mm pH="));Serial.print(tank.ph,2);
  Serial.print(F(" TDS=")); Serial.print(tank.tds,0);
  Serial.print(F("ppm Turb=")); Serial.print(tank.turb,2);
  Serial.print(F("NTU DO=")); Serial.print(tank.do_pct,1); Serial.println(F("%"));
  Serial.print(F("Grow  T=")); Serial.print(grow.temp,2);
  Serial.print(F("C H="));      Serial.print(grow.hum,1);
  Serial.print(F("% Leak=0b"));  Serial.print(grow.leak_bits, BIN);
  Serial.print(F(" LED="));      Serial.print(grow.led); Serial.println(F("%"));
  Serial.print(F("Nutri Ratio="));
  Serial.print(nutri.ratio[0]); Serial.print('/');
  Serial.print(nutri.ratio[1]); Serial.print('/');
  Serial.print(nutri.ratio[2]); Serial.print('/');
  Serial.print(nutri.ratio[3]);
  Serial.print(F(" Remain="));
  Serial.print(nutri.remain[0]); Serial.print('/');
  Serial.print(nutri.remain[1]); Serial.print('/');
  Serial.print(nutri.remain[2]); Serial.print('/');
  Serial.print(nutri.remain[3]); Serial.println(F(" ml"));
  Serial.print(F("Feed  Remain=")); Serial.print(feed.remain_g); Serial.println(F(" g"));
  Serial.println(F("Commands: help | feed <g> | led <0-100> | srvdown | srvup"));
}

static void applyCommand(const String& line) {
  if (line.startsWith("help")) {
    Serial.println(F("help: feed <g>, led <0-100>, srvdown, srvup"));
  } else if (line.startsWith("feed")) {
    int g = 5;
    sscanf(line.c_str()+4, "%d", &g);
    if (g<0) g=0;
    if (feed.remain_g >= g) feed.remain_g -= g;
    else feed.remain_g = 0;
    Serial.printf("Dispense feed: %d g\r\n", g);
  } else if (line.startsWith("led")) {
    int v = 50; sscanf(line.c_str()+3, "%d", &v);
    v = constrain(v, 0, 100);
    grow.led = v;
    prefs.putInt("g_led", v);
    Serial.printf("Set grow LED: %d%%\r\n", v);
  } else if (line.startsWith("srvdown")) {
    uartConnected = false; ledState.blue = false; applyLeds(); Serial.println("UART link -> DOWN");
  } else if (line.startsWith("srvup")) {
    uartConnected = true; ledState.blue = true;  applyLeds(); Serial.println("UART link -> UP");
  } else {
    Serial.println("Unknown command");
  }
}

static void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c=='\r' || c=='\n') {
      if (inbuf.length()) { applyCommand(inbuf); inbuf = ""; }
    } else {
      if (inbuf.length() < 120) inbuf += c;
    }
  }
}

// ---------------------- Arduino lifecycle ---------------------- //
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\r\nAquaponics Main (Arduino) starting...");

  prefs.begin("aqua", false);
  grow.led = prefs.getInt("g_led", 40);

  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  ledState.blue = true; // assume up at boot
  applyLeds();

  // initialize state
  nutri.remain[0]=nutri.remain[1]=nutri.remain[2]=nutri.remain[3]=3000;
  feed.remain_g = 500;
  randomSeed((uint32_t)esp_random()); // ESP32-only; ok in Arduino-ESP32

  // stamp lastSeen to "now" to avoid early false alarms
  uint32_t n = nowMs();
  lastSeen[TANK]=lastSeen[GROW]=lastSeen[NUTRI]=lastSeen[FEED]=n;

  tCan  = nowMs();
  tUart = nowMs();
  tUI   = nowMs();
  Serial.println("Ready. Type 'help' for commands.");
}

void loop() {
  pollSerial();
  unsigned long now = nowMs();

  // CAN tick (100 ms)
  if (now - tCan >= CAN_PERIOD_MS) {
    tCan = now;
    simTick();  // or real CAN read when SIM_MODE==0
    updateWatchAndAlarms();
  }

  // UART uplink (200 ms)
  if (now - tUart >= UART_PERIOD_MS) {
    tUart = now;
    sendStatusPacket();
  }

  // UI refresh (200 ms)
  if (now - tUI >= UI_PERIOD_MS) {
    tUI = now;
    drawDashboard();
  }
}
