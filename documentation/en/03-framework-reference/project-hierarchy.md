# ModESP v4 Framework — Authoritative Architecture and Hierarchy

> 📖 **Українською:** [../../uk/03-framework-reference/project-hierarchy.md](../../uk/03-framework-reference/project-hierarchy.md)

**Purpose of this document.** This is the structural reference for the ModESP Framework — a manifest-driven ESP32 firmware framework for industrial automats (ESP-IDF v5.5, C++17 with ETL). It exists for ONE reason: so that an engineer or an AI agent understands how the system is built and does NOT BREAK what already works. Every structural statement here is checked against real code/manifests (paths in parentheses). Rule number one: UI/state/MQTT/module and driver registration are **generated** from manifests — to change something, edit the **manifest**, not the generated output.

> ⭐ The RULES themselves (the numbered "what not to break" charter) are split out into **[rules.md](rules.md)** — read it alongside this document. Here you get the hierarchy and the route; there you get the rules.

---

## 1. LAYERS — the three layers

| Layer | What it is | Where it lives |
|-----|-------|---------|
| **Product** | Business logic + hardware map of a specific product | `modules/`, `boards/` |
| **Framework** | Universal C++ components + generator | `components/`, `tools/` |
| **Platform** | ESP-IDF v5.5, FreeRTOS, NimBLE (bt), LittleFS | `managed_components/`, ESP-IDF |

The dependency direction is strictly `Product → Framework → Platform`. The Framework NEVER depends on a product module.

```
D:/ModESP_v4_Framework/
├── project.json          ← list of active modules + system block (single entry point)
├── CMakeLists.txt        ← board-resolution + generator launch + COMPONENTS
├── components/           ← 10 framework components (C++)
│   ├── modesp_core/         base_module, shared_state, module_manager, app  (ROOT of the graph)
│   ├── modesp_hal/          HAL, driver_manager, driver_registry, char_grid
│   ├── modesp_services/     config, nvs, persist(LittleFS), ota, watchdog, logger
│   ├── modesp_net/          wifi, http_service, ws_service        (optional: CONFIG_MODESP_NET_ENABLE)
│   ├── modesp_ble/          ble_service — the sole owner of the NimBLE host (optional: BLE_ENABLE)
│   ├── modesp_equipment/    EquipmentBase (generic driver binding)
│   ├── modesp_scenario/     track-based FSM / recipes (optional: SCENARIO_ENABLE)
│   ├── modesp_mqtt/         cloud backend MQTT  ┐ mutually exclusive
│   ├── modesp_aws/          cloud backend AWS   ┘ (Kconfig choice)
│   └── jsmn/                vendored JSON parser
├── modules/              ← PRODUCT: abs_test, datalogger, display, equipment,
│                            panel, player, presence, simple_thermo
├── drivers/              ← optional hardware drivers (sensor/actuator/display/audio/io)
│                            amt630a, at7456e, ds18b20, ntc, relay, digital_input,
│                            pcf8574_relay, pcf8574_input, max98357a, ld2410b,
│                            ble_xiaomi_th, ble_nrf_tilt, ble_led_panel
├── boards/               ← per-board hardware: dev, kc868a6, stand_s3
│                            (board.json + bindings.json [+ optional sdkconfig.board])
├── tools/                ← generate_ui.py (generator), compile_scenario.py,
│                            gen_osd_font.py, cmake/modesp_driver.cmake, schemas/
├── main/                 ← app entry component (main.cpp, Kconfig.boards)
├── generated/            ← AUTO-GENERATED .h + .cmake  (DO NOT EDIT)
├── data/                 ← LittleFS image: ui.json, board.json, bindings.json,
│                            www/, i18n/, scenarios/, audio/  (board.json/bindings.json — COPIES)
├── webui/                ← Svelte UI source (STATIC, NOT generated; loads ui.json at runtime)
├── documentation/        ← uk/ (+ en/ mirror)
└── managed_components/   ← ESP-IDF registry deps: etlcpp, littlefs, mqtt, mdns, libhelix-mp3
```

Components discovered: `ls components/` = jsmn, modesp_aws, modesp_ble, modesp_core, modesp_equipment, modesp_hal, modesp_mqtt, modesp_net, modesp_scenario, modesp_services. Modules: abs_test, datalogger, display, equipment, panel, player, presence, simple_thermo.

---

## 2. THE CORE MODEL — Module ↔ Role ↔ Device ↔ Binding

This is the central relationship of the framework. Understand it, and the rest falls into place.

### 2.0 The single peripheral route (principle #1)

