# 09 — Інтеграція з маніфестом

> 📖 **In English:** [documentation/en/03-framework-reference/scenario-engine/09_manifest_integration.md](../../../en/03-framework-reference/scenario-engine/09_manifest_integration.md)

**Статус:** Заповнено на Step 2a (`compile_scenario.py` готовий). Оновлено на Step 4 (розширення `generate_ui.py`).
**ADR:** [`adr/0004-recipe-as-manifest.md`](adr/0004-recipe-as-manifest.md)
**Код:** [`tools/compile_scenario.py`](../../tools/compile_scenario.py), [`tools/scenario_schema.json`](../../tools/scenario_schema.json), [`tools/known_actions.json`](../../tools/known_actions.json)

## Основний принцип

> **Рецепт — це маніфест.** Окремого формату файла немає. Існуючий пайплайн маніфестів ModESP (який обробляє секції state, ui, mqtt, loggable) розширюється опційною секцією `scenario`.

Один JSON-файл, два екстрактори:

| Екстрактор | Читає секцію | Вихід |
|---|---|---|
| `tools/generate_ui.py` (існуючий — невелике розширення на Step 4) | `state`, `ui`, `mqtt`, `loggable`, `features` | `data/ui.json`, `generated/state_meta.h`, `generated/mqtt_topics.h` тощо |
| `tools/compile_scenario.py` (новий, Step 2) | `scenario` | `data/scenarios/<recipe_name>.modr` (бінарний) |

## Скелет маніфесту рецепта

```jsonc
{
  "manifest_version": 1,
  "module": "recipe_my_process",       // ім'я модуля; використовується як ключ NVS і шлях до файла
  "module_type": "recipe",             // НОВЕ поле — відрізняє рецепт від модуля на C++
  "version": "1.0.0",
  "priority": 5,                       // ігнорується для рецептів (без runtime на C++)

  "state": {
    // Ключі дзеркального стану, які рушій ЗАПИСУВАТИМЕ під час виконання.
    // Перехресно валідуються `compile_scenario.py` (функція Step 2b).
    "recipe_my_process.scenario_state": {"type": "string", "access": "read"},
    "recipe_my_process.main_phase_name": {"type": "string", "access": "read"}
  },

  "ui": {
    // Стандартна секція UI — `generate_ui.py` генерує віджети.
    "page": "Process",
    "icon": "play",
    "cards": [{
      "title": "Status",
      "visible_when": {"recipe_my_process.scenario_state": ["running", "paused"]},
      "widgets": [{"key": "recipe_my_process.main_phase_name", "widget": "value"}]
    }]
  },

  "mqtt": {
    "publish": ["recipe_my_process.scenario_state"]
  },

  "scenario": {                        // НОВА секція — `compile_scenario.py` генерує .modr
    "default_phase_timeout_ms": 60000,
    "completion_rule": "all_tracks_complete",
    "tracks": [
      {
        "name": "main",
        "flags": ["main_track"],
        "phases": [
          {"name": "init", "transitions": [{"to": "$complete"}]}
        ]
      }
    ]
  }
}
```

## Інтеграція з пайплайном збірки

Обидва інструменти запускаються до збірки (хуки CMake). Порядок незалежний — вони не залежать від виходу одне одного. Обидва сканують `modules/*/manifest.json`.

### Алгоритм `compile_scenario.py`

1. **Пошук:** сканує `modules/*/manifest.json`, фільтрує ті, що мають `"module_type": "recipe"` ТА ключ `"scenario"`.

2. **Завантаження JSON:** парсить маніфест. Помилки перехоплюються і повторно піднімаються як `CompileError` з координатами file:line:col.

3. **Валідація схеми** ([`tools/scenario_schema.json`](../../tools/scenario_schema.json), draft-07):
    - Обов'язкові поля (`default_phase_timeout_ms`, `completion_rule`, `tracks`)
    - Обмеження типів і enum-ів
    - Обмеження розміру масивів (макс. 6 доріжок, макс. 32 фази на доріжку, макс. 8 переходів на фазу)
    - Обмеження за шаблонами (ім'я доріжки `^[a-z][a-z0-9_]{0,7}$`, ім'я фази `^[a-z][a-z0-9_]{0,15}$`)
    - Композиційні умови (`all_of`/`any_of`/`not` рекурсивно)
    Помилки → **E0101** (один код помилки охоплює всі провали схеми з докладним внутрішнім повідомленням).

