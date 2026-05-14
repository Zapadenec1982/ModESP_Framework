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
│  ├─ Engine (modesp::scenario::Engine — BaseModule, multi-instance)    │
│  ├─ ActionRegistry (caller-owned; домени реєструють дії)              │
│  ├─ ContinuousRegistry (caller-owned; стандартні примітиви у складі)  │
│  ├─ ResourceArbiter (engine-owned; ISA-88 §5.3 two-scope arbitration) │
│  ├─ IStateBackend (DI-інтерфейс — адаптер до SharedState у main/)     │
│  ├─ IEngineObserver hooks → NvsObserver (edge-triggered persistence)  │
│  ├─ mirror::publish (прямий виклик дзеркала — кожен тік)              │
│  └─ Loads .modr from /data/scenarios/<name>.modr                       │
│                                                                       │
│  modules/<recipe_name>/  (no C++ code; recipe is manifest-only)       │
│                                                                       │
│  WebUI loads ui.json → renders widgets (existing infrastructure)      │
│  visible_when constraints show recipe widgets only when active        │
└──────────────────────────────────────────────────────────────────────┘
```

Примітка: інтерфейсу `IResourceArbiter` немає — рушій містить конкретний
член `ResourceArbiter`. Точки ін'єкції обмежені лише `IStateBackend`,
`ActionRegistry`, `ContinuousRegistry` та спаном спостерігачів.

## Відповідальність компонентів

| Компонент | Файл | Роль |
|-----------|------|------|
| `Engine` | `include/modesp/scenario/engine.h` + `src/core/engine.cpp` | Поверхня публічного API; диспетчер кількох екземплярів; тактує запущені сценарії; видає події спостерігачам; щотакту прямо викликає `mirror::publish`. Конструктор приймає `IStateBackend&`, `ActionRegistry&`, `ContinuousRegistry&` та `etl::span<IEngineObserver*>` |
| `ActionRegistry` | `action_registry.{h,cpp}` | `ActionRegistry`, що належить викликачу (без синглтонів); зареєстровані дії резолвляться через 16-бітні djb2-хеші. Ін'єктується у `Engine` через конструктор. Два пули (дії / умови) у вигляді ETL flat_map фіксованої місткості |
| `ContinuousRegistry` | `continuous_behavior.{h,cpp}`, `continuous_primitives.{h,cpp}` | Належить викликачу, без синглтонів, ін'єктується через конструктор. Етап 2 постачає стандартні примітиви (PID, гістерезис, рампа) у `continuous_primitives.h` — реєстрація за бажанням викликача через `primitives::register_primitives()` |
| `ResourceArbiter` | `resource_arbiter.{h,cpp}` (конкретний клас; член рушія, без абстракції-інтерфейсу) | Атомарне захоплення/звільнення ресурсів за ISA-88 §5.3 на рівні сценарію та фази; zero-heap ETL flat_map |
| `IStateBackend` | `i_state_backend.h` | Єдиний погляд рушія на сховище стану. Два «сирих» віртуальних методи (`get_raw`, `set_raw`) над `modesp::StateValue`, типізовані аксесори inline. Продакшн-адаптер до `modesp::SharedState` живе у `main/`; host-тести використовують `StubStateBackend` |
| `IEngineObserver` | `i_engine_observer.h` | Три edge-події (`on_scenario_started`, `on_phase_entered`, `on_scenario_terminal`) + `on_tick`. Спостерігачі тільки читають — не змінюють стан рушія. Порожні тіла за замовчуванням → невикористані override-и компілюються у no-op |
| `NvsObserver` | `nvs_observer.{h,cpp}` | Імплементує `IEngineObserver`. Власник тротлінгу записів у NVS та підв'язки колбеків відновлення. Політика: зміни стану — миттєво, зміни фази основної доріжки — миттєво, інші доріжки — дебаунс ≥ 1 с. Рушій НЕ лінкує `nvs_flash` напряму — спостерігач приймає read/write-колбеки від викликача |
| `ModrLoader` | `modr_loader.{h,cpp}` | Валідує байтові буфери `.modr`, повертає представлення `LoadedScenario` |
| `BuiltinActions` | `builtin_actions.{h,cpp}` | Доменно-незалежні дії (`log`, `set_state`, `wait_ms`) + листові умови |
| `runtime_types.h` | `include/modesp/scenario/runtime_types.h` | POD-структури `TrackRuntime` та `SequenceRuntime` (стан одного запуску). Логіка FSM живе у `src/core/track.cpp` і `src/core/instance.cpp` та згорнута всередину `Engine::on_update` — окремих класів `SequenceTrack` / `SequenceInstance` більше немає |
| `NvsToken` | `nvs_token.{h,cpp}` | 96-байтовий токен збереження; серіалізація/десеріалізація з CRC16. Магічне число `'SCTK'` (`'SQTK'` відхиляється як легасі) |
| `EngineError` | `engine_error.h` | Уніфікований енум кодів помилок |

## Потік даних на одному такті

```
ModuleManager calls engine.on_update(dt_ms)
   │
   ▼
