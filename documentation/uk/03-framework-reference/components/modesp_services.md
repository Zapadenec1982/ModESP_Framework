# `modesp_services` — Config, Persist, Error, Watchdog, Logger, SystemMonitor

> 📖 **In English:** [documentation/en/03-framework-reference/components/modesp_services.md](../../../en/03-framework-reference/components/modesp_services.md)

`modesp_services` постачає системні служби, які потрібні кожній прошивці
незалежно від бізнес-логіки: завантаження конфігурації, збереження у NVS,
повідомлення про помилки, сторожовий таймер, буферизація журналу,
моніторинг системи та обробка OTA. Ці служби виконуються з пріоритетом
`CRITICAL` (або близько до нього) і ініціалізуються до будь-якого
бізнес-модуля.

Ця сторінка описує, що надає кожна служба, як вони підключаються до
SharedState і коли бізнес-модулі мають взаємодіяти з ними напряму
(рідко — більшість інтеграцій прозорі).

REQUIRES: `modesp_core`, `nvs_flash`, помічники ESP-IDF.

## Огляд служб

| Служба | Пріоритет | Призначення |
|---|---|---|
| **ErrorService** | CRITICAL | Централізоване повідомлення про помилки, відстеження несправностей. |
| **WatchdogService** | CRITICAL | Підгодовує task watchdog ESP-IDF з головного завдання. |
| **ConfigService** | CRITICAL | Читає `board.json` і `bindings.json` з LittleFS. |
| **PersistService** | CRITICAL | Дзеркалить збережені ключі стану у NVS. |
| **LoggerService** | CRITICAL | Кільцевий буфер у RAM з останніми рядками ESP_LOG (для `/api/log`). |
| **SystemMonitor** | CRITICAL | Купа, час роботи, статистика завдань, відстеження вільної RAM. |
| **OtaHandler** | (помічник) | Не BaseModule — утиліта, яку викликають HTTP / MQTT. |

Плюс простір імен `nvs_helper` — тонка C++-обгортка над API NVS ESP-IDF
(функції `nvs_*`). Використовується внутрішньо PersistService і доступна
модулям, що потребують прямого доступу до NVS.

## `ErrorService` — централізоване повідомлення про несправності

```cpp
#include "modesp/error_service.h"

class ErrorService : public modesp::BaseModule {
public:
    ErrorService();

    // API used by other modules
    void report_fault(ErrorCode code, const char* details);
    void clear_fault(ErrorCode code);
    bool has_fault() const;
    ErrorCode last_fault() const;
};

enum class ErrorCode : uint16_t {
    // Hardware
    SENSOR_FAILED          = 100,
    ACTUATOR_FAILED        = 110,
    DRIVER_NOT_HEALTHY     = 120,

    // Network
    WIFI_DISCONNECTED      = 200,
    MQTT_BROKER_LOST       = 210,

    // Filesystem
    NVS_WRITE_FAILED       = 300,
    LITTLEFS_MOUNT_FAILED  = 310,

    // ... etc.
};
```

Виставлені ключі стану:

| Ключ | Тип | Примітки |
|---|---|---|
| `error_service.last_code` | int | Числовий код останньої несправності. |
| `error_service.last_details` | string | Довільний текст від повідомника. |
| `error_service.active_count` | int | Кількість невирішених несправностей. |
| `error_service.cleared_count` | int | Сукупна кількість прибраних (для діагностики). |

Бізнес-модулі викликають `report_fault` зі свого `on_update`, коли щось
іде не так. Статус модуля (наприклад, `equipment.<role>_ok = false`)
плине через SharedState; ErrorService накопичує зведення несправностей
вищого рівня для публікації в UI / MQTT.

## `WatchdogService` — тримає пристрій живим

```cpp
class WatchdogService : public modesp::BaseModule {
public:
    WatchdogService();
};
```

Підписує себе на task watchdog ESP-IDF у `on_init` і викликає
`esp_task_wdt_reset()` з `on_update`. Якщо головне завдання зависає
(`on_update` якогось модуля займає надто багато часу), сторожовий таймер
викликає перезавантаження системи після налаштованого тайм-ауту
(за замовчуванням ~5 секунд).

