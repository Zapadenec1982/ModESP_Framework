# ModESP v4 — Документація

> 📖 **English version:** [documentation/en/](../en/README.md)
> 📋 **Style guide:** [STYLE.md](../STYLE.md)

ModESP v4 — це **фреймворк прошивок для ESP32, керований маніфестами**. Ви
додаєте маніфести модулів, запускаєте збирання й отримуєте готову до
виробництва прошивку з автоматично згенерованою схемою стану, віджетами
WebUI, темами MQTT, OTA та розміткою LittleFS.

Документація орієнтована на **авторів модулів** — інженерів, які пишуть
бізнес-модулі та рецепти сценаріїв поверх фреймворку. Інші аудиторії
(контриб'ютори фреймворку, інтегратори обладнання, оператори) теж мають
окремі розділи.

## Звідки почати

За порядком:

1. **[Швидкий старт](01-getting-started/quickstart.md)** — прошити пристрій,
   запустити еталонний сценарій, побачити стан у реальному часі в WebUI.
   Менш ніж 10 хвилин.
2. **[Концепції](01-getting-started/concepts.md)** — чотири ключові ідеї
   (керування маніфестами, модулі, сценарії, SharedState).
3. **[Module Author Guide → Огляд](02-module-author-guide/overview.md)** —
   починаєте писати свій перший модуль.

## Навігація

### 01 — Початок роботи

| Документ | Статус | Призначення |
|---|---|---|
| [quickstart.md](01-getting-started/quickstart.md) | ✅ | Прошивка, налаштування, запуск еталонного сценарію. |
| [installation.md](01-getting-started/installation.md) | ✅ | Встановлення ESP-IDF, клонування репозиторію, перше збирання. |
| [concepts.md](01-getting-started/concepts.md) | ✅ | Ментальна модель ядра. |

### 02 — Module Author Guide (Основна аудиторія)

| Документ | Статус | Призначення |
|---|---|---|
| [overview.md](02-module-author-guide/overview.md) | ✅ | Типи модулів, п'ять ключових ідей, анатомія. |
| [manifest.md](02-module-author-guide/manifest.md) | ✅ | Усі секції маніфесту з поясненнями (module/recipe/driver). |
| [writing-a-module.md](02-module-author-guide/writing-a-module.md) | ✅ | Анатомія C++ класу + хуки життєвого циклу. |
| [writing-a-driver.md](02-module-author-guide/writing-a-driver.md) | ✅ | Підклас `IDriver`, реєстрація, патерни sensor/actuator. |
| [shared-state.md](02-module-author-guide/shared-state.md) | ✅ | Патерни читання/запису, відстеження змін, правила типів. |
| [ui-widgets.md](02-module-author-guide/ui-widgets.md) | ✅ | Повний каталог віджетів, карток, `visible_when`, i18n. |
| [mqtt.md](02-module-author-guide/mqtt.md) | ✅ | Семантика публікації/підписки, формат тем, HA discovery. |
| [persistence.md](02-module-author-guide/persistence.md) | ✅ | NVS через PersistService, debounce, міграції. |
| [recipe-authoring.md](02-module-author-guide/recipe-authoring.md) | ✅ | Структура рецепту сценарію, треки, фази, переходи. |
| [recipe-actions.md](02-module-author-guide/recipe-actions.md) | ✅ | Вбудовані дії/умови, реєстрація власних. |
| [continuous-behaviors.md](02-module-author-guide/continuous-behaviors.md) | ✅ | PID, гістерезис, ramp; власні. |
| [debugging.md](02-module-author-guide/debugging.md) | ✅ | Логи, інспекція HTTP / WS, типові помилки. |
| [best-practices.md](02-module-author-guide/best-practices.md) | ✅ | Контрольний список патернів і анти-патернів. |

### 03 — Довідник фреймворку

| Документ | Статус | Призначення |
|---|---|---|
| [architecture.md](03-framework-reference/architecture.md) | ✅ | Шари системи, залежності, фази ініціалізації. |
| [components/modesp_core.md](03-framework-reference/components/modesp_core.md) | ✅ | SharedState, BaseModule, ModuleManager, App. |
| [components/modesp_hal.md](03-framework-reference/components/modesp_hal.md) | ✅ | Абстракції HAL, IDriver, DriverManager. |
| [components/modesp_services.md](03-framework-reference/components/modesp_services.md) | ✅ | Logger, Watchdog, Persist, Config, Error, SystemMonitor. |
| [components/modesp_net.md](03-framework-reference/components/modesp_net.md) | ✅ | Wi-Fi, HTTP сервер, WebSocket. |
| [components/modesp_mqtt.md](03-framework-reference/components/modesp_mqtt.md) | ✅ | Обгортка MQTT-клієнта з TLS і HA discovery. |
| [components/modesp_aws.md](03-framework-reference/components/modesp_aws.md) | ✅ | Альтернативний бекенд AWS IoT. |
| [components/modesp_json.md](03-framework-reference/components/modesp_json.md) | ✅ | Утиліти парсингу JSON (обгортка jsmn). |
| [components/modesp_scenario.md](03-framework-reference/components/modesp_scenario.md) | ✅ | Загальний огляд рушія сценаріїв. |
| [scenario-engine/](03-framework-reference/scenario-engine/README.md) | ✅ | Поглиблений розгляд рушія — архітектура, бінарний формат, FSM, ADR, посібники. |
| [modules/equipment.md](03-framework-reference/modules/equipment.md) | ✅ | Equipment Manager — міст між sensor/actuator HAL. |
| [modules/datalogger.md](03-framework-reference/modules/datalogger.md) | ✅ | Логування каналів, утримання, plot API. |
| [modules/simple_thermo.md](03-framework-reference/modules/simple_thermo.md) | ✅ | Еталонний ON/OFF термостат. |
| [modules/abs_test.md](03-framework-reference/modules/abs_test.md) | ✅ | Еталонний рецепт з двома паралельними треками. |
| [drivers/ds18b20.md](03-framework-reference/drivers/ds18b20.md) | ✅ | Температурний сенсор Dallas OneWire. |
| [drivers/ntc.md](03-framework-reference/drivers/ntc.md) | ✅ | NTC термістор через ADC. |
| [drivers/relay.md](03-framework-reference/drivers/relay.md) | ✅ | Релейний актуатор на GPIO. |
| [drivers/pcf8574_relay.md](03-framework-reference/drivers/pcf8574_relay.md) | ✅ | Реле через розширювач I2C (PCF8574). |
| [drivers/digital_input.md](03-framework-reference/drivers/digital_input.md) | ✅ | Контактний вхід на GPIO. |
| [drivers/pcf8574_input.md](03-framework-reference/drivers/pcf8574_input.md) | ✅ | Контактний вхід через розширювач I2C. |
| [web-ui.md](03-framework-reference/web-ui.md) | ✅ | Архітектура Svelte SPA, сховища стану, потік WebSocket, каталог віджетів. |

### 04 — Обладнання

| Документ | Статус | Призначення |
|---|---|---|
| [board-config.md](04-hardware/board-config.md) | ✅ | Схема `board.json` і приклади. |
| [bindings.md](04-hardware/bindings.md) | ✅ | `bindings.json` — зіставлення драйвер↔роль. |
| [ota.md](04-hardware/ota.md) | ✅ | Потік OTA, відкат, схема партицій. |
| [deployment.md](04-hardware/deployment.md) | ✅ | Прошивка, монітор, заводське скидання. |

### 05 — Інструменти

| Документ | Статус | Призначення |
|---|---|---|
| [generate_ui.md](05-tools/generate_ui.md) | ✅ | Огляд генератора часу збирання. |
| [compile_scenario.md](05-tools/compile_scenario.md) | ✅ | Компілятор рецептів і формат `.modr`. |
| [dump_modr.md](05-tools/dump_modr.md) | ✅ | Інспектор / зневаджувач `.modr`. |

### 06 — Контриб'ютори

| Документ | Статус | Призначення |
|---|---|---|
| [development-setup.md](06-contributing/development-setup.md) | ✅ | Налаштування середовища розробки. |
| [testing.md](06-contributing/testing.md) | ✅ | Тести на хості, HIL-тести, fuzz. |
| [code-style.md](06-contributing/code-style.md) | ✅ | Конвенції C++. |
| [docs-style.md](06-contributing/docs-style.md) | ✅ | Перехресні посилання на [STYLE.md](../STYLE.md). |

### ADR — Architecture Decision Records

Розташовані в [adr/](adr/) після написання. Рішення, специфічні для рушія,
знаходяться в розділі рушія сценаріїв.

## Статус

Документація — це **стратегічне переписування з чистого аркуша** після
перебудови рушія `modesp_sequence` → `modesp_scenario`. Усі сторінки
в індексі тепер ✅ готові й написані за єдиним стандартом якості
([STYLE.md](../STYLE.md)).

Попередня директорія `docs/` залишається доступною як **застарілий
довідник** — частина сторінок там досі фактично коректна, частина
застаріла. Жодна не є авторитетною, доки не переписана у
`documentation/`.

## Контриб'ютинг

Прочитайте **[STYLE.md](../STYLE.md)** перед написанням або редагуванням
будь-якої сторінки. Style guide фіксує планку якості для цієї директорії.
