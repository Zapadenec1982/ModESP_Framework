# Приклад 02: Синхронізація двох доріжок

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/usage/examples/02_dual_track_sync.md](../../../../../en/03-framework-reference/scenario-engine/usage/examples/02_dual_track_sync.md)

Дві паралельні доріжки координуються через SharedState. Демонструє семантику
порядку тіків з ADR-0003: записи доріжки 0 у межах тіка видимі для читань
доріжки 1 у тому самому тіку (порядок декларації має значення).

## Рецепт (`modules/abs_test/manifest.json`)

Еталонний рецепт Stage 1 знаходиться у
[modules/abs_test/manifest.json](../../../../modules/abs_test/manifest.json).
Нижче — відповідні фрагменти (повний файл: ~100 рядків).

### Доріжка 0: "main" — писач

```jsonc
{
  "name": "main",
  "flags": ["main_track"],
  "phases": [
    {
      "name": "phase_a",
      "timeout_ms": 10000,
      "entry": [
        {"action": "log",       "params": {"msg": "main: phase_a"}},
        {"action": "set_state", "params": {"key": "test.output_a", "type": "bool", "value": true}}
      ],
      "transitions": [{"to": "phase_b", "when": {"time_elapsed_ms": 1000}}]
    },
    {
      "name": "phase_b",
      "timeout_ms": 30000,
      "entry": [
        {"action": "set_state", "params": {"key": "test.output_a", "type": "bool", "value": false}},
        {"action": "set_state", "params": {"key": "test.output_b", "type": "bool", "value": true}}
      ],
      "transitions": [
        // Either input drives transition fast (з all_of), або 5s timer fallback
        {"to": "phase_c", "when": {"all_of": [
          {"state_key_gt": {"key": "test.input_a", "value": 10}},
          {"time_elapsed_ms": 500}
        ]}},
        {"to": "phase_c", "when": {"time_elapsed_ms": 5000}}
      ]
    },
    {
      "name": "phase_c",
      "timeout_ms": 5000,
      "entry": [
        {"action": "log", "params": {"msg": "main: completing"}},
        {"action": "set_state", "params": {"key": "test.output_b", "type": "bool", "value": false}}
      ],
      "transitions": [{"to": "$complete", "when": {"time_elapsed_ms": 500}}]
    }
  ]
}
```

### Доріжка 1: "watcher" — читач

```jsonc
{
  "name": "watcher",
  "phases": [
    {
      "name": "watching",
      "timeout_ms": 60000,
      "entry": [{"action": "log", "params": {"msg": "watcher: started"}}],
      "transitions": [
        {"to": "$complete", "when": {"state_key_eq": {
          "key": "abs_test.main_phase_name", "value": "phase_c"
        }}}
      ]
    }
  ]
}
```

Спостерігач читає записаний рушієм дзеркальний ключ
`abs_test.main_phase_name` — НЕ ключ, який пише користувач. Рушій
оновлює дзеркальні ключі при просуванні фази, перетворюючи їх на
канали публікація-підписка для координації доріжок.

## Семантика порядку тіків (ADR-0003)

Рушій тікає доріжки у порядку декларації:

```
tick N:
  ┌─ Track 0 (main) tick     — phase_a entry actions, eventually transitions to phase_b
  └─ Track 1 (watcher) tick  — reads main_phase_name (sees "phase_b" — main's update)
```

У межах одного тіка читання доріжки 1 відбувається ПІСЛЯ запису доріжки 0.
Рушій читає SharedState свіжо кожного разу, без знімків. Це відповідає
інтуїції розробника: «наступне читання після запису бачить нове значення».

**Висновок:** декларуйте доріжки у порядку «спочатку виробник, потім споживач».
Якби спостерігач був задекларований першим, він читав би ЗАСТАРІЛИЙ
`main_phase_name` на тіку N (перед тим, як main його оновив) і чекав би
ще один тік до конвергенції — зазвичай нешкідливо, але додає затримку.

## Верифікація

Синкронізація між доріжками верифікована end-to-end через host-тест:

```bash
python -m pytest tools/tests/test_sequence_host.py::test_track_synchronization_host
```

Тест завантажує мінімальний варіант з двома доріжками (sync_two_tracks.modr)
і запускає `instance_tick` до завершення: доріжка 0 пише `test.signal=true`,
умова `state_key_eq` доріжки 1 спрацьовує, сценарій досягає COMPLETED.
Обмеження: 200 тіків (2 с при тіку 10 мс).

## Додавання аварійного переривання (глобальний перехід)

Щоб примусово перервати сценарій незалежно від того, у яких фазах
перебувають доріжки, використовуйте глобальний перехід:

```jsonc
"scenario": {
  "global_transitions": [
    {"when": {"state_key_eq": {"key": "safety.fault", "value": true}},
     "priority": 255, "scope": "abort_scenario"}
  ],
  // ... tracks ...
}
```

Глобальні переходи оцінюються ПЕРШИМИ на кожному тіку, у порядку
зменшення пріоритету. Перший збіг перериває сценарій (усі доріжки →
FAILED → сценарій FAILED). `scope: abort_main_track`, навпаки, перериває
лише доріжку з прапором main_track, дозволяючи watcher-ам тощо продовжувати.

## Поширені патерни

### Доріжка-watchdog

Доріжка-монітор, що працює паралельно з main, провалюється при
порушенні безпеки:

```jsonc
{
  "name": "wdog",
  "phases": [{
    "name": "monitoring",
    "timeout_ms": 0,
    "transitions": [
      {"to": "$abort", "when": {"state_key_gt": {"key": "sensor.temp", "value": 100}}}
    ]
  }]
}
```

З `completion_rule: main_track_complete` `$abort` watchdog-а призводить
до провалу сценарію; `$complete` main-а — до успіху сценарію.

### Конвеєр producer-consumer

Доріжка 0 виробляє лічильник робочих елементів; доріжка 1 споживає,
чекаючи на цей лічильник:

```jsonc
// Track 0
{"action": "set_state", "params": {"key": "pipeline.count", "type": "i32", "value": 5}}

// Track 1
{"to": "next", "when": {"state_key_ge": {"key": "pipeline.count", "value": 5}}}
```

Семантика порядку тіків гарантує, що доріжка 1 побачить лічильник
негайно на тому самому тіку, на якому доріжка 0 його пише.

## Дивіться також

- [01_minimal_3phase.md](01_minimal_3phase.md) — основи однієї доріжки
- [05_synchronization.md](../../05_synchronization.md) — повна специфікація порядку тіків та граничні випадки
- [ADR-0003](../../adr/0003-tick-order-sync-semantics.md) — обґрунтування дизайну
