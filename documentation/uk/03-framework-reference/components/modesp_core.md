# `modesp_core` — App, ModuleManager, SharedState, BaseModule

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_core.md](../../../en/03-framework-reference/components/modesp_core.md)

`modesp_core` — це фундамент, на якому стоїть ModESP. Він не має
залежностей від фреймворка над ETL і FreeRTOS, а кожен інший компонент
залежить від нього. П'ять публічних типів — `App`, `ModuleManager`,
`SharedState`, `BaseModule` і визначення типів у `types.h` — разом
утворюють всю програмну модель фреймворка.

Ця сторінка детально описує кожен із них з повною поверхнею API,
гарантіями життєвого циклу та контрактами потокобезпеки. Якщо ви пишете
модулі, ви постійно торкатиметесь `BaseModule` і `SharedState`; `App` і
`ModuleManager` підключаються у main.cpp і рідко змінюються.

## Публічні заголовки

```
components/modesp_core/include/modesp/
├── app.h                    ← App singleton, lifecycle root
├── base_module.h            ← BaseModule — your module's base class
├── module_manager.h         ← ModuleManager — registry і tick driver
├── shared_state.h           ← SharedState — thread-safe key-value store
├── types.h                  ← StateKey, StateValue, ModulePriority, etc.
├── message.h                ← etl::imessage wrapper + message ID ranges
└── platform/timing.h        ← millis() helper (FreeRTOS-aware)
```

REQUIRES: `marcel-cd__etlcpp` (шаблони ETL), `freertos` (м'ютекси,
завдання).

## Типи — `modesp/types.h`

### `StateKey` і `StateValue`

```cpp
namespace modesp {

using StateKey   = etl::string<MODESP_MAX_KEY_LENGTH>;       // default 32 chars
using StringValue = etl::string<MODESP_MAX_STRING_VALUE_LENGTH>; // default 32 chars

using StateValue = etl::variant<int32_t, float, bool, StringValue>;

}
```

| Тип | Примітки |
|---|---|
| `StateKey` | До 32 символів. Задовгі імена відхиляються під час `state_set`. |
| `StringValue` | До 32 символів. Довші рядки потребують прямого доступу до NVS або LittleFS. |
| `StateValue` | Варіант з 4 випадків. Кожен ключ закріплюється за типом при першому встановленні. |

Налаштовується через Kconfig (`CONFIG_MODESP_MAX_KEY_LENGTH`,
`CONFIG_MODESP_MAX_STRING_VALUE_LENGTH`), але значення за замовчуванням
перевірені практикою. Збільшення лімітів лінійно зростає у витратах RAM
карти SharedState.

### `ModulePriority`

```cpp
enum class ModulePriority : uint8_t {
    CRITICAL = 0,   // error_service, watchdog
    HIGH     = 1,   // wifi, hal, drivers, scenario engine
    NORMAL   = 2,   // business modules (default)
    LOW      = 3,   // http, ws, datalogger
};
```

Керує фазою ініціалізації та порядком оновлення. Модулі у межах одного
рівня пріоритету ініціалізуються та оновлюються у **порядку реєстрації**.
Нижчий пріоритет → раніша фаза.

### Діапазони ідентифікаторів повідомлень

`message.h` ділить простір ідентифікаторів повідомлень uint16:

| Діапазон | Власник |
|---|---|
| 0-49 | система (`MSG_SYS_*`) |
| 50-99 | служби (`MSG_SVC_*`) |
| 100-109 | HAL |
| 110-149 | драйвери |
| 150-199 | (зарезервовано) |
| 200+ | прикладні модулі |

Використовуйте унікальні ідентифікатори щоразу, коли визначаєте нове
типізоване повідомлення — колізії ламають диспетчеризацію повідомлень
ETL.

## `BaseModule` — `modesp/base_module.h`

Клас, від якого успадковуються усі службові модулі. Час життя: статичний
у main.cpp (конструюється один раз на старті програми, ніколи не
знищується).

### Конструювання

```cpp
class BaseModule {
public:
    BaseModule(const char* name, ModulePriority priority);
    virtual ~BaseModule() = default;
};
```

`name` зіставляється з полем `"module"` у вашому маніфесті. Обмеження
довжини: `MODESP_MAX_MODULE_NAME_LENGTH` (за замовчуванням 16 символів).