Для кожного завантаженого слота у engine:
   │
   ├─ Тактуємо SequenceRuntime (згорнуто в engine.cpp / instance.cpp):
   │     │
   │     ├─ Process global transitions (priority sorted)
   │     │     └─ On match: abort instance (release phase_scope, fail tracks)
   │     │
   │     ├─ For each track (declaration order; src/core/track.cpp):
   │     │     ├─ Increment phase_elapsed_ms (saturating)
   │     │     ├─ Handle WAITING_FOR_RESOURCE (retry phase-scope acquire)
   │     │     ├─ Run exit actions (one per tick) if pending transition
   │     │     ├─ Run entry actions (one per tick)
   │     │     ├─ Evaluate transitions; on match latch target phase
   │     │     └─ Check phase timeout
   │     │
   │     └─ Check completion_rule; transition scenario state if satisfied
   │
   ├─ mirror::publish(state, slot)   ← прямий виклик, виконується безумовно
   │                                    щотакту. Хелпер у private/mirror.h.
   │
   └─ На переходах FSM рушій синхронно емітує події спостерігачам:
         on_scenario_started   — IDLE/LOADED → RUNNING
         on_phase_entered      — доріжка увійшла у нову фазу
         on_scenario_terminal  — сценарій досяг COMPLETED або FAILED
      NvsObserver слухає ці події та застосовує власну політику тротлінгу
      (зміни стану — миттєво; зміни фази основної доріжки — миттєво;
      інші доріжки — дебаунс ≥ 1 с). Також усім спостерігачам диспетчується
      `on_tick(dt_ms)` для tick-driven внутрішнього стану (NvsObserver
      використовує його для накопичення лічильника тротлінгу на слот).
```

Чого вже **немає** у рушії (порівняно з пре-rebuild
`modesp_sequence::SequenceEngine`):

- Немає методу `publish_mirror_keys()` — запис дзеркала йде прямим
  викликом `mirror::publish` із tick-шляху.
- Немає `persist_scan()` / `persist_slot()` — збереження повністю
  делеговане `NvsObserver`, який сам тримає свій стан тротлінгу.
- Жодних синглтонів. `ActionRegistry` і `ContinuousRegistry` — це
  посилання на об'єкти, що належать викликачу та ін'єктуються в
  конструктор; кілька екземплярів рушія можуть співіснувати з власними
  незалежними реєстрами (зручно для host-тестів).

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

На один слот (типове `CONFIG_MODESP_MAX_SEQUENCES = 2`):
- `SequenceRuntime` ~600 байтів (`tracks[6]` × ~100 байтів кожен)
- `uint8_t buffer[MODR_MAX_SIZE]` = 4 КБ (`CONFIG_MODESP_MODR_MAX_SIZE`,
  типово 4 096; піднімайте через menuconfig, якщо рецепт перевищує бюджет)
- Облік слота (`buffer_size`, прапори дедуплікації) ~24 байти
- **Разом на слот: ~4.6 КБ**

Загальні накладні витрати:
- Структура `Engine`: ~9.2 КБ (2 слоти × 4.6 КБ) плюс пам'ять для edge-detect
  (`last_emitted_state_`, `last_emitted_phase_`)
- `ActionRegistry`: ~3 КБ (до 64 записів × ~50 байтів, два пули)
- `ResourceArbiter`: ~640 байтів (32 записи × 20 байтів)
- `NvsObserver`: ~64 байти лічильників тротлінгу на слот (під ємність
  `MAX_SLOTS = 8` — стелю з Kconfig)

**Усього рушій: ~13 КБ SRAM** за типовою конфігурацією `MAX_SEQUENCES = 2` /
`MODR_MAX_SIZE = 4 КБ`. Лінійно масштабується з обома Kconfig-параметрами —
на історичних 4 × 16 КБ рушій усе ще комфортно вміщувався у 320 КБ DRAM
WROOM-32, але нові типові значення повертають ~54 КБ застосунку.

## Перехресні посилання

- [00_overview.md](00_overview.md) — що і навіщо
- [02_binary_format.md](02_binary_format.md) — побайтовий розклад `.modr`
- [03_api_reference.md](03_api_reference.md) — публічний C++ API
- [04_state_machines.md](04_state_machines.md) — скінченні автомати сценарію та доріжок
- [09_manifest_integration.md](09_manifest_integration.md) — повний конвеєр складання, включно з валідацією схеми та каталогом помилок компілятора
