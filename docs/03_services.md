# ModESP v4 — Системні сервіси (modesp_services)

Модулі, потрібні в **кожній** прошивці, незалежно від бізнес-логіки.
Реалізовані як звичайні BaseModule. Реєструються в main.cpp.

## Огляд

| Сервіс | Пріоритет | Функція | RAM |
|--------|-----------|---------|-----|
| ErrorService | CRITICAL | Помилки, Safe Mode | ~2 KB |
| WatchdogService | CRITICAL | Heartbeat модулів | ~0.5 KB |
| ConfigService | CRITICAL | Завантаження/збереження конфіг | ~1 KB |
| PersistService | CRITICAL | Автозбереження стану в NVS | ~0.5 KB |
| LoggerService | LOW | Ring buffer логів | ~5 KB |
| SystemMonitor | LOW | RAM, uptime, boot reason | ~0.2 KB |

## 1. ErrorService — Обробка помилок

Найважливіший сервіс. Без нього прошивка "мовчки" ламається.

### Рівні помилок

```
INFO     → тільки лог
WARNING  → лог + повідомлення на шину
ERROR    → лог + деградований режим
CRITICAL → лог + спроба перезапуску модуля
FATAL    → Safe Mode (вимкнути актуатори, чекати втручання)
```

### Safe Mode

Для промислового холодильника "продовжити працювати з помилкою" 
гірше ніж "зупинитись і повідомити". Safe Mode:

1. Вимикає всі актуатори (компресор OFF, нагрівач OFF)
2. Залишає сенсори активними (для моніторингу)
3. Публікує MsgSafeMode на шину
4. Логує причину
5. Чекає ручного втручання або перезавантаження

### API

```cpp
class ErrorService : public BaseModule {
public:
    ErrorService();  // BaseModule("error", CRITICAL)

    // Модулі повідомляють про помилки
    void report(const char* source, int32_t code,
                ErrorSeverity severity, const char* description);

    // Стан
    size_t error_count() const;
    bool is_safe_mode() const;

    // Останні 16 помилок (circular buffer для діагностики)
    const etl::circular_buffer<ErrorRecord, 16>& history() const;
};
```

## 2. WatchdogService — Моніторинг модулів

**Software watchdog** — перевіряє що модулі "живі".
ModuleManager оновлює `last_update_ms` при кожному update().
WatchdogService перевіряє чи не протерміновано.

```cpp
class WatchdogService : public BaseModule {
public:
    WatchdogService(ErrorService& error_service, ModuleManager& manager);

    // Таймаути по пріоритету модуля
    struct Timeouts {
        uint32_t critical_ms = 5000;   // 5 сек
        uint32_t high_ms     = 10000;  // 10 сек
        uint32_t normal_ms   = 30000;  // 30 сек
        uint32_t low_ms      = 60000;  // 1 хв
    };
    Timeouts timeouts;

    // Максимум 3 спроби перезапуску, потім — error report
    static constexpr uint8_t MAX_RESTARTS = 3;
};
```

**Два рівні watchdog:**
- Software (WatchdogService) → ловить зависання окремих модулів
- Hardware (ESP32 TWDT в App) → ловить зависання всієї системи

## 3. ConfigService — Конфігурація плати та драйверів

Парсинг `board.json` + `bindings.json` при старті → заповнення BoardConfig та Bindings у HAL.

```
Старт:
  data/board.json ──parse──► BoardConfig (GPIO pins, I2C buses, OneWire, ADC)
  data/bindings.json ──parse──► vector<Binding> (role → driver → hardware)

Runtime:
  POST /api/bindings → оновити bindings.json → restart
```

Парсить секції: `gpio_outputs`, `onewire_buses`, `gpio_inputs`, `adc_channels`,
`i2c_buses`, `i2c_expanders`, `expander_outputs`, `expander_inputs` (підтримка KC868-A6 та інших I2C плат).
Кожен Binding включає: `role`, `driver`, `module`, `output`/`bus`/`input`/`channel`, `address` (опційно).

## 4. PersistService — Автозбереження стану

