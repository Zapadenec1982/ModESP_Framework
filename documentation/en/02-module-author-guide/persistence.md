# Persistence

> 📖 **Українською:** [documentation/uk/02-module-author-guide/persistence.md](../../uk/02-module-author-guide/persistence.md)

Some state survives reboots — user setpoints, calibration constants, mode
selections. Other state is ephemeral — current readings, computed flags,
counters. ModESP makes the distinction declarative: add `"persist": true`
to your manifest's state key, and PersistService automatically saves it
to NVS і restores it on boot. No custom NVS code from you.

This page covers what gets persisted, how the throttling works, what
limits exist, і when to bypass the system for raw NVS access.

## Declarative persistence

Add the flag у your manifest's state declaration:

```json
"simple_thermo.setpoint": {
  "type": "float",
  "access": "readwrite",
  "default": 22.0,
  "min": 5, "max": 40,
  "persist": true,                  // ← це one line
  "description": "Temperature setpoint"
}
```

That's all. After boot:
- If а value was persisted previously: state key initialised to що value.
- If never persisted (fresh install або factory reset): state key uses
  the `default` field (or unset якщо absent).

After change:
- `state_set("simple_thermo.setpoint", 24.5)` triggers PersistService's
  callback.
- After а 5-second debounce, the value writes to NVS.
- Survives reboot, OTA, brownout (NVS uses redundant flash sectors).

## How it works

```
   Module: state_set("key", value)
                  │
                  ▼
   SharedState updates internal map, marks change
                  │
                  ▼ (callback)
                  │
   PersistService schedules а deferred write
                  │
                  ▼ (after DEBOUNCE_MS = 5000)
                  │
   nvs_helper::write_<type>("persist", "key", value)
                  │
                  ▼
   Survives across boots
```

On boot, `PersistService::on_init` iterates known persisted keys (declared
у `state_meta.h` із the `persist` flag) і restores each before any module's
`on_init` runs. Your module sees pre-restored values from its first read.

## Debounce timing

`PersistService::DEBOUNCE_MS = 5000` (5 seconds). When you set а key:

1. Timer starts at 5 s.
2. Any further set to the same key BEFORE 5 s resets the timer.
3. Once 5 s of "no change" passes, value writes to NVS.

This protects flash. NVS sectors are rated ~100,000 erase cycles. Writing
а setpoint while the user drags а slider could rack up thousands of writes
per minute without debounce.

Trade-off: power loss within 5 seconds of а change loses the change. Tolerable
for setpoints і configuration. Not OK for crash-critical state — for that,
use а scenario engine NVS observer (Phase 2 of engine rebuild) or explicit
NVS calls.

## What's stored under the hood

NVS namespace: `"persist"`. Keys mirror SharedState key names verbatim
(`simple_thermo.setpoint`, `equipment.ntc_beta`, etc.). Values stored у
the NVS-native typed slot:

| StateValue variant | NVS API |
|---|---|
| `int32_t` | `nvs_set_i32` / `nvs_get_i32` |
| `float` | Encoded as bytes (NVS has no native float) |
| `bool` | `nvs_set_u8` / `nvs_get_u8` (0 / 1) |
| `string` | `nvs_set_str` / `nvs_get_str` |

You don't need to know це — `nvs_helper` (у `modesp_services`) wraps it.
Just declare `persist: true` і it works.

## Reading а persisted value

No special API. Your module's regular `read_float` / `read_int` /
`read_bool` / `state_get` calls return the persisted value, because
PersistService has already restored it to SharedState before your `on_init`
runs.

```cpp
bool MyModule::on_init() {
    // Returns persisted setpoint OR 22.0 default if first boot.
    float setpoint = read_float("simple_thermo.setpoint", 22.0f);
    ESP_LOGI(TAG, "Loaded setpoint: %.1f°C", setpoint);
    return true;
}
```

## Writing а persisted value

No special API. Regular `state_set`. PersistService observes і schedules
the write.

```cpp
state_set("simple_thermo.setpoint", new_setpoint);
// 5 seconds later, NVS updated.
```

External writes — HTTP API `POST /api/settings`, MQTT commands — go through
the same path. WebUI slider writes setpoint → SharedState → PersistService
→ NVS. All routes consistent.

## When to use persistence

**Good candidates:**
- User setpoints (temperature, pressure, time).
- Calibration constants (sensor offsets, B-coefficients).
- Mode selections (heating/cooling, profile name).
- Network credentials (WiFi password, MQTT broker host).
- Counter / aggregate values що should survive (total runtime hours,
  defrost cycle count — но keep an eye on debounce vs. update rate).

**Bad candidates:**
- Current readings (`temperature` from а sensor) — change too often.
- Computed flags (`heating_active`) — derived from other state.
- Counters incrementing per tick — debounce can't keep up.
- Anything > 32 chars (SharedState string limit). Use raw NVS для blobs.
- Anything що changes faster than the debounce window — fast-changing
  values either skip writes (data loss on power off) або thrash NVS.

## What doesn't persist

- State keys without `persist: true`. Default is non-persistent.
- Keys removed from manifests (after а rebuild). NVS still has the old
  value але SharedState doesn't declare it; the key never appears.
- State values written via `state_set("key", val, /*track_change=*/false)`.
  Silent updates bypass the change tracker, и PersistService callback fires
  off the change tracker. Use this consciously для NEVER persist (counters).

