# Написання service модуля

> 📖 **In English:** [documentation/en/02-module-author-guide/writing-a-module.md](../../en/02-module-author-guide/writing-a-module.md)

Ця сторінка — повний walkthrough побудови C++ service модуля — класу що
derive-иться від `modesp::BaseModule`, реєструється з build, і запускає
business логіку на 100 Hz update loop. Прочитавши, ви зможете створити нову
module folder, написати manifest + C++, побачити як він boot-ається, і
взаємодіяти з його state через WebUI і HTTP API.

Recipe модулі (лише manifest, без C++) описані у
[recipe-authoring.md](recipe-authoring.md) *(planned)*. Драйвери у
[writing-a-driver.md](writing-a-driver.md) *(planned)*.

## Що таке модуль

Service module — це `modesp::BaseModule` subclass що:

- Живе у `modules/<name>/` зі своїм `CMakeLists.txt` і `manifest.json`.
- Конструюється при static-storage init (без heap, без `new`).
- Отримує три lifecycle hooks драйвлені `ModuleManager`: `on_init()` раз,
  `on_update(dt_ms)` кожні 10 мс, `on_stop()` при shutdown.
- Опціонально отримує messages через `on_message(const etl::imessage&)`.
- Читає і пише state через `SharedState` helpers.

Авто-генерація фреймворку робить boilerplate невидимим: маніфест декларує
що робить модуль; згенеровані `module_includes.h` / `module_instances.h` /
`module_register.h` headers wire-ять instance у `main.cpp` без manual edits.

## Структура папки

```
modules/your_module/
├── manifest.json          ← ОБОВ'ЯЗКОВО — контракт маніфесту
├── CMakeLists.txt         ← ОБОВ'ЯЗКОВО — один рядок на файл
├── include/
│   └── your_module.h      ← Декларація module класу
└── src/
    └── your_module.cpp    ← Implementation
```

> ℹ️ **Note:** імена header і source повинні match C++ class name конвенції
> що використовує генератор. Використовуйте `<name>_module.h` /
> `<name>_module.cpp` патерни з existing модулів (наприклад
> `simple_thermo_module.cpp`).

## Крок 1 — Написати маніфест

Покрийте базу: top-level поля, state keys, опціонально `ui` і `mqtt`. Повний
reference у [manifest.md](manifest.md).

Мінімальний приклад (`modules/my_counter/manifest.json`):

```json
{
  "manifest_version": 1,
  "module": "my_counter",
  "version": "0.1.0",
  "description": "Рахує секунди з моменту boot — proof-of-life demo",
  "priority": 2,

  "state": {
    "my_counter.seconds": {
      "type": "int",
      "access": "read",
      "description": "Секунди з моменту init модуля"
    }
  },

  "ui": {
    "page": "Counter",
    "icon": "clock",
    "cards": [{
      "title": "Uptime counter",
      "widgets": [
        {"key": "my_counter.seconds", "widget": "value"}
      ]
    }]
  }
}
```

Build підхоплює це автоматично через `project.json` (наступний крок).

## Крок 2 — Зареєструвати у project.json

Додайте ім'я модуля до project's module list:

```json
// project.json (root)
{
  "modules": ["equipment", "datalogger", "simple_thermo", "my_counter"]
}
```

Генератор обробляє маніфести лише для модулів перерахованих тут. Це
дозволяє тримати багато модулів у репо і обирати які shipping-уються у
конкретному firmware build.

## Крок 3 — Написати CMakeLists.txt

```cmake
# modules/my_counter/CMakeLists.txt
idf_component_register(
    SRCS "src/my_counter_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
)
```

`REQUIRES modesp_core` дає доступ до `BaseModule`, `SharedState`,
`ModulePriority`. Додайте інші компоненти (`modesp_services`, `modesp_hal`,
тощо) якщо ваш модуль їх потребує.

## Крок 4 — Написати C++ клас

Header:

```cpp
// modules/my_counter/include/my_counter_module.h
#pragma once
#include "modesp/base_module.h"

class MyCounterModule : public modesp::BaseModule {
public:
    MyCounterModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    uint32_t elapsed_ms_ = 0;
    int32_t  seconds_ = 0;
};
```

Source:

```cpp
// modules/my_counter/src/my_counter_module.cpp
#include "my_counter_module.h"
#include "esp_log.h"

static const char* TAG = "MyCounter";

MyCounterModule::MyCounterModule()
    : BaseModule("my_counter", modesp::ModulePriority::NORMAL)
{}

bool MyCounterModule::on_init() {
    state_set("my_counter.seconds", static_cast<int32_t>(0));
    ESP_LOGI(TAG, "Counter started");
    return true;
}

void MyCounterModule::on_update(uint32_t dt_ms) {
    elapsed_ms_ += dt_ms;
    while (elapsed_ms_ >= 1000) {
        elapsed_ms_ -= 1000;
        seconds_++;
        state_set("my_counter.seconds", seconds_);
    }
}
```

