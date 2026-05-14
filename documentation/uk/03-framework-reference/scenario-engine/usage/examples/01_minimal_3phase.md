# Приклад 01: Мінімальний 3-фазний рецепт з однією доріжкою

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/usage/examples/01_minimal_3phase.md](../../../../../en/03-framework-reference/scenario-engine/usage/examples/01_minimal_3phase.md)

Найпростіший нетривіальний рецепт — одна доріжка, що проходить через три фази
з переходами за часом. Демонструє основний патерн написання без складнощів
синхронізації між доріжками.

## Рецепт (`modules/min_3p/manifest.json`)

```jsonc
{
  "manifest_version": 1,
  "module": "min_3p",
  "module_type": "recipe",
  "version": "1.0.0",
  "priority": 5,
  "description": "Three-phase warmup → soak → cooldown demo",

  "state": {
    "min_3p.scenario_state":     {"type": "string", "access": "read"},
    "min_3p.scenario_elapsed_s": {"type": "int",    "access": "read"},
    "min_3p.last_error":         {"type": "int",    "access": "read"},
    "min_3p.main_state":         {"type": "string", "access": "read"},
    "min_3p.main_phase_name":    {"type": "string", "access": "read"},
    "min_3p.main_phase_idx":     {"type": "int",    "access": "read"},
    "min_3p.main_elapsed_s":     {"type": "int",    "access": "read"}
  },

  "ui": {
    "page": "Demo",
    "icon": "play",
    "cards": [{
      "title": "Mini 3-phase demo",
      "layout": "single",
      "visible_when": {"min_3p.scenario_state": ["running", "paused", "completed"]},
      "widgets": [
        {"key": "min_3p.scenario_state",  "widget": "value"},
        {"key": "min_3p.main_phase_name", "widget": "value"}
      ]
    }]
  },

  "scenario": {
    "default_phase_timeout_ms": 60000,
    "completion_rule": "all_tracks_complete",
    "tracks": [{
      "name": "main",
      "flags": ["main_track"],
      "phases": [
        {
          "name": "warmup",
          "timeout_ms": 5000,
          "entry": [
            {"action": "log",       "params": {"msg": "warmup begins"}},
            {"action": "set_state", "params": {"key": "demo.heater", "type": "bool", "value": true}}
          ],
          "transitions": [
            {"to": "soak", "when": {"time_elapsed_ms": 2000}}
          ]
        },
        {
          "name": "soak",
          "timeout_ms": 10000,
          "entry": [
            {"action": "log",       "params": {"msg": "soak phase"}},
            {"action": "set_state", "params": {"key": "demo.fan", "type": "bool", "value": true}}
          ],
          "transitions": [
            {"to": "cooldown", "when": {"time_elapsed_ms": 3000}}
          ]
        },
        {
          "name": "cooldown",
          "timeout_ms": 5000,
          "entry": [
            {"action": "log",       "params": {"msg": "cooldown — heater off"}},
            {"action": "set_state", "params": {"key": "demo.heater", "type": "bool", "value": false}},
            {"action": "set_state", "params": {"key": "demo.fan",    "type": "bool", "value": false}}
          ],
          "transitions": [
            {"to": "$complete", "when": {"time_elapsed_ms": 2000}}
          ]
        }
      ]
    }]
  }
}
```

## Покрокове пояснення

### Послідовність фаз

```
warmup (2s) ─time_elapsed_ms→ soak (3s) ─time_elapsed_ms→ cooldown (2s) ─→ $complete
```

Загальна тривалість сценарію: ~7 секунд. `default_phase_timeout_ms` сценарію
(60 с) — це верхня межа безпеки; per-phase `timeout_ms` (5 с / 10 с / 5 с) —
жорсткіші ліміти; якщо переходи не спрацьовують до того часу, фаза автоматично
провалюється.

### Що рушій записує у SharedState

Рушій записує дзеркальні ключі після входу у кожну фазу:

| Ключ | Значення під час warmup | під час soak | під час cooldown |
|-----|-------|------|-------|
| `min_3p.scenario_state` | "running" | "running" | "running" |
| `min_3p.main_phase_name` | "warmup" | "soak" | "cooldown" |
| `min_3p.main_phase_idx` | 0 | 1 | 2 |
| `min_3p.main_elapsed_s` | 0..2 | 0..3 | 0..2 |

Власні `entry`-дії рецепта записують `demo.heater`, `demo.fan` — це НЕ
дзеркальні ключі рушія; автор рецепта декларує їх у інших модулях
(або не декларує, погоджуючись на семантику last-write-wins з боку
бізнес-модулів).

### Компіляція

```bash
python tools/compile_scenario.py --recipe modules/min_3p/manifest.json \
                                 --output data/scenarios/min_3p.modr
```

Результат: невеликий `.modr` (~250 байт для цього рецепта). Перевірено
через `tools/tests/test_compile_scenario.py` (golden round-trip).

### Запуск

```cpp
auto h = engine.load_path("/data/scenarios/min_3p.modr");
engine.start(h);
// ~7 seconds later: engine.state(h) == COMPLETED
```

## Поширені варіації

**Ручний тригер замість таймера:** замініть `time_elapsed_ms` на
`state_key_eq`, що читає стан кнопки UI:

```jsonc
{"to": "soak", "when": {"state_key_eq": {"key": "ui.start_btn", "value": true}}}
```

**І таймер І кнопка:** використовуйте `all_of`:

```jsonc
{"to": "soak", "when": {"all_of": [
  {"time_elapsed_ms": 2000},
  {"state_key_eq": {"key": "ui.confirm", "value": true}}
]}}
```

**Або таймер АБО кнопка:** використовуйте `any_of` — повертає true, коли
спрацьовує будь-що з них:

```jsonc
{"to": "soak", "when": {"any_of": [
  {"time_elapsed_ms": 30000},
  {"state_key_eq": {"key": "ui.skip_btn", "value": true}}
]}}
```

## Дивіться також

- [02_dual_track_sync.md](02_dual_track_sync.md) — приклад з кількома
  доріжками та синхронізацією між ними
- [02_writing_recipes.md](../02_writing_recipes.md) — повний словник дій
  та умов
