# Persistence

> 📖 **In English:** [documentation/en/02-module-author-guide/persistence.md](../../en/02-module-author-guide/persistence.md)

Деякий state переживає reboots — user setpoints, calibration константи,
mode selections. Інший state ephemeral — current readings, computed flags,
counters. ModESP робить розрізнення декларативним: додайте `"persist": true`
до вашої manifest's state key, і PersistService автоматично saves його у
NVS і restore-ить при boot. Без custom NVS коду від вас.

Ця сторінка покриває що persist-иться, як throttling працює, які limits
існують, і коли bypass-ити систему для raw NVS access.

## Декларативна persistence

Додайте flag у manifest's state declaration:

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

Це все. Після boot:
- Якщо value був persisted previously: state key initialised до того value.
- Якщо ніколи не persisted (fresh install або factory reset): state key
  використовує `default` поле (або unset якщо absent).

Після зміни:
- `state_set("simple_thermo.setpoint", 24.5)` триггерить callback PersistService.
- Після 5-second debounce, value пишеться у NVS.
- Survive-ить reboot, OTA, brownout (NVS використовує redundant flash sectors).

## Як це працює

```
   Module: state_set("key", value)
                  │
                  ▼
   SharedState updates internal map, marks change
                  │
                  ▼ (callback)
                  │
   PersistService schedules deferred write
                  │
                  ▼ (через DEBOUNCE_MS = 5000)
                  │
   nvs_helper::write_<type>("persist", "key", value)
                  │
                  ▼
   Survive-ить через boots
```

При boot, `PersistService::on_init` iterates known persisted keys
(declared у `state_meta.h` з `persist` flag) і restore-ить кожен перед
запуском будь-якого module's `on_init`. Ваш модуль бачить pre-restored
values з першого read.

## Debounce timing

`PersistService::DEBOUNCE_MS = 5000` (5 секунд). Коли ви set-ите key:

1. Timer стартує на 5 с.
2. Будь-який further set до того ж key ПЕРЕД 5 с resets timer.
3. Раз 5 с "no change" passes, value пишеться у NVS.

Це захищає flash. NVS sectors rated ~100,000 erase cycles. Writing setpoint
поки user драгає slider міг би rack up thousands of writes per minute без
debounce.

Trade-off: power loss у межах 5 секунд після зміни втрачає зміну. Tolerable
для setpoints і configuration. Не OK для crash-critical state — для цього
використовуйте scenario engine NVS observer (Phase 2 engine rebuild) або
explicit NVS calls.

## Що зберігається under the hood

NVS namespace: `"persist"`. Keys mirror SharedState key names verbatim
(`simple_thermo.setpoint`, `equipment.ntc_beta`, тощо). Values зберігаються
у NVS-native typed slot:

| StateValue variant | NVS API |
|---|---|
| `int32_t` | `nvs_set_i32` / `nvs_get_i32` |
| `float` | Encoded as bytes (NVS не має native float) |
| `bool` | `nvs_set_u8` / `nvs_get_u8` (0 / 1) |
| `string` | `nvs_set_str` / `nvs_get_str` |

Вам не треба знати це — `nvs_helper` (у `modesp_services`) wraps це.
Просто declare `persist: true` і працює.

## Читання persisted value

Без спеціального API. Regular `read_float` / `read_int` / `read_bool` /
`state_get` calls вашого модуля повертають persisted value, бо PersistService
вже restore-нув його у SharedState перед запуском вашого `on_init`.

```cpp
bool MyModule::on_init() {
    // Returns persisted setpoint АБО 22.0 default якщо first boot.
    float setpoint = read_float("simple_thermo.setpoint", 22.0f);
    ESP_LOGI(TAG, "Loaded setpoint: %.1f°C", setpoint);
    return true;
}
```

## Запис persisted value

Без спеціального API. Regular `state_set`. PersistService observe-ить і
schedule-ить write.

```cpp
state_set("simple_thermo.setpoint", new_setpoint);
// 5 секунд пізніше, NVS updated.
```

External writes — HTTP API `POST /api/settings`, MQTT commands — йдуть
через той самий path. WebUI slider пише setpoint → SharedState →
PersistService → NVS. Усі routes consistent.

## Коли use persistence

**Good candidates:**
- User setpoints (temperature, pressure, time).
- Calibration константи (sensor offsets, B-coefficients).
- Mode selections (heating/cooling, profile name).
- Network credentials (WiFi password, MQTT broker host).
- Counter / aggregate values що повинні survive (total runtime hours,
  defrost cycle count — але keep an eye на debounce vs. update rate).

**Bad candidates:**
- Current readings (`temperature` від sensor) — змінюються занадто часто.
- Computed flags (`heating_active`) — derived з іншого state.
- Counters incrementing per tick — debounce не keep up.
- Будь-що > 32 chars (SharedState string limit). Use raw NVS для blobs.
- Будь-що що змінюється швидше ніж debounce window — fast-changing values
  або skip writes (data loss при power off) або thrash NVS.

## Що не persist-иться

- State keys без `persist: true`. Default — non-persistent.
- Keys removed з маніфестів (після rebuild). NVS still has old value
  але SharedState не declare it; key ніколи не appears.
