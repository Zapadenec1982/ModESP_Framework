# Написання сервісного модуля

> 📖 **In English:** [documentation/en/02-module-author-guide/writing-a-module.md](../../en/02-module-author-guide/writing-a-module.md)

Ця сторінка — повне покрокове проходження побудови C++ сервісного
модуля — класу, що успадковується від `modesp::BaseModule`, реєструється
у збірці й виконує бізнес-логіку у циклі оновлення 100 Hz. Прочитавши,
ви зможете створити нову папку модуля, написати маніфест і C++, побачити
як модуль завантажується, та взаємодіяти з його станом через WebUI і
HTTP API.

Модулі-рецепти (лише маніфест, без C++) описані у
[recipe-authoring.md](recipe-authoring.md) *(заплановано)*. Драйвери — у
[writing-a-driver.md](writing-a-driver.md) *(заплановано)*.

## Що таке модуль

Сервісний модуль — це підклас `modesp::BaseModule`, який:

- Живе у `modules/<name>/` зі своїм `CMakeLists.txt` і `manifest.json`.
- Конструюється під час static-storage init (без heap, без `new`).
- Отримує три життєві гачки, керовані `ModuleManager`: `on_init()` один
  раз, `on_update(dt_ms)` кожні 10 мс, `on_stop()` при зупинці.
- Опційно отримує повідомлення через `on_message(const etl::imessage&)`.
- Читає й пише стан через хелпери `SharedState`.

Система авто-генерації фреймворку робить шаблонний код невидимим:
маніфест декларує, що робить модуль; згенеровані заголовки
`module_includes.h` / `module_instances.h` / `module_register.h`
прив'язують екземпляр до `main.cpp` без ручних правок.

## Структура папки

```
modules/your_module/
├── manifest.json          ← ОБОВ'ЯЗКОВО — контракт маніфесту
├── CMakeLists.txt         ← ОБОВ'ЯЗКОВО — один рядок на файл
├── include/
│   └── your_module.h      ← Декларація класу модуля
└── src/
    └── your_module.cpp    ← Реалізація
```

> ℹ️ **Примітка:** імена файлів заголовка та джерела мають відповідати
> конвенціям імен C++-класів, які використовує генератор. Використовуйте
> патерни `<name>_module.h` / `<name>_module.cpp` з наявних модулів
> (наприклад, `simple_thermo_module.cpp`).

## Крок 1 — Написати маніфест

Покрийте основу: поля верхнього рівня, ключі стану, опційно `ui` і
`mqtt`. Повний довідник у [manifest.md](manifest.md).

Мінімальний приклад (`modules/my_counter/manifest.json`):

```json
{
  "manifest_version": 1,
  "module": "my_counter",
  "version": "0.1.0",
  "description": "Рахує секунди з моменту boot — proof-of-life demo",
  "priority": 2,

  "state": {
    "my_counter.seconds": {
      "type": "int",
      "access": "read",
      "description": "Секунди з моменту ініціалізації модуля"
    }
  },

  "ui": {
    "page": "Counter",
    "icon": "clock",
    "cards": [{
      "title": "Uptime counter",
      "widgets": [
        {"key": "my_counter.seconds", "widget": "value"}
      ]
    }]
  }
}
```

Збірка автоматично підхоплює це через `project.json` (наступний крок).

## Крок 2 — Зареєструвати у project.json

Додайте ім'я модуля до списку модулів проєкту:

```json
// project.json (root)
{
  "modules": ["equipment", "datalogger", "simple_thermo", "my_counter"]
}
```

Генератор обробляє маніфести лише модулів, перелічених тут. Це дозволяє
тримати багато модулів у репозиторії та обирати, які з них потрапляють
у конкретну збірку прошивки.

Потім додайте те саме ім'я модуля до `main/CMakeLists.txt` → `PRIV_REQUIRES`.
Автореєстрація генерує includes/instances/реєстрацію, але CMake усе одно
потребує явного переліку компонента, щоб залінкувати залежність:

```cmake
# main/CMakeLists.txt
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/generated"
    PRIV_REQUIRES modesp_core modesp_services modesp_hal modesp_net \
                  modesp_scenario equipment datalogger simple_thermo \
                  display my_counter   # ← додайте свій модуль сюди
)
```

Якщо це пропустити — збірка не залінкує символи модуля, навіть якщо він
є у `project.json`.

## Крок 3 — Написати CMakeLists.txt

```cmake
# modules/my_counter/CMakeLists.txt
idf_component_register(
    SRCS "src/my_counter_module.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
)
```

`REQUIRES modesp_core` дає доступ до `BaseModule`, `SharedState`,
`ModulePriority`. Додайте інші компоненти (`modesp_services`,
`modesp_hal` тощо), якщо вашому модулю вони потрібні.