## NVS partition і capacity

NVS partition declared у `partitions.csv`:

```
nvs,    data, nvs,     0x9000,   0x6000,
```

24 KB total. Hosts:
- Settings (`"persist"` namespace) — what це page covers.
- WiFi credentials (`"nvs.net80211"` — ESP-IDF managed).
- OTA state (`"otadata"` partition, separate).
- Other helpers (`"auth"`, `"time"`, `"seqstate"`, etc.).

Typical usage: ~5-10 KB після boot. Generous headroom for ~100 persisted
keys. If you hit limits, NVS write returns ESP_ERR_NVS_NOT_ENOUGH_SPACE —
PersistService logs а warning і continues serving existing keys.

## Factory reset / clearing persistence

User-driven via WebUI **System → Factory Reset** або HTTP:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/factory-reset
```

This erases the entire NVS partition і reboots. After restart all persisted
keys revert до their `default` values. Network credentials і auth need
re-configuration.

Developer-driven via monitor:

```
idf.py erase-flash flash monitor
```

Wipes everything, re-flashes firmware і LittleFS, fresh boot.

## Versioning і migrations

The framework doesn't ship а formal NVS schema migration system. If you
rename а state key OR change its type:

1. Old NVS entry remains under the old name — wasted space але not harmful.
2. New name reads default (no migration).

If migration matters (user setpoints carry value forward across firmware
updates), add migration code до your module's `on_init`:

```cpp
bool MyModule::on_init() {
    // Migration: rename old "thermo.target" to new "simple_thermo.setpoint"
    float old_value = 0;
    if (nvs_helper::read_float("persist", "thermo.target", old_value)) {
        // Got value from old key.
        state_set("simple_thermo.setpoint", old_value);
        nvs_helper::erase_key("persist", "thermo.target");
        ESP_LOGI(TAG, "Migrated setpoint з old key");
    }
    return true;
}
```

Stage 1.5 may formalize migrations as а manifest section. For now ad-hoc.

## Bypassing PersistService

Sometimes you need raw NVS — для blobs larger than 32 chars, schemas що
don't fit а variant, or critical state що can't tolerate debounce.

Use `nvs_helper` directly:

```cpp
#include "modesp/services/nvs_helper.h"

// Write
nvs_helper::write_blob("my_namespace", "key", data, len);

// Read
size_t len = 0;
if (nvs_helper::read_blob("my_namespace", "key", buf, sizeof(buf), len)) {
    // use buf[0..len]
}
```

Use а namespace OTHER than `"persist"` to avoid collision із the auto-managed
keys.

Use cases у the framework itself:
- `seqstate` (scenario engine recovery tokens) — own namespace, byte
  schema, bypass-debounced because power loss recovery matters.
- `time` (NTP timezone string).
- `auth` (admin credentials).

For business modules це rare. Default path (`persist: true`) covers 95%
of needs.

## Concurrent writes і atomicity

PersistService runs у the same task as your module. Sequential writes are
atomic at the SharedState level (mutex). NVS writes happen у the
PersistService's debounce callback path — also sequential.

`nvs_commit` is called after each batch of writes. If power is lost between
`nvs_write` and `nvs_commit`, the write rolls back automatically — NVS's
journaled write protocol prevents corruption.

## Common mistakes

**`persist: true` on fast-changing keys:** sensor readings update at 100 Hz.
Even із 5 s debounce, you'd write to NVS each time the sensor settles
to а new value — possibly hundreds per hour. Flash wear, slow boot. Only
persist user-controlled values.

**Counters як persisted:** "total runtime hours" — sounds reasonable, але
а counter writing every second produces NVS thrashing. Update only on
shutdown / state change (`equipment.fault` rising edge), або bypass the
default debounce із explicit NVS writes.

**Expecting persistence без declaration:** writing а new state key із
`state_set` does NOT persist it unless you declared `persist: true` у the
manifest. The generator's `state_meta.h` builds the persisted-keys list at
compile time.

**Storing too much data:** SharedState string values are 32 chars max.
Don't try to store JSON blobs у the variant. Use raw NVS (or LittleFS
files) for larger payloads.

**Default value mismatching constraint:** if `min/max` constraints say
5-40 але `default` is 0, you'll boot із an invalid state value. PersistService
restores raw without re-checking constraints. Sanity-check your defaults.

## Next steps

- **[manifest.md](manifest.md#per-key-fields)** — `persist: true` flag і
  `default` field reference.
- **[shared-state.md](shared-state.md)** — read/write API that integrates
  з persistence transparently.
- **[components/modesp_services.md](../03-framework-reference/components/modesp_services.md)**
  *(planned)* — PersistService internals + `nvs_helper` reference.
- **[scenario-engine/07_persistence.md](../03-framework-reference/scenario-engine/07_persistence.md)**
  — scenario engine NVS token persistence (different from this page's
  general module persistence).

## Source

- [`components/modesp_services/src/persist_service.cpp`](../../../components/modesp_services/src/persist_service.cpp)
  — implementation.
- [`components/modesp_services/include/modesp/services/persist_service.h`](../../../components/modesp_services/include/modesp/services/persist_service.h)
  — `DEBOUNCE_MS` і API surface.
- [`components/modesp_services/include/modesp/services/nvs_helper.h`](../../../components/modesp_services/include/modesp/services/nvs_helper.h)
  — raw NVS API для when you need to bypass.
