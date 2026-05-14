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
    modesp::scenario::SequenceEngine* engine_;
    modesp::scenario::SequenceHandle handle_ = 0;

public:
    void set_engine(modesp::scenario::SequenceEngine* e) { engine_ = e; }

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

Інтеграція у `main.cpp` (одноразово під час boot — див. Step 16):

```cpp
#include "modesp/scenario/engine.h"
#include "modesp/scenario/builtin_actions.h"

static modesp::scenario::SequenceEngine sequence_engine(&app.state());
static MyBusinessModule biz_module;
biz_module.set_engine(&sequence_engine);

// Register builtins ONCE before any module init runs
modesp::scenario::builtins::register_builtins();

// Register engine before business modules що залежать від нього
app.modules().register_module(sequence_engine);
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

Якщо пристрій перезавантажується посередині сценарію, рушій читає NVS-токен
у `on_init`, відновлює `phase_idx` та `phase_elapsed_ms` і переходить у стан
PAUSED. WebUI може показати банер через `visible_when: {abs_test.scenario_state: ["paused"]}`.

Продовжити:

```cpp
engine_->resume(handle_);  // continues from saved phase + elapsed_ms
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
