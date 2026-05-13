# ModESP v4 — Документація

> 📖 **English version:** [docs/en/](../en/README.md)

ModESP v4 — це **manifest-driven framework прошивок для ESP32**. Дозволяє
відвантажувати hardware-aware застосунки без перепису платформи щоразу.
Закидаєш module manifests, запускаєш build, отримуєш production-ready
прошивку з автогенерованою схемою стану, віджетами WebUI, MQTT topics
і підтримкою OTA.

Документація структурована для **module authors** — інженерів які пишуть
бізнес-логіку (modules + scenario recipes) поверх фреймворку. Інші
аудиторії (контриб'ютори core, hardware integrators, operators) мають
окремі розділи.

## Звідки почати

Якщо ви тут вперше — читайте у порядку:

1. **[Початок роботи → Швидкий старт](01-getting-started/quickstart.md)** —
   запустити reference scenario `abs_test` на залізі за 10 хвилин.
2. **[Початок роботи → Концепції](01-getting-started/concepts.md)** —
   чотири ключові ідеї (manifest-driven, modules, scenarios, SharedState).
3. **[Module Author Guide → Огляд](02-module-author-guide/overview.md)** —
   починаєте писати ваш перший модуль.

## Навігація

### 01 — Початок роботи

| Документ | Призначення |
|---|---|
| [installation.md](01-getting-started/installation.md) | Встановлення ESP-IDF, клонування репо, перший build. |
| [quickstart.md](01-getting-started/quickstart.md) | Прошивка і запуск reference scenario. |
| [concepts.md](01-getting-started/concepts.md) | Ментальна модель фреймворку. |

### 02 — Module Author Guide (Основна аудиторія)

| Документ | Призначення |
|---|---|
| [overview.md](02-module-author-guide/overview.md) | Що таке модуль; lifecycle; ментальна модель. |
| [manifest.md](02-module-author-guide/manifest.md) | Усі секції маніфесту. |
| [best-practices.md](02-module-author-guide/best-practices.md) | Патерни та анти-патерни. |
| writing-a-module.md *(planned)* | Анатомія C++ класу + реєстрація. |
| shared-state.md *(planned)* | Читання і запис стану з модуля. |
| ui-widgets.md *(planned)* | Генерація UI карток із маніфесту. |
| mqtt.md *(planned)* | Налаштування pub/sub. |
| persistence.md *(planned)* | NVS через PersistService. |
| recipe-authoring.md *(planned)* | Написання scenario рецептів. |
| recipe-actions.md *(planned)* | Built-in vs custom actions. |
| continuous-behaviors.md *(planned)* | PID, hysteresis, ramp; кастомні. |
| debugging.md *(planned)* | Логи, HTTP API для перегляду стану. |

### 03 — Reference фреймворку

| Документ | Призначення |
|---|---|
| [architecture.md](03-framework-reference/architecture.md) | Шари системи, dependency diagram. |
| [components/](03-framework-reference/components/) | Per-component reference (9 компонентів). |
| [modules/](03-framework-reference/modules/) | Per-module reference. |
| [scenario-engine/](03-framework-reference/scenario-engine/) | Scenario engine deep dive (`modesp_scenario`). |
| [web-ui.md](03-framework-reference/web-ui.md) | Svelte SPA архітектура. |

### 04 — Hardware

| Документ | Призначення |
|---|---|
| board-config.md *(planned)* | Схема `board.json` і приклади. |
| bindings.md *(planned)* | `bindings.json`: відповідність driver↔GPIO. |
| ota.md *(planned)* | OTA flow, rollback, dual-image розмітка. |
| deployment.md *(planned)* | Flash, monitor, factory reset. |

### 05 — Tools

| Документ | Призначення |
|---|---|
| generate_ui.md *(planned)* | Build-time генератор схеми стану, UI, MQTT topics. |
| compile_scenario.md *(planned)* | Компілятор рецептів (`.modr` бінарний формат). |
| dump_modr.md *(planned)* | Інспектор / дебагер рецептів. |

### 06 — Контриб'ютори

| Документ | Призначення |
|---|---|
| development-setup.md *(planned)* | Налаштування dev environment. |
| testing.md *(planned)* | Host tests, HIL tests, fuzz tests. |
| code-style.md *(planned)* | C++ конвенції, naming, стиль коментарів. |
| docs-style.md *(planned)* | Як оновлювати і підтримувати ці docs. |

### ADR — Architecture Decision Records

У [adr/](adr/). Engine-specific ADRs у
[03-framework-reference/scenario-engine/adr/](03-framework-reference/scenario-engine/adr/).

## Статус

Документація переписується паралельно з rebuild engine `modesp_sequence` →
`modesp_scenario` (див. [CHANGELOG](../../CHANGELOG.md) під "Phase 0..4").
Сторінки позначені *(planned)* — у плані на наступні сесії. Існуючий контент
мігрований і stale references підчищені, але якість контенту варіюється
до dedicated rewrite passes.

## Контриб'ютинг у документацію

Це **plain Markdown**. Без build step, без static site generator. Редагуйте
файли напряму, шліть PR (або commit-те у feature branch). Конвенції:

- Один файл == одна тема.
- Кожна сторінка починається з 1-абзацного "що і навіщо" лід.
- Code examples мусять бути copy-paste runnable (без pseudocode без явного
  callout `<!-- pseudocode -->`).
- Bilingual: кожна UK сторінка має EN counterpart за дзеркальним шляхом у
  [docs/en/](../en/). Переклади незалежні — оновлюйте обидва при змінах.
