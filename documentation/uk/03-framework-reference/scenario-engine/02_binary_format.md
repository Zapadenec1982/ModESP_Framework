# 02 — Бінарний формат `.modr` (v1)

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/02_binary_format.md](../../../en/03-framework-reference/scenario-engine/02_binary_format.md)

**Джерело істини:** [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../../components/modesp_scenario/include/modesp/scenario/modr_format.h)
**Інваріанти в тестах:** [`tools/tests/test_modr_format.py`](../../../../tools/tests/test_modr_format.py)
**Еталонний бінарний файл:** [`tools/tests/fixtures/scenarios/minimal_v1.modr`](../../../../tools/tests/fixtures/scenarios/minimal_v1.modr) (114 байтів)

## Огляд

`.modr` — однофайловий бінарний рецепт, який `Engine` завантажує під час роботи з LittleFS. Little-endian, природне вирівнювання по 4 байти (атрибут `__attribute__((packed))` не потрібен). Увесь файл вміщується у фіксований буфер `MODR_MAX_SIZE = 4 КБ` (налаштовується через Kconfig `CONFIG_MODESP_MODR_MAX_SIZE`).

Конвеєр завантаження: `f_read` усього файлу → перевірка магічного числа / версії / CRC → розбір зміщень → виконання. Парсингу на ходу немає — рушій читає структури безпосередньо з буфера.

## Константи

| Символ | Значення | Опис |
|--------|-------|-------------|
| `MODR_MAGIC` | `0x52444F4D` | 'MODR' як LE `uint32` |
| `MODR_FORMAT_VERSION` | `1` | Збільшується при несумісних змінах схеми |
| `MODR_MAX_SIZE` | `4096` (4 КБ) | Граничний розмір буфера на один завантажений сценарій; налаштовується через Kconfig `CONFIG_MODESP_MODR_MAX_SIZE` |
| `MODR_NO_OFFSET` | `0xFFFF` | Маркер «немає запису» (`entry/exit_action_off` тощо) |
| `MODR_TARGET_COMPLETE` | `0xFFFF` | Цільове значення переходу = `$complete` (цієї доріжки) |
| `MODR_TARGET_ABORT` | `0xFFFE` | Цільове значення переходу = `$abort` (усього сценарію) |

## Розклад файлу

```
┌────────────────────────────────────┐  offset 0
│  modr_header (56 bytes)            │
├────────────────────────────────────┤
│  modr_track[track_count] (16 each) │  ← header.track_table_off
├────────────────────────────────────┤
│  modr_phase[N] (20 each)           │  ← track[i].phases_off (per-track phase array)
│  ...                               │
├────────────────────────────────────┤
│  modr_transition[] (12 each)       │  ← phase[j].transitions_off (inline transitions)
│  ...                               │
├────────────────────────────────────┤
│  modr_action[] action pool (8)     │  ← header.action_pool_off
├────────────────────────────────────┤
│  modr_action[] cond pool (8)       │  ← header.cond_pool_off
├────────────────────────────────────┤
│  modr_param_entry[] (8)            │  ← header.param_pool_off
├────────────────────────────────────┤
│  modr_global_transition[] (8)      │  ← header.global_trans_off
├────────────────────────────────────┤
│  modr_resource_decl[] (4)          │  ← header.resource_off
├────────────────────────────────────┤
│  modr_phase_resource_claim[] (4)   │  ← phase[j].phase_resources_off
│  ...                               │
├────────────────────────────────────┤
│  String pool (length-prefixed)     │  ← header.string_pool_off
│    [u8 len][bytes][u8 len][bytes]  │
├────────────────────────────────────┤
│  CRC32 trailer (4 bytes)           │  ← total_size - 4
└────────────────────────────────────┘  total_size

CRC = CRC-32/ISO-HDLC computed over [0 .. total_size-4]
       (matches Python zlib.crc32 i ESP-IDF esp_crc32_le)
```

## Розміри структур (валідуються `static_assert` + `pytest`)

| Структура | Розмір | Примітки |
|--------|------|-------|
| `modr_header` | 56 | Містить `default_phase_timeout_ms` та `scenario_timeout_max_ms` |
| `modr_track` | 16 | Запис однієї доріжки |
| `modr_phase` | 20 | +4 до специфікації плану Q1 для полів `phase_resources` (крок 0.75) |
| `modr_transition` | 12 | **Виправлено** з 8 у специфікації плану Q1 (корекція вирівнювання) |
| `modr_global_transition` | 8 | Застосовується до всіх доріжок на кожному такті |
| `modr_action` | 8 | **Виправлено** з 6 у специфікації плану Q1 (доповнено для вирівнювання) |
| `modr_param_entry` | 8 | |
| `modr_resource_decl` | 4 | Ресурс рівня сценарію |
| `modr_phase_resource_claim` | 4 | Захоплення рівня фази (крок 0.75) |

## Корекції розкладу відносно плану Q1

У специфікації байтових розмірів плану Q1 знайдено дві арифметичні помилки під час реалізації на кроці 1:

1. **`modr_transition`**: у специфікації — 8 байтів, але поля разом займають 2+2+1+1+4 = 10 байтів; `uint32` має бути вирівняний по 4 → доповнено до 12.
2. **`modr_action`**: у специфікації — 6 байтів, але не вирівняно по 4 байти → доповнено до 8 (додано `uint16_t reserved`).