Без ключів стану, без API. Модуль існує переважно для того, щоб
довести, що фреймворк коректно тактує. Якщо ви бачите перезавантаження
від сторожового таймера у журналах — запустіть профайлер: якийсь модуль
перевищує свій бюджет такту.

## `ConfigService` — завантаження плати і прив'язок

```cpp
#include "modesp/services/config_service.h"

class ConfigService : public modesp::BaseModule {
public:
    bool load_from_littlefs(const char* path = "/data");
    const BoardConfig& board() const;
    const BindingTable& bindings() const;
    bool reload();
};
```

Читає `data/board.json` і `data/bindings.json` у `on_init`. Надає
розібрані структури, які споживають інші компоненти (HAL, Equipment
Manager, драйвери).

`reload()` перечитує файли. Використовується після заміни файлів через
OTA або редактором прив'язок у WebUI (планується). Наразі викликається
лише з обробника HTTP `/api/bindings` POST.

Ключі стану:

| Ключ | Примітки |
|---|---|
| `config.board_name` | Ідентифікатор плати з `board.json::board`. |
| `config.bindings_count` | Кількість записів прив'язок. |

## `PersistService` — дзеркало стану у NVS

```cpp
#include "modesp/services/persist_service.h"

class PersistService : public modesp::BaseModule {
public:
    PersistService();

    static constexpr uint32_t DEBOUNCE_MS = 5000;
    static constexpr const char* NVS_NAMESPACE = "persist";

    // Internal — wired through SharedState::set_persist_callback.
    static void on_state_changed(const StateKey&, const StateValue&, void*);
};
```

Цикл:
1. У `on_init` сканує `state_meta.h` на наявність усіх ключів з
   `persist: true`. Читає кожен з простору імен NVS `"persist"` і
   встановлює значення у SharedState.
2. Реєструє зворотний виклик через `SharedState::set_persist_callback`.
   Зворотний виклик спрацьовує при кожному відстеженому записі.
3. Зворотний виклик визначає ключі, що зберігаються, і планує запис у
   NVS із затримкою (5 секунд "без подальших змін" → скидання у флеш).

Докладніше: [persistence.md](../../02-module-author-guide/persistence.md).

Поверхня API для модулів — **лише прапорець `persist: true` у
маніфесті**. PersistService напряму не викликають.

## `LoggerService` — буфер останніх рядків журналу

```cpp
#include "modesp/logger_service.h"

class LoggerService : public modesp::BaseModule {
public:
    LoggerService();

    // Internal — captures ESP_LOG vprintf hook.
    static int esp_log_vprintf_hook(const char* fmt, va_list args);

    // Public — retrieve recent lines for /api/log.
    void copy_recent(char* out, size_t cap, size_t* out_len);
};
```

Перехоплює `esp_log_set_vprintf` з ESP-IDF і пише останні рядки журналу
у кільцевий буфер у RAM (за замовчуванням ~4 КБ, останні ~100 рядків).
HTTP `/api/log` віддає цей буфер для віддаленої діагностики — корисно,
коли UART недоступний.

Ключі стану:

| Ключ | Примітки |
|---|---|
| `logger.line_count` | Кількість рядків, захоплених з моменту завантаження. |
| `logger.buffer_used_bytes` | Поточна кількість байт у кільцевому буфері. |

У MVP немає фільтрації / керування рівнями. Stage 1.5 може додати
налаштування рівнів для кожного тегу через `/api/log/config`.

## `SystemMonitor` — статистика купи та часу роботи

```cpp
#include "modesp/system_monitor.h"

class SystemMonitor : public modesp::BaseModule {
public:
    SystemMonitor(ErrorService& errors);
};
```

Періодично (кожну ~1 секунду) пише діагностичні ключі стану:

| Ключ | Примітки |
|---|---|
| `system.free_heap` | байти від `esp_get_free_heap_size()`. |
| `system.min_free_heap` | `esp_get_minimum_free_heap_size()`. |
| `system.uptime_s` | секунд з моменту завантаження. |
| `system.reset_reason` | одне з "POWERON" / "BROWNOUT" / "WDT" / "EXTERNAL" тощо. |
| `system.task_count` | активні завдання FreeRTOS. |
| `system.cpu_freq_mhz` | поточна частота CPU. |