### Хуки життєвого циклу

```cpp
virtual bool on_init()                            { return true; }
virtual void on_update(uint32_t dt_ms)            {}
virtual void on_message(const etl::imessage& msg) {}
virtual void on_stop()                            {}
```

| Хук | Викликається | Повернення / блокування |
|---|---|---|
| `on_init` | Один раз при старті у фазі, що відповідає `priority`. | Поверніть `false`, щоб позначити модуль FAILED. |
| `on_update` | Кожні 10 мс (100 Гц), коли модуль у стані INITIALISED. | Не повинен блокувати (< 1 мс зазвичай). |
| `on_message` | Коли `ModuleManager::send_message` адресовано цьому модулю. | Не повинен блокувати; за потреби передавайте у внутрішні черги. |
| `on_stop` | Один раз при зупинці АБО при виклику `stop_all()`. | Прибирання; рідкість на практиці. |

### Машина станів модуля

```
   CREATED ──init→ INITIALISED ──tick→ INITIALISED  (steady state)
       │              │
       │              └──stop→ STOPPED
       │
       └──init failed→ FAILED
```

| Стан | Значення |
|---|---|
| `CREATED` | Сконструйовано, але `on_init` ще не викликано. |
| `INITIALISED` | `on_init` повернув `true`. Модуль отримує виклики `on_update`. |
| `FAILED` | `on_init` повернув `false`. Модуль зареєстрований, але неактивний. |
| `STOPPED` | `on_stop` виконано. Модуль більше не отримує тактів. |

Видно через `BaseModule::state() const`.

### Зручні методи доступу (на основі SharedState)

```cpp
// Write — typed overloads, all delegate до SharedState::set.
bool state_set(const char* key, int32_t value, bool track_change = true);
bool state_set(const char* key, float value,   bool track_change = true);
bool state_set(const char* key, bool value,    bool track_change = true);
bool state_set(const char* key, const char* value, bool track_change = true);

// Read — typed із default fallback.
float   read_float(const char* key, float def = 0.0f) const;
int32_t read_int(const char* key, int32_t def = 0) const;
bool    read_bool(const char* key, bool def = false) const;

// Generic — etl::optional<StateValue>.
etl::optional<StateValue> state_get(const char* key) const;
```

Див. [shared-state.md](../../02-module-author-guide/shared-state.md) для
повної семантики та типових помилок.

### Ідентичність і діагностика

```cpp
const char*    name() const;
ModulePriority priority() const;
ModuleState    state() const;
```

HTTP-точка доступу `/api/modules` повідомляє `name()` і `state()` для
кожного зареєстрованого модуля.

## `ModuleManager` — `modesp/module_manager.h`

Тримає масив фіксованої місткості з посилань `BaseModule*` і керує їхніми
життєвими циклами. Один екземпляр живе всередині `App`.

### Місткість

Місткість за замовчуванням задається в Kconfig
(`CONFIG_MODESP_MAX_MODULES`, зазвичай 32). Перевищення тихо скасовує
реєстрацію (виклики реєстрації повертають `false`).

### API

```cpp
class ModuleManager {
public:
    bool register_module(BaseModule& m);          // false if already registered or full
    bool init_all(SharedState& state);            // calls on_init() on each CREATED module
    void update_all(uint32_t dt_ms);              // calls on_update() on each INITIALISED module
    void on_message(const etl::imessage& msg);    // dispatch to addressed module's on_message
    void stop_all();                              // calls on_stop() on all initialised modules
    bool send_message(const char* target, const etl::imessage& msg);

    // Diagnostic
    size_t count() const;
    BaseModule* find(const char* name) const;
    void for_each(std::function<void(BaseModule&)> fn) const;
};
```

### Шаблон трифазної ініціалізації

