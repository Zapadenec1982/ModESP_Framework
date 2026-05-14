# Швидкий старт — Hello, Scenario

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/usage/01_quickstart.md](../../../../en/03-framework-reference/scenario-engine/usage/01_quickstart.md)

5-хвилинне практичне знайомство для розробників бізнес-модулів ModESP. Охоплює
завантаження рецепта, його запуск, спостереження за живим станом і відновлення
після перезавантаження живлення.

## Передумови

- Налаштований toolchain ESP-IDF 5.x (`idf.py --version` працює)
- Клонований фреймворк ModESP, який чисто збирається
- Налаштовані облікові дані WiFi (або використовуйте лише serial monitor)

## 1. Напишіть рецепт

Рецепт — це модуль ModESP із `module_type: "recipe"` плюс секція `scenario`.
Розмістіть у `modules/<your_recipe>/manifest.json`. Бюджет імені рецепта:
**≤ 12 символів**, щоб укластися в обмеження ключа SharedState у 32 символи.

Еталонний рецепт знаходиться у [modules/abs_test/manifest.json](../../../modules/abs_test/manifest.json).
Основні моменти:

```jsonc
{
  "manifest_version": 1,
  "module": "abs_test",            // ≤ 12 chars
  "module_type": "recipe",         // tells generator skip C++ binding
  "version": "1.0.0",
  "priority": 5,

  "state": {
    "abs_test.scenario_state":  {"type": "string", "access": "read"},
    "abs_test.main_phase_name": {"type": "string", "access": "read"}
    // ... mirror keys engine writes runtime
  },

  "ui": { /* widgets з visible_when, standard generate_ui.py pipeline */ },

  "scenario": {
    "default_phase_timeout_ms": 30000,
    "completion_rule": "all_tracks_complete",
    "tracks": [
      { "name": "main", "flags": ["main_track"],
        "phases": [
          { "name": "phase_a",
            "entry": [{"action": "set_state",
                       "params": {"key": "test.output_a", "type": "bool", "value": true}}],
            "transitions": [{"to": "phase_b", "when": {"time_elapsed_ms": 1000}}] }
          // ...
        ]
      }
    ]
  }
}
```

Довідник вбудованих дій та умов — у [02_writing_recipes.md](02_writing_recipes.md).

## 2. Збірка

`compile_scenario.py` запускається автоматично під час `idf.py build` (CMake pre-step).
Ручний виклик для швидкої перевірки:

```bash
python tools/compile_scenario.py --recipe modules/abs_test/manifest.json \
                                 --output data/scenarios/abs_test.modr
```

Вихідний `.modr` автоматично пакується у образ розділу LittleFS.

## 3. Запуск з вашого бізнес-модуля

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/builtin_actions.h"

class MyBusinessModule : public modesp::BaseModule {
    modesp::scenario::Engine* engine_;
    modesp::scenario::SequenceHandle handle_ = 0;

public:
    void set_engine(modesp::scenario::Engine* e) { engine_ = e; }

    bool on_init() override {
        // Load recipe (engine resolves /data/scenarios/abs_test.modr)
        handle_ = engine_->load_path("/data/scenarios/abs_test.modr");
        if (handle_ == 0) {
            ESP_LOGE("biz", "load failed: %d",
                     static_cast<int>(engine_->last_error()));
            return false;
        }
        return true;
    }

    void on_some_event() {
        if (handle_ != 0
         && engine_->state(handle_) == modesp::scenario::SequenceRuntime::State::LOADED) {
            engine_->start(handle_);
        }
    }
};
```

Інтеграція у `main.cpp` (одноразово під час boot — див. Step 16). Рушій
отримує свої залежності (state backend, реєстри, спостерігачі) через
конструктор — жодних синглтонів:

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/continuous_behavior.h"
#include "modesp/scenario/continuous_primitives.h"
#include "modesp/scenario/nvs_observer.h"
#include "modesp/scenario/builtin_actions.h"
#include "shared_state_backend.h"  // адаптер для modesp::SharedState (application layer)

// State backend adapter
static SharedStateBackend sb{app.state()};

// Caller-owned реєстри (без синглтонів)
static modesp::scenario::ActionRegistry     actions;
static modesp::scenario::ContinuousRegistry continuous;

// NVS persistence observer — викликач постачає read/write коллбеки
static modesp::scenario::NvsObserver nvs_obs{nvs_write_fn, nvs_read_fn, nullptr};
static modesp::scenario::IEngineObserver* obs_list[] = {&nvs_obs};

// Engine — конструктор приймає state, реєстри та span зі спостерігачами
static modesp::scenario::Engine engine{sb, actions, continuous, obs_list};

// Наповніть реєстри ДО ініціалізації будь-яких модулів
modesp::scenario::builtins::register_builtins(actions);
// Опціонально: стандартні continuous-примітиви (PID, hysteresis, ramp).
// Доменні модулі також можуть реєструвати свої власні.
modesp::scenario::primitives::register_primitives(continuous);

// Прив'яжіть спостерігача до рушія (NvsObserver читає стан при серіалізації)
nvs_obs.bind_engine(engine);

static MyBusinessModule biz_module;
biz_module.set_engine(&engine);

// Зареєструйте рушій перед бізнес-модулями, що залежать від нього
app.modules().register_module(engine);
app.modules().register_module(biz_module);
```

## 4. Спостереження

Сторінка WebUI «Тест» (налаштована у секції `ui` рецепта) показує дзеркальні
ключі у реальному часі. Картки керуються через `visible_when`, тож приховані,
коли сценарій у стані IDLE.

Serial monitor показує переходи, залоговані через дію `log`:

```
I (12345) abs_test: main: phase_a
I (13345) abs_test: main: completing
I (14345) abs_test: watcher: started
```

## 5. Відновлення після перезавантаження живлення

Відновлення **не** є автоматичним. Після перезавантаження пристрою та
повторного завантаження сценарію (`load_path`/`load_buffer`) викликач має
явно запросити відновлення стану з NVS, викликавши `try_recover()` і
передавши той самий `NvsObserver`, що сконфігуровано у `main.cpp`. У разі
успіху `phase_idx` + `phase_elapsed_ms` відновлюються, а сценарій
переходить у стан **PAUSED** — викликач має потім явно викликати
`resume()`, щоб продовжити.

```cpp
// Після успішного load_path():
auto err = engine_->try_recover(handle_, nvs_obs);
if (err == modesp::scenario::EngineError::OK) {
    // Сценарій тепер у PAUSED на відновленій фазі / elapsed_ms.
    // WebUI може показати банер через visible_when: {abs_test.scenario_state: ["paused"]}.
    // Викликач вирішує, коли продовжити — наприклад, після підтвердження користувача:
    engine_->resume(handle_);  // продовжує з збереженої фази + elapsed_ms
}
```

Перервати:

```cpp
engine_->abort(handle_);   // tracks transition through their abort paths
```

## Наступні кроки

- [02_writing_recipes.md](02_writing_recipes.md) — повний словник дій, типи переходів, написання параметрів
- [03_registering_actions.md](03_registering_actions.md) — додавання доменно-специфічних дій
- [examples/01_minimal_3phase.md](examples/01_minimal_3phase.md) — мінімальний рецепт з однією доріжкою
- [examples/02_dual_track_sync.md](examples/02_dual_track_sync.md) — патерн синхронізації між доріжками