**Sensor, actuator, display, panel — all of these are PERIPHERALS.** Regardless of the type of
peripheral and the transport (GPIO / I2C / OneWire / BLE-observer / BLE-connect), the route is
**THE SAME**. No special cases like "panel is separate" or "connect is separate":

```
Peripheral (driver) ─► becomes available ─► [UI BINDING] onto a module role ─► module uses it via the role
   any type            board.json /          "Bindings" page                    knows nothing about the hardware
                       Devices page          (runtime, via the web)
```

- **Binding happens through the UI**, at runtime — the user on the "Bindings" page
  maps a role to a specific peripheral. It is the same mechanism for a wired sensor,
  a BLE observer, a connect display, and a relay.
- **A role is a named slot-need.** A module may declare **any number** of roles,
  including **several of the same type**. Example: a module that needs two displays declares
  `display_main` + `display_aux` (both `type: display`); through the UI each role is bound to
  its own physical display. Same route as "temperature role → sensor".
- Hence you must NOT hardcode a specific peripheral into a module/core: a module declares *needs*
  (roles), the peripheral exists *separately*, and the UI binding stitches them together — the same
  for all types.


```
   MODULE                         DEVICE
 (needs a role)                (specific hardware)
  requires[]                    board.json / devices.json
      │                                │
      │  role: "display_main"          │  id: "disp_0"
      │  type: display                 │  chip: amt630a
      │  driver: [amt630a]             │  bus: i2c_0
      └──────────┐          ┌──────────┘
                 ▼          ▼
             BINDING  (bindings.json)
   {hardware:"disp_0", driver:"amt630a",
    role:"display_main", module:"display"}
              connects the module's role to the device
```

**Definitions:**

- **MODULE** — a unit of business logic (`modules/<name>/`, class `<Name>Module : public BaseModule`). Declares the roles it needs in the top-level `requires[]`.
- **ROLE** — a named slot that a module **needs**: `{role, type: sensor|actuator|display, driver: [allowed drivers], label, optional?}`. It is a capability contract, not hardware.
- **DEVICE** — specific hardware with an `id`. Two sources: (1) WIRED in `board.json` under typed sections (`i2c_displays`, `i2s_buses`, `onewire_buses`, `gpio_outputs`…); (2) RUNTIME BLE in `/data/devices.json`, subscribed via the "Devices" web page.
- **BINDING** — a row in `bindings.json` that JOINS a module's role to a device: `{hardware=<device id>, driver, role, module, address?, settings?}`. The on-device struct is `Binding{hardware_id, role, driver_type, module_name, address, settings[]}` (`hal_types.h`).

**Ownership rule (stated plainly):** a role is declared by EXACTLY THE module that CONSUMES it, in its own `requires`. Devices are configured/subscribed SEPARATELY (board.json / Devices page). The Binding stitches them together. The identity of the role provider is detected without hardcoding: `role_providers()` collects every module that has a top-level `requires` (`tools/generate_ui.py:135-142` — "no hardcoding of the equipment name"). ANY module can be a role provider.

**`binding.module` is routing, not documentation.** DriverManager stamps `entry.module = b.module_name` on every driver (`driver_manager.cpp`), and the consuming module filters bindings by it: `display_module.cpp:75` — `if (!(b.module_name == "display")) continue;`.

**Example (verified on stand_s3):**
- The `display` module owns the `display_main` role (`display/manifest.json:8-16`) → binding `{hardware:disp_0, driver:amt630a, role:display_main, module:display}` (`boards/stand_s3/bindings.json`).
- The `player` module owns `audio_main` → binding `{hardware:i2s_0, driver:max98357a, role:audio_main, module:player}`.

**Example of a rich peripheral driver on the same route — `panel`.** The `panel` role is
declared by the **consumer** module `panel` (`panel/manifest.json` → `requires`), not equipment. The
`ble_led_panel` driver is an ordinary actuator: DriverManager creates it from the binding and indexes it by role
(like a relay). The `panel` module resolves the SAME object by the role name — `find_actuator(role)->as_panel()`
(`panel_module.cpp::on_bind`, filter `binding.module == "panel"`) — and pushes content through its
`IPanelPort`. `as_panel()` is a capability-cast without RTTI on `IActuatorDriver` (the same idiom as
`IDisplayPort::as_power()`). No global singleton: the route is identical to "sensor → role → module".

---

## 3. DEVICE LIFECYCLE — wired (build-time) vs runtime-BLE

### Wired hardware — board.json, build-time
The I2C bus/display, OneWire, GPIO relays/inputs, ADC, UART, I2S, PCF8574 expanders are declared in `board.json` and physically initialised by `HAL::init` at boot. At runtime — GET only (there is no write endpoint for board.json). To change wired hardware — edit board.json and reflash.

