# Roadmap: capability-typed, transport-generic peripherals

> 📖 **In Ukrainian:** [../../uk/03-framework-reference/capability-roadmap.md](../../uk/03-framework-reference/capability-roadmap.md)

**Status:** approved (2026-07). Goal — take to production the model where a **role = capability, never a driver**, for **sensors AND actuators**, transport-generically (wired + BLE now; LoRa/MQTT/ESP-NOW next). The design came out of a design pass (grounded); the judge picked an incremental architecture + grafts.

## Principle
`capability` is a **build-time concept**. A role = an instance of a capability; on-device it resolves via `find_sensor/find_actuator(role)`, and `Binding{hardware,driver,role,address}` is already transport-agnostic. Therefore **the HAL never learns the word "capability", and phases P1–P4 = 0 firmware changes.** The genericity lives in the manifests + `generate_ui.py` + webui. See memory `role-equals-capability`, `default-to-universal`, `ble-device-registry-model`.

## Model
- **`tools/capabilities.json`** — the dictionary SSOT: `{name, kind:sensor|actuator, direction:in|out, value_type, unit?}`.
- A **role** declares a `capability` (not a driver). Several roles can share a capability (`air_temp` chamber, `room_temp` room — both `temperature`, accept any source).
- A **driver**: `provides.capability` (single-channel) or `provides.channels[{channel,capability}]` (multi-channel, recommended); `address_channels[].capability` — the back-compat path. One device → channels of different capabilities → different roles.
- **`transport`** — a separate driver field (`wired|ble|lora|mqtt|espnow`), auto-derived from `hardware_type`. The device registry: `{id, transport, identity, name}` — `identity` an opaque blob (MAC/topic/devaddr); **roles never see it** (identity never on a role binding).
- **Match:** a role accepts a channel ⟺ `capability` equal + direction consistent. No driver/hw_type/transport in the predicate.
- **New transport = new component + bridge driver** (like `modesp_ble`). HAL/generator/webui are untouched. **The HAL depends on no transport** (invariant `core←hal←…←ble`).

## Phases
| ID | Content | C++? | Risk |
|----|---------|------|------|
| **P0** | `capabilities.json` + load/validate/derive in the generator + `--report-capabilities` observe-only + golden byte-identical | no | very low |
| **P1** | optional `capability`/`transport` fields in the driver/module schemas + drift-guard + fallback (everything builds identically) | no | very low |
| **P2** | 13 drivers get `capability`+`transport` (auto-migration + confirmation of ambiguous ones); generator builds the eligible-by-capability index | no | low |
| **P3** | capability match in the generator (`cross_validate` + `_bindings_page`) + module manifest migration (roles drop their driver lists) | no | medium |
| **P4** | webui: a flat **Sensors/Actuators** list, `compatibleHw` by capability, `DevicesPage` by `transports[]` | no | low–medium |
| **P5** | **single firmware phase**: `BleDeviceConfig→RemoteDeviceConfig{id,transport,identity,name}`, `ble_devices→remote_devices`, `MAX_BLE_DEVICES→MAX_REMOTE_DEVICES` (=16), aliases, byte-preserving, review-before-commit. `modesp_ble` is NOT touched | yes (rename) | medium |
| P6 | (deferred) take a 2nd transport end-to-end | — | — |

## Locked-in decisions
- The capabilities dictionary is **OPEN** (the author adds rows, the generator validates; a typo is caught by an eligible-set warning, not a schema-reject).
- Aliases (`type↔kind`, `mac↔identity`, `ble_devices↔remote_devices`) — **forever** (no flag-day).
- A role naming a driver alongside a capability → **WARN** (a nudge not to over-constrain).
- HAL facet `capability_of()/channel_capability()` + `DiscoveredDevice.transport[12]` — **laid in** (small footprint; the `_CAP` register macros degrade to the plain ones → hand-written .cpp does not change).
- `MAX_REMOTE_DEVICES=16` across ALL transports (we'll raise it if needed). `identity` width **40** (refactor later).

## Migration (no flag-day)
All new fields are **optional**; until a driver/role declares a capability, the generator falls back to the current `category==type`. `tools/migrate_capabilities.py` derives a capability from existing signals (unit °C→temperature, category actuator+gpio→relay_out, address_channels temperature/humidity/battery, ld2410b presence/distance/energy, nRF angle/accel, digital_input→binary_in). `board.json ble_devices` is read as `remote_devices` (alias; a row without transport→"ble", without identity→mac). `bindings.json` — NO changes. The old `/data/devices.json` on devices is parsed after OTA.

## Status (2026-07)
P0–P5 **done and in `main`**. P4.5 (UX: role without transport in the name, per-driver `channels_by_driver`, auto-binding a single channel, a friendly device name) — also in `main`. P6 (2nd transport) deferred. Details — memory `capability-roadmap-status`.

## Deferred architectural directions (bottom-up — after P5)
Two questions of the same root: the model is currently **top-down** (a peripheral appears in the system only when some module EXPLICITLY demands it via a role), whereas the target universal vision is **bottom-up** (a device gave N channels → all N are visible; roles are optional).

### 1. Channel visibility
**Symptom:** a device broadcasts several channels (Xiaomi: temperature/humidity/**battery**; nRF: angle/**tilted**/**battery**/**axes**), but only the ones a role exists for are visible (`room_temp`=temperature, `orientation`=angle). Humidity/battery/axes are invisible because nobody consumes them. `EquipmentBase` publishes **one state per ROLE** (`equipment.<role>`), so `equipment.room_humid` (which the panel reads) nobody publishes — the role `room_humid` **never existed** (not a regression, an old hole).

**Direction (B chosen):** every subscribed device channel is **auto-registered as a sensor** in SharedState (e.g. `sensor.<device>.<channel>`), independently of module roles. Roles remain for **logic** (a thermostat needs a temperature — that's a role); but raw visibility/logging/MQTT/panel do NOT require a role. A framework change (SharedState publication of device-registry channels + generator/webui showing them). The temporary crutch A (add `room_humid`/`room_batt` roles to the module) is **rejected** as per-channel hardcode against the philosophy.

### 2. Merging `display` + `panel`
Both "output information"; splitting into two capabilities is over-specialization. The goal is one `display` capability with facets `as_menu()`/`as_text()` (following the existing `IDisplayPort::as_*()` pattern). Full brief — spawn_task chip + memory `display-panel-unify-todo`.

Both are real multi-component changes: build + host tests + adversarial review + review-before-commit.
