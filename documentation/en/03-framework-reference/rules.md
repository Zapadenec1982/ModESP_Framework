# Framework rules (living code)

> 📖 **Українською:** [documentation/uk/03-framework-reference/rules.md](../../../uk/03-framework-reference/rules.md)

**The single authoritative source of ModESP rules.** Every rule is numbered, with a **why** (rationale) and a **how to apply**. A violation = broken build, silent bug on hardware, or broken architecture. This complements [project-hierarchy.md](project-hierarchy.md) (hierarchy + peripheral route) — the RULES themselves are pulled out here so they can be checked at a glance.

> Keep it living: when you add a capability — add/update a rule here. `CLAUDE.md` and `project-hierarchy.md` link here.

---

## 0. Founding principles (never violate)

### R0.1 — Role = capability, never a driver
A role declares a `capability` (temperature/relay_out/panel…), NOT a driver. A thermostat needs "temperature" and does not know who provides it (ds18b20 / NTC / BLE channel / future LoRa).
**Why:** the whole abstraction system exists precisely for this — so the source is replaceable. Circumventing it = loss of meaning.
**How:** in the module manifest `{"role":..., "capability":"temperature"}`; on-device resolution via `find_sensor/find_actuator(role)`. Never hardcode a driver in a role/module. See memory `role-equals-capability`.

### R0.2 — Universal and transport-agnostic by default
Never hardcode a driver / transport / channel / count. Design for a multi-transport future (BLE/LoRa/MQTT/ESP-NOW), sensors AND actuators.
**Why:** the use case must stay flexible; every hardcode is a future refactor.
**How:** new field → optional with a fallback; new capability → a line in `capabilities.json`, not a branch in code. See `default-to-universal`.

### R0.3 — Identity lives on the device, never on the role
A `Binding` references the device `id`; identity (MAC/adv-name/topic/devaddr) lives on the device row (board.json/devices.json), NOT in bindings.json. `find_remote_device` resolves id→identity/name.
**Why:** the role is transport-agnostic; a MAC on a role binding ties the role to a transport.

---

## 1. Naming

### R1.1 — Driver/module name == folder == manifest field
Regex `^[a-z][a-z0-9_]*$`. For a driver — also the first argument of the register macro. `modules/heat_pump/` → `"heat_pump"` in project.json → class `HeatPumpModule` → `heat_pump_module.h`.
**Why:** the generator maps folder↔manifest↔registration; a mismatch breaks the build.

### R1.2 — Only the owning module declares a role
The role is placed by THE module that consumes it. Even a rich connect driver (panel) declares its role in its own module and resolves by role, not via a global.

### R1.3 — Role/channel names — no transport
A role label does not mention the transport ("Room temperature", not "BLE room sensor"). A channel label is derived from `capabilities.json`; a driver overrides it only when needed (a different unit, or to distinguish 2+ channels of the same capability).

---

## 2. Zero-heap hot path

### R2.1 — No heap allocations in the hot path
NEVER `std::string`/`std::vector`/`new`/`malloc` in `on_update()`/`on_message()`. ALWAYS `etl::string<N>`/`etl::vector<T,N>`/`etl::variant`/`etl::optional`.
**Why:** heap fragmentation on ESP32 = a crash after days of uptime.

---

## 3. Peripherals: capability match + route

### R3.1 — Role and channel match by capability only
A role accepts a channel ⟺ `capability` equal + direction (in/out) consistent. No driver/hw_type/transport in the predicate. `capabilities.json` is the SSOT of the vocabulary.

### R3.2 — One driver = one register macro
`MODESP_REGISTER_SENSOR/ACTUATOR(name, &factory)` — a single point. Decoders/matchers register at BOOT (register hook), NOT in the factory — otherwise an unbound device is invisible in the scan.

### R3.3 — Modules do not touch GPIO
Only via `ISensorDriver`/`IActuatorDriver`/`IDisplayPort`/`IAudioSink`/`IPanelPort` + bindings + SharedState. Analog actuators override `set_value/get_value/supports_analog` (default = discrete on/off).

### R3.4 — `binding.module` — routing
It must name a module from project.json AND be the owner of the role, otherwise the build fails or the hardware is connected to no one.

### R3.5 — per-driver attributes are gated by the CHOSEN hardware
`requires_address`/`channels`/`scan` are PER-DRIVER. WebUI keys them on the BOUND driver (`addr_drivers`, `channels_by_driver`), not on the role aggregate — otherwise a wired binding demands a BLE address.

