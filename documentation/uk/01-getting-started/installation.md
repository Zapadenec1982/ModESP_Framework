# Встановлення — інструментарій і перше збирання

> 📖 **In English:** [documentation/en/01-getting-started/installation.md](../../en/01-getting-started/installation.md)

Ця сторінка описує налаштування інструментарію ESP-IDF, клонування
репозиторію та створення вашого першого бінарного файлу прошивки. Після
цього переходьте до **[швидкого старту](quickstart.md)**, щоб прошити
пристрій.

Орієнтовний час: **20–40 хвилин** залежно від того, чи вже встановлений
ESP-IDF.

## Передумови

- Пристрій ESP32 з USB-послідовним адаптером (більшість плат розробника
  мають це вбудовано). Рекомендовано: **ESP32-S3** з 4 МБ+ flash.
  ESP32-WROOM теж підходить.
- USB-кабель з лініями даних (НЕ кабель лише для зарядки — поширена
  помилка).
- Операційна система: Windows 10+, Linux або macOS.
- **8 ГБ вільного місця на диску** (ESP-IDF + інструментарій).
- Python 3.8+ (ESP-IDF встановлює власний).

## Крок 1 — Встановлення ESP-IDF

Фреймворк націлений на ESP-IDF **v5.x** (будь-яка 5.x.y; протестовано на
5.1, 5.2, 5.3).

**Windows (PowerShell, рекомендовано):**

Скористайтеся офіційним інсталятором: <https://dl.espressif.com/dl/esp-idf/>.
Він об'єднує Python, Git та інструментарій в один MSI. Виберіть
"Online installer" і версію v5.x.

Після встановлення відкрийте "ESP-IDF 5.x PowerShell" зі стартового
меню — це попередньо налаштована оболонка з установленим
`$env:IDF_PATH`.

**Linux / macOS:**

```bash
mkdir -p ~/esp && cd ~/esp
git clone --branch release/v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32,esp32s3
```

Додайте крок активації до rc-файла вашої оболонки:

```bash
echo '. $HOME/esp/esp-idf/export.sh' >> ~/.bashrc
```

Відкрийте новий термінал — `idf.py --version` має працювати.

## Крок 2 — Клонування фреймворку

```
git clone https://github.com/<your-org>/modesp-v4.git
cd modesp-v4
```

Репозиторій містить:

- `components/` — бібліотеки фреймворку (modesp_*).
- `modules/` — бізнес-модулі (рецепти та не-рецепти).
- `drivers/` — апаратні драйвери.
- `main/` — `main.cpp` з підключенням модулів.
- `tools/` — генератори часу збирання на Python.
- `data/www/` — попередньо зібраний бандл WebUI.

## Крок 3 — Встановлення Python-інструментарію

Збирання викликає кілька Python-інструментів (`generate_ui.py`,
`compile_scenario.py`). Вони використовують пакети з
`tools/requirements.txt`:

```
pip install -r tools/requirements.txt
```

(Використовуйте той Python, який активував ESP-IDF. На Windows
PowerShell ESP-IDF його фіксує; на Linux/macOS це робить скрипт
активації.)

## Крок 4 — Перше збирання

З кореня репозиторію:

```
idf.py set-target esp32s3    # or esp32, esp32c3 — match your hardware
idf.py build
```

Це:

1. Генерує метадані UI / стану / тем MQTT з маніфестів.
2. Компілює всі рецепти `.modr`.
3. Компілює весь проєкт.
4. Будує образ даних LittleFS.

Загальний час першого збирання: **3–8 хвилин** залежно від CPU.
Подальші інкрементальні збирання: **5–30 секунд**.

Очікуваний вивід завершується на:

```
Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool ... write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x10000 build/<project>.bin 0x8000 build/partition_table/partition-table.bin
```

Якщо ви це бачите — інструментарій справний. Переходьте до
**[quickstart.md](quickstart.md)**.

## Крок 5 — Налаштування редактора (необов'язково)

**VS Code:** встановіть офіційне розширення **Espressif IDF**.
Налаштуйте: Command Palette → "ESP-IDF: Configure ESP-IDF Extension".
Розширення підключає IntelliSense, зневаджувач, команди прошивки та
монітора.

**CLion / інші:** направте CMake на проєкт. ESP-IDF генерує
`compile_commands.json`, який розпізнає більшість редакторів C/C++.

## Типові помилки

**`idf.py: command not found`** — середовище IDF не активовано.
Запустіть `~/esp/esp-idf/export.sh` або відкрийте ESP-IDF PowerShell.

**`Python is not installed correctly`** — зазвичай Python WSL/MSYS
конфліктує з Python від ESP-IDF. Використовуйте Python, який поставляє
ESP-IDF; закрийте будь-який інший термінал.

**Збирання зависає на "Building ETL..."** — `marcel-cd__etlcpp` 1.0.1
має відому ваду `externalproject_add`. Виправлення: відредагуйте
`managed_components/marcel-cd__etlcpp/CMakeLists.txt` і закоментуйте
блок `externalproject_add`. (Див. журнал перебудови рушія для
контексту.)

**`fatal: could not read Username`** — Git запитує облікові дані для
вивантаження підмодулів. Налаштуйте помічника облікових даних GitHub
або використовуйте SSH-URL у `.gitmodules`.

**Місце на диску вичерпалося посеред збирання:** очистіть командою
`idf.py fullclean` і перевірте розмір `build/` перед повторною
спробою. Артефакти збирання займають ~2 ГБ.

## Що далі

- **[quickstart.md](quickstart.md)** — прошивка й запуск еталонного
  сценарію.
- **[concepts.md](concepts.md)** — 4 ключові ментальні моделі перед
  написанням вашого першого модуля.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)** —
  починаєте писати.

## Джерела

- Документація ESP-IDF: [Get Started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html).
- `tools/requirements.txt` у корені фреймворку.
