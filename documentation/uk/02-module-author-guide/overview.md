# Module Author Guide — Огляд

> 📖 **In English:** [docs/en/02-module-author-guide/overview.md](../../en/02-module-author-guide/overview.md)

Цей гайд — для інженерів які пишуть **business-logic модулі** і **scenario
рецепти** поверх ModESP. Прочитавши цей розділ ви зможете:

- Кинути новий модуль у `modules/your_thing/` і він автоматично підхопиться
  build системою.
- Декларувати state keys, UI widgets, і MQTT topics декларативно (без
  ручної схеми, без C++ boilerplate на кожен ключ).
- Написати scenario recipe як частину маніфесту і отримати скомпільований
  бінарь що engine виконує у runtime.
- Читати і писати state через thread-safe, type-checked store (SharedState).
- Persist-ити конфіг модуля між ребутами без прямих NVS API.

## Що таке модуль?

У ModESP **модуль** — це одиниця бізнес-логіки що живе у власній директорії
під `modules/` і поставляється з **manifest.json** що описує все що
фреймворку треба знати про нього:

- Які **state keys** модуль читає і пише (типізовані: int, float, bool, string).
- Які **UI widgets** з'являються для нього у WebUI (декларативно — жодного
  Svelte коду у ваших руках).
- Які **MQTT topics** він публікує / на які підписується.
- (Опціонально) **`scenario`** секція якщо модуль — це recipe — декларативний
  phase/transition граф скомпільований у бінарь `.modr` при build.
- (Опціонально) **Feature flags**, **i18n strings**, **datalogger channels**.

Build-time генератор фреймворку (`tools/generate_ui.py`) читає всі module
manifests, виробляє merged UI schema, C++ state-metadata header, MQTT topic
константи, і CMake module list. Ви пишете C++ клас з бізнес-логікою; усе
інше згенероване.

## Дві категорії модулів

| Тип | Має C++ код? | Має manifest scenario? | Коли |
|---|---|---|---|
| **Service module** | Так (BaseModule subclass) | Ні | Continuous logic: thermostat, sensor reader, alarm manager. Активний кожен tick. |
| **Recipe module** | Ні | Так | Time-bounded процес: cook program, batch reactor цикл, irrigation послідовність. Engine веде його через phases. |

Можна змішувати — business module що завантажує recipe on-demand — валідний
дизайн (наприклад, оператор обирає recipe A або B через UI, ваш модуль
викликає `engine.load_path()`).

## П'ять core ідей

### 1. Manifest-driven everything

Контракт вашого модуля — це JSON file. Фреймворк його читає, генерує усе що
може статично, і просить вас написати лише actual логіку. Додавання нового
state key — це один рядок у `manifest.json` — без C++ змін, без UI змін,
без MQTT plumbing.

Див. **[manifest.md](manifest.md)** для повної схеми.

### 2. SharedState як data backbone

Існує один in-process state store (`modesp::SharedState`) спільний для
всіх модулів. Це типізована, thread-safe key-value мапа обмеженої місткості.
Модулі читають output один одного через відповідні state keys — без прямих
покажчиків, без observer реєстрації. HTTP API, WebSocket, MQTT publisher,
і datalogger також спостерігають SharedState.

Див. **shared-state.md** *(planned)* для read/write патернів, change tracking,
і lifetime гарантій.

### 3. Three-phase init lifecycle

Модулі конструюються при static-storage init, далі проходять три init phases
що драйвить App / ModuleManager:

1. **Phase 1 (CRITICAL):** error service, watchdog, config, persistence,
   system monitor. Першими.
2. **Phase 2 (HIGH/NORMAL):** Wi-Fi, cloud, equipment, drivers, **scenario
   engine**, бізнес-модулі. Після того як конфіг завантажений.
3. **Phase 3 (LOW):** HTTP, WebSocket. Останніми — залежать від усього вище.

Поле `priority` у вашому маніфесті обирає phase. Див. **writing-a-module.md**
*(planned)* для lifecycle hooks (`on_init`, `on_update`, `on_message`,
`on_stop`).

