# Current Task

## Goal: Demo on real board for potential customer

**Priority:** Reliability and stability over new features.

## What matters for demo

- All 5 modules working correctly on KC868-A6 board
- WebUI responsive and professional (Premium dark theme)
- Temperature reading + thermostat cycling correctly
- Defrost cycle completes without errors
- Protection alarms trigger and clear properly
- WiFi stable (STA mode preferred, AP fallback works)
- MQTT connected to broker (HA Discovery optional for demo)
- OTA update works (show capability)

## Current state (Phase 18)

- 5 modules, 6 drivers — fully operational
- 119 module state keys (63 STATE_META), 108 host C++ tests, 310 pytest
- KC868-A6 board: I2C PCF8574 expanders (6 relays, 6 inputs)
- WebUI: 76KB gzipped, premium dark theme, UA/EN

## Board for demo

- KC868-A6 (I2C PCF8574 expanders)
- `data/board.json` = `data/board_kc868a6.json`
- Flash: `idf.py -p COM15 flash monitor`
