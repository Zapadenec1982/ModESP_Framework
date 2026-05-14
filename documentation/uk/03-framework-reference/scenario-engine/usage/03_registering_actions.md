# Реєстрація користувацьких дій — посібник для доменних модулів

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/usage/03_registering_actions.md](../../../../en/03-framework-reference/scenario-engine/usage/03_registering_actions.md)

Бізнес-модулі ModESP, якими має керувати сценарій, додають доменно-специфічні
дії та умови до ActionRegistry. Вбудований словник (`log`, `set_state`,
`wait_ms` плюс 10 leaf-умов) свідомо не залежить від конкретного обладнання;
фактичне керування обладнанням живе у доменних модулях.

## Коли реєструвати

Вбудовані дії обробляють:
- Логування (`log`)
- Запис у SharedState (`set_state` — встановлює типізовані значення, які інші
  модулі спостерігають через `state.get()`)
- Часові затримки (`wait_ms`)

Вбудовані дії НЕ підходять для:
- Прямого доступу до HAL (встановлення GPIO, читання ADC, налаштування I²C)
- Доменної логіки (setpoint PID-регулятора, смуга гістерезису, профіль ramp)
- Багатокрокових робочих процесів, які потребують внутрішнього стану дії

Для цього ваш модуль реєструє користувацькі дії.

## Патерн реєстрації

`ActionRegistry` **не** є синглтоном — це інстанс, яким володіє викликач:
`main.cpp` створює його та інжектить у рушій. Доменні модулі мають отримати
**той самий** `ActionRegistry&` (зазвичай через сетер або аргумент
конструктора), щоб їхні реєстрації потрапили в реєстр, який рушій буде
використовувати для пошуку.

План викликає `builtins::register_builtins(reg)` один раз у `main.cpp`
перед ініціалізацією модулів — реєстрації вашого модуля відбуваються
після цього, у `on_init()`.

```cpp
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"

class MulticookerModule : public modesp::BaseModule {
public:
    MulticookerModule() : BaseModule("mc", modesp::ModulePriority::NORMAL) {}

    /// Інжекція того самого ActionRegistry, який main.cpp передав у рушій.
    /// Має бути викликано до on_init().
    void set_action_registry(modesp::scenario::ActionRegistry& reg) { reg_ = &reg; }

    bool on_init() override {
        using namespace modesp::scenario;
        if (!reg_) {
            ESP_LOGE("mc", "ActionRegistry не інжектовано");
            return false;
        }

        bool ok = true;
        ok &= reg_->register_action({
            djb2_hash16("mc.set_target_temp"),
            "mc.set_target_temp",
            &MulticookerModule::action_set_target_temp,
            /*param_min=*/1, /*param_max=*/1
        });
        ok &= reg_->register_action({
            djb2_hash16("mc.start_pid"),
            "mc.start_pid",
            &MulticookerModule::action_start_pid,
            /*param_min=*/0, /*param_max=*/0
        });
        ok &= reg_->register_condition({
            djb2_hash16("mc.temp_within"),
            "mc.temp_within",
            &MulticookerModule::cond_temp_within,
            /*param_min=*/2, /*param_max=*/2
        });

        if (!ok) {
            ESP_LOGE("mc", "Action registration failed");
            return false;
        }
        return true;
    }

private:
    modesp::scenario::ActionRegistry* reg_ = nullptr;

    static modesp::scenario::ActionStatus action_set_target_temp(
        modesp::scenario::ActionContext& ctx);
    static modesp::scenario::ActionStatus action_start_pid(
        modesp::scenario::ActionContext& ctx);
    static modesp::scenario::ActionStatus cond_temp_within(
        modesp::scenario::ActionContext& ctx);
};
```

У `main.cpp` той самий caller-owned реєстр інжектиться і в рушій,
і в доменний модуль:

```cpp
static modesp::scenario::ActionRegistry actions;
static modesp::scenario::Engine engine{sb, actions, continuous, obs_list};

modesp::scenario::builtins::register_builtins(actions);

static MulticookerModule mc_module;
mc_module.set_action_registry(actions);
```

## Сигнатура функції дії

Усі обробники дій та умов мають однакову сигнатуру:

```cpp
ActionStatus (*ActionFn)(ActionContext& ctx);
```

`ActionContext` надає все, що потрібно обробнику:

```cpp
struct ActionContext {
    SharedState*    state;            // read/write SharedState
    const ActionParam* params;        // recipe-provided typed params
    uint8_t         param_count;
    const char*     string_pool;      // for STR-type params
    uint16_t        string_pool_size;
    uint32_t        scenario_elapsed_ms;
    uint32_t        phase_elapsed_ms;
    uint8_t         phase_idx;
    SequenceHandle  handle;           // owning scenario instance
    TrackIdx        track;            // owning track index
    const char*     recipe_name;      // diagnostic
    const char*     track_name;       // diagnostic
};
```

## Читання параметрів

Автор рецепта викликає дію з іменованими параметрами:

```jsonc
{"action": "mc.set_target_temp", "params": {"temp": 85.5}}
```