- State values written через `state_set("key", val, /*track_change=*/false)`.
  Silent updates bypass change tracker, і PersistService callback fires
  off change tracker. Use це consciously для NEVER persist (counters).

## NVS partition і capacity

NVS partition declared у `partitions.csv`:

```
nvs,    data, nvs,     0x9000,   0x6000,
```

24 KB total. Hosts:
- Settings (`"persist"` namespace) — що ця сторінка покриває.
- WiFi credentials (`"nvs.net80211"` — ESP-IDF managed).
- OTA state (`"otadata"` partition, separate).
- Інші helpers (`"auth"`, `"time"`, `"seqstate"`, тощо).

Typical usage: ~5-10 KB після boot. Generous headroom для ~100 persisted
keys. Якщо hit limits, NVS write returns ESP_ERR_NVS_NOT_ENOUGH_SPACE —
PersistService логує warning і continues serving existing keys.

## Factory reset / clearing persistence

User-driven через WebUI **System → Factory Reset** або HTTP:

```bash
curl -u admin:modesp -X POST http://192.168.1.85/api/factory-reset
```

Це erase-ить entire NVS partition і reboots. Після restart усі persisted
keys revert до `default` values. Network credentials і auth потребують
re-configuration.

Developer-driven через monitor:

```
idf.py erase-flash flash monitor
```

Wipes все, re-flashes прошивку і LittleFS, fresh boot.

## Versioning і migrations

Фреймворк не ships formal NVS schema migration system. Якщо ви rename
state key АБО зміните його type:

1. Old NVS entry лишається під old name — wasted space але не harmful.
2. New name reads default (без migration).

Якщо migration matters (user setpoints carry value forward через firmware
updates), додайте migration код у `on_init` вашого модуля:

```cpp
bool MyModule::on_init() {
    // Migration: rename old "thermo.target" до new "simple_thermo.setpoint"
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

Stage 1.5 може formalize migrations як manifest section. Зараз ad-hoc.

## Bypassing PersistService

Іноді вам потрібен raw NVS — для blobs більших ніж 32 chars, schemas що
не fit variant, або critical state що не може tolerate debounce.

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

Use namespace ІНШИЙ ніж `"persist"` щоб уникнути collision з auto-managed
keys.

Use cases у самому фреймворку:
- `seqstate` (scenario engine recovery tokens) — own namespace, byte
  schema, bypass-debounced бо power loss recovery matters.
- `time` (NTP timezone string).
- `auth` (admin credentials).

Для business modules це рідко. Default path (`persist: true`) covers 95%
needs.

## Concurrent writes і atomicity

PersistService runs у тому ж task що ваш модуль. Sequential writes atomic
на SharedState level (mutex). NVS writes happen у PersistService's debounce
callback path — також sequential.

`nvs_commit` викликається після кожного batch of writes. Якщо power lost
між `nvs_write` і `nvs_commit`, write rolls back автоматично — journaled
write protocol NVS prevents corruption.

## Поширені помилки

**`persist: true` на fast-changing keys:** sensor readings update at 100
Hz. Навіть з 5 с debounce, ви б писали у NVS кожен раз як sensor settles
до нового value — можливо hundreds per hour. Flash wear, slow boot. Persist
лише user-controlled values.

**Counters як persisted:** "total runtime hours" — звучить розумно, але
counter writing every second produces NVS thrashing. Update лише при
shutdown / state change (`equipment.fault` rising edge), або bypass default
debounce з explicit NVS writes.

**Expecting persistence без declaration:** writing new state key через
`state_set` НЕ persist його unless ви declared `persist: true` у маніфесті.
Generator's `state_meta.h` будує persisted-keys list at compile time.

**Storing too much data:** SharedState string values — 32 chars max.
Не try store JSON blobs у variant. Use raw NVS (або LittleFS files) для
larger payloads.

**Default value mismatching constraint:** якщо `min/max` constraints say
5-40 але `default` — 0, ви будете boot-итись з invalid state value.
PersistService restore-ить raw без re-checking constraints. Sanity-check
ваші defaults.

## Що далі

- **[manifest.md](manifest.md#per-key-fields)** — `persist: true` flag і
  `default` field reference.
- **[shared-state.md](shared-state.md)** — read/write API що integrates
  з persistence transparently.
- **[components/modesp_services.md](../03-framework-reference/components/modesp_services.md)**
  *(planned)* — PersistService internals + `nvs_helper` reference.
- **[scenario-engine/07_persistence.md](../03-framework-reference/scenario-engine/07_persistence.md)**
  — scenario engine NVS token persistence (different від general module
  persistence на цій сторінці).

## Source

- [`components/modesp_services/src/persist_service.cpp`](../../../components/modesp_services/src/persist_service.cpp)
  — implementation.
- [`components/modesp_services/include/modesp/services/persist_service.h`](../../../components/modesp_services/include/modesp/services/persist_service.h)
  — `DEBOUNCE_MS` і API surface.
- [`components/modesp_services/include/modesp/services/nvs_helper.h`](../../../components/modesp_services/include/modesp/services/nvs_helper.h)
  — raw NVS API для коли вам треба bypass.
