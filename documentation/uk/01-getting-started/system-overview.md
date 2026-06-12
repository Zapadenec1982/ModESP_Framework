# Огляд системи — як усе сполучається

> 📖 **In English:** [documentation/en/01-getting-started/system-overview.md](../../en/01-getting-started/system-overview.md)

Це та єдина сторінка, яку варто прочитати, перш ніж щось чіпати. Вона пояснює
**філософію**, **єдине джерело істини** і **ланцюг зв'язків** від фізичного піна
до віджета у WebUI — щоб решта документації набула сенсу і ти знав, *де* міняти *що*.

- [concepts.md](concepts.md) дає чотири runtime-моделі мислення (маніфест,
  модулі+драйвери, SharedState, сценарії). Читай наступним.
- [03-framework-reference/architecture.md](../03-framework-reference/architecture.md)
  — повний reference (шари, задачі, фази init). Читай, коли треба глибина.
- Ця сторінка — **сполучна тканина** між ними.

---

## Філософія (чому фреймворк саме такий)

1. **Єдине джерело істини, оголошене один раз.** Річ описана рівно в одному
   місці — маніфесті — а білд *генерує* все похідне (UI, MQTT-топіки, метадані
   стану, C++-реєстрацію, Kconfig). Перейменуй ключ стану в маніфесті — і UI,
   MQTT та персистентність підуть за ним. Жодної ручної синхронізації двох файлів.

2. **Маніфест — це контракт; C++ — це поведінка.** Прочитай `manifest.json`
   модуля, щоб зрозуміти *що це*, не читаючи жодного рядка C++. Реалізація може
   змінюватись, поки тримає контракт.

3. **Згенеровані файли не редагуються.** Усе під `generated/`, плюс
   `data/ui.json` і `components/modesp_hal/Kconfig`, перезаписується щозбірки.
   Щоб їх змінити — зміни маніфест і перезбери. Вони мають банери `DO NOT EDIT`.

4. **Неузгодженість ламає білд, а не пристрій.** Прив'язка на неіснуючий пін,
   неправильний тип драйвера чи драйвер, вимкнений у menuconfig — це *помилка
   збірки* з чітким повідомленням, а не тихе «воно просто не працює» на
   промисловому автоматі.

5. **Два типи громадян, одна шина.** Усе підключуване — це або **модуль**
   (бізнес-логіка), або **драйвер** (залізо). Вони ніколи не викликають одне
   одного напряму — спілкуються лише через ключі **SharedState**. Завдяки цьому
   топологія системи читається з маніфестів, а кожен модуль тривіально тестується.

6. **Модулі не торкаються заліза.** Драйвери володіють GPIO/I2C/OneWire/ADC і
   публікують ключі `equipment.<role>`; модулі читають ці ключі й пишуть стан
   вищого рівня. Заміна DS18B20 на NTC змінює прив'язку, а не рядок бізнес-логіки.

7. **Zero heap у hot path.** Жодних `new`/`std::string`/`std::vector` у
   `on_update()`/`on_message()` — лише ETL-типи фіксованої місткості. Ціль —
   ESP32 на 4 МБ, що працює на 100 Гц вічно, без фрагментації.

8. **Декларативність там, де вона окупається.** Багатофазні процеси (розморозка,
   pulldown, OTA) пишуться як **сценарії** (декларативні FSM у JSON), а не
   рукописні C++-автомати.

---

## Два шари: що пишеш ти, а що робить фреймворк

```
ПРОДУКТ  (ти)                  ФРЕЙМВОРК  (надано)
─────────────────              ──────────────────────────────────────────
modules/<m>/manifest.json  →   генератор → ui.json, state_meta.h, mqtt, …
modules/<m>/*.cpp           ┐
drivers/<d>/manifest.json   │   EquipmentBase, DriverManager + registry
drivers/<d>/*.cpp           ├─→ SharedState, ModuleManager, App, 100 Гц loop
boards/<b>/board.json       │   HTTP/WS/MQTT/AWS, OTA, DataLogger, LittleFS
boards/<b>/bindings.json    │   Scenario engine, PersistService, Watchdog
project.json                ┘
```

