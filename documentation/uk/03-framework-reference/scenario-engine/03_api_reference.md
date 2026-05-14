# 03 — Довідник C++ API

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/03_api_reference.md](../../../en/03-framework-reference/scenario-engine/03_api_reference.md)

Авторитетний довідник публічної поверхні C++ API простору імен
`modesp::scenario`. Джерело: `components/modesp_scenario/include/modesp/scenario/`.

## Константи

```cpp
namespace modesp::scenario {

// Пул слотів — налаштовується через Kconfig CONFIG_MODESP_MAX_SEQUENCES
constexpr size_t   MAX_SEQUENCES = 2;            // типове; зазвичай діапазон 2..8

// Обмеження кількості доріжок
constexpr uint8_t  MAX_TRACKS_PER_SCENARIO = 6;
// MAX_TOTAL_TRACKS = MAX_SEQUENCES × MAX_TRACKS_PER_SCENARIO

// Глибина дерева складених умов
constexpr uint8_t  MAX_CONDITION_DEPTH = 16;

// Розмір мапи володіння ресурсами
constexpr size_t   MAX_RESOURCES = 32;

// Маркер для невалідного індексу доріжки (= володіння на рівні сценарію)
constexpr TrackIdx TRACK_IDX_SCENARIO = 0xFF;

// Місткості реєстрів
constexpr size_t   MAX_REGISTRY_ENTRIES = 64;          // окремо для дій ТА умов
constexpr size_t   MAX_CONTINUOUS_REGISTRATIONS = 32;  // фабрики неперервних поведінок

// Маркерні хеші складених умов
constexpr uint16_t MODR_COND_HASH_ALL_OF;        // djb2_hash16("all_of")
constexpr uint16_t MODR_COND_HASH_ANY_OF;        // djb2_hash16("any_of")
constexpr uint16_t MODR_COND_HASH_NOT;           // djb2_hash16("not")

// Формат файлу
constexpr uint32_t MODR_MAGIC = 0x52444F4D;      // 'MODR' LE
constexpr uint16_t MODR_FORMAT_VERSION = 1;
constexpr size_t   MODR_MAX_SIZE = 4 * 1024;     // типове 4 КБ; налаштовується через Kconfig

// Токен персистентності — magic змінено зі старого 'SQTK' під час перебудови
constexpr uint32_t SEQ_TOKEN_MAGIC = 0x4B544353; // 'SCTK' LE
constexpr uint16_t SEQ_TOKEN_VERSION = 1;
constexpr size_t   SEQ_TOKEN_SIZE = 96;

}
```

> **Зауваження щодо зміни magic:** magic токена персистентності змінено з
> `'SQTK'` (0x4B545153) у застарілому рушії `modesp_sequence` на `'SCTK'`
> (0x4B544353) у `modesp_scenario`. Старі токени у NVS мовчки відхиляються
> після оновлення прошивки — відповідні сценарії стартують з нуля.

## Псевдоніми типів

```cpp
using SequenceHandle = uint8_t;   // 1..MAX_SEQUENCES, 0 = недійсний
using TrackIdx       = uint8_t;   // 0..MAX_TRACKS_PER_SCENARIO-1
```

## Перелічення

### `EngineError` — уніфікований код помилки