### 4. Scenarios як скомпільовані artifacts

Якщо ви пишете recipe — він не виконується як JSON у runtime. Build-time
`compile_scenario.py` виробляє бінарний `.modr` blob (захищений CRC,
4-byte aligned, обмежений ≤ 16 KB) що engine завантажує з LittleFS. Це
означає що recipe authoring зміщує production complexity з runtime у
build time — невалідні рецепти ніколи не доходять до пристрою.

Див. **recipe-authoring.md** *(planned)* і
**[scenario-engine/](../03-framework-reference/scenario-engine/)** для deep dive.

### 5. Zero heap allocation

Фреймворк цільовий до ESP32-WROOM-32 (320 KB DRAM). Стандартних C++
containers уникаємо — використовуємо ETL (Embedded Template Library) для
fixed-capacity maps, vectors, queues, optionals. Модулі ПОВИННІ робити так
само — жодних `std::vector`, `std::map`, `new`/`delete`. Використовуйте ETL
або static-size POD типи.

Див. **best-practices.md** *(planned)* для allocation конвенцій і common
pitfalls.

## Анатомія module folder

```
modules/your_thing/
├── manifest.json          ← ОБОВ'ЯЗКОВО — контракт модуля
├── CMakeLists.txt         ← ОБОВ'ЯЗКОВО для service modules; пропустити для recipes
├── include/
│   └── your_thing.h       ← Module C++ class declaration (service modules)
└── src/
    └── your_thing.cpp     ← Implementation (service modules)
```

Recipe модулі містять **лише** `manifest.json` — без C++, без CMakeLists.
Фреймворк розпізнає їх по `"module_type": "recipe"` у маніфесті.

## Коли вам НЕ потрібен новий модуль

Іноді ви хочете поведінку, не новий модуль. Розгляньте:

- **One-off scenario** без постійного state? Використайте recipe (без C++,
  швидша ітерація).
- **Кастомна action / condition** для recipes? Зареєструйте через
  `ActionRegistry` з init існуючого модуля — див.
  **recipe-actions.md** *(planned)*.
- **Новий continuous control primitive (варіант PID)?** Зареєструйте
  `ContinuousBehavior` factory — див. **continuous-behaviors.md** *(planned)*.
- **Hardware-specific driver (новий I2C сенсор)?** Йде у
  `components/modesp_hal/` з новим `IDriver` subclass, не модуль. Див.
  **[hardware/bindings.md](../04-hardware/bindings.md)** *(planned)*.

## Рекомендований порядок читання

1. [Manifest reference](manifest.md) — що ви будете писати найчастіше.
2. shared-state.md *(planned)* — як дані flow-ять.
3. writing-a-module.md *(planned)* — C++ сторона.
4. ui-widgets.md *(planned)* — як WebUI рендерить ваш state.
5. [best-practices.md](best-practices.md) — патерни і анти-патерни.

Якщо ваша мета — recipe, перейдіть до:

1. [Manifest reference](manifest.md) — той самий маніфест хостить `scenario` секцію.
2. recipe-authoring.md *(planned)*.
3. recipe-actions.md *(planned)*.

## Існуючі модулі як worked examples

Дивіться на ці для reference:

- [`modules/simple_thermo/`](../../../modules/simple_thermo/) — мінімальний
  service module (ON/OFF thermostat). Manifest зі state, UI, MQTT; ~150 LOC
  C++. Хороший перший приклад.
- [`modules/datalogger/`](../../../modules/datalogger/) — більший service
  module з features (channels, retention, plot data API). Див.
  [datalogger reference](../03-framework-reference/modules/datalogger.md).
- [`modules/equipment/`](../../../modules/equipment/) — service module що
  bridge-ить manifest з HAL drivers (найбільш coupled модуль — читайте
  ПІСЛЯ того як зрозумієте основи).
- [`modules/abs_test/`](../../../modules/abs_test/) — чистий recipe модуль
  (без C++). Два паралельні tracks з cross-track синхронізацією. Reference
  для recipe authoring.