`init_all` — це **шлагбаум**: він ініціалізує лише ті модулі, що зараз
знаходяться у стані CREATED. Виклик один раз при завантаженні ініціалізує
кожен зареєстрований модуль; виклик тричі з перемежованими реєстраціями
дає фазову ініціалізацію, описану у
[architecture.md](../architecture.md#three-phase-init).

```cpp
// Phase 1
app.modules().register_module(error_service);
app.modules().register_module(watchdog);
app.modules().init_all(app.state());            // these inited

// Phase 2
app.modules().register_module(wifi);
app.modules().register_module(hal);
app.modules().init_all(app.state());            // now these inited; phase 1 modules skipped

// Phase 3
app.modules().register_module(http);
app.modules().init_all(app.state());            // only http inited here
```

### Такт оновлення

`update_all` ітерує зареєстровані модулі у порядку додавання і викликає
`on_update(dt_ms)` для кожного модуля зі станом `INITIALISED`.
Однопотоково — без паралельного виконання модулів.

### Повідомлення

Модуль надсилає типізоване `etl::imessage` іншому модулю за іменем:

```cpp
struct OtaProgressMsg : public etl::message<200> {
    uint32_t bytes_transferred;
    uint32_t total_bytes;
};

OtaProgressMsg msg{12345, 67890};
app.modules().send_message("datalogger", msg);
```

`on_message` адресата виконується синхронно у завданні-відправнику.
Використовуйте обережно — більшість обмінів має йти через SharedState.

## `SharedState` — `modesp/shared_state.h`

Потокобезпечне типізоване сховище ключ-значення. Один екземпляр живе
всередині `App`.

### Сховище

```cpp
using Map = etl::unordered_map<StateKey, StateValue, MODESP_MAX_STATE_ENTRIES>;
```

`MODESP_MAX_STATE_ENTRIES` за замовчуванням дорівнює 96, генерується
автоматично у `state_meta.h` з маніфестів. Збільшується через Kconfig за
потреби (вартість: ~80 байт RAM на запис).

### API читання/запису

```cpp
class SharedState {
public:
    // Write
    bool set(const StateKey& key, const StateValue& value, bool track_change = true);
    bool set(const char* key, int32_t value, bool track_change = true);
    bool set(const char* key, float value, bool track_change = true);
    bool set(const char* key, bool value, bool track_change = true);
    bool set(const char* key, const char* value, bool track_change = true);

    // Read
    etl::optional<StateValue> get(const StateKey& key) const;
    etl::optional<StateValue> get(const char* key) const;
    bool has(const StateKey& key) const;
    bool remove(const StateKey& key);

    // Maintenance
    size_t size() const;
    void clear();
};
```

### Відстеження змін та інтеграція з WebSocket

```cpp
class SharedState {
public:
    using IterCallback = void(*)(const StateKey&, const StateValue&, void* user_data);

    // Iterate ALL keys (під mutex — callback must be fast).
    void for_each(IterCallback cb, void* user_data) const;

    // Iterate changed keys (since last flush) AND clear list.
    bool for_each_changed_and_clear(IterCallback cb, void* user_data);

    bool has_changes() const;
    bool needs_full_broadcast() const;   // true if changed_keys_ overflowed
    uint32_t version() const;             // monotonic counter on every tracked set
    uint32_t set_failures() const;        // diagnostic — set() rejection count
};
```

Служба WebSocket використовує ці методи для розсилки дельт кожні ~500 мс.
PersistService і MqttService також спостерігають за змінами стану через
цей API.

### Гак збереження

```cpp
using PersistCallback = void(*)(const StateKey&, const StateValue&, void* user_data);
void set_persist_callback(PersistCallback cb, void* user_data);
```

PersistService реєструє зворотний виклик, що спрацьовує при кожному
відстеженому записі — саме так працює `persist: true` (див.
[persistence.md](../../02-module-author-guide/persistence.md)).

### Потокобезпека

Усі методи внутрішньо захоплюють м'ютекс FreeRTOS. Безпечно з:
- завдання тактів модуля (за замовчуванням);
- завдання HTTP-запиту;
- завдання зворотного виклику підписки MQTT;
- рушія сценаріїв рецептів;
- **НЕ ISR** (м'ютекс блокує).

Тайм-аут м'ютекса — 100 мс. Якщо `set` повертає `false`, лічильник
`set_failures()` зростає — ненульове значення натякає на конкуренцію.

## `App` — `modesp/app.h`

Синглтон застосунку, що володіє SharedState і ModuleManager.

```cpp
class App {
public:
    static App& instance();

    bool init();                          // creates state, manager. Idempotent.
    ModuleManager& modules();             // mutable accessor
    SharedState&   state();               // mutable accessor

private:
    App() = default;                      // private — use instance()
    SharedState   state_;
    ModuleManager modules_;
};
```

Час життя: лінько конструюється при першому виклику `App::instance()`.
Живе до виходу програми (на вбудованих пристроях — ніколи). Обидва
методи доступу повертають посилання, що переживають будь-який модуль.

## Пам'ять і бюджет розміру

| Стаття | Витрата RAM |
|---|---|
| SharedState з 96 записами | ~8 КБ (ключ + варіант + службова інформація) |
| ModuleManager з 32 слотами | ~512 байт (масив вказівників + статус) |
| Один підклас BaseModule | залежить від ваших полів; ціль — < 256 байт |
| М'ютекс (FreeRTOS) | ~88 байт |

Загальні накладні витрати ядра: ~10 КБ RAM. Використання купи: нуль (усе
ETL — статичне).

## Типові шаблони використання

### Типовий каркас модуля

```cpp
// my_module/include/my_module.h
#pragma once
#include "modesp/base_module.h"

class MyModule : public modesp::BaseModule {
public:
    MyModule() : BaseModule("my_module", modesp::ModulePriority::NORMAL) {}

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    uint32_t elapsed_ = 0;
};

// my_module/src/my_module.cpp
bool MyModule::on_init() {
    state_set("my_module.counter", static_cast<int32_t>(0));
    return true;
}

void MyModule::on_update(uint32_t dt_ms) {
    elapsed_ += dt_ms;
    if (elapsed_ < 1000) return;
    elapsed_ = 0;

    int32_t n = read_int("my_module.counter", 0);
    state_set("my_module.counter", n + 1);
}
```

### Діагностичний знімок з обробника HTTP

```cpp
// runs on httpd task
auto& state = HttpService::app_ref().state();
state.for_each([](const auto& k, const auto& v, void* ctx) {
    auto* json = static_cast<JsonWriter*>(ctx);
    // emit key=value into JSON
}, &json_writer);
```

(Обробники HTTP зазвичай не викликають `register_module`; вони
використовують `app.state()` напряму через впровадження залежностей.)

### Статична ініціалізація модулів

Модулі створюються як статичні об'єкти файлової області у `main.cpp`
(згенерований `module_instances.h`):

```cpp
static modesp::ErrorService error_service;
static MyModule             my_module;
// ... constructed before app_main runs.
```

Тіла конструкторів повинні бути **тривіальними** — лише встановлення
типових значень. Справжня ініціалізація йде у `on_init`, коли SharedState
вже існує.

## Типові помилки

**Виклик `state_set` із конструктора:** SharedState ще не існує під час
статичної ініціалізації. Завжди відкладайте до `on_init`.

**Реентрантні виклики `SharedState` зсередини зворотного виклику
`for_each`:** м'ютекс утримується на час ітерації; виклик `set` / `get`
з вашого зворотного виклику призводить до взаємного блокування. Буферте
зміни ззовні та застосовуйте їх після ітерації.

**Важка робота у `on_message`:** виконується в тому завданні, що
надіслало повідомлення. Тривалі обробники блокують це завдання. Ставте
роботу у власну машину станів для обробки в `on_update`.

**Відсутній `override` на віртуальних методах:** мовчазна помилка
друку (`on_initt`) залишає стандартну порожню реалізацію на місці.
Завжди використовуйте `override`.

## Що далі

- **[components/modesp_services.md](modesp_services.md)** *(планується)*
  — служби Error/Watchdog/Config/Persist/Logger, побудовані на
  modesp_core.
- **[components/modesp_hal.md](modesp_hal.md)** *(планується)* — HAL і
  DriverManager.
- **[02-module-author-guide/writing-a-module.md](../../02-module-author-guide/writing-a-module.md)**
  — покрокове проходження з боку автора з використанням цього API.
- **[02-module-author-guide/shared-state.md](../../02-module-author-guide/shared-state.md)**
  — шаблони SharedState і правила типів.

## Джерела

- [`components/modesp_core/include/modesp/`](../../../../components/modesp_core/include/modesp/)
  — публічні заголовки.
- [`components/modesp_core/src/`](../../../../components/modesp_core/src/)
  — реалізації.
