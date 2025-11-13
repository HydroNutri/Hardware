# Aquaponics Main Controller – ESP-IDF C++ Reference (SIM_MODE)

This is a **C++ (ESP-IDF)** reference implementation of the "메인 컨트롤러 모듈" firmware.
It defaults to **SIM_MODE** (no real CAN/UART hardware) and matches the key requirements:
- CAN collection at **100 ms**
- UART server uplink at **200 ms**
- UI/console refresh at **200 ms**
- LED/Alarm rules (Blue/Green/Red)
- Fail-safe behavior when server link is lost
- Basic schedules for feeding / LED brightness

## Build & Flash (ESP-IDF v5+)
```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Switch to real hardware (optional)
- Set `SIM_MODE` to `0` in `config.h` and implement TWAI(CAN) + UART pin mapping.
- Adjust LED (GPIO) pins in `config.h` to suit your board.

## Console commands (over USB Serial)
`help | feed <g> | led <0-100> | srvdown | srvup`