Це весь модуль. Конструктор передає ім'я і priority до `BaseModule`;
lifecycle hooks роблять роботу; `state_set` пише через SharedState.

## Крок 5 — Build і flash

```bash
idf.py build
idf.py -p COM15 flash monitor
```

Маєте побачити у boot log:

```
I (12345) ModuleManager: Registering my_counter (priority=NORMAL)
I (12350) MyCounter: Counter started
```

У WebUI перейдіть на сторінку **Counter** (auto-generated з вашої `ui`
секції). Widget "seconds" оновлюється раз на секунду.

## BaseModule API reference

### Конструктор

```cpp
BaseModule(const char* name, modesp::ModulePriority priority);
```

`name` повинен match поле `"module"` у `manifest.json`. `priority` обирає
init phase:

| Priority | Значення | Phase | Використання |
|---|---|---|---|
| `CRITICAL` | 0 | 1 (перший) | Error service, watchdog. |
| `HIGH` | 1 | 2 | WiFi, HAL, drivers, scenario engine. |
| `NORMAL` | 2 | 2 | Business logic (default). |
| `LOW` | 3 | 3 (останній) | HTTP, WebSocket, datalogger. |

### Lifecycle hooks

Всі повертають default якщо не overridden.

| Hook | Сигнатура | Викликається | Примітки |
|---|---|---|---|
| `on_init` | `virtual bool on_init()` | Один раз при startup | Поверніть `false` щоб abort registration. |
| `on_update` | `virtual void on_update(uint32_t dt_ms)` | Кожні 10 мс | Hot path — повинен бути non-blocking, типово < 1 мс. |
| `on_message` | `virtual void on_message(const etl::imessage& msg)` | Коли message addressed до цього модуля dispatch-ається | Використовуйте sparingly — більшість communication через SharedState. |
| `on_stop` | `virtual void on_stop()` | Один раз при shutdown | Звільнити non-trivial resources, flush queues. |

### State access

```cpp
// Write — typed overloads, всі delegate до SharedState::set.
bool state_set(const char* key, int32_t value, bool track_change = true);
bool state_set(const char* key, float value, bool track_change = true);
bool state_set(const char* key, bool value, bool track_change = true);
bool state_set(const char* key, const char* value, bool track_change = true);

// Read — typed convenience з default fallback.
float   read_float(const char* key, float def = 0.0f) const;
int32_t read_int(const char* key, int32_t def = 0) const;
bool    read_bool(const char* key, bool def = false) const;

// Generic — повертає std::optional з variant. Use коли тип невідомий або polymorphic.
etl::optional<modesp::StateValue> state_get(const char* key) const;
```

**Флаг `track_change`:** default `true` тригерить WebSocket delta-broadcast.
Поставте `false` для silent updates (counters, fast-changing values що
спамлять WS).

Повна SharedState семантика: [shared-state.md](shared-state.md) *(planned)*.

## Three-phase init ordering — що actually запускається коли

`ModuleManager::init_all` викликається тричі у `main.cpp`:

```cpp
// Phase 1 — CRITICAL priority модулі
ESP_LOGI(TAG, "Phase 1: Initializing system services...");
app.modules().init_all(app.state());

// ... Wi-Fi, drivers, scenario engine register-яться тут ...

// Phase 2 — HIGH + NORMAL priority модулі
ESP_LOGI(TAG, "Phase 2: Initializing WiFi + business modules...");
app.modules().init_all(app.state());

// ... HTTP, WS register-яться тут ...

// Phase 3 — LOW priority модулі
ESP_LOGI(TAG, "Phase 3: Initializing HTTP + WebSocket...");
app.modules().init_all(app.state());
```

Кожен виклик iterates по registered modules і викликає `on_init()` ТІЛЬКИ
на тих що ще у `CREATED` state. Ось як priority maps до phase: priority
`0`/CRITICAL ініціалізується у першому `init_all` call (бо нічого вище
не існує і він у CREATED state); priority `1`/HIGH і `2`/NORMAL у другому
call; priority `3`/LOW у третьому.

**Practical rule:** якщо ваш модуль залежить від чогось вже initialised —
оберіть вищий priority value (пізніший phase). Якщо він provides
foundational service для інших модулів — оберіть нижчий.

## Що йде у on_update vs on_init

**`on_init`:**
- Встановити initial state values.
- Прочитати persisted settings (PersistService мав би вже їх restore-нути
  якщо ваші `state` keys мали `persist: true`).
- Cache references / lookup tables що не змінюються.
- Принтнути один ESP_LOGI рядок "initialised з <ключовими параметрами>".

**`on_update`:**
- Актуальна business логіка.
- Прочитати inputs (state keys written іншими модулями / drivers).
- Compute next state.
- Записати outputs.

**Анти-патерни у `on_update`:**

