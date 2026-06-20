# Phase 3b — BLE-Observer Sensors as `hardware_type "ble"` (implementation plan)

> From the `ble-phase3b-understand` workflow (2026-06-20). Mirrors the LD2410b
> multi-channel-on-a-bus integration. Decode already works in the modesp_ble scanner
> for the user's Xiaomi `a4:c1:38:b4:dc:11` (BTHome).

## Architecture (resolved)
`modesp_ble` owns the radio; the driver owns decode→channel mapping; HAL stays out (mostly).
- New `BleCentral` registry in modesp_ble = the existing file-static `s_seen[]` cache, made public + zero-heap. GAP cb (NimBLE host task) calls `dispatch()`; driver calls `register_mac()` at factory time + reads the cached `BleReading` in `read()`.
- Dependency (no cycle): `drivers/ble_xiaomi_th → modesp_ble (+ modesp_hal for ISensorDriver/Binding/registry)`. `driver_manager` stays BLE-agnostic (string→factory via DriverRegistry).
- EquipmentBase auto-publishes any sensor whose `type() != "digital_input"` as `equipment.<role>` + `equipment.<role>_ok` (EMA-filtered). So channels surface with ZERO module code.

## Steps (each independently buildable)
1. **modesp_ble `BleCentral`** — new `include/modesp/ble/ble_central.h` (`BleReading{temp_c,has_temp,hum_pct,has_hum,batt_pct,batt_mv,rssi,last_us}`, `BleCentral{register_mac(mac6), dispatch(...), device_count()}`, fixed pool MAX_DEVICES=8, zero-heap). Move `update_sensor()`/`seen_index()` merge logic into `dispatch()`/`find_or_add()`; `ble_service.cpp update_sensor()` also calls `BleCentral::instance().dispatch(...)`.
2. **generate_ui.py** — add `"ble"` to `VALID_HARDWARE_TYPES` (~L464) and `"ble_devices": "ble"` to `BOARD_SECTION_TO_HW_TYPE` (~L615). Auto-Kconfig/register-all already picks up `category:"sensor"` drivers — nothing else. Optional MAC-regex check on the board entry. Add a pytest case.
3. **drivers/ble_xiaomi_th/** — manifest (`hardware_type:"ble"`, `category:"sensor"`, `requires_address:true`, `multiple_per_bus:true`, `_address_channels` temperature/humidity/battery, `stale_ms` setting); CMakeLists via `modesp_driver_component(... REQUIRES modesp_ble)`; cpp = `ISensorDriver` impl (cache pointer + channel enum, `read()` returns cached per channel, `is_healthy()` = fresh within `stale_ms`) + `ble_xiaomi_th_factory(Binding,HAL)` (parse MAC, `BleCentral::register_mac`, set role/channel) + `MODESP_REGISTER_SENSOR(ble_xiaomi_th, &factory)`. Fixed pool of driver instances.
4. **boards/stand_s3/board.json** — `"ble_devices":[{"id":"ble_xiaomi_bthome","mac":"a4:c1:38:b4:dc:11","format":"auto","label":...}]`. **bindings.json** — 3 entries (one per channel) `{hardware:"ble_xiaomi_bthome", driver:"ble_xiaomi_th", role:"<room_*>", module:"equipment", address:"temperature|humidity|battery"}`. Then `drivers_sync.py --fix` to enable `CONFIG_MODESP_DRIVER_BLE_XIAOMI_TH`.
5. **HAL/Equipment** — Equipment: automatic (no code). HAL: depends on MAC-plumbing decision (see below).
6. **main.cpp / main CMakeLists** — none (driver auto-registers; modesp_ble already linked).

## Open decisions
1. **Role names** → `equipment.<role>` keys (e.g. room_temp/room_humid/room_batt).
2. **Battery channel?** EquipmentBase EMA-smooths it (fine for slow %).
3. **MAC→factory plumbing (the real fork):**
   - **B-ii (recommended):** small `BleDeviceConfig{id,mac}` in BoardConfig + ConfigService parse + `HAL::find_ble_device(id)` → factory does `hal.find_ble_device(b.hardware_id)->mac`. Preserves the `factory(Binding,HAL)` symmetry; mirrors I2S precedent (HAL holds config only, never touches NimBLE).
   - **Avoid-HAL:** ConfigService stashes ble_devices for `BleCentral`; factory keys off `b.hardware_id`. HAL 100% untouched but board-config parse moves into modesp_ble/ConfigService.
4. **Kconfig:** ble driver needs `MODESP_BLE_CENTRAL`; for now enable both flags manually (defer auto `depends on`).
5. **MAC-format validation** in generate_ui (recommended).
