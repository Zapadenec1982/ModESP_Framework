# `compile_scenario.py` — маніфест рецепту → двійковий файл `.modr`

> 📖 **In English:** [documentation/en/05-tools/compile_scenario.md](../../en/05-tools/compile_scenario.md)

Інструмент часу складання, який компілює блоки `scenario:` з маніфестів
рецептів у компактний двійковий формат (`.modr`), що споживається
рушієм сценаріїв.

Аналог `generate_ui.py` — та сама роль на стороні складання, інший
вихід: замість генерації C++-заголовків і `ui.json` цей інструмент
видає один файл `.modr` на рецепт. Рушій `Engine::load_buffer` парсить
їх під час виконання.

ВИМАГАЄ: Python 3.8+, `jsonschema` (`pip install -r tools/requirements.txt`).

```
python tools/compile_scenario.py \
    --modules-dir modules \
    --output-dir data/scenarios
```

Скомпільовані файли `.modr` потрапляють у `data/scenarios/` і
постачаються як частина розділу даних LittleFS. Рушій завантажує їх
або за шляхом (`Engine::load_path`), або з попередньо завантаженого
буфера (`Engine::load_buffer`).

## Що читає

- Кожен `modules/<name>/manifest.json` з `module_type: "recipe"` і
  ключем `scenario:`.
- `tools/scenario_schema.json` — JSON-схема, що застосовується до
  кожного маніфесту.
- `tools/known_actions.json` — каталог дозволених імен дій та умов із
  їхніми сигнатурами параметрів. Маніфести рецептів можуть посилатися
  лише на дії з цього каталогу.

## Що пише

На кожен рецепт — один двійковий файл:

```
data/scenarios/<recipe_name>.modr
```

Двійковий формат задокументований у
`modesp_scenario/include/modesp/scenario/modr_format.h`. Основне:

- Магічне число `'MODR'` (0x52444F4D), версія 1.
- Заголовок (56 байт) із лічильниками і зсувами до кожної секції.
- Масив доріжок, масив фаз, масив переходів, масив дій, масив параметрів.
- Пул рядків (дедупльовані UTF-8 рядки з префіксом довжини).
- Хвостовий CRC32 для перевірки цілісності під час завантаження.

Максимальний розмір: 16 КБ на рецепт (`MODR_MAX_SIZE`). Більшість
рецептів утримуються у межах ~2-4 КБ.

## Імена дій і умов → 16-бітні хеші

Дії й умови хешуються через **djb2_hash16**, щоб рушій міг
диспетчеризувати їх за константний час без порівняння рядків.
Компілятор хешує імена, реєстри реєструють обробники за хешем. Колізії
(рідкісні у невеликому просторі імен known_actions) спричиняють помилку
компіляції з вказівником на обидва імена, що зіткнулися — одне з них
треба перейменувати.

```python
def djb2_hash16(s: str) -> int:
    h = 5381
    for ch in s.encode("utf-8"):
        h = ((h << 5) + h + ch) & 0xFFFFFFFF
    return h & 0xFFFF
```

Сторона C++ має ідентичну реалізацію у `modr_format.h`.

## Категорії помилок

Інструмент видає `<file>:<line>:<col>: error[<CODE>]: <msg>` із
числовими кодами, згрупованими за фазою:

| Префікс коду | Фаза | Приклад |
|---|---|---|
| E01xx | Валідація схеми | E0101: `tracks` is not an array |
| E02xx | Семантика (посилання, типи, колізії хешу) | E0204: action `unknown_action` not у known_actions.json |
| E03xx | Емісія двійкового файлу | E0301: phase string pool overflow |
| E04xx | Перехресна валідація із manifest.state | E0401: scenario refs `equipment.foo` not declared у state |

Ненульовий код виходу при будь-якій помилці. Складання CMake падає
відповідно.

## Використання CLI

**Зібрати всі рецепти:**

```
python tools/compile_scenario.py --modules-dir modules --output-dir data/scenarios
```

**Зібрати окремий рецепт:**

```
python tools/compile_scenario.py --recipe modules/abs_test/manifest.json --output abs_test.modr
```

**Перевірити без запису:**

```
python tools/compile_scenario.py --modules-dir modules --dry-run
```

## Інтеграція з CMake

Підключений у складання так само, як і `generate_ui.py` — кастомна
команда, що залежить від глобу маніфестів, з виводом, записаним у
образ розділу даних:

```cmake
add_custom_command(
    OUTPUT ${MODR_FILES}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/compile_scenario.py ...
    DEPENDS ${RECIPE_MANIFESTS}
)
```

Файли `.modr` потрапляють до образу LittleFS, що прошивається разом із
прошивкою.

## Перевірка результату

Використайте `dump_modr.py` для огляду скомпільованого `.modr`:

```
python tools/dump_modr.py data/scenarios/abs_test.modr
```

Виводить заголовок, доріжки, фази, переходи та пул рядків. Скористайтеся
`--hex` для сирого байтового дампу поруч зі структурованим виглядом.

## Типові помилки

**`jsonschema` не встановлено:** інструмент виходить із кодом 2 і
вказівником на `tools/requirements.txt`. Встановіть і повторіть.

**`known_actions.json` застарілий:** коли ви додаєте дію у C++, також
зареєструйте її у `known_actions.json` із сигнатурою параметрів. Інакше
всі рецепти, що посилаються на неї, не скомпілюються.

**Ім'я рецепту задовге:** дзеркальні ключі мають формат `<recipe>.<key>`
з бюджетом 32 символи. Бюджет імені рецепту — **12 символів**, щоб
типові дзеркальні ключі вміщувалися. Інструмент попереджає при 13+;
видає помилку при 16+.

**Колізія хешу:** якщо ім'я нової дії зіштовхується з наявним, ви
отримаєте помилку часу компіляції з обома іменами. Перейменуйте одне,
щоб розв'язати колізію. У djb2 ймовірність колізії ~1 на 60 тис. —
рідкість на практиці.

## Що далі

- **[02-module-author-guide/recipe-authoring.md](../02-module-author-guide/recipe-authoring.md)** —
  довідник граматики рецепту.
- **[dump_modr.md](dump_modr.md)** — огляд скомпільованих двійкових файлів.
- **[03-framework-reference/components/modesp_scenario.md](../03-framework-reference/components/modesp_scenario.md)** —
  рушій, що споживає `.modr`.
- **[03-framework-reference/modules/abs_test.md](../03-framework-reference/modules/abs_test.md)** —
  референсний рецепт.

## Джерела

- [`tools/compile_scenario.py`](../../../tools/compile_scenario.py)
- [`tools/scenario_schema.json`](../../../tools/scenario_schema.json)
- [`tools/known_actions.json`](../../../tools/known_actions.json)
- [`components/modesp_scenario/include/modesp/scenario/modr_format.h`](../../../components/modesp_scenario/include/modesp/scenario/modr_format.h) —
  двійковий формат.
