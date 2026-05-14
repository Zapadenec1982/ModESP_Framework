# 01 — Архітектура

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/01_architecture.md](../../../en/03-framework-reference/scenario-engine/01_architecture.md)

Високорівневий огляд того, як компоненти рушія сценаріїв поєднуються між собою — від написання маніфесту до виконання під час роботи.

## Конвеєр на етапах складання та виконання

```
┌──────────────────────────────────────────────────────────────────────┐
│                        BUILD TIME PIPELINE                            │
│                                                                       │
│  modules/<recipe_name>/manifest.json                                  │
│  ├─ existing sections (state, ui, mqtt, loggable, features)           │
│  └─ NEW section: "scenario" {tracks: [{phases: [...]}], ...}          │
│                                                                       │
│       │                                          │                    │
│       ▼ existing pipeline                        ▼ new build step     │
│  tools/generate_ui.py                       tools/compile_scenario.py │
│  (extends to recognize module_type=recipe)  (new script — Step 2)     │
│       │                                          │                    │
│       ▼                                          ▼                    │
│  generated/state_meta.h                    data/scenarios/<n>.modr    │
│  data/ui.json                              (binary — staged до LFS)   │
│  generated/mqtt_topics.h                                              │
│                                                                       │
└──────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                        RUNTIME (ESP32)                                │
│                                                                       │
│  components/modesp_scenario/                                          │
│  ├─ SequenceEngine (BaseModule, multi-instance, multi-track)          │
│  ├─ ActionRegistry (domain modules register actions/conditions)       │
│  ├─ ContinuousRegistry (Stage 2 — PID, hysteresis, ramp)              │
│  ├─ ResourceArbiter (ISA-88 §5.3 two-scope arbitration)               │
│  ├─ ModrLoader (validates .modr blobs)                                │
│  ├─ NvsToken (persist/recover state)                                  │
│  └─ Loads .modr from /data/scenarios/<name>.modr                       │
│                                                                       │
│  modules/<recipe_name>/  (no C++ code; recipe is manifest-only)       │
│                                                                       │
│  WebUI loads ui.json → renders widgets (existing infrastructure)      │
│  visible_when constraints show recipe widgets only when active        │
└──────────────────────────────────────────────────────────────────────┘
```

## Відповідальність компонентів

| Компонент | Файл | Роль |
|-----------|------|------|
| `SequenceEngine` | `sequence_engine.{h,cpp}` | Поверхня публічного API; диспетчер кількох екземплярів; тактує запущені сценарії; публікує дзеркальні ключі; керує збереженням |
| `ActionRegistry` | `action_registry.{h,cpp}` | Синглтон-таблиця «хеш → `ActionDescriptor`»; доменні модулі реєструють власні дії |
| `ContinuousRegistry` | `continuous_behavior.{h,cpp}`, `continuous_registry.cpp` | Зарезервовано для етапу 2 (PID, гістерезис, рампи) |
| `ModrLoader` | `modr_loader.{h,cpp}` | Валідує байтові буфери `.modr`, повертає представлення `LoadedScenario` |
| `ResourceArbiter` | `resource_arbiter.{h,cpp}` | Атомарне захоплення/звільнення ресурсів за ISA-88 §5.3 на рівні сценарію та фази |
| `BuiltinActions` | `builtin_actions.{h,cpp}` | 3 доменно-незалежні дії (`log`, `set_state`, `wait_ms`) + 10 листових умов |
| `SequenceTrack` | `sequence_track.{h,cpp}` | Автомат стану однієї доріжки (`track_tick`); обчислювач умов |
| `SequenceInstance` | `sequence_instance.{h,cpp}` | Автомат стану одного сценарію (`instance_tick`); обробка глобальних переходів |
| `NvsToken` | `nvs_token.{h,cpp}` | 96-байтовий токен збереження; серіалізація/десеріалізація з CRC16 |
| `EngineError` | `engine_error.h` | Уніфікований енум кодів помилок |

## Потік даних на одному такті

```
ModuleManager calls engine.on_update(dt_ms)
   │
   ▼
For each loaded slot у engine:
   │
   ├─ instance_tick(runtime, dt_ms, state, arbiter)
   │     │
   │     ├─ Process global transitions (priority sorted)
   │     │     └─ On match: instance_abort (release phase_scope, fail tracks)
   │     │
   │     ├─ For each track (declaration order):
   │     │     └─ track_tick(runtime, track_idx, dt_ms, state, arbiter)
   │     │           │
   │     │           ├─ Increment phase_elapsed_ms (saturating)
   │     │           ├─ Handle WAITING_FOR_RESOURCE (try acquire phase resources)
   │     │           ├─ Run exit actions (one per tick) если pending transition
   │     │           ├─ Run entry actions (one per tick)
   │     │           ├─ Evaluate transitions; on match latch target
   │     │           └─ Check phase timeout
   │     │
   │     └─ Check completion_rule; transition scenario state if satisfied
   │
   ├─ publish_mirror_keys(slot)  ← writes <recipe>.<...> keys to SharedState
   │
   └─ persist_scan(dt_ms)   ← detects changes, throttle, invokes NVS callback
```

## Інтеграція через маніфест

Наріжне архітектурне рішення (за ADR-0004): рецепт — це **маніфест модуля ModESP** із `module_type: "recipe"` та секцією `scenario`. Це повторно використовує наявний конвеєр:

| Секція | Хто читає | Вихід |
|---------|---------|--------|
| `state` | `generate_ui.py` (наявний) | Декларації в `state_meta.h` |
| `ui` | `generate_ui.py` (наявний) | Віджети в `ui.json` |
| `mqtt` | `generate_ui.py` (наявний) | `mqtt_topics.h` |
| `scenario` (НОВА) | `compile_scenario.py` (новий) | Бінарний файл `data/scenarios/<n>.modr` |

Нуль змін у генераторі WebUI. Нуль змін у типах віджетів. Автор рецепту отримує повну інтеграцію з UI, станом, MQTT та збереженням «безкоштовно», бо наявний інструментарій читає стандартні секції з маніфесту.

## Бюджет пам'яті (ESP32-WROOM-32)

На один слот (4 за замовчуванням):
- `SequenceRuntime` ~600 байтів (`tracks[6]` × ~100 байтів кожен)
- `uint8_t buffer[MODR_MAX_SIZE]` = 16 КБ
- Облік збереження ~24 байти
- **Разом на слот: ~16.6 КБ**

Загальні накладні витрати:
- Структура `SequenceEngine`: ~64 КБ (4 слоти × 16.6 КБ)
- `ActionRegistry`: ~3 КБ (64 записи × ~50 байтів)
- `ResourceArbiter`: ~640 байтів (32 записи × 20 байтів)

**Усього рушій: ~67 КБ SRAM** (типова конфігурація). Комфортно вміщується у 320 КБ DRAM WROOM-32.

## Перехресні посилання

- [00_overview.md](00_overview.md) — що і навіщо
- [02_binary_format.md](02_binary_format.md) — побайтовий розклад `.modr`
- [03_api_reference.md](03_api_reference.md) — публічний C++ API
- [04_state_machines.md](04_state_machines.md) — скінченні автомати сценарію та доріжок
- [09_manifest_integration.md](09_manifest_integration.md) — повний конвеєр складання, включно з валідацією схеми та каталогом помилок компілятора