`compile_scenario.py` упаковує це в ActionParam[] з `key_hash = djb2("temp")`.
Обробник дії шукає за хешем:

```cpp
ActionStatus MulticookerModule::action_set_target_temp(
    modesp::scenario::ActionContext& ctx) {
    using namespace modesp::scenario;

    // Validate param count (also enforced by registry's param_min/max)
    if (ctx.param_count != 1) return ActionStatus::FAILED_ABORT;

    // Find param by name hash. Linear scan на short arrays — params typically
    // 1-3, faster than hashmap lookup для це size.
    const ActionParam* p_temp = nullptr;
    for (uint8_t i = 0; i < ctx.param_count; ++i) {
        if (ctx.params[i].key_hash == djb2_hash16("temp")) {
            p_temp = &ctx.params[i];
            break;
        }
    }
    if (!p_temp || p_temp->type != static_cast<uint8_t>(ParamType::F32)) {
        return ActionStatus::FAILED_ABORT;  // recipe author error
    }

    float temp = p_temp->v.f;
    if (temp < 0.0f || temp > 200.0f) {
        // Out-of-range value — recoverable; log + continue з transitions
        ESP_LOGW("mc", "target_temp %.1f° out of range", temp);
        return ActionStatus::FAILED_RECOVERABLE;
    }

    // Write target to SharedState — actual hardware module reads це і drives
    // PWM/relay accordingly
    ctx.state->set("mc.target_temp", temp);
    return ActionStatus::OK;
}
```

## Читання параметрів типу STR

Параметри типу string зберігають зміщення у пулі рядків рецепта.
Скористайтеся допоміжним кодом:

```cpp
char keybuf[32];
const auto* p_key = /* find param із hash djb2("key"), type STR */;
uint16_t off = p_key->v.s_idx;
// Manually walk pool: byte at offset = length, followed by raw bytes
if (off >= ctx.string_pool_size) return ActionStatus::FAILED_ABORT;
uint8_t len = static_cast<uint8_t>(ctx.string_pool[off]);
if (off + 1 + len > ctx.string_pool_size) return ActionStatus::FAILED_ABORT;
if (len + 1u > sizeof(keybuf)) return ActionStatus::FAILED_ABORT;
std::memcpy(keybuf, &ctx.string_pool[off + 1], len);
keybuf[len] = '\0';
// keybuf тепер null-terminated string
```

(Допоміжна функція `copy_string` вбудованої дії `set_state` у
`builtin_actions.cpp` демонструє цей патерн.)

## Повернення ActionStatus

Згідно з машиною політики помилок дій з плану Q12:

| Статус | Поведінка рушія (дії entry/exit) | Поведінка рушія (continuous tick) |
|---|---|---|
| `OK` | Перейти до наступної дії; оцінити переходи після виконання всіх | Продовжити continuous behavior |
| `PENDING` | Викликати ту саму дію на наступному тіку (ескалація після ~1с) | Викликати на наступному тіку |
| `FAILED_RECOVERABLE` | Пропустити решту дій у фазі, продовжити з переходами | Деактивувати ContinuousBehavior |
| `FAILED_ABORT` | Track → TRACK_FAILED, сценарій переривається якщо main_track | Track → TRACK_FAILED |

