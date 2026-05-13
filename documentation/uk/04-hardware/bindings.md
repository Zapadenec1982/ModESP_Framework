# bindings.json — підключення драйверів до ролей

> 📖 **In English:** [documentation/en/04-hardware/bindings.md](../../en/04-hardware/bindings.md)

`bindings.json` — це **deployment-specific підключення** — який driver type
handles який hardware pin, і яку семантичну **role** (ім'я типу `air_temp`,
`compressor`) він provides решті системи. Поки `board.json` каже "у мене є
ці GPIO outputs", `bindings.json` каже "GPIO реле 1 — це компресор,
говоріть з ним через `relay` driver".

Ця сторінка — reference для написання bindings, з реальними прикладами з
dev і KC868-A6 reference boards.

## Де bindings.json сидить у pipeline

```
   board.json              bindings.json            модулі
   ─────────               ─────────────            ──────
   "GPIO 14 існує"   →     "GPIO 14 — компресор   →   read_bool("equipment.compressor")
                            драйвлений relay driver"
```

Три шматки decouple:

1. **Board capabilities** stay constant per hardware revision.
2. **Bindings** vary per deployment (cold room vs. greenhouse vs. brewing
   setup можуть reuse ту саму PCB з різними role assignments).
3. **Модулі** піклуються лише role names (`equipment.air_temp`,
   `equipment.req_compressor`) — вони не знають який GPIO або який driver
   provides дані.

Ви змінюєте bindings без rebuild прошивки — drop new `bindings.json` у
LittleFS через OTA, або через WebUI's bindings editor (planned), і role
mapping updates.

## Розташування файлу

```
boards/<board_name>/bindings.json
```

Обирається разом із `board.json` через Kconfig `CONFIG_MODESP_BOARD=...`.
Обидва файли копіюються у LittleFS при build. Runtime path:
`/data/bindings.json`.

## Top-level shape

```json
{
  "manifest_version": 1,
  "bindings": [
    // ... масив binding об'єктів
  ]
}
```

Це все. Один array. Кожен об'єкт у array — це одне binding — driver
instance attached до одного hardware pin / channel / address.

## Анатомія одного binding

```json
{
  "hardware": "ow_1",        // board.json id (який pin / bus / expander pin)
  "driver": "ds18b20",       // driver type для instantiate
  "role": "air_temp",        // логічне ім'я — з'являється як equipment.<role> у SharedState
  "module": "equipment",     // модуль що consumes це binding (майже завжди "equipment")
  "address": "28:8C:5E:..."  // додаткова driver-specific конфіг (опціонально)
}
```

| Поле | Тип | Обов'язкове | Примітки |
|---|---|---|---|
| `hardware` | string | так | Повинен match `id` declared у `board.json` (будь-яка секція: `gpio_outputs`, `onewire_buses`, `expander_outputs`, тощо). |
| `driver` | string | так | Повинен match ім'я driver-а declared у `drivers/<name>/manifest.json`. |
| `role` | string | так | Семантичне ім'я. Стає `equipment.<role>` (sensors) або `equipment.req_<role>` (actuators) у SharedState. |
| `module` | string | так | Модуль що володіє binding (сьогодні: завжди `"equipment"`). |
| `address` | string | іноді | Required якщо driver manifest декларує `"requires_address": true` (1-Wire ROM, I²C extra addressing). |
| any extra | any | optional | Driver-specific params (offset, calibration, тощо) — passed до `driver->configure()`. |

## Конкретні приклади

### Dev board — мінімальний sensor + relay