WebUI показує це на сторінці "Система". MQTT публікує їх, якщо вони
зазначені у `mqtt.publish`.

Якщо `min_free_heap < threshold`, повідомляє `ErrorCode::LOW_MEMORY` до
ErrorService. Поріг налаштовується через Kconfig (за замовчуванням
~16 КБ).

## `OtaHandler` — оновлення прошивки

```cpp
#include "modesp/services/ota_handler.h"

class OtaHandler {
public:
    // Called from HTTP /api/ota/upload handler.
    bool begin(size_t total_size);
    bool write(const uint8_t* data, size_t len);
    bool finish(bool& valid);

    bool confirm_running_partition();    // mark current image stable
    bool rollback();                      // boot з previous partition
};
```

Не є BaseModule — це утиліта, яку використовують обробники `/api/ota/*`
у `modesp_net::HttpService`. Ключі стану у просторі імен `_ota.*`
(префікс підкреслення позначає внутрішні для фреймворка):

| Ключ | Примітки |
|---|---|
| `_ota.status` | "idle" / "downloading" / "verifying" / "rebooting" / "rolled_back" |
| `_ota.progress` | Передано байтів. |
| `_ota.error` | Рядок помилки, якщо status = "failed". |
| `_ota.version` | Версія активної прошивки. |
| `_ota.partition` | Ім'я активного розділу. |
| `_ota.date` / `_ota.idf` | Метадані складання. |

Повний потік OTA: [04-hardware/ota.md](../../04-hardware/ota.md)
*(планується)*.

## `nvs_helper` — низькорівневий API NVS

Для випадків, коли `persist: true` недостатньо — великі великі обʼєкти,
власне кодування, окремий простір імен.

```cpp
#include "modesp/services/nvs_helper.h"
namespace modesp::nvs_helper {

bool init();
bool erase_all();

// Typed
bool read_i32(const char* ns, const char* key, int32_t& out);
bool read_float(const char* ns, const char* key, float& out);
bool read_bool(const char* ns, const char* key, bool& out);
bool read_str(const char* ns, const char* key, char* out, size_t max_len);
bool read_blob(const char* ns, const char* key, void* out, size_t max_len, size_t& out_len);

bool write_i32(const char* ns, const char* key, int32_t value);
bool write_float(const char* ns, const char* key, float value);
bool write_bool(const char* ns, const char* key, bool value);
bool write_str(const char* ns, const char* key, const char* value);
bool write_blob(const char* ns, const char* key, const void* data, size_t len);

bool erase_key(const char* ns, const char* key);
bool erase_namespace(const char* ns);

// Batch (швидше для many writes)
struct nvs_handle_t* batch_open(const char* ns, bool readonly);
bool batch_read_*(...);   // same but on open handle
bool batch_write_*(...);
bool batch_close(struct nvs_handle_t* h);

}
```

Приклади використання у самому фреймворку:
- простір імен `scnstate` (токени рушія сценаріїв);
- простір імен `auth` (облікові дані адміністратора);
- простір імен `time` (NTP / часовий пояс);
- `seqstate` (застарілий рушій сценаріїв — поступово прибирається).

Використовуйте простір імен, ВІДМІННИЙ від `"persist"`, щоб уникнути
конфлікту з ключами, якими автоматично керує PersistService.

## Реєстрація служб у main.cpp

```cpp
static modesp::ErrorService    error_service;
static modesp::ConfigService   config_service;
static modesp::PersistService  persist_service;
static modesp::LoggerService   logger_service;
static modesp::SystemMonitor   system_monitor(error_service);
// WatchdogService instantiated dynamically because it needs ModuleManager ref:
// (see main.cpp's app_main)

// Phase 1 registration:
app.modules().register_module(error_service);
app.modules().register_module(logger_service);
app.modules().register_module(config_service);

persist_service.set_state(&app.state());
app.modules().register_module(persist_service);

app.modules().register_module(system_monitor);
app.modules().register_module(watchdog_service);

app.modules().init_all(app.state());   // Phase 1 complete
```

