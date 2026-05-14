# 03 — Довідник C++ API

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/03_api_reference.md](../../../en/03-framework-reference/scenario-engine/03_api_reference.md)

Авторитетний довідник публічної поверхні C++ API простору імен
`modesp::scenario`. Джерело: `components/modesp_scenario/include/modesp/scenario/`.

## Константи

```cpp
namespace modesp::scenario {

// Пул слотів — налаштовується через Kconfig MODESP_MAX_SEQUENCES
constexpr size_t   MAX_SEQUENCES = 4;            // типове; діапазон 2..8

// Обмеження кількості доріжок
constexpr uint8_t  MAX_TRACKS_PER_SCENARIO = 6;
// MAX_TOTAL_TRACKS = MAX_SEQUENCES × MAX_TRACKS_PER_SCENARIO = 24 (типове)

// Глибина дерева складених умов
constexpr uint8_t  MAX_CONDITION_DEPTH = 16;

// Розмір мапи володіння ресурсами
constexpr size_t   MAX_RESOURCES = 32;

// Маркер для невалідного індексу доріжки (= володіння на рівні сценарію)
constexpr TrackIdx TRACK_IDX_SCENARIO = 0xFF;

// Маркерні хеші складених умов
constexpr uint16_t MODR_COND_HASH_ALL_OF;        // djb2_hash16("all_of")
constexpr uint16_t MODR_COND_HASH_ANY_OF;        // djb2_hash16("any_of")
constexpr uint16_t MODR_COND_HASH_NOT;           // djb2_hash16("not")

// Формат файлу
constexpr uint32_t MODR_MAGIC = 0x52444F4D;      // 'MODR' LE
constexpr uint16_t MODR_FORMAT_VERSION = 1;
constexpr size_t   MODR_MAX_SIZE = 16 * 1024;    // 16 КБ

// Токен персистентності
constexpr uint32_t SEQ_TOKEN_MAGIC = 0x4B545153; // 'SQTK' LE
constexpr uint16_t SEQ_TOKEN_VERSION = 1;
constexpr size_t   SEQ_TOKEN_SIZE = 96;

}
```

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
    modesp::SharedState* state;
    const ActionParam*   params;
    uint8_t              param_count;
    const char*          string_pool;
    uint16_t             string_pool_size;
    uint32_t             scenario_elapsed_ms;
    uint32_t             phase_elapsed_ms;
    uint8_t              phase_idx;
    SequenceHandle       handle;
    TrackIdx             track;
    const char*          recipe_name;       ///< із заголовка рецепта (пул рядків)
    const char*          track_name;        ///< з запису доріжки (пул рядків)
};
```

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

## `SequenceEngine` — публічний API

`class Engine : public modesp::BaseModule`. Конструктор за замовчуванням:
`SequenceEngine(SharedState* state = nullptr)`.

### Створення та підключення

```cpp
// Статичний екземпляр
static modesp::scenario::SequenceEngine engine;

// У main.cpp після створення:
engine.set_state(&app.state());

// Опційно: NVS-колбеки персистентності (див. [07_persistence.md](07_persistence.md))
engine.set_nvs_callbacks(&write_fn, &read_fn, user_ctx);

// Реєструємо вбудовані ОДИН РАЗ до ініціалізації будь-якого модуля
modesp::scenario::builtins::register_builtins();

// Реєструємо рушій у ModuleManager
app.modules().register_module(engine);
```

### Інтерфейс BaseModule (керується ModuleManager)

```cpp
bool on_init() override;            // скидає всі слоти, очищує арбітр
void on_update(uint32_t dt_ms) override;
                                    // тактує запущені екземпляри, публікує
                                    // дзеркальні ключі, виконує persist-сканування
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

```cpp
using NvsWriteFn = bool (*)(void* user, uint8_t slot,
                            const uint8_t* token, size_t len);
using NvsReadFn  = bool (*)(void* user, uint8_t slot,
                            uint8_t* buf, size_t* in_out_len);

void set_nvs_callbacks(NvsWriteFn write, NvsReadFn read, void* user);
EngineError try_recover(SequenceHandle h);
```

`try_recover` вимагає, щоб слот уже був у стані LOADED (рецепт повторно завантажено
після завантаження системи). Викликає колбек читання, десеріалізує токен, застосовує
стан і встановлює стан сценарію у PAUSED. Див. [07_persistence.md](07_persistence.md).

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

ResourceArbiter& arbiter();             ///< прямий доступ до арбітра (тести)
```

Усі діагностичні методи повертають безпечні значення за замовчуванням (стан IDLE,
нульові лічильники) для недійсних дескрипторів — вони ніколи не падають.

## `ActionRegistry` — одинак

Джерело: `action_registry.h`. Використовується доменними модулями для реєстрації
користувацьких дій та умов.

```cpp
class ActionRegistry {
public:
    static ActionRegistry& instance();

    bool register_action(const ActionDescriptor& d);
    bool register_condition(const ActionDescriptor& d);
                            ///< Повертає false при колізії, вичерпанні ємності
                            ///< або невідповідності hash != djb2_hash16(name).

    const ActionDescriptor* find_action(uint16_t hash) const;
    const ActionDescriptor* find_condition(uint16_t hash) const;
                            ///< nullptr, якщо не знайдено.

    size_t action_count() const;
    size_t condition_count() const;

    void clear_for_tests();  ///< лише для тестів — у продакшені не викликається
};

constexpr uint16_t djb2_hash16(const char* str) noexcept;
```

Ємність: `MAX_REGISTRY_ENTRIES = 64` на реєстр (для дій та умов окремо).
Див. [usage/03_registering_actions.md](usage/03_registering_actions.md).

## `ContinuousRegistry` — одинак (Етап 1.5)

Джерело: `continuous_behavior.h`. Зарезервовано для майбутніх реалізацій
ContinuousBehavior (PID, гістерезис, ramp). На Етапі 1 — 0 вбудованих.

```cpp
class ContinuousBehavior {
public:
    virtual void on_activate(const ActionParam* params, uint8_t n,
                             const char* string_pool, ActionContext& ctx) = 0;
    virtual void on_tick(uint32_t dt_ms, ActionContext& ctx) = 0;
    virtual void on_deactivate(ActionContext& ctx) = 0;
    virtual size_t serialize(uint8_t* buf, size_t cap) const { return 0; }
    virtual bool deserialize(const uint8_t* buf, size_t len) { return true; }
    virtual uint16_t hash() const = 0;
    virtual const char* name() const = 0;
};

class ContinuousRegistry {
public:
    using FactoryFn = ContinuousBehavior* (*)();
    static ContinuousRegistry& instance();

    bool register_factory(uint16_t hash, const char* name, FactoryFn);
    ContinuousBehavior* create(uint16_t hash);
};
```

## Вбудовані дії та умови

Джерело: `builtin_actions.h`. Реєструються через `builtins::register_builtins()`.

3 дії:
- `log {msg: string}` — діагностичний ESP_LOG
- `set_state {key: str, type: i32-enum, value: typed}` — типізований запис у SharedState
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
                          LoadedScenario& out);
uint32_t crc32_iso_hdlc(const uint8_t* data, size_t len);
```

Клієнти рушія зазвичай не торкаються `LoadedScenario` напряму — натомість
використовуйте `SequenceEngine::load_*`.

## `ResourceArbiter` — ISA-88 §5.3

Джерело: `resource_arbiter.h`. Внутрішній для рушія, але відкритий через
`engine.arbiter()` для діагностики й доступу з тестів.

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