19 значень. Повна таблиця в [10_error_model.md](10_error_model.md#engineerror-codes).

```cpp
enum class EngineError : uint8_t {
    OK = 0,
    INVALID_FILE, UNSUPPORTED_VERSION, CRC_MISMATCH, BUFFER_OVERFLOW,
    UNKNOWN_ACTION, UNKNOWN_CONDITION, INVALID_TRANSITION,
    TOO_MANY_TRACKS, NAME_TOO_LONG,
    NO_SLOT, NOT_LOADED, INVALID_HANDLE, INVALID_TRACK,
    PARAM_OUT_OF_RANGE, PARAM_NOT_OVERRIDABLE,
    RESOURCE_CONTENDED, NVS_ERROR, ABORTED_BY_SAFETY,
};
constexpr bool is_ok(EngineError e);  // допоміжна функція
```

### `ActionStatus` — код повернення обробника дії

```cpp
enum class ActionStatus : uint8_t {
    OK = 0,                ///< дію завершено, рушій просувається далі
    PENDING = 1,           ///< викликати знову на наступному такті (довготривалі операції)
    FAILED_RECOVERABLE = 2,///< залогувати + пропустити решту у фазі, продовжити через переходи
    FAILED_ABORT = 3,      ///< доріжка → TRACK_FAILED
};
```

### `ParamType` — дискримінатор типізованого параметра

```cpp
enum class ParamType : uint8_t {
    I32 = 0, F32 = 1, BOOL = 2, STR = 3,
};
```

### Перелічення станів

```cpp
struct SequenceRuntime {
    enum class State : uint8_t {
        IDLE = 0,        ///< слот порожній
        LOADED,          ///< .modr завантажено й перевірено, очікує start()
        RUNNING,
        PAUSED,
        ABORTING,        ///< переривання в процесі; доріжки виконують exit-дії
                         ///< (фазовий $abort) або примусовий FAILED (переривання сценарію)
        COMPLETED,       ///< completion_rule виконано
        FAILED,          ///< шлях переривання завершено або термінальна помилка
    };
};

struct TrackRuntime {
    enum class State : uint8_t {
        IDLE = 0,                ///< до старту сценарію
        RUNNING,                 ///< виконання entry/dwell/переходів фази
        WAITING_FOR_RESOURCE,    ///< вхід у фазу заблоковано фазовою претензією
        ABORTING,                ///< спрацював фазовий $abort; виконуються exit-дії
        COMPLETED,               ///< досягнуто MODR_TARGET_COMPLETE
        FAILED,                  ///< $abort, FAILED_ABORT або переривання на рівні сценарію
    };
};
```

## Базові типи

### `ActionParam` — типізований параметр

Еквівалентний за провідним форматом до `modr_param_entry`. POD, рівно 8 байт.

```cpp
struct ActionParam {
    uint16_t key_hash;     ///< djb2_hash16 від імені параметра
    uint8_t  type;         ///< значення ParamType
    uint8_t  flags;        ///< MODR_PARAM_FLAG_*
    union {
        int32_t  i;
        float    f;
        bool     b;
        uint16_t s_idx;    ///< зміщення в пулі рядків (коли type == STR)
    } v;
};
```

### `ActionContext` — передається кожному обробнику дії/умови

```cpp
struct ActionContext {
    IStateBackend*       state;             ///< інжектований state-backend (nullable у тестах)
    const ActionParam*   params;            ///< масив із param_count записів
    uint8_t              param_count;
    uint8_t              _pad0;
    uint16_t             string_pool_size;
    const char*          string_pool;       ///< використовується для розв'язання STR-параметрів
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;         ///< поточна фаза в межах доріжки
    SequenceHandle       handle;
    TrackIdx             track;
    uint8_t              _pad1;
    const char*          recipe_name;       ///< із заголовка рецепта (пул рядків)
    const char*          track_name;        ///< із запису доріжки (пул рядків)
};
```

`state` — це `IStateBackend*`: рушій більше не має жорсткої прив'язки до
`modesp::SharedState`. Обробники дій читають/пишуть стан через
`ctx.state->get<T>(key, out)` / `ctx.state->set<T>(key, value)`. Див.
[`IStateBackend`](#istatebackend--інтерфейс-state-бекенда) нижче.

### `ActionDescriptor` — запис реєстру

```cpp
using ActionFn = ActionStatus (*)(ActionContext&);

struct ActionDescriptor {
    uint16_t hash;          ///< МУСИТЬ дорівнювати djb2_hash16(name)
    const char* name;
    ActionFn fn;
    uint8_t param_min;      ///< мінімум параметрів, які перевіряє рушій (контроль викликача)
    uint8_t param_max;      ///< максимум параметрів
};
```

## `IStateBackend` — інтерфейс state-бекенда

Джерело: `i_state_backend.h`. Рушій спілкується з оточуючим сховищем стану
**виключно** через цей інтерфейс. Продакшен підключає `modesp::SharedState`
через тонкий адаптер (живе в `main/`); хост-тести використовують
`StubStateBackend`.

```cpp
class IStateBackend {
public:
    virtual ~IStateBackend() = default;

    // Дві сирі віртуальні функції — їх реалізує бекенд
    virtual bool get_raw(const char* key, modesp::StateValue& out) const = 0;
    virtual bool set_raw(const char* key, const modesp::StateValue& value) = 0;

    // Не-віртуальні типізовані помічники (нульова вартість v-таблиці)
    template <typename T> bool get(const char* key, T& out) const;
    template <typename T> bool set(const char* key, T value);
    bool set(const char* key, const char* value);   // перевантаження для рядкового літерала
};
```

Дві сирі віртуальні функції над наявним варіантом `modesp::StateValue`
(int32/float/bool/string); типізовані `get<T>`/`set<T>` — інлайн-шаблони у
заголовку, які діляться на варіант — без витрат v-таблиці на тип.

## `Engine` — публічний API

`class Engine : public modesp::BaseModule`. **Володіє ним викликач**, без
одинаків. Усі колабораторні залежності інжектуються через конструктор.

### Створення та підключення

```cpp
// 1. Адаптер state-бекенда — реалізує IStateBackend поверх SharedState
static SharedStateBackend sb{shared_state};

// 2. Реєстри, якими володіє викликач (заповнюються до engine.start())
static modesp::scenario::ActionRegistry     actions;
static modesp::scenario::ContinuousRegistry continuous;

// 3. Спостерігач NVS-персистентності (опційно — пропустіть, якщо відновлення не потрібне)
static modesp::scenario::NvsObserver        nvs_obs{nvs_write_fn, nvs_read_fn, nullptr};
static modesp::scenario::IEngineObserver*   obs_list[] = {&nvs_obs};

// 4. Рушій з інжектованими залежностями
static modesp::scenario::Engine engine{sb, actions, continuous, obs_list};

// 5. Заповнюємо реєстри — мусить завершитися до того, як будь-який сценарій ними скористається
modesp::scenario::builtins::register_builtins(actions);
modesp::scenario::primitives::register_primitives(continuous);  // опційні стандартні примітиви

// 6. Прив'язуємо спостерігач назад до рушія (потрібно для серіалізації в колбеках)
nvs_obs.bind_engine(engine);

// 7. Реєструємо рушій у ModuleManager
manager.register_module(engine);
```

Конструктор:

```cpp
Engine(IStateBackend& state,
       ActionRegistry& actions,
       ContinuousRegistry& continuous,
       etl::span<IEngineObserver*> observers = {});
```

Час життя посилань мусить перевищувати час життя рушія. Span спостерігачів
відомий на етапі constexpr — API додавання/видалення в runtime відсутнє.

### Інтерфейс BaseModule (керується ModuleManager)

```cpp
bool on_init() override;            // скидає всі слоти, очищує арбітр
void on_update(uint32_t dt_ms) override;
                                    // тактує запущені екземпляри й розсилає
                                    // події життєвого циклу спостерігачам
void on_stop() override;            // вивантажує всі слоти
```

### Завантаження рецепта

```cpp
SequenceHandle load_buffer(const uint8_t* data, size_t size);
SequenceHandle load_path(const char* path);
EngineError unload(SequenceHandle h);
```

`load_buffer` копіює байти у буфер слота, яким володіє рушій (макс. MODR_MAX_SIZE).
Повертає 0 при невдачі; перевірте `last_error()`.

`load_path` читає файл через `std::fopen` (цільове середовище: ESP-IDF VFS LittleFS).
Повертає 0, якщо файл не знайдено, виникла помилка читання або не пройшла валідація.

### Життєвий цикл

```cpp
EngineError start(SequenceHandle h);                     // LOADED → RUNNING
EngineError pause(SequenceHandle h);                     // RUNNING → PAUSED
EngineError resume(SequenceHandle h);                    // PAUSED → RUNNING
EngineError abort(SequenceHandle h, uint8_t reason = 0); // → ABORTING
```

`start` атомарно захоплює ресурси на рівні сценарію. Повертає `RESOURCE_CONTENDED`
при конфлікті без часткового стану.

`abort` примусово переводить доріжки у FAILED (з вивільненням фазових ресурсів).
Згідно з обсягом MVP плану Q7: переривання на рівні сценарію НЕ виконує exit-дії
фаз — автори рецептів використовують глобальні переходи до спеціальних фаз очищення.

### Персистентність (Етап 1)

NVS-колбеки **не** встановлюються на рушії. Ними володіє `NvsObserver`, який
реєструється з рушієм через span спостерігачів у конструкторі.

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* buf, size_t* in_out_len);

// Відновлення — це двосторонній обмін, тому викликач явно передає NvsObserver,
// а не рушій робить id-cast через список спостерігачів.
EngineError try_recover(SequenceHandle h, NvsObserver& nvs);
```

`try_recover` вимагає, щоб слот уже був у стані LOADED (рецепт повторно
завантажено після завантаження системи). Він викликає колбек читання
спостерігача, десеріалізує токен, застосовує стан і встановлює стан сценарію у
PAUSED. Див. [07_persistence.md](07_persistence.md).

### Діагностика

```cpp
SequenceRuntime::State state(SequenceHandle h) const;
EngineError last_error() const;
uint32_t scenario_elapsed_ms(SequenceHandle h) const;
uint8_t  track_count(SequenceHandle h) const;
TrackRuntime::State track_state(SequenceHandle h, TrackIdx t) const;
uint8_t  track_phase_idx(SequenceHandle h, TrackIdx t) const;
uint32_t track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const;
uint8_t  active_count() const;          ///< рахує слоти RUNNING + PAUSED

ResourceArbiter&    arbiter();          ///< прямий доступ до арбітра (тести, HTTP-діагностика)
ActionRegistry&     actions();          ///< зручний accessor (те саме посилання, що інжектоване)
ContinuousRegistry& continuous();       ///< аналогічно

SequenceRuntime*       runtime_for(SequenceHandle h);        ///< для спостерігача персистентності
const SequenceRuntime* runtime_for(SequenceHandle h) const;  ///< аналогічно
```

Усі діагностичні методи повертають безпечні значення за замовчуванням (стан IDLE,
нульові лічильники) для недійсних дескрипторів — вони ніколи не падають.

## `IEngineObserver` — хуки життєвого циклу

Джерело: `i_engine_observer.h`. Рушій синхронно з task оновлення емітить три
події на фронтах + tick-хук. Порожні дефолтні тіла — перевизначаєте лише те,
що вас цікавить. **Спостерігачі не повинні мутувати стан рушія.**

```cpp
class IEngineObserver {
public:
    virtual ~IEngineObserver() = default;

    virtual void on_scenario_started(SequenceHandle h) {}
    virtual void on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) {}
    virtual void on_scenario_terminal(SequenceHandle h, SequenceRuntime::State final_state) {}
    virtual void on_tick(uint32_t dt_ms) {}
};
```

Дзеркальні записи (ключі `scenario.*` у SharedState) НЕ є спостерігачами —
рушій пише їх кожного такту прямим викликом. Сюди належать лише побічні
ефекти, що спрацьовують на фронтах.

## `NvsObserver` — вбудований спостерігач персистентності

Джерело: `nvs_observer.h`. Реалізує `IEngineObserver`; зберігає 96-байтні
токени у NVS через колбеки, надані викликачем. Політика throttle: негайний
запис на `scenario_started`, `scenario_terminal` і `phase_entered` основної
доріжки; `phase_entered` неосновної доріжки throttle до мінімуму 1 с між
записами.

```cpp
class NvsObserver : public IEngineObserver {
public:
    NvsObserver(NvsWriteFn write_fn, NvsReadFn read_fn, void* user);