Ти оголошуєш *що* (маніфести) і пишеш *поведінку* (C++ модуля + фабрики
драйверів). Фреймворк дає *все інше* і зв'язує це на етапі збірки.

---

## Єдине джерело істини — чотири входи

| Вхід | Володіє | Хто пише |
|---|---|---|
| `modules/<m>/manifest.json` | ключі стану модуля, UI-картки, MQTT-топіки, персистентність, лог-канали, (рецепти) сценарій | Автор модуля |
| `drivers/<d>/manifest.json` | `category`, `hardware_type`, `requires_address`, налаштування, discovery драйвера | Автор драйвера |
| `boards/<b>/board.json` | фізичні ресурси плати (id GPIO/OneWire/ADC/I2C-розширювача) | Автор плати |
| `boards/<b>/bindings.json` | проводка: `{hardware → driver → role}` для цієї плати | Деплоєр |
| `project.json` | які модулі входять у цю прошивку | Власник продукту |

Білд копіює `board.json`/`bindings.json` **активної** плати в `data/` (плата
обирається через `idf.py menuconfig`), потім генератор читає все вищезгадане
разом і перехресно перевіряє.

---

## Build-пайплайн — один генератор, багато виходів

`tools/generate_ui.py` запускається як pre-build CMake-крок (до `project()` в
ESP-IDF), усе валідує і — тільки якщо валідно — емітить:

| Згенерований артефакт | З чого | Споживач |
|---|---|---|
| `data/ui.json` | маніфести модулів | WebUI |
| `generated/state_meta.h` | ключі стану | SharedState / Persist / MQTT |
| `generated/mqtt_topics.h` | mqtt-секції | MqttService |
| `generated/module_{includes,instances,register}.h`, `modules.cmake` | project.json | `main.cpp`, CMake |
| `generated/datalogger_{channels,events}.h` | `loggable`-секції | DataLogger |
| `components/modesp_hal/Kconfig` | `drivers/*/manifest.json` | menuconfig (toggle на драйвер) |
| `generated/drivers.cmake` | драйвери | `modesp_hal` REQUIRES |
| `generated/driver_register_all.h` | драйвери | DriverManager (реєстрація) |
| `generated/required_drivers.cmake` | активні bindings | build-time gate узгодженості |
| `data/www/i18n/*.json` | i18n модулів | WebUI |

`tools/compile_scenario.py` працює поруч, компілюючи `scenario`-блоки рецептів у
`data/scenarios/*.modr`. **Валідація йде першою**: поганий маніфест, прив'язка на
неіснуючий пін/драйвер, невідповідність типу driver↔hardware чи помилка рецепта
ламають білд до запису будь-якого файлу. Див.
[05-tools/generate_ui.md](../05-tools/generate_ui.md).

---

## Повний ланцюг — від піна до віджета

Простежимо один датчик температури наскрізь. Кожна стрілка оголошена, не
прокладена руками:

```
board.json:   onewire_buses[{id:"ow_1", gpio:32}]          ← шина існує
bindings.json:{hardware:"ow_1", driver:"ds18b20",          ← проводка driver→role
               role:"air_temp", address:"28:..."}
drivers/ds18b20/manifest.json: hardware_type=onewire_bus   ← тип має збігтись з ow_1
   │  (генератор валідує усе вище, емітить Kconfig-toggle + registry glue)
   ▼ збірка
DriverManager::init()  → DriverRegistry.create_sensor("ds18b20", binding, hal)
   │  фабрика ds18b20 знаходить ow_1 у HAL, конфігурує драйвер
   ▼ runtime, 100 Гц
драйвер ds18b20 читає шину  →  SharedState["equipment.air_temp"] = 4.5
   ▼
модуль simple_thermo читає "equipment.air_temp", пише "simple_thermo.output"
   ▼  (change-tracked — без полінгу)
WS broadcast → віджет WebUI   |   MqttService → публікація   |   DataLogger
```

Модуль так і не дізнався, що там був DS18B20, OneWire-шина чи GPIO 32 — лише ключ
`equipment.air_temp`. Це принцип №6 у дії.

