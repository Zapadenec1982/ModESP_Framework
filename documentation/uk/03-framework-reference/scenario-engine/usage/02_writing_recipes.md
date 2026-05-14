# Написання рецептів — настанови автора

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/usage/02_writing_recipes.md](../../../../en/03-framework-reference/scenario-engine/usage/02_writing_recipes.md)

**Статус:** Каркас (Q4 у Step 2b cleanup). Повний зміст разом із прикладами
буде заповнено разом із фінальною інтеграцією Stage 1 (Step 16) та Stage 2 (редактор WebUI).

---

## Аудиторія

Для авторів бізнес-модулів C++ та доменних інтеграторів. Рецепти описують
часозалежні алгоритми (multi-track, умовні, з перевизначеннями параметрів),
які виконуються рушієм `SequenceEngine`.

## Швидкий старт

Рецепт = модуль ModESP із `module_type: "recipe"` плюс секція scenario.

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_xxx",         // ≤ 12 chars (32-char SharedState budget)
  "module_type": "recipe",
  "version": "1.0.0",
  "priority": 5,
  "state": { ... },               // mirror keys engine writes runtime
  "scenario": { ... }             // NEW section — compile_scenario.py emits .modr
}
```

## Структура scenario

```jsonc
"scenario": {
  "default_phase_timeout_ms": 60000,
  "scenario_timeout_max_ms": 0,        // 0 = unlimited
  "completion_rule": "all_tracks_complete",  // | "any_track_complete" | "main_track_complete"
  "params": { ... },                   // optional: recipe-level configurable values
  "resources": [ ... ],                // optional: scenario-scope resources (claim at start)
  "global_transitions": [ ... ],       // optional: priority-sorted abort triggers
  "tracks": [ ... ]                    // 1..6 parallel tracks
}
```

## Доріжки та фази

Кожна доріжка має власну послідовність фаз:

```jsonc
"tracks": [
  {
    "name": "main",
    "flags": ["main_track"],            // optional: ["main_track", "loop_on_complete"]
    "phases": [
      {
        "name": "warmup",
        "timeout_ms": 10000,             // 0 → use default_phase_timeout_ms
        "entry": [ ...actions... ],
        "exit": [ ...actions... ],
        "transitions": [ ...transitions... ],
        "continuous": [],                // ContinuousBehaviors active during phase
        "phase_resources": []            // phase-scope resource claims
      }
    ]
  }
]
```

## Вбудовані дії

| Дія | Параметри | Опис |
|---|---|---|
| `log` | `msg` (string) | Записує діагностичне повідомлення через ESP_LOG_INFO |
| `set_state` | `key` (string), `type` (`i32`/`f32`/`bool`), `value` | Записує ключ SharedState |
| `wait_ms` | `ms` (i32) | Чекає вказану кількість мс (повертає PENDING до завершення) |

## Вбудовані умови

Композиційні (вкладені):
- `all_of: [<cond>, ...]` — усі правдиві
- `any_of: [<cond>, ...]` — хоча б одна правдива
- `not: <cond>` — інверсія

Скалярні:
- `time_elapsed_ms: int` — мс з моменту входу у фазу
- `state_key_eq` / `_ne` / `_lt` / `_gt` / `_le` / `_ge` (`{key, value}`)
- `state_key_in_range: {key, min, max}`
- `state_key_changed: {key}` — детектування зміни
- `time_of_day_eq: {hh, mm}` — збіг з настінним годинником (потребує SNTP)

Максимальна глибина вкладеності: 16 (захист від DoS).

## Переходи

```jsonc
"transitions": [
  {"to": "$complete"},                                 // unconditional
  {"to": "next_phase", "when": {"time_elapsed_ms": 5000}},
  {"to": "$abort", "when": {"all_of": [
      {"state_key_eq": {"key": "safety.fault", "value": true}},
      {"time_elapsed_ms": 100}
  ]}}
]
```

Спеціальні цілі:
- `$complete` — завершити цю доріжку
- `$abort` — перервати весь сценарій (через completion_rule)

## Параметри рецепта (`@param:`)

Параметри декларуються на рівні сценарію зі значеннями за замовчуванням.
Редактор WebUI (Stage 2) використовуватиме це для рендерингу форм.
Підстановка під час компіляції — рушій бачить лише літерали.

```jsonc
"scenario": {
  "params": {
    "moisture_low": {"type": "f32", "default": 40.0, "min": 0, "max": 100, "overridable": true},
    "watering_duration_ms": {"type": "i32", "default": 600000, "min": 60000, "max": 3600000}
  },
  "tracks": [{
    "phases": [{
      "transitions": [
        {"to": "watering", "when": {
          "state_key_lt": {"key": "sensor.moisture", "value": "@param:moisture_low"}
        }}
      ]
    }]
  }]
}
```

`overridable: true` — підказка для редактора WebUI (показати у формі редагування).
Рушій не бачить параметрів окремо — лише літеральні значення після підстановки.

## Дзеркальні ключі стану (перехресна валідація)

Рушій автоматично записує такі ключі стану (для рецепта з двома доріжками "a", "b"):

```
recipe_X.scenario_state    (string)    "idle" | "running" | "completed" | ...
recipe_X.scenario_elapsed_s (int)
recipe_X.last_error        (int)
recipe_X.a_state           (string)
recipe_X.a_phase_name      (string)
recipe_X.a_phase_idx       (int)
recipe_X.a_elapsed_s       (int)
recipe_X.b_state           (string)
... (similar для b)
```

**Усі ці ключі ПОВИННІ бути задекларовані у `manifest.state`** з правильним
типом. Невідповідність → помилка компіляції E0401 (відсутній) або E0403 (неправильний тип).

```jsonc
"state": {
  "recipe_X.scenario_state":     {"type": "string", "access": "read"},
  "recipe_X.scenario_elapsed_s": {"type": "int",    "access": "read"},
  "recipe_X.last_error":         {"type": "int",    "access": "read"},
  "recipe_X.a_state":            {"type": "string", "access": "read"},
  "recipe_X.a_phase_name":       {"type": "string", "access": "read"},
  "recipe_X.a_phase_idx":        {"type": "int",    "access": "read"},
  "recipe_X.a_elapsed_s":        {"type": "int",    "access": "read"}
  // ... аналогічно для всіх tracks
}
```

## Бюджет імен

- Ім'я рецепта (модуля) ≤ 12 символів
- Ім'я доріжки ≤ 8 символів
- Ім'я фази ≤ 16 символів
- Загальна довжина дзеркального ключа ≤ 32 символи (SharedState `MODESP_MAX_KEY_LENGTH`)

## Ресурси (ISA-88 §5.3)

Рівень сценарію (захоплюються при старті):
```jsonc
"resources": [
  {"resource": "equipment.heater", "exclusive": true}
]
```

Рівень фази (захоплюються при вході у фазу, звільняються при виході):
```jsonc
"phases": [{
  "name": "watering",
  "phase_resources": [{"resource": "equipment.pump", "exclusive": true}]
}]
```

## Глобальні переходи

Перевіряються на кожному тіку через усі доріжки перед per-phase переходами.
Сортуються за пріоритетом у спадному порядку. Завжди мають abort-ціль
(бінарний формат не має поля target_phase).

```jsonc
"global_transitions": [
  {"when": {"state_key_eq": {"key": "safety.fault", "value": true}},
   "priority": 255, "scope": "abort_scenario"},
  {"when": {"state_key_eq": {"key": "ui.user_abort", "value": true}},
   "priority": 200}
]
```

## Компіляція та валідація

```bash
# Single recipe
python tools/compile_scenario.py --recipe modules/recipe_X/manifest.json --output recipe_X.modr

