# ModESP v4 — Документація

> 📖 **English version:** [documentation/en/](../en/README.md)
> 📋 **Style guide:** [STYLE.md](../STYLE.md)

ModESP v4 — **manifest-driven framework прошивок для ESP32**. Закидаєте
module manifests, запускаєте build, отримуєте production-ready прошивку з
автогенерованою схемою стану, віджетами WebUI, MQTT topics, OTA і
LittleFS partitioning.

Документація орієнтована на **module authors** — інженерів які пишуть
бізнес-модулі і scenario рецепти поверх фреймворку. Інші аудиторії
(контриб'ютори core, hardware integrators, operators) мають окремі розділи.

## Звідки почати

У порядку:

1. **[Швидкий старт](01-getting-started/quickstart.md)** — прошити пристрій,
   запустити reference scenario, побачити live state у WebUI. Менш ніж 10
   хвилин.
2. **[Концепції](01-getting-started/concepts.md)** — чотири
   ключові ідеї (manifest-driven, modules, scenarios, SharedState).
3. **[Module Author Guide → Огляд](02-module-author-guide/overview.md)** —
   починаєте писати ваш перший модуль.

## Навігація

### 01 — Початок роботи

| Документ | Статус | Призначення |
|---|---|---|
| [quickstart.md](01-getting-started/quickstart.md) | ✅ | Flash, налаштування, запуск reference scenario. |
| [installation.md](01-getting-started/installation.md) | ✅ | Встановлення ESP-IDF, клонування репо, перший build. |
| [concepts.md](01-getting-started/concepts.md) | ✅ | Ментальна модель. |

### 02 — Module Author Guide (Основна аудиторія)

| Документ | Статус | Призначення |
|---|---|---|
| [overview.md](02-module-author-guide/overview.md) | ✅ | Типи модулів, п'ять core ідей, анатомія. |
| [manifest.md](02-module-author-guide/manifest.md) | ✅ | Усі секції маніфесту з reference (module/recipe/driver). |
| [writing-a-module.md](02-module-author-guide/writing-a-module.md) | ✅ | Анатомія C++ класу + lifecycle hooks. |
| [writing-a-driver.md](02-module-author-guide/writing-a-driver.md) | ✅ | IDriver subclass, реєстрація, sensor/actuator патерни. |
| [shared-state.md](02-module-author-guide/shared-state.md) | ✅ | Read/write патерни, change tracking, type rules. |
| [ui-widgets.md](02-module-author-guide/ui-widgets.md) | ✅ | Повний каталог widgets, cards, visible_when, i18n. |
| [mqtt.md](02-module-author-guide/mqtt.md) | ✅ | Publish/subscribe семантика, topic format, HA discovery. |
| [persistence.md](02-module-author-guide/persistence.md) | ✅ | NVS через PersistService, debounce, migrations. |
| [recipe-authoring.md](02-module-author-guide/recipe-authoring.md) | ✅ | Структура scenario рецепту, tracks, phases, transitions. |
| [recipe-actions.md](02-module-author-guide/recipe-actions.md) | ✅ | Built-in actions/conditions, кастомна реєстрація. |
| [continuous-behaviors.md](02-module-author-guide/continuous-behaviors.md) | ✅ | PID, hysteresis, ramp; кастомні. |
| [debugging.md](02-module-author-guide/debugging.md) | ✅ | Логи, HTTP / WS inspection, common bugs. |
| [best-practices.md](02-module-author-guide/best-practices.md) | ✅ | Checklist патернів і анти-патернів. |

### 03 — Reference фреймворку

| Документ | Статус | Призначення |
|---|---|---|
| [architecture.md](03-framework-reference/architecture.md) | ✅ | Шари системи, залежності, init phases. |
| [components/modesp_core.md](03-framework-reference/components/modesp_core.md) | ✅ | SharedState, BaseModule, ModuleManager, App. |
| [components/modesp_hal.md](03-framework-reference/components/modesp_hal.md) | ✅ | HAL абстракції, IDriver, DriverManager. |
| [components/modesp_services.md](03-framework-reference/components/modesp_services.md) | ✅ | Logger, Watchdog, Persist, Config, Error, SystemMonitor. |
| [components/modesp_net.md](03-framework-reference/components/modesp_net.md) | ✅ | Wi-Fi, HTTP сервер, WebSocket. |
| [components/modesp_mqtt.md](03-framework-reference/components/modesp_mqtt.md) | ✅ | MQTT client wrapper з TLS і HA discovery. |
| [components/modesp_aws.md](03-framework-reference/components/modesp_aws.md) | ✅ | AWS IoT alternative backend. |
| [components/modesp_json.md](03-framework-reference/components/modesp_json.md) | ✅ | JSON parsing utilities (jsmn wrapper). |
| [components/modesp_scenario.md](03-framework-reference/components/modesp_scenario.md) | ✅ | High-level огляд scenario engine. |
| scenario-engine/ | ⏳ planned | Engine deep dive (буде link на migrated content). |
| [modules/equipment.md](03-framework-reference/modules/equipment.md) | ✅ | Equipment Manager — bridge між sensor/actuator HAL. |
| [modules/datalogger.md](03-framework-reference/modules/datalogger.md) | ✅ | Channel logging, retention, plot API. |
| [modules/simple_thermo.md](03-framework-reference/modules/simple_thermo.md) | ✅ | Reference ON/OFF thermostat. |
| [modules/abs_test.md](03-framework-reference/modules/abs_test.md) | ✅ | Reference recipe з двома паралельними tracks. |
| [drivers/ds18b20.md](03-framework-reference/drivers/ds18b20.md) | ✅ | Dallas OneWire температурний сенсор. |
| [drivers/ntc.md](03-framework-reference/drivers/ntc.md) | ✅ | NTC термістор через ADC. |
| [drivers/relay.md](03-framework-reference/drivers/relay.md) | ✅ | GPIO реле-актуатор. |
| [drivers/pcf8574_relay.md](03-framework-reference/drivers/pcf8574_relay.md) | ✅ | I2C-розширене реле (PCF8574). |
| [drivers/digital_input.md](03-framework-reference/drivers/digital_input.md) | ✅ | GPIO контактний вхід. |
| [drivers/pcf8574_input.md](03-framework-reference/drivers/pcf8574_input.md) | ✅ | I2C-розширений контактний вхід. |
| web-ui.md | ⏳ planned | Svelte SPA архітектура, state stores. |

### 04 — Hardware

| Документ | Статус | Призначення |
|---|---|---|
| [board-config.md](04-hardware/board-config.md) | ✅ | Схема `board.json` і приклади. |
| [bindings.md](04-hardware/bindings.md) | ✅ | `bindings.json` — driver↔role mapping. |
| [ota.md](04-hardware/ota.md) | ✅ | OTA flow, rollback, partition layout. |
| [deployment.md](04-hardware/deployment.md) | ✅ | Flash, monitor, factory reset. |

### 05 — Tools

| Документ | Статус | Призначення |
|---|---|---|
| [generate_ui.md](05-tools/generate_ui.md) | ✅ | Build-time генератор. |
| [compile_scenario.md](05-tools/compile_scenario.md) | ✅ | Компілятор рецептів і `.modr` формат. |
| [dump_modr.md](05-tools/dump_modr.md) | ✅ | `.modr` інспектор / debugger. |

### 06 — Контриб'ютори

| Документ | Статус | Призначення |
|---|---|---|
| [development-setup.md](06-contributing/development-setup.md) | ✅ | Налаштування dev environment. |
| [testing.md](06-contributing/testing.md) | ✅ | Host tests, HIL tests, fuzz. |
| [code-style.md](06-contributing/code-style.md) | ✅ | C++ конвенції. |
| [docs-style.md](06-contributing/docs-style.md) | ✅ | Cross-references [STYLE.md](../STYLE.md). |

### ADR — Architecture Decision Records

У [adr/](adr/) коли будуть написані. Engine-specific decisions — у scenario
engine section.

## Статус

Документація — **clean-slate strategic rewrite** після rebuild engine
`modesp_sequence` → `modesp_scenario`. Усі core сторінки тепер ✅ готові;
залишилися ⏳ planned пункти (e.g. `scenario-engine/` deep dive,
`web-ui.md`) — у плані на наступні сесії.

Попередня директорія `docs/` залишається доступною як **legacy reference** —
частина сторінок там все ще фактично коректна, частина застаріла. Жодна не
авторитетна доки не переписана у `documentation/`.

## Контриб'ютинг

Прочитайте **[STYLE.md](../STYLE.md)** перед писанням або редагуванням
будь-якої сторінки. Style guide фіксує quality bar для цієї директорії.
