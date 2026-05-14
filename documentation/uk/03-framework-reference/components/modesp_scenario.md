# `modesp_scenario` — рушій сценаріїв

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_scenario.md](../../../en/03-framework-reference/components/modesp_scenario.md)

`modesp_scenario` — це рушій часу виконання, який виконує рецепти
сценаріїв. Він бере бінарний blob `.modr` (скомпільований
`tools/compile_scenario.py` з секції `scenario` маніфесту), валідує
його та крутить отримані машини фаз на кожному такті рушія. Підтримує
до 4 одночасних незалежних рецептів, кожен — до 6 паралельних треків,
з вбудованими діями, умовами та неперервними поведінками.

Ця сторінка — оглядове резюме; повне занурення — у перенесеному
підкаталозі [scenario-engine/](../scenario-engine/) (10 архітектурних
документів + 8 ADR + посібники з використання).

ЗАЛЕЖНОСТІ: `modesp_core`, `marcel-cd__etlcpp`. Опціонально
`nvs_flash` для observer-а персистентності.

## Поверхня публічного API

```cpp
#include "modesp/scenario/engine.h"

namespace modesp::scenario {

class Engine : public modesp::BaseModule {
public:
    Engine(IStateBackend& state,
           ActionRegistry& actions,
           ContinuousRegistry& continuous,
           etl::span<IEngineObserver*> observers = {});

    SequenceHandle load_buffer(const uint8_t* data, size_t size);
    SequenceHandle load_path(const char* path);
    EngineError    unload(SequenceHandle h);

    EngineError start(SequenceHandle h);
    EngineError pause(SequenceHandle h);
    EngineError resume(SequenceHandle h);
    EngineError abort(SequenceHandle h, uint8_t reason_code = 0);
    EngineError try_recover(SequenceHandle h, NvsObserver& nvs);

    // Diagnostic accessors
    SequenceRuntime::State state(SequenceHandle h) const;
    uint32_t scenario_elapsed_ms(SequenceHandle h) const;
    uint8_t  track_count(SequenceHandle h) const;
    TrackRuntime::State track_state(SequenceHandle h, TrackIdx t) const;
    uint8_t  track_phase_idx(SequenceHandle h, TrackIdx t) const;
    uint32_t track_phase_elapsed_ms(SequenceHandle h, TrackIdx t) const;
    uint8_t  active_count() const;
};

}
```

Рушій конструюється з впровадженням залежностей: бекенд стану (зазвичай
адаптер `SharedStateBackend`, що обгортає глобальний SharedState),
реєстри дій і неперервних поведінок (належать викликачу, заповнюються
вбудованими + власними реєстраціями) та span спостерігачів
(`NvsObserver` для персистентності плюс будь-які власні).

Пріоритет ініціалізації: HIGH (1), у Phase 2 — запускається після
Wi-Fi і HAL, але перед бізнес-модулями, що можуть завантажувати
рецепти.

## Структура компонента

```
components/modesp_scenario/include/modesp/scenario/
├── engine.h                ← Public engine class
├── action_registry.h       ← ActionRegistry (custom actions + conditions)
├── continuous_behavior.h   ← ContinuousRegistry + ContinuousBehavior base
├── continuous_primitives.h ← PID, Hysteresis, Ramp built-ins
├── builtin_actions.h       ← register_builtins() entry point
├── action_param.h          ← POD types (ActionParam, ActionContext, ActionStatus)
├── runtime_types.h         ← SequenceRuntime, TrackRuntime, state enums
├── engine_error.h          ← EngineError enum
├── i_state_backend.h       ← IStateBackend interface (DI seam)
├── i_engine_observer.h     ← IEngineObserver interface
├── nvs_observer.h          ← NvsObserver impl (NVS persistence)
├── nvs_token.h             ← 96-byte token format
├── modr_format.h           ← Binary .modr wire format
├── modr_loader.h           ← Validator i LoadedScenario view
└── resource_arbiter.h      ← ISA-88 §5.3 resource claims

components/modesp_scenario/private/
├── instance.h              ← Per-instance FSM
├── track.h                 ← Per-track FSM
└── mirror.h                ← Mirror keys publisher (direct call)

components/modesp_scenario/src/
├── core/      engine.cpp, instance.cpp, track.cpp, modr_loader.cpp
├── actions/   action_registry.cpp, builtin_actions.cpp
├── continuous/continuous_registry.cpp, continuous_primitives.cpp
├── arbiter/   resource_arbiter.cpp
└── observers/ mirror.cpp, nvs_observer.cpp, nvs_token.cpp
```