## Крок 4 — Написати C++-клас

Заголовок:

```cpp
// modules/my_counter/include/my_counter_module.h
#pragma once
#include "modesp/base_module.h"

class MyCounterModule : public modesp::BaseModule {
public:
    MyCounterModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

private:
    uint32_t elapsed_ms_ = 0;
    int32_t  seconds_ = 0;
};
```

Джерело:

```cpp
// modules/my_counter/src/my_counter_module.cpp
#include "my_counter_module.h"
#include "esp_log.h"

static const char* TAG = "MyCounter";

MyCounterModule::MyCounterModule()
    : BaseModule("my_counter", modesp::ModulePriority::NORMAL)
{}

bool MyCounterModule::on_init() {
    state_set("my_counter.seconds", static_cast<int32_t>(0));
    ESP_LOGI(TAG, "Counter started");
    return true;
}

void MyCounterModule::on_update(uint32_t dt_ms) {
    elapsed_ms_ += dt_ms;
    while (elapsed_ms_ >= 1000) {
        elapsed_ms_ -= 1000;
        seconds_++;
        state_set("my_counter.seconds", seconds_);
    }
}
```

Це весь модуль. Конструктор передає ім'я модуля та priority до
`BaseModule`; життєві гачки роблять роботу; `state_set` пише через
SharedState.

## Крок 5 — Збірка та прошивка

```bash
idf.py build
idf.py -p COM15 flash monitor
```

У журналі завантаження ви маєте побачити:

```
I (12345) ModuleManager: Registering my_counter (priority=NORMAL)
I (12350) MyCounter: Counter started
```

У WebUI перейдіть на сторінку **Counter** (автоматично згенеровану з
вашої секції `ui`). Віджет "seconds" оновлюється раз на секунду.

## Крок 6 — Задокументуйте модуль

Готовий модуль не «існує» для інших, поки не описаний. Додайте
**двомовну** референс-сторінку — обидві мови оновлюються в одному PR:

```
documentation/uk/03-framework-reference/modules/<name>.md
documentation/en/03-framework-reference/modules/<name>.md
```

Тримайтесь анатомії сторінки з
[docs-style.md](../06-contributing/docs-style.md). Для модуля типовий
каркас (зразки — [`simple_thermo.md`](../03-framework-reference/modules/simple_thermo.md)
і [`presence.md`](../03-framework-reference/modules/presence.md)):

```markdown
# `<name>` — <однорядковий опис>

> 📖 **In English:** [twin link]

<2-4 абзаци: що це? чому існує? хто має читати?>

## Поведінка        — що модуль робить (потік даних)
## Ключі стану      — таблиця state-ключів з маніфесту (тип / доступ / опис)
## WebUI / MQTT     — як налаштовується (якщо є секція ui / mqtt)
## Типові помилки   — граблі, на які натрапить читач
## Що далі          — 3-5 посилань, потрібних читачеві далі
## Джерела          — посилання на manifest.json, .cpp, тести
```

Тоді **зареєструйте** сторінку в індексі —
`documentation/{uk,en}/README.md`, таблиця «03 — Довідник фреймворку»,
статус ✅:

```markdown
| [modules/<name>.md](03-framework-reference/modules/<name>.md) | ✅ | <однорядкове призначення>. |
```

> Двомовний паритет і розділ **Джерела** обов'язкові; биті
> перехресні посилання провалюють рев'ю. Повні правила —
> [docs-style.md](../06-contributing/docs-style.md).

## Довідник API BaseModule

### Конструктор

```cpp
BaseModule(const char* name, modesp::ModulePriority priority);
```

`name` має відповідати полю `"module"` у `manifest.json`. `priority`
обирає фазу ініціалізації:

| Priority | Значення | Фаза | Для чого |
|---|---|---|---|
| `CRITICAL` | 0 | 1 (перша) | Служба помилок, watchdog. |
| `HIGH` | 1 | 2 | WiFi, HAL, драйвери, сценарний рушій. |
| `NORMAL` | 2 | 2 | Бізнес-логіка (за замовчуванням). |
| `LOW` | 3 | 3 (остання) | HTTP, WebSocket, datalogger. |

### Життєві гачки

Усі повертають значення за замовчуванням, якщо не перевизначені.