## Що слід знати бізнес-модулям

Для 95% бізнес-модулів цей компонент можна повністю ігнорувати. Речі,
які варто знати, якщо вони вас зачеплять:

1. **`persist: true`** у вашому маніфесті → PersistService подбає про
   збереження.
2. **Несправності, які ви виявляєте** → викликайте
   `error_service.report_fault(code, "...")`, щоб показати їх у UI /
   MQTT.
3. **Довгі операції** → сторожовий таймер скине вас, якщо `on_update`
   перевищить ~5 с. Розбийте роботу на такти.
4. **Записи у NVS** → використовуйте `persist: true` (затримка 5 с) або
   явний `nvs_helper::write_*` для одноразових випадків. Не пишіть на
   кожен такт.
5. **`/api/log`** → ваші повідомлення ESP_LOG видно через HTTP. Не
   журналюйте чутливі дані.

## Типові шаблони

### Модуль повідомляє про несправність і відновлюється

```cpp
void Module::on_update(uint32_t dt_ms) {
    bool sensor_ok = read_bool("equipment.air_temp_ok", false);

    if (!sensor_ok && !was_faulted_) {
        error_service_->report_fault(ErrorCode::SENSOR_FAILED,
                                     "air_temp sensor unhealthy");
        was_faulted_ = true;
    } else if (sensor_ok && was_faulted_) {
        error_service_->clear_fault(ErrorCode::SENSOR_FAILED);
        was_faulted_ = false;
    }
}
```

Шаблон тригера за фронтом — повідомили один раз, прибрали один раз. Не
спамте на кожному такті.

### Власний простір імен NVS для великих даних

```cpp
bool Module::save_calibration_table(const CalibTable& tbl) {
    return modesp::nvs_helper::write_blob(
        "my_module", "calib_v1", &tbl, sizeof(tbl));
}

bool Module::load_calibration_table(CalibTable& out) {
    size_t len = 0;
    return modesp::nvs_helper::read_blob(
        "my_module", "calib_v1", &out, sizeof(out), len);
}
```

Інкапсулюйте доступ до NVS у методах класу; викликайте з `on_init`
(завантаження) і за свідомим тригером збереження (наприклад, дія
`apply_calibration`). Ніколи не у `on_update`.

## Типові помилки

**`PersistService` не відновлює значення при першому завантаженні:**
це правильно — відсутність значення у NVS означає використати значення
за замовчуванням. Переконайтесь, що ваше оголошення `state` має поле
`default` для запасу при свіжому завантаженні.

**`ConfigService::reload()` з `on_update`:** розбирає JSON синхронно,
займає ~50 мс. Блокує. Використовуйте лише з одноразового HTTP-обробника.

**Витік пам'яті з `nvs_helper::read_blob`:** функція заповнює буфер,
який ви надаєте, нічого не виділяє. Але якщо ви виділяєте цей буфер
через `new`, не забудьте `delete`. По можливості використовуйте буфери
на стеку.

**Причина скидання "WDT" зберігається між завантаженнями:** це
очікувано — остання причина скидання передається через перезавантаження.
Якщо ви бачите її після циклу живлення — це означає, що попередній
запуск спрацював сторожовий таймер. Захопіть дамп журналу через
`/api/log`, якщо проблема відтворюється.

## Що далі

- **[components/modesp_hal.md](modesp_hal.md)** *(планується)* — HAL і
  DriverManager.
- **[components/modesp_net.md](modesp_net.md)** *(планується)* — служби
  Wi-Fi і HTTP, що використовують ConfigService і OtaHandler.
- **[02-module-author-guide/persistence.md](../../02-module-author-guide/persistence.md)**
  — погляд автора на PersistService.
- **[02-module-author-guide/debugging.md](../../02-module-author-guide/debugging.md)**
  — `/api/log`, `/api/state`, `/api/modules` — поверхня налагодження.

## Джерела

- [`components/modesp_services/include/modesp/`](../../../../components/modesp_services/include/modesp/)
  — публічні заголовки.
- [`components/modesp_services/src/`](../../../../components/modesp_services/src/)
  — реалізації.
