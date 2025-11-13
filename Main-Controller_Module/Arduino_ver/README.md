# Aquaponics Main – Arduino IDE Sketch

## 보드/툴체인
- Arduino IDE 최신 버전
- **ESP32 by Espressif Systems** 보드 패키지 설치
- 보드: **ESP32 Dev Module** (또는 ESP32 DevKitC WROOM-32D)

## 빌드/실행
1) 이 폴더 전체를 Arduino IDE에서 엽니다(AquaponicsMain 폴더 이름 유지).
2) 시리얼 포트 선택 후 업로드.
3) 시리얼 모니터(115200)에서 대시보드와 명령 입력:
   - `help`, `feed <g>`, `led <0-100>`, `srvdown`, `srvup`

## 구성
- `SIM_MODE=1` (기본): 실제 CAN/UART 없이 **수신·표시 시뮬레이션** 실행
- 타이밍: **CAN 100ms**, **UART 200ms**, **UI 200ms** (요구사항 준수)
- LED 핀: 2(Blue), 4(Green), 5(Red) — 보드에 맞춰 `config.h` 수정 가능
- 설정 저장: 재배기 LED 밝기는 `Preferences`에 저장/복구

## 하드웨어 연동으로 전환하려면
- `config.h`에서 `SIM_MODE`를 `0`으로 변경
- CAN(TWAI)/UART 수신부를 `simTick()` 대체 코드로 구현
- LED/부저/TFT/로터리 핀매핑 적용

## 요구사항 매핑
- 100ms/200ms 주기, LED/알람 규칙, UART 프레임 `[STX][LEN][TYPE][DATA][CRC][ETX]` 반영.