| Гачок | Сигнатура | Викликається | Примітки |
|---|---|---|---|
| `on_init` | `virtual bool on_init()` | Один раз при старті | Поверніть `false`, щоб скасувати реєстрацію. |
| `on_update` | `virtual void on_update(uint32_t dt_ms)` | Кожні 10 мс | Гарячий шлях — має бути неблокувальним, типово < 1 мс. |
| `on_message` | `virtual void on_message(const etl::imessage& msg)` | Коли повідомлення, адресоване цьому модулю, диспетчиться | Використовуйте помірковано — більшість комунікації через SharedState. |
| `on_stop` | `virtual void on_stop()` | Один раз при зупинці | Звільнити нетривіальні ресурси, скинути черги. |

### Доступ до стану

```cpp
// Запис — типізовані перевантаження, всі делегують до SharedState::set.
bool state_set(const char* key, int32_t value, bool track_change = true);
bool state_set(const char* key, float value, bool track_change = true);
bool state_set(const char* key, bool value, bool track_change = true);
bool state_set(const char* key, const char* value, bool track_change = true);

// Читання — типізована зручність зі значенням за замовчуванням.
float   read_float(const char* key, float def = 0.0f) const;
int32_t read_int(const char* key, int32_t def = 0) const;
bool    read_bool(const char* key, bool def = false) const;

// Узагальнене — повертає std::optional з варіантом. Використовуйте, коли тип невідомий або поліморфний.
etl::optional<modesp::StateValue> state_get(const char* key) const;
```

**Прапорець `track_change`:** за замовчуванням `true` запускає
дельта-розсилку WebSocket. Встановіть `false` для тихих оновлень
(лічильники, швидкозмінні значення, які заспамлюють WS).

Повна семантика SharedState: [shared-state.md](shared-state.md)
*(заплановано)*.

## Порядок трифазної ініціалізації — що насправді запускається коли

`ModuleManager::init_all` викликається тричі у `main.cpp`:

```cpp
// Фаза 1 — модулі з priority CRITICAL
ESP_LOGI(TAG, "Phase 1: Initializing system services...");
app.modules().init_all(app.state());

// ... Wi-Fi, драйвери, сценарний рушій реєструються тут ...

// Фаза 2 — модулі з priority HIGH + NORMAL
ESP_LOGI(TAG, "Phase 2: Initializing WiFi + business modules...");
app.modules().init_all(app.state());

// ... HTTP, WS реєструються тут ...

// Фаза 3 — модулі з priority LOW
ESP_LOGI(TAG, "Phase 3: Initializing HTTP + WebSocket...");
app.modules().init_all(app.state());
```

Кожен виклик ітерує зареєстровані модулі та викликає `on_init()` ЛИШЕ
для тих, що ще у стані `CREATED`. Ось як priority відображається на
фазу: priority `0`/CRITICAL ініціалізується у першому виклику `init_all`
(бо нічого вищого не існує, і він у стані CREATED); priority `1`/HIGH і
`2`/NORMAL — у другому виклику; priority `3`/LOW — у третьому.

**Практичне правило:** якщо ваш модуль залежить від чогось іншого, що
вже має бути ініціалізоване, оберіть вище значення priority (пізнішу
фазу). Якщо він надає фундаментальну послугу іншим модулям, оберіть
нижче.

## Що йде до on_update порівняно з on_init

**`on_init`:**
- Встановити початкові значення стану.
- Прочитати збережені налаштування (PersistService мав би вже їх
  відновити, якщо ваші ключі `state` мали `persist: true`).
- Кешувати посилання / таблиці пошуку, що не змінюються.
- Надрукувати один рядок ESP_LOGI на кшталт "initialised з <ключовими параметрами>".

**`on_update`:**
- Власне бізнес-логіка.
- Прочитати входи (ключі стану, записані іншими модулями / драйверами).
- Обчислити наступний стан.
- Записати виходи.

**Антипатерни у `on_update`:**

- ❌ `vTaskDelay` / `sleep` — блокує цикл оновлення 100 Hz, морить голодом інші модулі.
- ❌ Heap-алокація (`new`, `std::vector::push_back`, `std::string`) — при 100 Hz це втрачає байти на такт.
- ❌ Запис у NVS — синхронний I/O, ~5-50 мс кожен. Відкладіть до обробника подій рівня модуля або використайте `state_set` з `persist: true` (PersistService обмежує частоту).
- ❌ Важке логування (>1 ESP_LOG на секунду на модуль) — UART затоплюється, монітор лагає.
- ❌ Читання складного JSON / розбір рядків — обчисліть наперед у `on_init`.

## Міжмодульна комунікація

Модулі не тримають покажчиків один на одного. Натомість:

1. **Чистий потік даних** — модуль А пише `keyA`, модуль Б читає `keyA`.
   Модуль Б запускається після А на тому ж такті завдяки порядку
   декларації (модулі, створені у `module_instances.h`, реєструються
   у порядку, визначеному в `project.json`, що задає порядок оновлення
   в межах фази).