4. **Семантична унікальність** (E0208):
    - Імена доріжок унікальні в межах сценарію
    - Імена фаз унікальні в межах кожної доріжки (різні доріжки можуть повторно використовувати імена)
    - `uniqueItems` JSON Schema не діє на масиви об'єктів, тому перевірка явна.

5. **Розв'язання хешів:**
    - Імена дій/умов → нижні 16 біт djb2_hash16
    - Перехресно перевіряються з реєстром `tools/known_actions.json`
    - Виявлення колізій: дві відомі дії з однаковим хешем → **E0203**
    - Невідповідність хешу (збережений ≠ обчислений): **E0202**

6. **Перехресна валідація** (E04XX, відкладено до Step 2b):
    - Ключі дзеркального стану, які рушій записує, виводяться з секції scenario
    - Порівнюються з секцією `state` маніфесту
    - Невідповідність → помилка компіляції

7. **Емісія бінарного формату** (за [`02_binary_format.md`](02_binary_format.md)):
    - Інтернування пулу рядків (дедуплікація з префіксом довжини)
    - Планування розміщення (зсуви обчислюються заздалегідь)
    - Заголовок → доріжки → таблиці фаз → масиви переходів → ресурси → пул рядків → CRC32
    - Усі структури вирівняні природно (без packing)
    - Загальний розмір звіряється з полем `header.total_size`

8. **Хвіст CRC32:** CRC-32/ISO-HDLC по всьому тілу (збігається з Python `zlib.crc32` ТА ESP-IDF `esp_crc32_le`).

9. **Вихід:** `data/scenarios/<module_name>.modr` (запаковується в LittleFS під час прошивки).

### Формат повідомлення про помилку

```
<file>:<line>:<col>: error[<code>]: <human message>
```

Приклад:
```
modules/recipe_plov/manifest.json:42:18: error[E0207]: transition target 'wrong_phase' from phase 'simmer' not found у track 'heat'. Valid targets: ['warmup', 'simmer', 'finish'] + ['$complete', '$abort']
```

### Каталог кодів помилок

| Код | Клас | Тригер |
|------|-------|---------|
| E0001 | Setup | Не вистачає необхідного Python-пакета (jsonschema) |
| E0101 | Schema | Будь-який провал валідації JSON Schema draft-07 |
| E0102 | Parse | Некоректний JSON |
| E0103 | Manifest | Відсутнє поле `module` |
| E0104 | Manifest | Хибне/відсутнє `module_type` (має бути "recipe") |
| E0105 | Manifest | Відсутня секція `scenario` |
| E0202 | Registry | Хеш у `known_actions.json` ≠ djb2_hash16(name) |
| E0203 | Registry | Два імені в `known_actions.json` з однаковим хешем (колізія) |
| E0204 | Semantics | Використано ресурси phase-scope (відкладено до Step 2b) |
| E0205 | Semantics | Використано глобальні переходи (відкладено до Step 2b) |
| E0206 | Semantics | Використано умовні переходи (відкладено до Step 2b) |
| E0207 | Semantics | Ціль переходу посилається на невідому фазу |
| E0208 | Semantics | Дубльоване ім'я доріжки або фази |
| E0210 | Semantics | Невідомий оператор умови |
| E0211 | Semantics | Вираз умови має бути об'єктом з одним ключем |
| E0212 | Semantics | `time_elapsed_ms` вимагає невід'ємного цілого |
| E0213 | Semantics | `state_key_*` без обов'язкового поля (key/value) |
| E0214 | Semantics | `time_of_day_eq` без hh/mm |
| E0215 | Semantics | `all_of`/`any_of` вимагає непорожнього масиву |
| E0216 | Semantics | Рядкове значення в умові без контексту string_pool |
| E0217 | Semantics | Непідтримуваний тип значення для параметра умови |
| E0218 | Semantics | Композитна умова перевищує максимальну глибину вкладеності (16) — захист від DoS |
| E0220 | Action | Виклик дії без поля 'action' |
| E0221 | Action | Параметри дії мають бути об'єктом |
| E0222 | Action | Кількість параметрів дії не збігається з дескриптором |
| E0223 | Globals | Глобальний перехід `to` не "$abort" і не пропущений |
| E0224 | Globals | Глобальний перехід без блоку `when` |
| E0225 | Action | Хибний параметр 'type' для `set_state` (має бути i32/f32/bool) |
| E0301 | Emission | Рядок перевищує ліміт довжини u8 (>255 байт) |
| E0302 | Emission | Скомпільований бінарник перевищує `MODR_MAX_SIZE` (16 КБ) |
| E0303 | Emission | Внутрішня помилка: записано байтів ≠ `header.total_size` |
| E0226 | Strict | Невідоме ім'я дії (режим --strict підвищує W0220) |
| E0231 | Strict | Невідомий ContinuousBehavior (режим --strict підвищує W0230) |
| E0401 | Cross-val | У `manifest.state` бракує оголошень дзеркальних ключів |
| E0402 | Cross-val | Виведений дзеркальний ключ перевищує бюджет SharedState (32 символи) |
| E0403 | Cross-val | Невідповідність типу — маніфест оголошує хибний тип для дзеркального ключа |