`modr_phase` також розширено до 20 байтів (з 16 у специфікації) для полів `phase_resources_off + phase_resource_n + reserved` після того, як паперовий пілот на кроці 0.75 виявив потребу в арбітражі ресурсів на рівні фази.

Ці виправлення НЕ ламають наявну функціональність — на етапі 0 ще немає скомпільованих рецептів. План Q1 буде оновлено відповідно до реалізації.

## Поля заголовка (зміщення → поле)

| Зміщення | Розмір | Поле | Примітки |
|--------|------|-------|-------|
| 0 | 4 | `magic` | `0x52444F4D` |
| 4 | 2 | `format_version` | `1` |
| 6 | 2 | `flags` | бітове поле: `MODR_FLAG_*` |
| 8 | 4 | `total_size` | повний розмір файлу разом із CRC |
| 12 | 2 | `scenario_id` | молодші 16 біт `djb2(module_name)` |
| 14 | 2 | `name_str_idx` | зміщення в пулі рядків для імені рецепту |
| 16 | 1 | `track_count` | 1..6 |
| 17 | 1 | `cont_count` | кількість слотів `ContinuousBehavior` (0 у MVP) |
| 18 | 1 | `resource_count` | ресурси рівня сценарію |
| 19 | 1 | `global_trans_count` | глобальні переходи |
| 20 | 1 | `completion_rule` | `MODR_COMPLETION_*` |
| 21 | 1 | `reserved_a` | |
| 22 | 2 | `track_table_off` | |
| 24 | 2 | `param_pool_off` | |
| 26 | 2 | `param_pool_count` | |
| 28 | 2 | `action_pool_off` | |
| 30 | 2 | `action_pool_count` | |
| 32 | 2 | `cond_pool_off` | |
| 34 | 2 | `cond_pool_count` | |
| 36 | 2 | `string_pool_off` | |
| 38 | 2 | `global_trans_off` | |
| 40 | 2 | `resource_off` | |
| 42 | 2 | `reserved_b` | заповнювач для вирівнювання `uint32` |
| 44 | 4 | `default_phase_timeout_ms` | застосовується до фаз без явного таймауту |
| 48 | 4 | `scenario_timeout_max_ms` | жорстке обмеження; 0 = без обмеження |
| 52 | 4 | `reserved_c` | |

## Правила валідації (завантажувач)

Застосовуються у `modr_loader.cpp`:

1. `magic == MODR_MAGIC` → інакше `INVALID_FILE`
2. `format_version == 1` → інакше `UNSUPPORTED_VERSION`
3. `total_size <= MODR_MAX_SIZE` І `total_size <= file_size` → інакше `INVALID_FILE`
4. CRC32 над `[0..total_size-4]` збігається з кінцевиком → інакше `CRC_MISMATCH`
5. `track_count >= 1` І `<= MAX_TRACKS_PER_SCENARIO`
6. Кожна доріжка: `phase_count >= 1`
7. Кожна фаза: усі індекси в пулі дій менші за `action_pool_count`
8. Кожен перехід: `target_phase < phase_count` АБО маркер
9. `global_trans_off + global_trans_count * sizeof(modr_global_transition) <= total_size - 4`
10. Усі `action_hash` дій розв'язуються в `ActionRegistry::find_action`
11. Усі `action_hash` умов розв'язуються в `ActionRegistry::find_condition`
12. Типовий таймаут фази > 0
13. Усі зміщення пулу рядків < `total_size - 4`

Збій → повертається `EngineError`, файл відкидається, стан рушія не змінюється.

## Граничні випадки

- **Порожні пули** (немає дій / глобальних переходів / ресурсів) — представлені `*_count = 0`, зміщення може бути 0 (завантажувач пропускає).
- **Сценарій з однією доріжкою** — `track_count = 1`, найпростіша валідна форма. Використовується в еталонному `minimal_v1.modr`.
- **Безумовний перехід** — `kind = MODR_TRANS_KIND_UNCONDITIONAL (4)`, поля `cond_pool_idx` і `time_threshold_ms` ігноруються. Спрацьовує одразу після завершення дій входу у фазу. Рушій відхиляє неоднозначні комбінації (наприклад, `kind=COND` із `cond_pool_idx=NO_OFFSET`) як `INVALID_FILE`.
- **Неявний перехід по таймауту** — `timeout_ms = 0` у фазі замінюється на `header.default_phase_timeout_ms`. Якщо досягнуто таймауту І жоден явний часовий перехід його не перехопив → рушій синтезує неявний перехід до `$abort`.

## Політика версіонування

Зміни версії формату:
- **Патч (1.x):** лише зміни коментарів, без змін розкладу структур. Та сама `format_version = 1`, але задокументована в ADR.
- **Мінорна:** нові опціональні поля в кінці структури (нульова ініціалізація прийнятна для старих завантажувачів). `format_version` збільшується до 2; завантажувач підтримує обидві.
- **Мажорна (несумісна):** розклад структур змінюється несумісно. Нова `format_version`. Надається інструмент міграції (після етапу 1).

Зараз `format_version = 1`. Збільшиться, якщо бінарний розклад зміниться після першого продакшн-розгортання.

## Дивіться також

- ADR-0001 — обґрунтування бінарного формату проти `constexpr`
- ADR-0007 — обов'язкові таймаути фаз (правило валідації 12)
- ADR-0008 — висновки паперового пілоту з кроку 0.75 (поля фазових ресурсів, +4 байти)
- План `.claude/plans/quirky-imagining-lake.md` Q1 — оригінальна специфікація (з арифметичними виправленнями, відзначеними вище)