2. **Події / команди** — публікуються через запис ключа стану,
   спостерігаються через його читання на наступному такті. Детекція
   фронту через ваш власний член `prev_value_`.

3. **Повідомлення** (рідко) —
   `ModuleManager::send_message(target, msg)` досягає `on_message`.
   Використовуйте, коли повідомлення має типізоване корисне навантаження,
   що не вміщується в один ключ стану.

4. **HTTP API** — зовнішні клієнти пишуть ключі через
   `POST /api/settings`, що дії `set_state` теж можуть. Той самий
   механізм, інший актор.

> 💡 **Підказка:** для нового проєкту за замовчуванням використовуйте
> чистий потік даних через SharedState. Додавайте події / повідомлення,
> лише коли координація між тактами цього вимагає. Більшість пар
> модулів не потребують явної сигналізації.

## Читання стану сенсорів / актуаторів

Модуль `equipment` володіє драйверами HAL. Значення сенсорів
потрапляють у ключі на кшталт `equipment.air_temp`, `equipment.evap_temp`.
Запити до актуаторів пишуться у `equipment.req_compressor`,
`equipment.req_fan` тощо, і equipment відображає їх на фізичні реле на
основі `bindings.json`.

Ваш бізнес-модуль читає ключі сенсорів, пише ключі запитів актуаторів.
Обладнання відокремлене.

```cpp
void MyModule::on_update(uint32_t dt_ms) {
    float temp = read_float("equipment.air_temp", 0.0f);
    bool need_cooling = (temp > setpoint_);
    state_set("equipment.req_compressor", need_cooling);
}
```

Повні деталі HAL: [components/modesp_hal.md](../03-framework-reference/components/modesp_hal.md)
*(заплановано)* та [hardware/bindings.md](../04-hardware/bindings.md)
*(заплановано)*.

## Згенеровані заголовки — що автоматично

Після `idf.py build` у `generated/` міститься:

| Файл | Вміст |
|---|---|
| `module_includes.h` | `#include "your_module.h"` для кожного модуля з project.json. |
| `module_instances.h` | Декларації `static YourModule your_module;`. |
| `module_register.h` | Виклики `manager.register_module(your_module)` у `modesp_register_modules(app)`. |
| `state_meta.h` | Constexpr-таблиця усіх задекларованих ключів стану, типів, max-length. |
| `mqtt_topics.h` | Рядкові константи для кожної теми MQTT. |
| `features_config.h` | `#define` для кожного прапорця можливості. |

Ви не торкаєтеся їх руками. Генератор перезаписує їх щоразу під час
збірки актуальними маніфестами.

## Тестування вашого модуля

**Host-збірка (рекомендована для швидкої ітерації):**

```bash
cd tests/host
make MODULE=my_counter
./build/test_my_counter
```

Патерн: маленький `test_<name>.cpp` інстанціює модуль із заглушкою
SharedState, неодноразово викликає `on_init` та `on_update`, перевіряє
значення стану. Див. [testing.md](../06-contributing/testing.md)
*(заплановано)* для fixtures і покрокових інструкцій.

**On-target HIL:**

`tools/tests/test_hil.py` навантажує прошивку, що працює, через HTTP API.
Додавайте тести за допомогою `pytest` і `requests`
([довідник test_hil.py](../../../tools/tests/test_hil.py)).

## Що далі

- **[shared-state.md](shared-state.md)** *(заплановано)* — глибші
  патерни SharedState (відстеження змін, опційні читання, валідація типів).
- **[ui-widgets.md](ui-widgets.md)** *(заплановано)* — повний довідник
  віджетів з візуальними прикладами.
- **[mqtt.md](mqtt.md)** *(заплановано)* — прив'язка ключів
  `mqtt.subscribe` і патерни публікації.
- **[persistence.md](persistence.md)** *(заплановано)* — прапорець
  `persist: true` і PersistService.
- **[debugging.md](debugging.md)** *(заплановано)* — перевірка журналу,
  інспекція стану через HTTP, типові runtime-проблеми.

## Наявні модулі для вивчення з джерельного коду

- [`modules/simple_thermo/`](../../../modules/simple_thermo/) — ~55
  рядків C++, показує патерн гістерезису, читання/запис кількох ключів.
  Найкраще перше читання.
- [`modules/datalogger/`](../../../modules/datalogger/) — більший
  модуль з можливостями, кількома ключами стану, NVS-буферами. Читайте
  після основ.
- [`modules/equipment/`](../../../modules/equipment/) — з'єднує маніфест
  з драйверами HAL. Найбільш зв'язаний модуль — читайте, коли зрозумієте
  SharedState і драйвери.