    /// Має бути викликано після створення рушія, але до engine.start().
    void bind_engine(const Engine& eng);

    // Перевизначення з IEngineObserver
    void on_scenario_started(SequenceHandle h) override;
    void on_phase_entered(SequenceHandle h, TrackIdx t, uint8_t phase_idx) override;
    void on_scenario_terminal(SequenceHandle h, SequenceRuntime::State final_state) override;
    void on_tick(uint32_t dt_ms) override;

    /// Engine::try_recover() форвардить у це.
    EngineError try_recover(SequenceHandle h, SequenceRuntime& sr);
};
```

## `ActionRegistry` — належить викликачу

Джерело: `action_registry.h`. **Не одинак.** Викликач (зазвичай `main.cpp`)
створює екземпляр `ActionRegistry`, заповнює його через
`builtins::register_builtins(reg)` плюс будь-які власні реєстрації та інжектує
посилання у конструктор рушія. У межах одного процесу можуть співіснувати
кілька реєстрів.

```cpp
class ActionRegistry {
public:
    ActionRegistry() = default;
    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;
    ActionRegistry(ActionRegistry&&) = default;
    ActionRegistry& operator=(ActionRegistry&&) = default;

    bool register_action(const ActionDescriptor& d);
    bool register_condition(const ActionDescriptor& d);
                            ///< Повертає false при колізії, вичерпанні ємності
                            ///< або невідповідності hash != djb2_hash16(name).