# All recipe modules
python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios

# Strict mode (CI)
python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios --strict
```

`--strict` підвищує W0220 (невідома дія) та W0230 (невідомий ContinuousBehavior)
до помилок. Використовуйте у CI.

## Інспекція скомпільованого бінарного файлу

```bash
python tools/dump_modr.py data/scenarios/recipe_X.modr
python tools/dump_modr.py --hex recipe_X.modr   # +raw bytes
```

## Перехресні посилання

- [`02_binary_format.md`](../02_binary_format.md) — байтова розкладка `.modr`
- [`05_synchronization.md`](../05_synchronization.md) — синхронізація між доріжками за порядком тіків
- [`09_manifest_integration.md`](../09_manifest_integration.md) — повний каталог кодів помилок, build pipeline
- [`10_error_model.md`](../10_error_model.md) — опис кодів помилок
- ADR-0004 — обґрунтування «рецепт як маніфест»
- ADR-0005 — арбітраж ресурсів ISA-88 §5.3

---

**TODO для повного змісту (заповнюється разом зі Stage 1 Step 16 та Stage 2):**
- Опрацьовані приклади (мінімальний 3-фазний, синхронізація двох доріжок, параметризована теплиця)
- Поширені патерни (debounce, гістерезис, watchdog таймауту)
- Антипатерни (циклічні очікування, гонки доріжок, витоки ресурсів)
- Усунення поширених помилок компілятора
- Тестування рецептів (юніт-тести, налаштування HIL-тестів)
- Міграція з ручної state machine на рецепт
