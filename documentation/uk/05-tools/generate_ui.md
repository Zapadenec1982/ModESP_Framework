# `generate_ui.py` — валідатор маніфестів і генератор коду

> 📖 **In English:** [documentation/en/05-tools/generate_ui.md](../../en/05-tools/generate_ui.md)

`tools/generate_ui.py` — це точка входу під час складання, яка
перетворює **маніфести модулів** на:

1. **`data/ui.json`** — об'єднана схема UI, що подається під час
   виконання через `GET /api/ui`.
2. **`generated/state_meta.h`** — `constexpr`-метадані, які
   використовують HTTP-обробники та SharedState для валідації.
3. **`generated/mqtt_topics.h`** — попередньо обчислені рядки топіків MQTT.
4. **`generated/display_screens.h`** — дані меню дисплея/LCD.

Запускайте з кореня проєкту:

```
python tools/generate_ui.py
```

Вивід детермінований: повторний запуск на незмінних вхідних даних
дає байт-у-байт ідентичні файли. Система складання ESP-IDF викликає це
через кастомну команду CMake — генератор вбудований у граф складання,
тож редагування маніфесту запускає повторну генерацію.

Сам WebUI (`data/www/*.html|js|css`) є **статичним**. Він завантажує
`ui.json` під час виконання — саме це робить фреймворк
manifest-driven.

ВИМАГАЄ: Python 3.8+. Без зовнішніх пакетів (лише stdlib).

## Що читає

| Вхід | Призначення |
|---|---|
| `project.json` | Список модулів і опції часу складання. |
| `modules/<name>/manifest.json` | Стан, UI, MQTT, журналювання для модуля. |
| `drivers/<name>/manifest.json` | Стан, налаштування, UI для драйвера. |

Завантажуються і перехресно перевіряються як маніфести модулів, так і
маніфести драйверів.

## Що валідує

`ManifestValidator` проходить кожен маніфест перед генерацією:

- `manifest_version` присутній і відповідає підтримуваному (1).
- Обов'язкові поля верхнього рівня (`module`, `state`).
- Угода про іменування ключів стану (`<module>.<key>`, ≤32 символи).
- Сумісність типу віджета з типом стану (наприклад, `slider` потребує
  `float`/`int`).
- Перехресні посилання між модулями: умови `visible_when` вказують на
  справжні ключі.
- Специфічне для рецептів: імена доріжок, посилання на фази, імена дій
  та умов у `tools/known_actions.json`.
- **Bindings ↔ board ↔ driver** (`bindings.json` активної плати): кожен
  `hardware` існує в `board.json`; кожен `driver` має маніфест; `hardware_type`
  драйвера збігається з секцією board його hardware; `requires_address`
  дотримано; повтор hardware лише з `multiple_per_bus` + окремими адресами;
  унікальна role в межах модуля. Неправильна прив'язка ламає білд.

У разі помилки генератор виводить
`<file>:<line>:<col>: error[<code>]: <msg>` (або `[context] message` для
крос-перевірок) і виходить із кодом 1. Складання падає.

## Вихід 1: `data/ui.json` (схема часу виконання)

Структура схеми:

```json
{
  "pages": [
    {
      "id": "thermostat",
      "title": "Thermostat",
      "icon": "home",
      "cards": [...]
    }
  ],
  "i18n": {"uk": {...}, "en": {...}}
}
```

WebUI отримує це один раз при завантаженні та перерендерює, якщо хеш
відрізняється від попереднього завантаження. Локалізовані рядки з
блоків `i18n` кожного маніфесту об'єднуються у словники верхнього рівня.

## Вихід 2: `generated/state_meta.h`

```cpp
namespace modesp::generated {
    struct StateMeta {
        const char* key;
        StateValueType type;
        bool mqtt_publish;
        bool mqtt_subscribe;
        // ...
    };
    constexpr StateMeta STATE_KEYS[] = { ... };
    constexpr size_t STATE_KEY_COUNT = ...;
}
```

Використовується HTTP-обробниками для валідації переданих POST-ом
ключів, MQTT для маршрутизації публікацій та SharedState для
типобезпечного приведення.

## Вихід 3: `generated/mqtt_topics.h`

```cpp
namespace modesp::generated::mqtt {
    constexpr const char* TOPIC_THERMO_TEMPERATURE = "modesp/+/thermostat/temperature";
    // ...
}
```

Обчислені один раз, уникають `sprintf` під час виконання.
Використовується `modesp_mqtt`.

## Вихід 4: `generated/display_screens.h`

Ієрархічне дерево екранного меню, зібране із секцій `display:` усіх
модулів. Споживається модулем `display`
([modules/display.md](../03-framework-reference/modules/display.md)).

```cpp
namespace modesp::gen {
    enum class DisplayItemType : uint8_t {
        SUBMENU, VALUE, EDIT_FLOAT, EDIT_INT, EDIT_BOOL, EDIT_ENUM };

    struct DisplayMenuNode {
        const char* label; const char* key;
        const char* format; const char* unit;
        DisplayItemType type;
        float min, max, step;                  // межі редагування зі state
        const DisplayEnumOption* options;      // для enum/bool
        uint8_t option_count;
        uint8_t first_child, child_count;      // для SUBMENU
    };

    static constexpr DisplayMenuNode MENU_NODES[] = { /* ... */ };
    static constexpr uint8_t MENU_ROOT_COUNT = 1;       // підменю модулів
    static constexpr DisplayMainValue MAIN_VALUES[] = { /* головний екран */ };
}
```