    const ActionDescriptor* find_action(uint16_t hash) const;
    const ActionDescriptor* find_condition(uint16_t hash) const;
                            ///< nullptr, якщо не знайдено.

    size_t action_count() const;
    size_t condition_count() const;

    void clear();           ///< помічник ізоляції тестів
};

constexpr uint16_t djb2_hash16(const char* str) noexcept;
```

Ємність: `MAX_REGISTRY_ENTRIES = 64` на реєстр (для дій та умов окремо).
Потокова безпека: пошуки (`find_*`) — read-only й lock-free після
ініціалізації. Реєстрації мають завершитися до старту рушія; конкурентні
виклики `register_*` НЕ безпечні.

Див. [usage/03_registering_actions.md](usage/03_registering_actions.md).

## `ContinuousRegistry` — належить викликачу

Джерело: `continuous_behavior.h`. **Не одинак** — та сама модель володіння та
потокова модель, що й `ActionRegistry`. Викликач створює, заповнює фабричними
функціями та інжектує посилання у конструктор рушія.

```cpp
class ContinuousBehavior {
public:
    virtual ~ContinuousBehavior() = default;
    virtual void on_activate(const ActionParam* params, uint8_t n,
                             const char* string_pool, ActionContext& ctx) = 0;
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;
    virtual void on_deactivate(ActionContext& ctx) = 0;
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};