### Попередження (без блокування, типовий режим)

| Код | Клас | Тригер | Стає в --strict |
|------|-------|---------|-------------------|
| W0220 | Action | Невідоме ім'я дії (доменний модуль має зареєструвати її на runtime) | E0226 |
| W0230 | Continuous | Невідоме посилання на ContinuousBehavior у `phase.continuous` | E0231 |

### Прапорці CLI

- `--strict` — підвищує попередження до помилок. Стандартний для індустрії патерн (TypeScript `--strict`, GCC `-Werror`, ESLint `--max-warnings 0`). Використовується в CI для виявлення друкарських помилок і дрейфу.

Повні описи з прикладами → [`10_error_model.md`](10_error_model.md) (заповниться на Step 2b, коли накопичаться).

## Розширення `generate_ui.py` (Step 4)

Мінімальні зміни (~30 LOC):
- Розпізнавати `"module_type": "recipe"` як валідне значення (зараз лише "module").
- Пропускати генерацію C++ binding для рецептів (немає `<name>_module.cpp`, зареєстрованого в `module_register.h`).
- Розпізнавати секцію `"scenario"` як валідну (пропускати — її обробляє `compile_scenario.py`).

З боку UI: існуючі віджети обробляють ключі стану рецепта без змін. Обмеження `visible_when` (вже функція `generate_ui.py`) показують віджети рецепта лише коли сценарій активний.

## Інфраструктура golden-файлів

`tools/tests/fixtures/scenarios/<name>.modr` — закомічені бінарники. Pytest читає їх і порівнює побайтово з виходом збирача/компілятора.

Процедура оновлення (Step 2b, у роботі):
```bash
python tools/compile_scenario.py --regenerate-goldens
```
З явним підтвердженням — оновлення golden-файлів потребує перегляду.

## Модель розповсюдження

Рецепти постачаються з прошивкою (запаковуються в LittleFS під час прошивки). Додавання/зміна рецепта наразі вимагає перебудови прошивки. Це відповідає скоупу MVP.

**Розширення Stage 1.5:** OTA-завантажувані рецепти через узагальнені ключі дзеркального стану (за потреби). Модель «рецепт як маніфест» залишається; лише механізм доставки додатково підтримує cloud push.

## Обмеження іменування і бюджету

| Обмеження | Ліміт | Обґрунтування |
|---|---|---|
| Ім'я рецепта (модуля) | ≤ 12 символів (`MAX_RECIPE_NAME_LEN`) | Бюджет ключа SharedState (32 символи разом) |
| Ім'я доріжки | ≤ 8 символів (`MAX_TRACK_NAME_LEN`) | Той самий бюджет — `recipe_X.track_field` ≤ 32 |
| Ім'я фази | ≤ 16 символів | Вільніше — використовується лише в дзеркальному ключі `phase_name`, не конкатенується |
| Доріжок на сценарій | ≤ 6 (`MAX_TRACKS_PER_SCENARIO`) | Бюджет RAM (~32 Б на екземпляр доріжки × 4 екземпляри) |
| Фаз на доріжку | ≤ 32 | З запасом; реалістичні рецепти 3–12 фаз |
| Переходів на фазу | ≤ 8 | Обмеження вартості per-tick eval |
| Ресурсів на сценарій | ≤ 32 | Розмір карти володіння рушія |
| Загальний розмір .modr | ≤ 16 КБ (`MODR_MAX_SIZE`) | Буфер заздалегідь виділяється для кожного завантаженого сценарію |

## Дивіться також

- [02_binary_format.md](02_binary_format.md) — деталі байтового розміщення `.modr`
- [10_error_model.md](10_error_model.md) — описи кодів помилок з прикладами тригерів
- [usage/02_writing_recipes.md](usage/02_writing_recipes.md) — посібник для авторів
- [adr/0004-recipe-as-manifest.md](adr/0004-recipe-as-manifest.md) — обґрунтування дизайну
- План `.claude/plans/quirky-imagining-lake.md` Q9 — оригінальна специфікація