- ❌ `vTaskDelay` / `sleep` — блокує 100 Hz update loop, starvує інші модулі.
- ❌ Heap allocation (`new`, `std::vector::push_back`, `std::string`) — на 100 Hz це leak-ає bytes per tick.
- ❌ NVS writes — synchronous I/O, ~5-50 мс кожен. Defer до module-level event handler або use `state_set` з `persist: true` (PersistService throttles).
- ❌ Heavy logging (>1 ESP_LOG за секунду на модуль) — UART floods, monitor lags.
- ❌ Reading complex JSON / parsing strings — pre-compute у `on_init`.

## Cross-module комунікація

Модулі не тримають pointers один на одного. Натомість:

1. **Pure data flow** — module А пише `keyA`, module В читає `keyA`. Module
   В запускається після А на тому ж tick через declaration order (модулі
   створюються у `module_instances.h` register-яться у порядку defined у
   `project.json`, що визначає update порядок у phase).

2. **Events / commands** — публікуються через запис state key, спостерігаються
   через читання на наступному tick. Edge detection через ваш власний
   `prev_value_` member.

3. **Messages** (рідко) — `ModuleManager::send_message(target, msg)`
   reaches `on_message`. Використовуйте коли message має typed payload
   що не fit-ається у єдиний state key.

4. **HTTP API** — external clients пишуть keys через `POST /api/settings`,
   що `set_state` actions також можуть. Той самий механізм, інший актор.

> 💡 **Tip:** для нового проекту, default до pure data flow через
> SharedState. Додавайте events / messages лише коли cross-tick coordination
> вимагає це. Більшість пар модулів не потребують explicit signaling.

## Читання sensor / actuator state

Модуль `equipment` володіє HAL drivers. Sensor values land-ять у keys типу
`equipment.air_temp`, `equipment.evap_temp`. Actuator requests пишуться до
`equipment.req_compressor`, `equipment.req_fan` тощо, і equipment maps їх
на фізичні relays на основі `bindings.json`.

Ваш business module читає sensor keys, пише actuator request keys.
Hardware decoupled.

```cpp
void MyModule::on_update(uint32_t dt_ms) {
    float temp = read_float("equipment.air_temp", 0.0f);
    bool need_cooling = (temp > setpoint_);
    state_set("equipment.req_compressor", need_cooling);
}
```

Повні HAL деталі: [components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)
*(planned)* і [hardware/bindings.md](../04-hardware/bindings.md) *(planned)*.

## Згенеровані headers — що автоматично

Після `idf.py build`, `generated/` містить:

| File | Content |
|---|---|
| `module_includes.h` | `#include "your_module.h"` для кожного модуля у project.json. |
| `module_instances.h` | `static YourModule your_module;` декларації. |
| `module_register.h` | `manager.register_module(your_module)` calls у `modesp_register_modules(app)`. |
| `state_meta.h` | Constexpr table усіх declared state keys, типів, max-length. |
| `mqtt_topics.h` | String константи для кожного MQTT topic. |
| `features_config.h` | `#define` для кожного feature flag. |

Ви їх не торкаєтесь руками. Генератор перезаписує їх кожен build з latest
маніфестами.

## Тестування модуля

**Host build (preferred для швидкої ітерації):**

```bash
cd tests/host
make MODULE=my_counter
./build/test_my_counter
```

Pattern: маленький `test_<name>.cpp` instantiates модуль з stub SharedState,
викликає `on_init` і `on_update` repeatedly, asserts state values. Див.
[testing.md](../06-contributing/testing.md) *(planned)* для fixtures і
how-tos.

**On-target HIL:**

`tools/tests/test_hil.py` exercises running firmware через HTTP API.
Додайте tests з `pytest` і `requests`
([test_hil.py reference](../../../tools/tests/test_hil.py)).

## Що далі

- **[shared-state.md](shared-state.md)** *(planned)* — глибші SharedState
  патерни (change tracking, optional reads, type validation).
- **[ui-widgets.md](ui-widgets.md)** *(planned)* — повний widget reference
  з visual examples.
- **[mqtt.md](mqtt.md)** *(planned)* — wiring `mqtt.subscribe` keys і
  publish патерни.
- **[persistence.md](persistence.md)** *(planned)* — флаг `persist: true`
  і PersistService.
- **[debugging.md](debugging.md)** *(planned)* — log inspection, state
  inspection через HTTP, поширені runtime issues.

## Existing модулі для вивчення source-first

- [`modules/simple_thermo/`](../../../modules/simple_thermo/) — ~55 LOC C++,
  показує hysteresis pattern, multi-key read/write. Найкраща перша річ
  для читання.
- [`modules/datalogger/`](../../../modules/datalogger/) — більший модуль з
  features, multiple state keys, NVS-backed buffers. Читайте після основ.
- [`modules/equipment/`](../../../modules/equipment/) — bridge маніфест до
  HAL drivers. Найбільш coupled модуль — читайте коли зрозумієте
  SharedState і drivers.