## Архітектура в одному параграфі

Рушій — це багатоінстансний виконавець сценаріїв. Кожен слот володіє
одним завантаженим blob `.modr` і його станом часу виконання
(`SequenceRuntime`). На кожному такті рушія: інстанс кожного
завантаженого слота тактується → інстанс тактує кожен трек у порядку
оголошення → трек обчислює переходи та дії входу поточної фази. Дії та
умови розв'язуються через uint16-хеш djb2 у впровадженому
ActionRegistry. Дзеркальні ключі (`<recipe>.scenario_state`,
`<recipe>.<track>_phase_name` тощо) пишуться у SharedState на кожному
такті прямим викликом. Персистентність NVS обробляється спостерігачем,
що слухає старт сценарію, вхід у фазу та термінальні події.

## Вбудовані дії та умови

Надаються через `register_builtin_actions(registry)`:

**Дії (3):** `log`, `set_state`, `wait_ms`.

**Умови (10 листових + 3 композитні):** `time_elapsed_ms`,
`state_key_eq`/`_ne`/`_lt`/`_gt`/`_le`/`_ge`, `state_key_in_range`,
`state_key_changed`, `time_of_day_eq`, `all_of`, `any_of`, `not`.

Див. [02-module-author-guide/recipe-actions.md](../../02-module-author-guide/recipe-actions.md).

## Неперервні примітиви

Надаються через `register_primitives(continuous_registry)`:

- `pid` — PID у паралельній формі з anti-windup і похідною за виміром.
- `hysteresis` — релейний з симетричною зоною нечутливості.
- `ramp` — лінійна інтерполяція від початкового до кінцевого значення
  за тривалість.

Див. [02-module-author-guide/continuous-behaviors.md](../../02-module-author-guide/continuous-behaviors.md).

## Підключення в main.cpp

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/builtin_actions.h"
#include "modesp/scenario/continuous_primitives.h"
#include "modesp/scenario/nvs_observer.h"
#include "shared_state_backend.h"  // local adapter (main/)

static modesp::scenario::ActionRegistry     scenario_actions;
static modesp::scenario::ContinuousRegistry scenario_continuous;

// In app_main, after app.state() is available:
static SharedStateBackend                shared_state_backend{app.state()};
static modesp::scenario::NvsObserver     scenario_nvs_obs{
    seq_nvs_write, seq_nvs_read, nullptr};
static modesp::scenario::IEngineObserver* scenario_obs_list[] = { &scenario_nvs_obs };
static modesp::scenario::Engine          scenario_engine{
    shared_state_backend,
    scenario_actions,
    scenario_continuous,
    scenario_obs_list};

scenario_nvs_obs.bind_engine(scenario_engine);
modesp::scenario::builtins::register_builtins(scenario_actions);
modesp::scenario::primitives::register_primitives(scenario_continuous);