Розкладка `MENU_NODES`: перші `MENU_ROOT_COUNT` вузлів — підменю
модулів (діти root); далі — пункти, неперервно на кожне підменю
(`first_child`/`child_count`). Тип пункту виводиться зі `state`:
`readwrite` float/int → `EDIT_FLOAT`/`EDIT_INT` з `min`/`max`/`step`;
`options` → `EDIT_ENUM` з таблицею опцій; bool → `EDIT_BOOL` з
`on_label`/`off_label`; `read` → `VALUE`. Дерево обмежене 255 вузлами
(індекси `uint8_t`) — генератор зупиняє збірку при перевищенні.

## Вихід 5: реєстрація модулів, драйверний glue та інше

Компактні виходи, що зв'язують білд (усе `DO NOT EDIT`):

| Файл | З чого | Призначення |
|---|---|---|
| `generated/module_includes.h` / `module_instances.h` / `module_register.h` | `project.json` | `#include`, статичні інстанси, виклики `register_module()` для `main.cpp` |
| `generated/modules.cmake` | `project.json` | список module-компонентів для REQUIRES у `main/CMakeLists.txt` |
| `generated/mqtt_topics.h` | секції `mqtt` | константи топіків на ключ для `MqttService` |
| `generated/datalogger_channels.h` / `datalogger_events.h` | секції `loggable` | таблиці id каналів/подій для DataLogger |
| `components/modesp_hal/Kconfig` | `drivers/*/manifest.json` | toggle `CONFIG_MODESP_DRIVER_<NAME>` на драйвер (menu "ModESP Drivers") |
| `generated/drivers.cmake` | драйвери | список `MODESP_ALL_DRIVERS` для REQUIRES `modesp_hal` |
| `generated/driver_register_all.h` | драйвери | guarded `modesp_register_all_drivers()` (пропускає вимкнені) |
| `generated/required_drivers.cmake` | активний `bindings.json` | `MODESP_BOUND_DRIVERS` — перевіряється `modesp_hal/CMakeLists.txt`: FATAL, якщо bound-драйвер вимкнено |
| `data/www/i18n/*.json` | `i18n/` модулів | об'єднані пакети перекладів |

Драйверний glue робить кожен драйвер опційним і самореєструваним; див.
[drivers_sync.md](drivers_sync.md) і
[writing-a-driver.md](../02-module-author-guide/writing-a-driver.md).

## Опції CLI

```
python tools/generate_ui.py --help
```

| Прапор | Призначення |
|---|---|
| `--project FILE` | Шлях до project.json (типово: `./project.json`). |
| `--modules-dir DIR` | Корінь маніфестів модулів (типово: `./modules`). |
| `--output-data DIR` | Куди писати `ui.json` (типово: `./data`). |
| `--output-gen DIR` | Куди писати `state_meta.h` тощо (типово: `./generated`). |
| `--strict` | Розглядати попередження як помилки. |

## Інтеграція з CMake

`CMakeLists.txt` оголошує кастомну команду, що залежить від глобу
маніфестів:

```cmake
add_custom_command(
    OUTPUT ${GEN_HEADERS}
    COMMAND python ${CMAKE_SOURCE_DIR}/tools/generate_ui.py ...
    DEPENDS ${MANIFEST_FILES}
)
```

Редагування маніфесту → CMake перезапускає генератор → перегенеровані
заголовки → відповідні компоненти перезбираються.

## Типові помилки

**Помилки кодування на Windows:** скрипт автоматично переналаштовує
stdout на UTF-8. Якщо перенаправляєте у файл і бачите символи `?`,
скористайтеся PowerShell з UTF-8 за замовчуванням або пропустіть через
`chcp 65001`.

**Забута реєстрація маніфесту:** модулі, не перелічені у `project.json`,
мовчки пропускаються. Якщо UI вашого нового модуля не з'являється,
спершу перевірте `project.json`.

**Невідповідність типу віджета:** `slider` на ключі `bool` дає помилку
валідатора. Або оберіть правильний віджет (`toggle` для bool-значень),
або виправте тип стану.

**Розбіжність схеми при повторному складанні:** якщо `ui.json` змінює
байти без змін у маніфестах — це баг; генерація має бути
детермінованою. Створіть issue.

## Що далі

- **[compile_scenario.md](compile_scenario.md)** — аналогічний
  інструмент для маніфестів рецептів, що видає двійкові файли `.modr`.
- **[02-module-author-guide/manifest.md](../02-module-author-guide/manifest.md)** —
  довідник схеми маніфесту.
- **[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)** —
  які віджети UI видає генератор.

## Джерела

- [`tools/generate_ui.py`](../../../tools/generate_ui.py)
- [`tools/known_actions.json`](../../../tools/known_actions.json) — каталог дій / умов.