using ContinuousFactory = ContinuousBehavior* (*)();

class ContinuousRegistry {
public:
    ContinuousRegistry() = default;
    ContinuousRegistry(const ContinuousRegistry&) = delete;
    ContinuousRegistry& operator=(const ContinuousRegistry&) = delete;
    ContinuousRegistry(ContinuousRegistry&&) = default;
    ContinuousRegistry& operator=(ContinuousRegistry&&) = default;

    bool register_factory(uint16_t hash, const char* name, ContinuousFactory factory);
    ContinuousFactory   find(uint16_t hash) const;
    ContinuousBehavior* create(uint16_t hash) const;  ///< скорочення для find() + factory()

    size_t count() const;
    void   clear();
};
```

Ємність: `MAX_CONTINUOUS_REGISTRATIONS = 32`.

### Стандартні примітиви (Етап 2)

Згідно з ADR-0006 фреймворк все ще постачає **0 вбудованих неперервних
поведінок, що реєструються автоматично**. Етап 2 додав *опціональний*
каталог стандартних примітивів керування у `continuous_primitives.h`:

- `PidController` — замкнений PID із anti-windup; читає `input_key`, пише
  `output_key`. Параметри: `input_key`, `output_key`, `setpoint`, `kp`, `ki`,
  `kd`, `out_min`, `out_max`.
- `HysteresisController` — bang-bang з симетричною мертвою зоною; режим
  cooling або heating. Параметри: `input_key`, `output_key`, `setpoint`,
  `deadband`, `mode` (0 = cooling, 1 = heating).
- `RampProfile` — лінійний ramp із `start_value` у `end_value` за
  `duration_ms`, далі утримує `end_value`. Параметри: `output_key`,
  `start_value`, `end_value`, `duration_ms`.

Щоб зробити їх доступними сценаріям, викличте помічник реєстрації на вашому
екземплярі `ContinuousRegistry`:

```cpp
namespace modesp::scenario::primitives {
    ContinuousBehavior* pid_factory();
    ContinuousBehavior* hysteresis_factory();
    ContinuousBehavior* ramp_factory();

    /// Зареєструвати всі три примітиви у вказаний реєстр.
    bool register_primitives(ContinuousRegistry& registry);

