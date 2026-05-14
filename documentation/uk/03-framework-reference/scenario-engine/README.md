# Документація рушія сценаріїв

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/README.md](../../../en/03-framework-reference/scenario-engine/README.md)

`components/modesp_scenario/` — рушій часозалежних алгоритмів на основі доріжок (tracks) для ModESP_v4.

> **Статус:** Етап 0 (специфікації + кістяк) — реалізація триває. Більшість файлів — це заготовки, які поступово наповнюються відповідно до кроків реалізації. Дивіться план у `.claude/plans/quirky-imagining-lake.md`.

## Архітектурна документація

| Файл | Тема | Наповнюється на кроці |
|---|---|---|
| [00_overview.md](00_overview.md) | Що / навіщо / для кого | 0 (зараз) |
| [01_architecture.md](01_architecture.md) | Високорівневі діаграми + компоненти | 1+ |
| [02_binary_format.md](02_binary_format.md) | Побайтова специфікація `.modr` | 1 |
| [03_api_reference.md](03_api_reference.md) | Публічний C++ API | 5, 6, 14 |
| [04_state_machines.md](04_state_machines.md) | Скінченні автомати сценарію та доріжок | 11, 12 |
| [05_synchronization.md](05_synchronization.md) | Синхронізація доріжок у порядку тактів | 13 |
| [06_resource_arbitration.md](06_resource_arbitration.md) | Відображення ISA-88 §5.3 | 10 |
| [07_persistence.md](07_persistence.md) | Структура NVS + політика запису | 15 |
| [08_lifecycle.md](08_lifecycle.md) | Життєвий цикл на етапі складання та виконання | 14 |
| [09_manifest_integration.md](09_manifest_integration.md) | Конвеєр «рецепт-як-маніфест» | 2, 4 |
| [10_error_model.md](10_error_model.md) | Коди `EngineError` + автомат збоїв дій | 7, 8 |

## Документація для розробника (посібник користувача рушія)

| Файл | Тема | Наповнюється на кроці |
|---|---|---|
| [usage/01_quickstart.md](usage/01_quickstart.md) | 5-хвилинний практичний приклад | 16 |
| [usage/02_writing_recipes.md](usage/02_writing_recipes.md) | Посібник з написання рецептів | 2, 7, 13 |
| [usage/03_registering_actions.md](usage/03_registering_actions.md) | Користувацькі дії в доменних модулях | 5 |
| [usage/examples/01_minimal_3phase.md](usage/examples/01_minimal_3phase.md) | Приклад з однією доріжкою | 16 |
| [usage/examples/02_dual_track_sync.md](usage/examples/02_dual_track_sync.md) | Приклад синхронізації кількох доріжок | 17 |

## Записи архітектурних рішень (ADR)

| ADR | Назва | Статус |
|---|---|---|
| [0001](adr/0001-binary-format-not-constexpr.md) | Бінарний формат `.modr` у LittleFS, а НЕ C++ `constexpr` | Прийнято |
| [0002](adr/0002-tracks-as-first-class.md) | Доріжки — повноцінне поняття (а не «накладка» зверху) | Прийнято |
| [0003](adr/0003-tick-order-sync-semantics.md) | Синхронізація доріжок у порядку тактів (а не за знімком) | заготовка, наповнення на кроці 13 |
| [0004](adr/0004-recipe-as-manifest.md) | Рецепт = маніфест із секцією `scenario` | заготовка, наповнення на кроці 2 |
| [0005](adr/0005-isa88-resource-arbitration.md) | ISA-88 §5.3: «захопити перед стартом» | заготовка, наповнення на кроці 10 |
| [0006](adr/0006-no-builtin-continuous-behaviors.md) | Нуль вбудованих неперервних поведінок у MVP | заготовка, наповнення на кроці 6 |
| [0007](adr/0007-mandatory-phase-timeouts.md) | Обов'язкові таймаути на кожну фазу | заготовка, наповнення на кроці 8 |
| [0008](adr/0008-expressiveness-paper-pilot.md) | «Паперовий пілот» виразності (крок 0.75) | Прийнято |

## Відкладено до етапу 1.5

Ці документи НЕ входять до результатів етапу 1; вони заплановані на етап 1.5, коли проявиться реальна цінність:
- `usage/04_custom_continuous.md`, `usage/05_resource_management.md`, `usage/06_persistence_and_recovery.md`, `usage/07_testing_recipes.md`
- `usage/troubleshooting.md`
- `usage/examples/03_resource_contention.md`, `04_long_running_with_resume.md`, `05_irrigation_cycle.md` (рецепт пройшов паперовий пілот на кроці 0.75)
- `maint/01_contributing.md`, `02_binary_format_versioning.md`, `03_adding_builtin_action.md`, `04_test_strategy.md`, `05_release_checklist.md`
- Тести валідації документації (код у документації, перевірка посилань, лінтер повноти API-довідника)

## Порядок читання

Для нових учасників / користувачів:
1. [00_overview.md](00_overview.md) — що це таке
2. [usage/01_quickstart.md](usage/01_quickstart.md) — мінімальний приклад
3. [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — посібник з написання
4. [09_manifest_integration.md](09_manifest_integration.md) — інтеграція з конвеєром
5. [03_api_reference.md](03_api_reference.md) — C++ API для бізнес-модулів
6. ADR `0001-0007` — критичні рішення з обґрунтуваннями

Для розробників самого рушія:
1. План `.claude/plans/quirky-imagining-lake.md` — єдине джерело істини для архітектури
2. ADR — запобігають повторному обговоренню вже ухвалених рішень
3. Архітектурні документи (01-10) — наповнюйте їх у міру появи коду