### Runtime-BLE — /data/devices.json, subscription via the web
BLE devices are **NOT hardcoded** in board.json. On stand_s3 `board.json` has `ble_devices: []`, and bindings.json contains only the two wired bindings — with an explicit `_note_ble_runtime` note that ALL BLE (Xiaomi/nRF observers + iPixel panel) are added at runtime, after which the room_temp/orientation/panel roles are bound.

**Flow: scan → subscribe → bind**

```
GET /api/ble/scan ─► seen table (only devices with type != '', i.e. IDENTIFIED)
      │
      ▼  the user picks a device on the "Devices" page
POST /api/devices ─► handle_post_devices → validate → write /data/devices.json
      │             (MAX_RUNTIME_DEVICES=12; logs "restart needed")
      ▼  RESTART
ConfigService::on_init: parse_board_json ∪ parse_devices_json  (runtime-wins-by-id)
      │  merged → HAL.remote_devices_ (MAX_REMOTE_DEVICES=16; RemoteDeviceConfig{transport,identity,name})
      ▼
role is bound to a device id (transport-agnostic; find_remote_device resolves id→identity/name)
```

**Two transport seams:**

| | OBSERVER (passive) | CONNECT (GATT) |
|---|---|---|
| Identity | MAC | full adv-name |
| Example | ble_xiaomi_th, ble_nrf_tilt | ble_led_panel (prefix `LED_BLE`) |
| Seam | `adv_decoder.h` | `central_link.h` |
| Registration | `register_adv_decoder` / `register_adv_mfg_decoder` | `register_connect_profile` + `register_connect_matcher` |
| Model | push into a per-MAC cache; `update()` no-op | write THROUGH `ICentralLink` |

**Decoders/matchers are registered at BOOT**, in the driver's register hook (`extern "C" modesp_register_driver_<name>`), NOT in the factory. Verified: `ble_xiaomi_th_driver.cpp:188-191` (`register_adv_decoder` in the hook), `ble_led_panel_driver.cpp:266` (`register_connect_matcher("LED_BLE","ble_led_panel")`). DriverManager::init calls `modesp_register_all_drivers()`. Reason: the factory only runs once a binding already exists — a decoder registered in the factory makes an UNBOUND device INVISIBLE in the scan, so it can never be subscribed. `modesp_ble` is pure transport: all byte parsing lives in the driver.

---

## 4. MANIFEST-DRIVEN CODEGEN — the single source of truth

```
project.json ─┐
modules/*/manifest.json ─┤
drivers/*/manifest.json ─┼─► tools/generate_ui.py ─► data/ui.json + generated/*.h + generated/*.cmake
boards/<b>/board.json ────┤       (+ compile_scenario.py)     + components/modesp_hal/Kconfig
boards/<b>/bindings.json ─┘                                    + main/Kconfig.boards + data/www/i18n/*
```

The generator is `tools/generate_ui.py`. It reads board.json/bindings.json from **`data/`** (not from `boards/`) — CMake copies the active board's pair into `data/` at configure time, before running the generator.

### Generated files — NEVER edit them by hand
Each carries the header `Auto-generated … DO NOT EDIT`. The full set from `generate_ui.py`:

- `data/ui.json` — merged runtime UI schema (WebUI loads it; the WebUI itself is STATIC)
- `generated/state_meta.h` — StateMeta[] + `MODESP_MAX_STATE_ENTRIES`
- `generated/mqtt_topics.h` — MQTT_PUBLISH/SUBSCRIBE/ALARM, TOPIC_ROOT, HA_ENTITIES
- `generated/display_screens.h` — LCD menu tree, MAIN_VALUES
- `generated/features_config.h` — FeatureConfig[] (active per bindings)
- `generated/module_includes.h`, `module_instances.h`, `module_register.h`, `modules.cmake` — auto-registration of modules from project.json (recipe modules excluded)
- `generated/drivers.cmake` (MODESP_ALL_DRIVERS), `required_drivers.cmake` (MODESP_BOUND_DRIVERS), `driver_register_all.h` (guarded register-all)
- `components/modesp_hal/Kconfig` — the "ModESP Drivers" menu (a toggle per driver)
- `main/Kconfig.boards` — the board choice from boards/*/board.json
- `generated/datalogger_channels.h`, `datalogger_events.h` — from manifest `loggable`
- `data/www/i18n/*.json` — language packs

**Exception:** `generated/panel_font_data.h` is NOT written by `generate_ui.py` — it is produced by a separate `tools/gen_osd_font.py`. Do not attribute it to the manifest generator.

### Auto-retrigger + build-time validation
Editing any manifest/project.json/schema/i18n restarts the CMake configure (via `CMAKE_CONFIGURE_DEPENDS` + `CONFIGURE_DEPENDS` GLOBs), which reruns `generate_ui.py`. A manual `idf.py reconfigure` is not needed.

Validation FAILS the build (non-zero exit → CMake FATAL_ERROR) in two layers: (1) **JSON Schema** (draft-07, `tools/schemas/*.schema.json`, `additionalProperties:false`; `jsonschema` is a mandatory build dependency); (2) **domain validators**: ManifestValidator, DriverManifestValidator, cross_validate, validate_loggable, `validate_bindings`. `validate_bindings` checks: fields present; the module is in project.json; the hardware id exists on the board; the driver manifest exists; `driver.hardware_type` matches the type of the board section; requires_address is satisfied; no duplicate role within a module; shared hardware only when `multiple_per_bus` with distinct addresses. Warnings (unused drivers, etc.) do NOT break the build.

---

## 5. DEPENDENCY DIRECTIONS — allowed and forbidden arrows

```
                platform (ESP-IDF, ETL, littlefs, bt/NimBLE)
                        ▲
   ┌────────────────────┼─────────────────────┐
 modesp_core ◄─ hal ◄─ services ◄─ net ◄─ ble  scenario  mqtt/aws  equipment
     (root)         ▲              ▲    │                              ▲
   drivers ─────────┘        ble ──┘ (PRIV_REQUIRES net)         modules/equipment
      ▲                                                                │
   (modesp_hal auto)                                            (subclass EquipmentBase)
   modules ─► framework components (NEVER ─► driver by name)
```

**Allowed:**
- `modules → framework components → platform`. A module REQUIREs `modesp_core` (always) and, as needed, `modesp_hal`/`modesp_net`/`modesp_equipment`/`modesp_scenario`.
- `drivers → modesp_hal` (added automatically by `tools/cmake/modesp_driver.cmake`). BLE drivers additionally `PRIV_REQUIRES modesp_ble`.
- `modesp_ble → modesp_net` (BLE privately requires net — verified `modesp_ble/CMakeLists.txt:14`).

**Forbidden:**
- ❌ `modesp_net → modesp_ble` — the arrow is ONLY `ble → net`. Otherwise NimBLE would leak into offline/WiFi-only builds and invert transport ownership. (`modesp_net/CMakeLists.txt:15` REQUIREs core/services/hal — WITHOUT ble.)
- ❌ any `component → module` or `component → driver by name`. The Framework does not depend on the product.
- ❌ any `modesp_* in modesp_core.REQUIRES` — core is the root (REQUIREs only ETL + platform).

**Optionality:** net/ble/scenario/mqtt/aws gate SRCS on `CONFIG_*` (an empty component when off), but REQUIRES CANNOT be gated (ESP-IDF resolves requirements before loading sdkconfig). Cloud is a mutually exclusive Kconfig choice (MQTT | AWS | NONE); main links exactly one via `target_link_libraries`. That is why the root COMPONENTS explicitly lists mqtt/aws/ble (target_link_libraries does not participate in discovery).

---

## 6. EXTENSION RECIPES

### Add a MODULE
1. `modules/<name>/`: `manifest.json` (`"module":"<name>"`), `CMakeLists.txt`, class `<Name>Module : public BaseModule`.
2. Add `"<name>"` to `project.json → modules`.
3. `idf.py build`. The generator makes the includes/instances/register (sorts by priority)/modules.cmake itself.
- Name: `^[a-z][a-z0-9_]*$`, folder == the `module` field. Class defaults to CamelCase+`Module` (override via `class_name`). `module_type:"recipe"` → excluded from C++.
- BaseModule hooks: `on_init` / `on_update(dt)` / `on_message` / `on_stop` / `on_bind(DriverManager&, BindingTable&, HAL&)` — the ONLY place where a hardware module resolves drivers from bindings.

### Add a DRIVER (sensor/actuator)
1. `drivers/<name>/manifest.json`: `driver`, `category` (sensor|actuator|io|display|audio), `hardware_type` (one of the values from `BOARD_SECTION_TO_HW_TYPE`), `provides`, `requires_address?`, `multiple_per_bus?`, `settings`.
2. In the `.cpp`: a factory `fn(const Binding&, HAL&)` + ONE macro at file scope: `MODESP_REGISTER_SENSOR(<name>,&factory)` / `MODESP_REGISTER_ACTUATOR(...)` / `_WITH_DISCOVERY` / `_DISPLAY` / `_AUDIO`.
3. `CMakeLists.txt` via `modesp_driver_component()` (makes the driver optional: SRCS are gated on `CONFIG_MODESP_DRIVER_<NAME>`).
4. Use it in `bindings.json`.
- **The driver name == the folder == the first macro arg** — the generator mechanically derives `modesp_register_driver_<name>`. A mismatch = a link error or silently unregistered.
- **display/audio** is created NOT by DriverManager but by the owner module in its `on_bind` (`create_display`/`create_audio`) — these are module-bound backends (`is_module_backend`). `IDisplayPort` is a semantic seam (ADR-002: geometry does NOT leak through caps()). **panel is NOT like that:** it is an ordinary actuator created by DriverManager; the module resolves it by role (`find_actuator(role)->as_panel()`) — a rich peripheral without a separate seam.

### Add a ROLE
In the `requires[]` of the owner module: `{role, type, driver:[...], label, optional?}`; on the board — a binding `{hardware, driver, role, module}`. The module reads the value from SharedState/the resolved driver — it does not touch GPIO. A single BLE device feeds several roles via `address_channels` (binding.address selects the channel).

### Add a BLE device type
- **OBSERVER:** a sensor driver (`hardware_type:"ble"`, `requires_address:true`) + in the boot hook `register_adv_decoder(fn)` (16-bit service data) or `register_adv_mfg_decoder(fn)` (manufacturer). Report via `report_sensor()`.
- **CONNECT:** `register_connect_profile({name_prefix, write_uuid, notify_uuid, on_notify})` in the factory (returns `ICentralLink`) + `register_connect_matcher(prefix, type)` in the BOOT hook (so an unbound device is visible in the scan). The wire format lives in the driver; `modesp_ble` is device-agnostic.
- Everything is under `#if CONFIG_MODESP_BLE_ENABLE && CONFIG_MODESP_BLE_CENTRAL`.

---

## 7. INVARIANTS — DO NOT BREAK

1. **A role is declared only by the owner module** (the one that consumes it). Do not put a role in someone else's module. This applies to ALL peripheral types — even a rich connect driver (panel) declares the role in its own module and is resolved by role, not through a global.
2. **NEVER edit generated files** — `data/ui.json`, all of `generated/*.h` + `generated/*.cmake`, `components/modesp_hal/Kconfig`, `main/Kconfig.boards`, `data/www/i18n/*`. Change the MANIFEST; CMAKE_CONFIGURE_DEPENDS regenerates.
3. **No transport-specific field on a Binding.** A Binding references a device id; the identity (transport identity/adv-name) lives on the device row (board.json/devices.json), not in bindings.json. `find_remote_device` resolves id→identity/name (transport-agnostic).
4. **Decoders/matchers are registered at BOOT** (the register hook `modesp_register_driver_<name>`), NOT in the factory — otherwise an unbound device is invisible in the scan and cannot be subscribed.
5. **Wired → board.json; runtime-BLE → devices.json.** board.json is GET-only; its BLE section is only a factory seed. Do not hardcode BLE in board.json.
6. **Dependency directions:** modules→framework→platform; drivers→hal; ble→net (NOT net→ble); nothing depends on modesp_core in reverse; the framework does not depend on the product.
7. **`binding.module` is routing.** It must name a module from project.json AND be the owner of the role, otherwise the build fails or the hardware is connected to no one.
8. **The driver/module name == the folder == the manifest field** (`^[a-z][a-z0-9_]*$`); for a driver, also == the first arg of the register macro.
9. **Optional components gate only SRCS on CONFIG_*, never REQUIRES.** A driver is always via `modesp_driver_component()`, never a bare `idf_component_register`.
10. **Modules do not touch GPIO** — only `ISensorDriver`/`IActuatorDriver`/`IDisplayPort`/`IAudioSink`/`IPanelPort` via bindings + SharedState/publish. One driver = one register macro. Analog actuators must override `set_value/get_value/supports_analog` (default = discrete on/off).
11. **Cloud is mutually exclusive** (mqtt XOR aws XOR none); a board hardcode in modules is forbidden — hardware is expressed only through board.json/bindings.json. Edit them in `boards/<board>/`, NOT in `data/` (those are copies that get overwritten).
12. **Limits are hard caps:** MAX_BINDINGS=24, MAX_REMOTE_DEVICES=16, MAX_RUNTIME_DEVICES=12, MAX_LOG_CHANNELS=6, menu ≤255 nodes / ≤15 root submenus. An unknown board.json section is silently ignored (warning only) — the hardware disappears.