app.modules().register_module(scenario_engine);
http_service.set_scenario_engine(&scenario_engine);
```

## Ключі стану (рівень рушія)

| Ключ | Примітки |
|---|---|
| `scenario.engine_active_count` | Кількість запущених сценаріїв. |
| `scenario.engine_active_tracks` | Загальна кількість активних треків по всіх сценаріях. |
| `scenario.engine_recovery_pending` | true, якщо відновлений сценарій очікує ручного відновлення. |

Дзеркальні ключі для конкретних рецептів (оголошені у маніфесті
кожного рецепта): `<recipe>.scenario_state`,
`<recipe>.<track>_phase_name` тощо.

## HTTP API

Кінцеві точки рушія у `modesp_net`:

| Кінцева точка | Призначення |
|---|---|
| `GET /api/scenario/list` | Усі завантажені сценарії + стани. |
| `GET /api/scenario/info?handle=N` | Деталі по сценарію. |
| `POST /api/scenario/load` | Завантажити `.modr` за шляхом. |
| `POST /api/scenario/start` / `pause` / `resume` / `abort` / `unload` | Життєвий цикл. |

Повний HIL-тест: [test_hil_scenario.py](../../../../tools/tests/test_hil_scenario.py).

## Пам'ять і ресурси

| Елемент | Вартість |
|---|---|
| Рушій з 4 слотами | ~16 КБ (слоти включають буфер `.modr` до MODR_MAX_SIZE = 16 КБ кожен) |
| ActionRegistry з мапами на 64 записи | ~6 КБ |
| ContinuousRegistry з 32 записами | ~2 КБ |
| ResourceArbiter з мапою на 32 записи | ~1 КБ |
| NvsObserver | ~256 байт |

Типове MAX_SEQUENCES=2 (Kconfig); кожен слот заздалегідь виділяє
буфер 16 КБ. Підніміть до 4 для продакшен-розгортань з кількома
рецептами; вартість ~64 КБ RAM.

## Глибше занурення

Перенесена документація рушія сценаріїв живе у
[scenario-engine/](../scenario-engine/):

- `00_overview.md` — що він робить і для кого.
- `01_architecture.md` — внутрішня архітектура з діаграмами.
- `02_binary_format.md` — байтовий формат `.modr` з таблицями.
- `03_api_reference.md` — повна поверхня C++ API.
- `04_state_machines.md` — кінцеві автомати сценарію + треку з діаграмами.
- `05_synchronization.md` — крос-трекова синхронізація за порядком тактів.
- `06_resource_arbitration.md` — відображення ISA-88 §5.3.
- `07_persistence.md` — структура NVS, політика запису.
- `08_lifecycle.md` — життєвий цикл збірки + часу виконання.
- `09_manifest_integration.md` — конвеєр «рецепт як маніфест».
- `10_error_model.md` — таксономія помилок рушія.

Плюс 8 ADR, що документують неочевидні рішення, і 3 посібники з
використання.

> ℹ️ **Примітка:** документи scenario-engine перенесені зі старого
> місця `modesp_sequence`; вони фактично актуальні, але говорять зі
> старою назвою класу рушія. Перебудова Phase 0/1/2 перейменувала
> `modesp_sequence` → `modesp_scenario` І `SequenceEngine` → `Engine`.
> Документи здебільшого автоматично прочищені; якщо ви знайдете
> застарілі посилання — виправляйте на місці.

## Що далі

- **[scenario-engine/](../scenario-engine/)** — глибокі занурення.
- **[02-module-author-guide/recipe-authoring.md](../../02-module-author-guide/recipe-authoring.md)**
  — посібник з боку автора для написання рецептів.
- **[02-module-author-guide/recipe-actions.md](../../02-module-author-guide/recipe-actions.md)**
  — каталог дій і умов.
- **[02-module-author-guide/continuous-behaviors.md](../../02-module-author-guide/continuous-behaviors.md)**
  — PID / гістерезис / лінійне наростання.
- **[modules/abs_test.md](../modules/abs_test.md)** *(заплановано)* —
  еталонний рецепт.

## Джерела

- [`components/modesp_scenario/`](../../../../components/modesp_scenario/) — реалізація.
- [`tools/compile_scenario.py`](../../../../tools/compile_scenario.py) —
  компілятор часу збірки.
- [`tools/known_actions.json`](../../../../tools/known_actions.json) —
  каталог аудиту дій.