---

## 4. Transport genericity

### R4.1 — Device = `RemoteDeviceConfig{id, transport, identity, name}`
`identity` is an opaque blob (BLE MAC; future LoRa devaddr/MQTT topic). `transport` is a separate field, auto-derived from `hardware_type`. Registry in HAL (`remote_devices_`), resolution via `find_remote_device(id)`.

### R4.2 — New transport = new component + bridge driver
Like `modesp_ble`. HAL/generator/webui are **not** touched. HAL does **not** depend on any transport.

### R4.3 — devices.json — runtime-only
Written by the device to `/data/devices.json`, NEVER a build input. Do not leave it in `data/` during a build (it would land in data.bin and overwrite the real subscriptions). Gitignored.

---

## 5. Dependencies and optionality

### R5.1 — Dependency directions
`modules→framework→platform`; `drivers→hal`; `ble→net` (NOT net→ble); nothing depends on `modesp_core` in reverse; framework does not depend on product. Core invariant: `core←hal←services←net←ble`.

### R5.2 — Optional components gate only SRCS on `CONFIG_*`, never REQUIRES
A driver always goes through `modesp_driver_component()`, never a bare `idf_component_register`. A driver disabled in menuconfig is not compiled.

### R5.3 — Cloud is mutually exclusive
mqtt XOR aws XOR none. Board hardcoding in modules is forbidden — hardware only via board.json/bindings.json.

---

## 6. Generated files — DO NOT EDIT

### R6.1 — Never edit generated files
`data/ui.json`, all of `generated/*.h` + `generated/*.cmake`, `components/modesp_hal/Kconfig`, `main/Kconfig.boards`, `data/www/i18n/*`. Change the **MANIFEST** — `CMAKE_CONFIGURE_DEPENDS` regenerates.
**Why:** they are overwritten on build; the edit is lost.

### R6.2 — Wired → board.json; runtime-remote → devices.json
board.json is GET-only on the device; its remote section is only a factory seed. Do not hardcode BLE in board.json. Edit board/bindings in `boards/<board>/`, NOT in `data/` (those are copies).

---

## 7. Limits (hard caps)

### R7.1
`MAX_BINDINGS=24`, `MAX_REMOTE_DEVICES=16`, `MAX_RUNTIME_DEVICES=12`, `MAX_LOG_CHANNELS=6`, menu ≤255 nodes / ≤15 root submenus. An unknown board.json section is silently ignored (warning only) — the hardware disappears, so double-check the names.

---

## 8. Build

### R8.1 — data.bin is always fresh → build via `run_build.ps1`
The Ninja target of the LittleFS image has a phony output → `DEPENDS`/`ninja -t clean` do NOT force a rebuild; a changed `data/` ships stale. After the build `run_build.ps1` reruns the littlefs command → the image = the current `data/`. A manual `Remove-Item build\data.bin` is not needed.

### R8.2 — Editing a manifest re-triggers generation by itself
`CMAKE_CONFIGURE_DEPENDS` covers manifest/project.json/schemas — a manual `idf.py reconfigure` is not needed. After `fullclean`, if needed, `Remove-Item build\esp-idf\marcel-cd__etlcpp` (ETL clang-fix).

### R8.3 — Build-time validation
`generate_ui.py` checks bindings↔board↔driver + capability↔vocabulary. An invalid binding/unknown capability breaks the build. A board that binds a disabled driver → FATAL; reconcile with `tools/drivers_sync.py --fix/--prune`.

---

## 9. Documentation (this is a rule too)

### R9.1 — Every driver and module MUST have a doc
`documentation/{uk,en}/03-framework-reference/{drivers,modules}/<name>.md`. A host lint fails the build if `drivers/*/` or `modules/*/` lacks a doc. The doc header is derived from the manifest (capability/channels/state) — auto-checked against drift.

### R9.2 — uk is authoritative, en is the mirror
You write in Ukrainian; English mirrors. Style — [STYLE.md](../../STYLE.md) / [docs-style](../06-contributing/docs-style.md).

---

## Sources (where this was consolidated from)
- [project-hierarchy.md](project-hierarchy.md) — INVARIANTS + peripheral route.
- [capability-roadmap.md](capability-roadmap.md) — the fixed decisions of the capability model.
- `CLAUDE.md` — critical build rules (links here).
- Memory: `role-equals-capability`, `default-to-universal`, `ble-device-registry-model`, `capability-roadmap-status`.