`boards/dev/bindings.json`:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "ow_1",    "driver": "ds18b20", "role": "air_temp",   "module": "equipment"},
    {"hardware": "relay_1", "driver": "relay",   "role": "actuator_1", "module": "equipment"}
  ]
}
```

Ця board має один OneWire bus і чотири GPIO relays declared у `board.json`.
Ми bind:
- Перший OneWire bus до `ds18b20` driver, role `air_temp` — температура
  з'являється у `equipment.air_temp`.
- Перше реле до generic `relay` driver, role `actuator_1` — контрольоване
  через `equipment.req_actuator_1` (writes by business modules) і
  reflected у `equipment.actuator_1` (current state).

Зверніть увагу **немає `address`** на OneWire binding: driver auto-discover
єдиний sensor на шині. Якщо було б багато sensors, кожен потребував би
окремого binding з його ROM address.

### KC868-A6 — production refrigeration

`boards/kc868a6/bindings.json`:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "relay_1", "driver": "pcf8574_relay", "role": "compressor",     "module": "equipment"},
    {"hardware": "relay_2", "driver": "pcf8574_relay", "role": "evap_fan",       "module": "equipment"},
    {"hardware": "relay_3", "driver": "pcf8574_relay", "role": "cond_fan",       "module": "equipment"},
    {"hardware": "relay_4", "driver": "pcf8574_relay", "role": "defrost_relay",  "module": "equipment"},
    {"hardware": "ow_1",    "driver": "ds18b20",       "role": "air_temp",       "module": "equipment", "address": "28:8C:5E:45:D4:08:44:09"},
    {"hardware": "ow_1",    "driver": "ds18b20",       "role": "evap_temp",      "module": "equipment", "address": "28:40:0A:45:D4:72:7E:F0"},
    {"hardware": "din_1",   "driver": "pcf8574_input", "role": "door_contact",   "module": "equipment"}
  ]
}
```

Цей deployment ставить commercial refrigeration controller на KC868-A6.
Pins PCF8574 expander-а `relay_1`..`relay_4` стають refrigeration
actuators; `din_1` стає door contact; два DS18B20 sensors діляться тим же
OneWire bus, кожен зі своїм унікальним ROM address.

Зверніть увагу **`address` на OneWire bindings** — multiple sensors на
тій же шині потребують explicit addressing. Discover addresses через
driver's discovery API (`GET /api/drivers/ds18b20/scan`) once після wiring,
потім paste у bindings.

## Багато sensors на одній шині

OneWire — найпоширеніший multi-device case. Кожен sensor має 64-bit ROM
address (надрукований на корпусі sensor-а для деяких manufacturers, але
зазвичай discovered у software). Bindings reference `id` шини (з
`board.json::onewire_buses`) і supply `address` для кожного sensor:

```json
{"hardware": "ow_1", "driver": "ds18b20", "role": "air_temp",  "module": "equipment", "address": "28:8C:5E:45:D4:08:44:09"},
{"hardware": "ow_1", "driver": "ds18b20", "role": "evap_temp", "module": "equipment", "address": "28:40:0A:45:D4:72:7E:F0"},
{"hardware": "ow_1", "driver": "ds18b20", "role": "cond_temp", "module": "equipment", "address": "28:55:1B:35:E1:90:6A:24"}
```

Флаг `multiple_per_bus: true` у driver маніфесті каже фреймворку що
багато bindings можуть target той самий `hardware` ID.

## Sensors vs. actuators у SharedState

Після того як bindings завантажуються, Equipment Manager spawns driver
instances і exposes state keys:

| Binding pattern | SharedState keys згенеровано |
|---|---|
| Sensor (`category: "sensor"`) | `equipment.<role>` (current value, read-only) <br> `equipment.<role>_ok` (health, bool) |
| Actuator (`category: "actuator"`) | `equipment.<role>` (current actual state, read-only) <br> `equipment.req_<role>` (requested state, writable) |

Так write-ити `equipment.req_compressor = true` — це як business module
вмикає компресор. Equipment Manager читає request key, forwards до bound
actuator driver, і reflect-ить actual outcome назад у `equipment.compressor`.

## Address discovery

Для drivers із `discovery.supported: true` у їхньому маніфесті, фреймворк
exposes scanner endpoint:

```bash
# DS18B20 приклад
curl -u admin:modesp http://192.168.1.85/api/drivers/ds18b20/scan
```

Returns array discovered devices з їхніми addresses і current reading
(корисно для identify-ння фізично який sensor куди йде: warm up той що ви
ідентифікуєте рукою і look на rising reading).

```json
[
  {"address": "28:8C:5E:45:D4:08:44:09", "temperature": 22.5, "parasitic": false},
  {"address": "28:40:0A:45:D4:72:7E:F0", "temperature": 22.6, "parasitic": false}
]
```

Copy addresses у ваш `bindings.json`, rebuild, flash.

WebUI's discovery panel (planned, коли bindings editor lands) робить це
автоматично — scan, identify, drag-drop у bindings, save.

## Optional / fallback bindings

Bindings можуть декларувати себе як optional через `optional` поле у
driver manifest's `requires`. Equipment Manager skips missing optional
bindings silently. Required bindings що не resolve-яться abort startup
з log message — тому production deployments не run-яться з
silently-broken hardware.

## Валідація

`generate_ui.py` cross-валідує bindings проти `board.json`:

1. Кожен `hardware` reference resolves до якогось `id` у board.json.
2. Кожен `driver` reference resolves до driver директорії під `drivers/`.
3. `role` імена унікальні у межах модуля (не можна bind два компресори).
4. Drivers із `requires_address: true` реально мають `address` поле.
5. Roles match driver's `provides.type` проти module's `requires` декларацій.

Failures abort build з конкретними повідомленнями.

## Поширені помилки

**Неправильний hardware ID:** typo у `hardware` field — board.json має
`relay_1` але bindings каже `Relay_1`. Build fails з "hardware ID 'Relay_1'
not declared у board.json". Fix: case-sensitive copy.

**Mismatched driver і hardware category:** binding GPIO output до sensor
driver (`ds18b20` expects OneWire bus). Або correct driver, або fix
hardware ID.

**Missing address для multi-device bus:** OneWire з 2+ sensors але bindings
без `address` field — driver reads pick whichever responds first (зазвичай
найнижчий-ROM device). Bug nightmare — readings cross між sensors. Завжди
supply ROM addresses для multi-device buses.

**Duplicate role:** два bindings з тим самим `role` — Equipment Manager
crashes при init. Кожен role повинен бути унікальним у межах модуля.

**Editing bindings без OTA / rebuild:** file живе у LittleFS. Edits на
host що не propagate до flash пристрою не take effect. Re-flash, або use
OTA file replacement, або edit через WebUI bindings editor (planned).

## Workflow для нового deployment

1. **Decide your roles** based на recipe / use case. Приклад для cold
   room: `air_temp`, `evap_temp`, `compressor`, `evap_fan`,
   `defrost_relay`, `door_contact`.
2. **Match до board hardware.** Look на `board.json` щоб побачити що
   доступно — relays, OneWire buses, GPIO inputs.
3. **Choose drivers** per hardware: GPIO relay → `relay`; PCF8574 relay →
   `pcf8574_relay`; DS18B20 sensor → `ds18b20`; NTC sensor → `ntc`.
4. **Write `bindings.json`** з одним binding per role.
5. **Run address discovery** для OneWire / multi-device buses; copy
   addresses у bindings.
6. **Build, flash, monitor.** Verify що кожна role's `equipment.<role>`
   key з'являється у `/api/state` з sane values.
7. **Iterate** якщо щось off — wrong sensor identified, relay polarity
   inverted, тощо.

## Що далі

- **[board-config.md](board-config.md)** — що board.json декларує
  (prerequisite до написання bindings).
- **[modules/equipment.md](../03-framework-reference/modules/equipment.md)**
  *(planned)* — Equipment Manager — consumer of bindings.
- **[writing-a-driver.md](../02-module-author-guide/writing-a-driver.md)**
  — implement driver для нового hardware.
- **[deployment.md](deployment.md)** *(planned)* — повний deployment workflow.
