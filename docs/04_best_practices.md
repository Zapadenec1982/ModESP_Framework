# ModESP v4 — Best Practices

Зібрані правила та патерни для розробки модулів.
Засновані на MISRA C++, промисловому embedded досвіді та аналізі помилок v3.

## 1. Пам'ять

### Правило: Zero heap allocation в hot path

```
ДОЗВОЛЕНО в hot path (on_update, on_message):
  ✅ etl::string<N>, etl::vector<T,N>, etl::variant
  ✅ Stack-allocated структури
  ✅ publish() — повідомлення по значенню
  ✅ state_set/get — фіксована map

ЗАБОРОНЕНО в hot path:
  ❌ std::string, std::vector, std::map
  ❌ json parsing (будь-яке: jsmn тільки при init)
  ❌ new / malloc
  ❌ String (Arduino)
```

### Правило: Heap дозволений тільки при ініціалізації

```cpp
// ОК: один раз при старті (jsmn or config_service)
bool on_init() override {
    // ConfigService parses board.json + bindings.json using jsmn at boot
    setpoint_ = read_float("thermostat.setpoint");  // from SharedState (restored by PersistService)
    return true;
}

// НЕ ОК: кожен цикл
void on_update(uint32_t dt_ms) override {
    // ❌ будь-який json parsing в hot path — забороняється
    // ❌ std::string або new в on_update
}
```

## 2. Обробка помилок

### Правило: Retry з обмеженням для HAL операцій

```cpp
// Шаблон retry — використовувати для будь-яких HAL операцій
template<typename F>
bool retry(F operation, uint8_t max_attempts = 3, uint32_t delay_ms = 10) {
    for (uint8_t i = 0; i < max_attempts; i++) {
        if (operation()) return true;
        if (i < max_attempts - 1) vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return false;
}

// Використання:
void DS18B20Driver::on_update(uint32_t dt_ms) {
    float temp;
    if (retry([&]() { return read_temperature(temp); })) {
        state_set_float("sensors.temp1", temp);
        consecutive_errors_ = 0;
    } else {
        consecutive_errors_++;
        if (consecutive_errors_ >= 5) {
            // Повідомити ErrorService
            report_error(ERR_SENSOR_OFFLINE, ErrorSeverity::ERROR,
                        "DS18B20 not responding after 5 cycles");
        }
    }
}
```

### Правило: Валідація даних на межах

Сенсор може повернути -127°C (помилка DS18B20) або +300°C (зламаний NTC).
Ці значення НЕ повинні потрапити в SharedState та бізнес-логіку.

```cpp
struct SensorBounds {
    float min_valid;          // Фізичний мінімум датчика
    float max_valid;          // Фізичний максимум датчика
    float max_rate;           // Макс °C/сек (захист від стрибків)
};

// DS18B20: фізичні межі -55..+125°C
// Для холодильника реалістично: -40..+50°C
// Швидкість зміни: не більше 1°C/сек

bool validate_reading(float value, float prev_value,
                      uint32_t dt_ms, const SensorBounds& bounds) {
    if (value < bounds.min_valid || value > bounds.max_valid)
        return false;  // За фізичними межами

    if (dt_ms > 0) {
        float rate = fabsf(value - prev_value) / (dt_ms / 1000.0f);
        if (rate > bounds.max_rate) return false;  // Занадто швидка зміна
    }
    return true;
}
```

## 3. Модулі

### Правило: Модуль не знає про App

```cpp
// ❌ ПОГАНО — неявна залежність на глобальний singleton
void ThermostatModule::on_update(uint32_t dt_ms) {
    auto temp = App::instance().state().get("equipment.air_temp");
}

// ✅ ДОБРЕ — використовує BaseModule API
void ThermostatModule::on_update(uint32_t dt_ms) {
    auto temp = read_float("equipment.air_temp");
}
```

### Правило: Static allocation

Всі модулі створюються як static об'єкти в main.cpp.
Ніколи не створювати модулі через new.

```cpp
// ✅ В main.cpp — живуть весь час роботи прошивки
static modesp::HAL hal;
static modesp::DriverManager driver_manager;
static EquipmentModule equipment;

// ❌ Ніколи:
auto* thermostat = new ThermostatModule();
```

### Правило: Повідомлення визначаються поруч з модулем

```
drivers/ds18b20/include/
    ds18b20_driver.h      ← клас драйвера
    ds18b20_messages.h    ← MsgSensorReading для цього драйвера

modules/thermostat/include/
    thermostat_module.h
    thermostat_messages.h  ← MsgSetpointChanged
```

Інший модуль підключає тільки потрібні заголовки.
Ніякого центрального файлу "all_messages.h".

## 4. Shutdown

### Правило: Graceful shutdown у зворотному порядку

```
Init order:     CRITICAL → HIGH → NORMAL → LOW
Shutdown order: LOW → NORMAL → HIGH → CRITICAL

Shutdown:
1. LOW:      зупинити logger, sysmon, persist (force save)
2. NORMAL:   зупинити thermostat, alarm
3. HIGH:     зупинити драйвери (реле → OFF!)
4. CRITICAL: зупинити error, watchdog останніми
```

## 5. Версіонування

```cpp
// types.h або version.h
namespace modesp {
    constexpr const char* FIRMWARE_VERSION = "4.0.0";
    constexpr const char* BUILD_DATE = __DATE__ " " __TIME__;
    // BOARD_TYPE береться з Kconfig
}
```

SystemMonitor публікує це в SharedState при старті.
Видно через WebSocket та логи.

## 6. Equipment Layer

### Правило: Бізнес-модулі НЕ мають прямого доступу до HAL

Тільки EquipmentModule має посилання на драйвери. Всі інші модулі
працюють через SharedState requests/state:

```
Thermostat → state_set("thermostat.req.compressor", true)
Defrost    → state_set("defrost.req.defrost_relay", true)
Protection → reads equipment.air_temp → publishes alarm state

Equipment Manager → reads requests → arbitration → relay.set()
```

Ніколи не створювати драйвери в бізнес-модулях.
Ніколи не звертатись до HAL напряму з Thermostat/Defrost/Protection.
Єдиний модуль з доступом до HAL — EquipmentModule (priority CRITICAL).

---

## Changelog

- 2026-03-01 — Оновлено приклади (jsmn замість nlohmann, Equipment Layer pattern, read_float API)
