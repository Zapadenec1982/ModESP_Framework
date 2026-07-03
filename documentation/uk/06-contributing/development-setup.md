# Налаштування середовища розробки

> 📖 **In English:** [documentation/en/06-contributing/development-setup.md](../../en/06-contributing/development-setup.md)

Ця сторінка для **контриб'юторів фреймворку**. Якщо ви пишете бізнес-модулі
поверх фреймворку, дивіться натомість
**[01-getting-started/installation.md](../01-getting-started/installation.md)**.

## Передумови — ті самі, що й для налаштування автора модуля

- Інструментарій ESP-IDF v5.x (див. посібник з установки).
- Python 3.8+ зі встановленими залежностями з `tools/requirements.txt`.
- Git з підтримкою субмодулів.
- Необов'язково: clang-format для дотримання стилю C++.

Додаткові потреби контриб'ютора:

- Пристрій ESP32 для HIL-тестування (потрібний, щоб валідувати зміни рушія).
- Надійно під'єднаний USB-serial (дешеві адаптери CH340 можуть давати збої
  на високих швидкостях передачі — див. deployment.md).
- Сильно рекомендований Linux/macOS-хост для запуску хост-тестів;
  тестова обв'язка збирається стандартним `gcc`, а не xtensa-тулчейном.

## Структура репозиторію для контриб'юторів

```
modesp-v4/
├── components/         ← framework libraries (this is where you'll work)
│   ├── modesp_core/
│   ├── modesp_hal/
│   ├── modesp_services/
│   ├── modesp_net/
│   ├── modesp_mqtt/
│   ├── modesp_aws/
│   └── modesp_scenario/
├── modules/            ← reference business modules (rarely touched)
├── drivers/            ← hardware drivers (touch when adding а new device)
├── main/               ← module wiring; touch when adding а new system service
├── tools/              ← build-time generators AND host test fixtures
├── data/               ← static assets (WebUI, recipes)
└── documentation/      ← документація (EN + UK), яку ви читаєте
```

Уся документація — у `documentation/`. Старий каталог `docs/` видалено
під час чистки після перебудови рушія; його вміст перенесено у
`documentation/` за єдиним стандартом якості.

## Робота з гілками й PR

- `main` — релізна гілка. Прямі push'і заблоковано. Дозволені лише
  fast-forward злиття з гілок-фіч АБО повністю протестовані перебудови.
- Гілки-фічі: `claude/<descriptive-name>` для роботи із залученням ШІ
  АБО `<your-handle>/<descriptive-name>` для ручної роботи.
- Один PR = одна можливість АБО одне виправлення помилки. Уникайте
  змішування.
- Формат заголовка PR: `<area>: <imperative-summary>` (наприклад,
  `scenario: NVS observer + magic bump`).

## Цикл збирання

Значення мають два збирання:

**Збирання прошивки:**

```
idf.py build
idf.py -p COM4 flash flash_data monitor
```

Зачіпає один компонент ESP-IDF → перезбирає цей компонент і його залежних.
Зазвичай 5–30 с інкрементально.

**Збирання хост-тестів:**

```
cd components/modesp_scenario/tests/host
make
./test_engine
```

Виконується повністю на хості. Пристрій не потрібен. Кожен компонент із
каталогом `tests/host/` має власний `Makefile` і фікстуру
`stub_state_backend.h`. Використовуйте їх для швидких ітерацій над
чисто логічними змінами.

## HIL-тести

Обов'язкові для PR, що зачіпають рушій сценаріїв, персистентність АБО
HTTP-поверхню. Виконуються проти живого пристрою:

```
$env:ESP_IP="192.168.4.1"     # PowerShell
$env:ESP_USER="admin"
$env:ESP_PASS="modesp"
python -m pytest tools/tests/test_hil_scenario.py -v
```

Шість тестів покривають рушій сценаріїв наскрізно. Усі мають пройти
перед злиттям.

## Додавання нового компонента фреймворку

1. Створіть `components/modesp_<name>/` із `CMakeLists.txt`,
   `idf_component.yml`, `include/modesp/<name>/` і `src/`.
2. Оголосіть залежності в `idf_component.yml`.
3. Підключіть інстанціювання в `main/main.cpp` (ручний DI; без
   автореєстрації).
4. Додайте сторінку документації в
   `documentation/{en,uk}/03-framework-reference/components/modesp_<name>.md`.
5. Додайте хост-тести в `components/modesp_<name>/tests/host/`.

Файл **writing-a-module.md** з посібника для авторів модулів —
для авторів модулів, а не для контриб'юторів фреймворку. Компоненти
фреймворку не обов'язково успадковуються від `BaseModule` — вони
повноправні рівні наявних компонентів.

## Додавання нового драйвера

1. Створіть `drivers/<name>/` із `manifest.json`,
   `include/<name>_driver.h` і `src/<name>_driver.cpp`.
2. Реалізуйте `IDriver` (зазвичай `ISensorDriver` АБО `IActuatorDriver`).
3. Зареєструйте у списку `drivers:` у `project.json`.
4. Додайте документацію в
   `documentation/{en,uk}/03-framework-reference/drivers/<name>.md`.
5. Тестуйте з фізичним пристроєм — HAL-драйвери неможливо змістовно
   протестувати на хості.

## Залежності й реєстр компонентів

Зовнішні компоненти живуть у `managed_components/` і підтягуються
менеджером компонентів ESP-IDF з оголошень маніфесту. Не додавайте
файли до `managed_components/` вручну. Щоб закріпити версію:

```yaml
# idf_component.yml
dependencies:
  marcel-cd/etlcpp:
    version: "1.0.1"
```

Запустіть `idf.py reconfigure` після зміни залежностей.

## Типові помилки

**Забули перезібрати після зміни маніфесту:** CMake автоматично
викликає генератори під час інкрементальних збирань, але кешований
стан може застаріти після значних правок. Запустіть `idf.py fullclean`,
якщо бачите неузгоджений стан.

**Хост-тести проходять, а пристрій падає:** заглушка бекенду
поблажлива. Реальний SharedState відхиляє зміни типу; перевіряйте
`test_engine` і HIL pytest разом.

**Pull request з незастейдженими змінами WebUI:** пакет WebUI
(`data/www/`) комітиться в git, але збирається окремо. Якщо ви не
запускали збирання WebUI, не чіпайте `data/www/`.

**Дрейф субмодулів:** якщо `git status` показує субмодулі як змінені,
ймовірно сталася ненавмисна зміна стану ESP-IDF/managed_components.
Запустіть `git submodule update --init --recursive`.

## Що далі

- **[testing.md](testing.md)** — деталі хост- і HIL-тестів.
- **[code-style.md](code-style.md)** — конвенції C++.
- **[docs-style.md](docs-style.md)** — стайлгайд документації.

## Джерела

Ця сторінка — узагальнення операційних знань, зібраних під час
переписування ModESP. Файли:

- `tools/tests/test_hil_scenario.py` — фікстура HIL pytest.
- `components/*/tests/host/Makefile` — патерн хост-тестів.
- ESP-IDF [Build System docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html).