Пріоритет **CRITICAL** — ініціалізується в Phase 1 (до бізнес-модулів).

**Boot:** ітерує `STATE_META[]` (constexpr масив з `state_meta.h`), для записів з `persist: true`
читає NVS → SharedState (або встановлює default_val).

**Runtime:** SharedState persist callback → dirty flag → debounce 5s → NVS flush.

```
Boot:
  STATE_META[i].persist == true → NVS read → SharedState set (або default)
  Авто-міграція: "p0".."p32" (позиційні) → "sXXXXXXX" (djb2 hash)

Runtime:
  SharedState.set() → persist callback (ПОЗА mutex) → dirty flag
  on_update(): debounce 5s → NVS write dirty keys → clear flags
```

NVS namespace: `"persist"`, ключі: `"s" + 7 hex chars` (djb2 hash від state key name).
Підтримує float, int32_t, bool типи (з auto-conversion між float↔int32_t).

## 5. LoggerService — Логування

Ring buffer на 64 записи в RAM. Опціонально — flush на LittleFS.

```cpp
struct LogEntry {
    uint64_t timestamp_us;
    LogLevel level;           // DEBUG, INFO, WARNING, ERROR, CRITICAL
    etl::string<16> source;   // ім'я модуля
    etl::string<64> message;
};

class LoggerService : public BaseModule {
public:
    // Shortcut для модулів
    void log(LogLevel level, const char* source, const char* fmt, ...);

    // Читання (для WebSocket)
    using EntryCallback = void(*)(const LogEntry&, void*);
    void read_recent(size_t count, EntryCallback cb, void* user_data);

    // Налаштування
    LogLevel min_level = LogLevel::INFO;
    bool persist_to_flash = false;
    uint32_t flash_write_interval_ms = 10000;
};
```

## 6. SystemMonitor — Моніторинг ресурсів

Публікує стан системи в SharedState та на шину.

```cpp
class SystemMonitor : public BaseModule {
public:
    uint32_t report_interval_ms = 5000;

    // Порогові значення для аварій
    uint32_t heap_warning_threshold  = 20000;  // bytes
    uint32_t heap_critical_threshold = 10000;  // bytes

    bool on_init() override {
        // ─── Boot reason tracking ───
        esp_reset_reason_t reason = esp_reset_reason();
        state_set_int("system.reset_reason", (int32_t)reason);
        state_set_int("system.boot_count", load_boot_count_nvs());

        if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT) {
            // Останній перезапуск був через watchdog!
            // Повідомити ErrorService
        }
        return true;
    }

    void on_update(uint32_t dt_ms) override {
        // Оновлює: system.free_heap, system.min_free_heap,
        // system.heap_largest (найбільший вільний блок — моніторинг фрагментації),
        // system.uptime, system.cpu_temp
        // Перевіряє пороги heap → звертається до ErrorService
    }
};
```

**Boot reason** — критично для діагностики в полі.
Якщо ESP32 перезавантажився через watchdog — це ознака бага.
Якщо через brownout — проблема з живленням.

## 7. NVS Helper — утиліти для роботи з NVS

`nvs_helper.h` (`namespace nvs_helper`) — обгортки над `nvs_flash` API.

**Основні функції:**
- `init()` — ініціалізація NVS flash
- `read_str`, `write_str`, `read_i32`, `write_i32`, `read_bool`, `write_bool`, `read_float` — прості read/write операції

**Batch API** (для зменшення heap фрагментації):
- `batch_open(ns, readonly)` — відкриває NVS handle один раз
- `batch_read_float`, `batch_read_i32`, `batch_read_bool` — читання через відкритий handle
- `batch_write_float`, `batch_write_i32`, `batch_write_bool`, `batch_erase_key` — запис через handle
- `batch_close(handle)` — закриває handle + commit

Використовується в PersistService: один `batch_open` замість 30+ окремих `nvs_open/close` при boot та flush.

---

## Changelog

- 2026-03-01 — Оновлено пріоритети, додано NVS batch API, heap_largest, KC868-A6 parsing