    constexpr int PRIMITIVE_COUNT = 3;
}
```

Фабрики повертають екземпляри, виділені у купі — власник реєстру відповідає
за `delete` при вивантаженні / постійній деактивації.

## Вбудовані дії та умови

Джерело: `builtin_actions.h`. Реєструються через `builtins::register_builtins(reg)`
(приймає реєстр за посиланням — без одинаків).

```cpp
namespace modesp::scenario::builtins {
    bool register_builtins(ActionRegistry& registry);
    constexpr int BUILTIN_ACTION_COUNT = 3;
    constexpr int BUILTIN_CONDITION_COUNT = 10;
}
```

3 дії:
- `log {msg: string}` — діагностичний ESP_LOG
- `set_state {key: str, type: i32-enum, value: typed}` — типізований запис
  стану через `ctx.state->set(...)` (пише через інжектований `IStateBackend`)
- `wait_ms {ms: i32}` — затримка на основі PENDING

10 листкових умов:
- `time_elapsed_ms {ms: i32}`
- `state_key_eq/ne/lt/gt/le/ge {key, value}`
- `state_key_in_range {key, min, max}`
- `state_key_changed {key}` — заглушка, Крок 14+ підключить переходи на фронтах
- `time_of_day_eq {hh, mm}` — збіг з годинником реального часу (потрібен SNTP)

Складені умови (`all_of`, `any_of`, `not`) обробляються рушієм безпосередньо
без записів у реєстрі — використовуйте маркерні хеші у записах `cond_pool`.

## `LoadedScenario` — представлення лише для читання (просунутий рівень)

Джерело: `modr_loader.h`. Повертається `modr_validate` при успіху. Містить
сирі вказівники у буфер, який належить викликачеві.

```cpp
struct LoadedScenario {
    const uint8_t* buffer;
    size_t         buffer_size;

    const modr_header*           header() const;
    const modr_track*            tracks() const;
    const modr_phase*            phases(uint8_t track_idx) const;
    const modr_transition*       transitions(uint16_t off) const;
    const modr_action*           action_pool() const;
    const modr_action*           cond_pool() const;
    const modr_param_entry*      param_pool() const;
    const modr_global_transition* global_transitions() const;
    const modr_resource_decl*    resources() const;
    const modr_phase_resource_claim* phase_resources(uint16_t off) const;

    const char* string_pool_data() const;
    uint16_t    string_pool_size() const;
    bool read_string(uint16_t offset, char* buf, size_t buf_size) const;
};

EngineError modr_validate(const uint8_t* buffer, size_t size,
                          const ActionRegistry& registry,
                          LoadedScenario& out);
uint32_t crc32_iso_hdlc(const uint8_t* data, size_t len);
```

`modr_validate` розв'язує хеші дій та умов щодо вказаного посилання на
`ActionRegistry` — належить викликачеві, мусить пережити виклик. Клієнти
рушія зазвичай не торкаються `LoadedScenario` напряму — натомість
використовуйте `Engine::load_*`.

## `ResourceArbiter` — ISA-88 §5.3

Джерело: `resource_arbiter.h`. Конкретний клас (без інтерфейсної абстракції).
Внутрішній для рушія, але відкритий через `engine.arbiter()` для діагностики
й доступу з тестів.

```cpp
class ResourceArbiter {
public:
    EngineError acquire_scenario(SequenceHandle h,
                                  const modr_resource_decl* resources,
                                  uint8_t count, uint8_t phase_idx = 0);
    void release_scenario(SequenceHandle h);

    bool try_acquire_phase(SequenceHandle h, TrackIdx track, uint8_t phase_idx,
                           const modr_phase_resource_claim* claims, uint8_t count);
    void release_phase(SequenceHandle h, TrackIdx track);

    bool is_owned(uint16_t resource_hash) const;
    const OwnerInfo* owner_of(uint16_t resource_hash) const;
    size_t count() const;

    void clear_for_tests();
};
```

Опис дизайну див. у [06_resource_arbitration.md](06_resource_arbitration.md).

## Перехресні посилання

- [usage/01_quickstart.md](usage/01_quickstart.md) — типовий патерн використання
- [usage/03_registering_actions.md](usage/03_registering_actions.md) — розширення власними діями
- [02_binary_format.md](02_binary_format.md) — побайтова розкладка `.modr` (вхід завантажувача)
- [07_persistence.md](07_persistence.md) — контракт NVS-колбеків і формат токена
- [10_error_model.md](10_error_model.md) — повна таблиця EngineError із настановами щодо обробки
