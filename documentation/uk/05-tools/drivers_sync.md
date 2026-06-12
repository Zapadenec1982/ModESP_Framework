# `drivers_sync.py` — узгодження menuconfig з платою

> 📖 **In English:** [documentation/en/05-tools/drivers_sync.md](../../en/05-tools/drivers_sync.md)

Інструмент для свідомого запуску, що вирівнює per-driver toggle'и menuconfig
(`CONFIG_MODESP_DRIVER_<NAME>`) з тим, що реально використовує `bindings.json`
**активної** плати. Це фікс одною командою для помилки збірки, коли плата
прив'язує драйвер, вимкнений у menuconfig.

Він **не** запускається під час `idf.py build` — білд є неінтерактивним
fail-fast гейтом. Цей тул — місце, де живе інтерактивне вмикання/вимикання +
авто-редагування, що запускається навмисно.

## Що він робить

1. Читає `data/board.json`, `data/bindings.json`, усі `drivers/*/manifest.json`
   і `sdkconfig`.
2. **Валідує** bindings першим (ті самі перевірки, що й білд); якщо невалідні —
   повідомляє і нічого не міняє.
3. Обчислює два набори:
   - **Bound but disabled** — плата прив'язує драйвер, але
     `CONFIG_MODESP_DRIVER_<NAME>` вимкнено → пропонує **увімкнути**.
   - **Enabled but unused** — скомпільований, але жоден binding не використовує →
     пропонує **вимкнути** (менший бінарник). **Discovery-драйвери** (напр.
     `ds18b20`, який сканують *до* додавання bindings) виключаються.
4. Застосовує зміну редагуванням `sdkconfig` **напряму** (значення, не Kconfig
   default). Звичайний `idf.py build` потім її застосує — **без `fullclean`** для
   зміни значення.

## Використання

```bash
python tools/drivers_sync.py                 # інтерактивно: запит на кожну зміну
python tools/drivers_sync.py --dry-run       # показати різницю, нічого не міняти
python tools/drivers_sync.py --fix --yes     # увімкнути bound-but-disabled, без запитів
python tools/drivers_sync.py --fix --prune --yes   # також вимкнути невикористані
python tools/drivers_sync.py --fix --yes --rebuild # потім запустити idf.py build
```

| Прапорець | Дія |
|---|---|
| `--fix` | увімкнути драйвери, які плата прив'язує, але вимкнені |
| `--prune` | додатково вимкнути драйвери, увімкнені, але не використані платою |
| `--yes` | застосувати без запитів (для скриптів/CI) |
| `--dry-run` | лише звіт; нічого не пише |
| `--rebuild` | запустити `idf.py build` після (потрібен ESP-IDF-shell) |
| `--data-dir`, `--drivers-dir`, `--sdkconfig` | перевизначити шляхи за замовчуванням |

Без прапорців — інтерактивно; `--fix` мається на увазі, якщо не задано ні
`--fix`, ні `--prune`.

## Зв'язок зі збіркою

| Шар | Поведінка |
|---|---|
| `idf.py build` (gate) | Якщо bound-драйвер вимкнено → **FATAL** з повідомленням, що називає цей тул. Валідує bindings. Друкує `INFO:` зі списком увімкнених, але невикористаних драйверів. |
| `drivers_sync.py` (fix) | Інтерактивне/авто вмикання+вимикання, редагує `sdkconfig`, опційний rebuild. |

Gate живе в `components/modesp_hal/CMakeLists.txt` (після
`idf_component_register`, тож виконується лише в реальній config-фазі, де
`CONFIG_*` визначені). Набір bound-драйверів береться з
`generated/required_drivers.cmake`, який генератор виводить із bindings активної
плати.

## Нотатки

- `sdkconfig` у .gitignore і свій у кожного розробника; тул редагує *твою* копію.
- Зміна *значення* (увімкнути/вимкнути наявний драйвер) потребує лише
  `idf.py build`. `fullclean` потрібен тільки коли додано **нову теку драйвера**
  (вона додає новий component-Kconfig, який ESP-IDF кешує).
- Тул використовує `validate_bindings`/`unused_drivers` з
  [`generate_ui.py`](generate_ui.md), тож його правила точно збігаються зі збіркою.

## Джерело

- [`tools/drivers_sync.py`](../../../tools/drivers_sync.py)
- [04-hardware/bindings.md](../04-hardware/bindings.md) — правила валідації.
- [02-module-author-guide/writing-a-driver.md](../02-module-author-guide/writing-a-driver.md) — драйвери + menuconfig.