---

## Механізм драйверів (найновіший сполучний шар)

Драйвери **опційні, самореєструються і валідуються** — варто зрозуміти, бо це
зв'язує board, bindings, menuconfig і реєстр:

- **Реєстр, не хардкод.** `DriverManager` шукає `binding.driver_type` у
  `DriverRegistry` (мапа `type → фабрика`). Кожен драйвер самореєструється одним
  макросом (`MODESP_REGISTER_SENSOR/ACTUATOR`); решту робить генератор. Додавання
  драйвера **не** чіпає жоден файл фреймворку.
- **Опційність через menuconfig.** Кожен драйвер отримує авто-згенерований toggle
  `CONFIG_MODESP_DRIVER_<NAME>` (`idf.py menuconfig → ModESP Drivers`). Вимкнений
  драйвер не компілюється (менший бінарник).
- **Узгодженість enforce-иться.** Якщо активна плата *прив'язує* драйвер, який
  вимкнено, білд падає. `python tools/drivers_sync.py --fix` узгоджує menuconfig з
  платою (`--prune` вимикає невикористані; `--dry-run` показує). Див.
  [05-tools/drivers_sync.md](../05-tools/drivers_sync.md).

Повна деталізація: [02-module-author-guide/writing-a-driver.md](../02-module-author-guide/writing-a-driver.md).

---

## Runtime-модель (на одному диханні)

Один `App` володіє одним `SharedState` і одним `ModuleManager`. Модулі
реєструються у три пріоритетні фази, потім `update_all(dt_ms)` тікає кожен модуль
на 100 Гц у **main-задачі**. Модулі читають/пишуть SharedState; HTTP/WS/MQTT
працюють у **інших задачах** і тому торкаються стану лише через захищений
м'ютексом SharedState — ніколи не нутрощів модуля. Сценарії — це звичайний модуль
(`modesp_scenario`), що інтерпретує `.modr`-FSM. Глибина:
[architecture.md](../03-framework-reference/architecture.md).

---

## Навігаційна мапа — «щоб змінити X, дивись Y»

| Я хочу… | Редагувати | Читати |
|---|---|---|
| Додати ключ стану / UI-картку / MQTT-топік | `manifest.json` модуля | [manifest.md](../02-module-author-guide/manifest.md) |
| Змінити бізнес-логіку | `*.cpp` модуля (`on_update`) | [writing-a-module.md](../02-module-author-guide/writing-a-module.md) |
| Підтримати нове залізо | нову `drivers/<d>/` | [writing-a-driver.md](../02-module-author-guide/writing-a-driver.md) |
| Прокласти залізо для деплою | `boards/<b>/bindings.json` | [04-hardware/bindings.md](../04-hardware/bindings.md) |
| Оголосити піни плати | `boards/<b>/board.json` | [04-hardware/board-config.md](../04-hardware/board-config.md) |
| Обрати, які модулі в прошивці | `project.json` | [architecture.md](../03-framework-reference/architecture.md) |
| Увімкнути/вимкнути драйвери | `idf.py menuconfig` або `drivers_sync.py` | [drivers_sync.md](../05-tools/drivers_sync.md) |
| Написати багатофазний процес | `scenario`-блок рецепта | [recipe-authoring.md](../02-module-author-guide/recipe-authoring.md) |
| Зрозуміти чужий модуль | спершу його `manifest.json` | [concepts.md](concepts.md) |

**Що ніколи не редагуєш:** `generated/*`, `data/ui.json`,
`components/modesp_hal/Kconfig` (усе регенерується). **Що ніколи не викликаєш
напряму:** GPIO/шини (це драйвери), функції іншого модуля (це SharedState),
HTTP/MQTT-механіку (це маніфести).

## Куди далі

1. [concepts.md](concepts.md) — чотири runtime-моделі глибше.
2. [02-module-author-guide/overview.md](../02-module-author-guide/overview.md) — напиши перший модуль.
3. [architecture.md](../03-framework-reference/architecture.md) — повний reference, коли треба.
