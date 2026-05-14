# Документація рушія сценаріїв

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/README.md](../../../en/03-framework-reference/scenario-engine/README.md)

`components/modesp_scenario/` — рушій часозалежних алгоритмів на основі доріжок (tracks) для ModESP_v4.

## Архітектурна документація

| Файл | Тема |
|---|---|
| [00_overview.md](00_overview.md) | Що / навіщо / для кого |
| [01_architecture.md](01_architecture.md) | Високорівневі діаграми + компоненти |
| [02_binary_format.md](02_binary_format.md) | Побайтова специфікація `.modr` |
| [03_api_reference.md](03_api_reference.md) | Публічний C++ API |
| [04_state_machines.md](04_state_machines.md) | Скінченні автомати сценарію та доріжок |
| [05_synchronization.md](05_synchronization.md) | Синхронізація доріжок у порядку тактів |
| [06_resource_arbitration.md](06_resource_arbitration.md) | Відображення ISA-88 §5.3 |
| [07_persistence.md](07_persistence.md) | Структура NVS + політика запису |
| [08_lifecycle.md](08_lifecycle.md) | Життєвий цикл на етапі складання та виконання |
| [09_manifest_integration.md](09_manifest_integration.md) | Конвеєр «рецепт-як-маніфест» |
| [10_error_model.md](10_error_model.md) | Коди `EngineError` + автомат збоїв дій |

## Документація для розробника (посібник користувача рушія)

| Файл | Тема |
|---|---|
| [usage/01_quickstart.md](usage/01_quickstart.md) | 5-хвилинний практичний приклад |
| [usage/02_writing_recipes.md](usage/02_writing_recipes.md) | Посібник з написання рецептів |
| [usage/03_registering_actions.md](usage/03_registering_actions.md) | Користувацькі дії в доменних модулях |
| [usage/examples/01_minimal_3phase.md](usage/examples/01_minimal_3phase.md) | Приклад з однією доріжкою |
| [usage/examples/02_dual_track_sync.md](usage/examples/02_dual_track_sync.md) | Приклад синхронізації кількох доріжок |

## Записи архітектурних рішень (ADR)

| ADR | Назва | Статус |
|---|---|---|
| [0001](adr/0001-binary-format-not-constexpr.md) | Бінарний формат `.modr` у LittleFS, а НЕ C++ `constexpr` | Прийнято |
| [0002](adr/0002-tracks-as-first-class.md) | Доріжки — повноцінне поняття (а не «накладка» зверху) | Прийнято |
| [0003](adr/0003-tick-order-sync-semantics.md) | Синхронізація доріжок у порядку тактів (а не за знімком) | Прийнято |
| [0004](adr/0004-recipe-as-manifest.md) | Рецепт = маніфест із секцією `scenario` | Прийнято |
| [0005](adr/0005-isa88-resource-arbitration.md) | ISA-88 §5.3: «захопити перед стартом» | Прийнято |
| [0006](adr/0006-no-builtin-continuous-behaviors.md) | Нуль вбудованих неперервних поведінок у MVP | Прийнято (переглянуто на Stage 2) |
| [0007](adr/0007-mandatory-phase-timeouts.md) | Обов'язкові таймаути на кожну фазу | Прийнято |
| [0008](adr/0008-expressiveness-paper-pilot.md) | «Паперовий пілот» виразності (крок 0.75) | Прийнято |

## Порядок читання

Для нових учасників / користувачів:
1. [00_overview.md](00_overview.md) — що це таке
2. [usage/01_quickstart.md](usage/01_quickstart.md) — мінімальний приклад
3. [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — посібник з написання
4. [09_manifest_integration.md](09_manifest_integration.md) — інтеграція з конвеєром
5. [03_api_reference.md](03_api_reference.md) — C++ API для бізнес-модулів
6. ADR `0001-0008` — критичні рішення з обґрунтуваннями

Для розробників самого рушія:
1. ADR — запобігають повторному обговоренню вже ухвалених рішень
2. Архітектурні документи (01-10) — авторитетний довідник по поточному рушію