Обирайте обережно:
- **Програмна помилка** (неправильний тип параметра, відсутній обов'язковий параметр):
  `FAILED_ABORT` — це баг автора рецепта, провалюйтесь голосно, щоб його зловили
  під час розробки
- **Значення поза діапазоном, тимчасова проблема обладнання**: `FAILED_RECOVERABLE` —
  залогуйте, пропустіть, нехай переходи рецепта обробляють це
- **Довга операція** (наприклад, очікування конвертації DS18B20 ~750 мс):
  `PENDING` — рушій викличе наступного тіку. Корисно для послідовностей I²C/SPI
- **Порушення безпеки** (наприклад, небезпечна температура): `FAILED_ABORT` —
  доріжка переходить у термінальний стан, обладнання, яке залежало від виходу
  цієї дії, повинно мати безпечні значення за замовчуванням

## Обробка колізій хешу

`djb2_hash16` створює uint16 — парадокс днів народження каже про ~256 імен
до ймовірності колізій ≥50%. Більшість проєктів залишаються нижче 64 дій/умов.
При колізії:

1. `register_action` ActionRegistry повертає false (колізія виявлена
   під час вставки)
2. `compile_scenario.py` також виявляє через таблицю пошуку
   `tools/known_actions.json` (MVP: підтримується вручну — перелічуйте
   усі зареєстровані імена дій)

Розв'язання: перейменуйте одну з конфліктних дій. Конвенція: префікс
з ім'ям модуля (`mc.set_target_temp`, а не `set_target_temp`) зменшує
ймовірність колізій і допомагає діагностиці (ви одразу бачите, який
модуль володіє дією).

## Опрацьований приклад: мінімальний, але повний

`modules/mc_demo/manifest.json`:

```jsonc
{
  "manifest_version": 1,
  "module": "mc_demo",
  "module_type": "module",
  "version": "1.0.0",
  "priority": 5,
  "state": {
    "mc.target_temp": {"type": "float", "access": "read", "min": 0, "max": 200},
    "mc.current_temp": {"type": "float", "access": "read", "min": -20, "max": 250}
  }
}
```

`modules/mc_demo/src/mc_demo_module.cpp`:

```cpp
#include "modesp/base_module.h"
#include "modesp/shared_state.h"
#include "modesp/scenario/action_registry.h"
#include "modesp/scenario/action_param.h"
#include "esp_log.h"

class McDemoModule : public modesp::BaseModule {
public:
    McDemoModule() : BaseModule("mc_demo", modesp::ModulePriority::NORMAL) {}

    void set_action_registry(modesp::scenario::ActionRegistry& reg) { reg_ = &reg; }

    bool on_init() override {
        using namespace modesp::scenario;
        if (!reg_) return false;

        bool ok = true;
        ok &= reg_->register_action({
            djb2_hash16("mc.set_target_temp"),
            "mc.set_target_temp",
            &action_set_target_temp,
            1, 1
        });
        ok &= reg_->register_condition({
            djb2_hash16("mc.temp_reached"),
            "mc.temp_reached",
            &cond_temp_reached,
            1, 1
        });
        return ok;
    }

private:
    modesp::scenario::ActionRegistry* reg_ = nullptr;
    using AS = modesp::scenario::ActionStatus;
    using AC = modesp::scenario::ActionContext;
    using AP = modesp::scenario::ActionParam;
    using PT = modesp::scenario::ParamType;

    static const AP* find_param(AC& ctx, uint16_t key_hash) {
        for (uint8_t i = 0; i < ctx.param_count; ++i) {
            if (ctx.params[i].key_hash == key_hash) return &ctx.params[i];
        }
        return nullptr;
    }

    static AS action_set_target_temp(AC& ctx) {
        const AP* p = find_param(ctx, modesp::scenario::djb2_hash16("temp"));
        if (!p || p->type != static_cast<uint8_t>(PT::F32)) return AS::FAILED_ABORT;
        if (p->v.f < 0.0f || p->v.f > 200.0f) {
            ESP_LOGW("mc_demo", "target_temp %.1f° out of range", p->v.f);
            return AS::FAILED_RECOVERABLE;
        }
        ctx.state->set("mc.target_temp", p->v.f);
        return AS::OK;
    }

    /// Condition: true якщо |current - target| < tolerance (param "tol")
    static AS cond_temp_reached(AC& ctx) {
        const AP* p = find_param(ctx, modesp::scenario::djb2_hash16("tol"));
        if (!p || p->type != static_cast<uint8_t>(PT::F32)) return AS::FAILED_ABORT;
        auto cur = ctx.state->get("mc.current_temp");
        auto tgt = ctx.state->get("mc.target_temp");
        if (!cur.has_value() || !tgt.has_value()) return AS::FAILED_RECOVERABLE;
        auto* cf = etl::get_if<float>(&*cur);
        auto* tf = etl::get_if<float>(&*tgt);
        if (!cf || !tf) return AS::FAILED_ABORT;
        float diff = (*cf > *tf) ? (*cf - *tf) : (*tf - *cf);
        return (diff < p->v.f) ? AS::OK : AS::FAILED_RECOVERABLE;
    }
};
```

Тоді у рецепті:

```jsonc
{
  "scenario": {
    "tracks": [{
      "name": "main",
      "phases": [
        {
          "name": "warmup",
          "entry": [
            {"action": "mc.set_target_temp", "params": {"temp": 85.0}}
          ],
          "transitions": [
            {"to": "soak", "when": {"mc.temp_reached": {"tol": 1.0}}}
          ]
        },
        {"name": "soak", "transitions": [{"to": "$complete"}]}
      ]
    }]
  }
}
```

## Оновлення known_actions.json

Додайте свої зареєстровані імена та обчислені хеші до `tools/known_actions.json`:

```jsonc
{
  "actions": {
    "mc.set_target_temp": {
      "hash": 12345,    // djb2_hash16("mc.set_target_temp")
      "description": "Set multicooker PID setpoint",
      "params": {"temp": "f32"}
    }
  },
  "conditions": {
    "mc.temp_reached": {
      "hash": 23456,
      "description": "Current temp within tol of target",
      "params": {"tol": "f32"}
    }
  }
}
```

Обчислити хеш:

```bash
python -c "
def djb2_hash16(s):
    h = 5381
    for c in s.encode(): h = ((h << 5) + h + c) & 0xFFFFFFFF
    return h & 0xFFFF
print(hex(djb2_hash16('mc.set_target_temp')))"
```

`compile_scenario.py` використовує цю таблицю для валідації рецептів, які
посилаються на вашу дію — без запису рецепт отримає W0220 (невідома дія),
що `--strict` підвищує до помилки у CI.

## Дивіться також

- [02_writing_recipes.md](02_writing_recipes.md) — сторона написання рецепта
- `components/modesp_scenario/include/modesp/scenario/action_registry.h` —
  повний довідник API
- `components/modesp_scenario/src/builtin_actions.cpp` — вихідний код
  вбудованих дій (хороший шаблон для ваших власних реалізацій)
