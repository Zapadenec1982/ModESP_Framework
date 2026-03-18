# ModESP v4 — Архітектура

## Філософія

ModESP v4 — модульна прошивка для промислових холодильників на ESP32.

**Ключові принципи:**
- Zero heap allocation в hot path (ETL замість STL)
- Layered architecture: Core → Services → HAL → Drivers → Modules
- Кожен модуль — ізольований, тестований, замінний
- JSON тільки на cold path (конфігурація, WebSocket, діагностика)
- Передбачуване використання RAM: відоме при компіляції

## Шари системи

```
┌─────────────────────────────────────────────────────┐
│                      main.cpp                       │  Збирає все разом
├──────────┬──────────┬───────────┬──────────┬────────┤
│ modules/ │ drivers/ │ modesp_   │ modesp_  │ jsmn/  │
│ (бізнес) │ (HW)     │ net       │ mqtt     │ (JSON) │
├──────────┴──────────┴───────────┴──────────┴────────┤
│              modesp_services                         │  Системні сервіси
├─────────────────────────────────────────────────────┤
│                modesp_hal                            │  Hardware abstraction
├─────────────────────────────────────────────────────┤
│                modesp_core                           │  Ядро: BaseModule,
│                  + ETL                               │  SharedState, MessageBus
├─────────────────────────────────────────────────────┤
│            ESP-IDF / FreeRTOS                        │
└─────────────────────────────────────────────────────┘
```

## Залежності між компонентами

```
                     ┌─────────────┐
                     │    main/    │  Точка входу
                     └──────┬──────┘
                            │ збирає все разом
       ┌──────────┬─────────┼─────────┬──────────┐
       │          │         │         │          │
┌──────▼───┐ ┌───▼────┐ ┌──▼───┐ ┌───▼───┐ ┌───▼────┐
│ modules/ │ │drivers/│ │config│ │modesp │ │modesp  │
│(бізнес)  │ │(HW)   │ │JSON  │ │_net   │ │_mqtt   │
└──────┬───┘ └───┬────┘ │files │ └───┬───┘ └───┬────┘
       │         │      └──────┘     │         │
       │         │                   │         │
       └────┬────┘                   └────┬────┘
            │                             │
     ┌──────▼──────┐                      │
     │ modesp_hal/ │                      │
     │ (hardware)  │                      │
     └──────┬──────┘                      │
            │                             │
     ┌──────▼──────────────┐              │
     │ modesp_services/    │◄─────────────┘
     │ config, error, log, │
     │ watchdog, persist,  │
     │ system_monitor      │
     └──────┬──────────────┘
            │
     ┌──────▼──────┐
     │ modesp_core │  ← Залежить тільки від
     │  + ETL      │     ETL та FreeRTOS
     │  + jsmn     │
     └─────────────┘
```

**Правила залежностей:**
- `modesp_core` → тільки ETL + ESP-IDF (FreeRTOS)
- `modesp_services` → modesp_core + NVS/LittleFS
- `modesp_hal` → modesp_core
- `modesp_net` → modesp_core + modesp_services
- `modesp_mqtt` → modesp_core + modesp_net
- `jsmn/` → header-only C JSON parser, без залежностей
- `drivers/` → modesp_core + modesp_hal
- `modules/` → modesp_core (через SharedState, БЕЗ прямого HAL доступу; лише EquipmentModule має HAL)
- `main/` → все

## Структура проекту

```
ModESP_v4/
├── docs/                             # Документація
├── components/
│   ├── etl/                          # ETL (git submodule)
│   ├── modesp_core/                  # Ядро
│   │   ├── include/modesp/
│   │   │   ├── types.h               # StateKey, StateValue, msg_id, ModulePriority
│   │   │   ├── base_module.h         # Базовий клас модуля
│   │   │   ├── module_manager.h      # Реєстрація та lifecycle
│   │   │   ├── shared_state.h        # Key-value сховище стану
│   │   │   └── app.h                 # Application singleton
│   │   └── src/
│   ├── modesp_services/              # Системні сервіси
│   │   ├── include/modesp/services/
│   │   │   ├── error_service.h       # Помилки + Safe Mode
│   │   │   ├── watchdog_service.h    # Моніторинг здоров'я модулів
│   │   │   ├── config_service.h      # Завантаження board.json + bindings.json
│   │   │   ├── persist_service.h     # Автозбереження SharedState → NVS
│   │   │   ├── logger_service.h      # Ring buffer логів
│   │   │   ├── nvs_helper.h          # NVS read/write утиліти
│   │   │   └── system_monitor.h      # RAM, uptime, boot reason
│   │   └── src/
│   ├── modesp_hal/                   # Hardware abstraction
│   │   ├── include/modesp/hal/
│   │   │   ├── hal.h                 # GPIO ініціалізація з BoardConfig
│   │   │   ├── hal_types.h           # BoardConfig, BindingEntry, GpioInputConfig, AdcChannelConfig
│   │   │   ├── driver_manager.h      # Створення/управління драйверами
│   │   │   └── driver_interfaces.h   # ISensor, IActuator абстракції
│   │   └── src/
│   ├── modesp_net/                   # WiFi + HTTP + WebSocket
│   │   ├── include/modesp/net/
│   │   │   ├── wifi_service.h        # STA + AP fallback
│   │   │   ├── http_service.h        # REST API (port 80)
│   │   │   └── ws_service.h          # WebSocket real-time state
│   │   └── src/
│   ├── modesp_mqtt/                  # MQTT client (окремий component)
│   │   ├── include/modesp/net/
│   │   │   └── mqtt_service.h        # MQTT pub/sub
│   │   └── src/
│   ├── jsmn/                         # JSON parser (header-only, lightweight)
│   │   └── jsmn.h
│   └── modesp_json/                  # (placeholder, функціонал в jsmn/)
├── drivers/                          # Драйвери пристроїв
│   ├── ds18b20/                      # Dallas DS18B20 (MATCH_ROM + SEARCH_ROM)
│   ├── ntc/                          # NTC термістор через ADC
│   ├── relay/                        # GPIO relay з min on/off time
│   ├── digital_input/                # GPIO вхід з дебаунсом
│   ├── pcf8574_relay/                # I2C relay через PCF8574
│   └── pcf8574_input/                # I2C input через PCF8574
├── modules/                          # Бізнес-логіка
│   ├── equipment/                    # HAL owner, arbitration, driver reading
│   ├── thermostat/                   # Контроль температури
│   ├── protection/                   # Моніторинг аварій
│   ├── defrost/                      # 7-фазна відтайка
│   └── datalogger/                   # 6-channel logging + events
├── main/                             # Точка входу
│   └── main.cpp                      # 3-phase boot + main loop
├── tests/
│   └── host/                         # C++ host unit tests (doctest)
├── tools/
│   ├── generate_ui.py                # Manifest → UI + C++ headers generator
│   └── tests/                        # Python pytest тести (264 tests)
├── data/                             # Runtime конфігурація + WebUI
│   ├── board.json                    # PCB pin assignment
│   ├── bindings.json                 # Role → driver → GPIO mapping
│   ├── ui.json                       # Generated UI schema
│   └── www/                          # Deployed WebUI (Svelte)
├── webui/                            # Svelte 4 WebUI source
├── generated/                        # Generated C++ headers (5 files)
├── CMakeLists.txt
├── sdkconfig
└── partitions.csv
```

## Потік даних

Вся комунікація між модулями відбувається через SharedState (не через Message Bus напряму).
EquipmentModule — єдиний модуль з прямим доступом до HAL/drivers.

```
               ┌─────────────────────────┐
               │    EquipmentModule       │  CRITICAL priority
               │  (єдиний HAL owner)      │
               └──────┬──────────┬────────┘
                      │ reads    │ writes
                      ▼          ▼
              DS18B20 Driver   SharedState
              Relay Driver     ┌──────────────────────────────┐
              NTC Driver       │ equipment.air_temp = -2.5    │
              DI Driver        │ equipment.sensor1_ok = true  │
                               │ thermostat.req.compressor=1  │
                               │ defrost.req.defrost_relay = 0│
                               │ protection.lockout = false   │
                               └──────┬──────────┬────────────┘
                                      │          │
                            reads     │          │  reads
                    ┌─────────────────┘          └──────────────┐
                    ▼                                           ▼
           Thermostat                                    Protection
           state_get("equipment.air_temp")               state_get("equipment.air_temp")
           state_set("thermostat.req.compressor", true)  state_set("protection.alarm_high", true)
                    │                                           │
                    └──────────┬────────────────────────────────┘
                               │
                               ▼
                      EquipmentModule (arbitration)
                      Protection LOCKOUT > Defrost active > Thermostat
                               │
                               ▼
                         relay.set(on/off)
```

Паралельно: SharedState → WebSocket broadcast / MQTT publish (cold path, кожні 3 секунди)

## Розміри (оцінка)

| Компонент | RAM (приблизно) |
|-----------|-----------------|
| modesp_core (SharedState 128 entries) | ~8-9 KB |
| modesp_services | ~6-8 KB |
| modesp_hal + drivers | ~3-4 KB |
| modesp_net + modesp_mqtt | ~4-5 KB |
| modules (5 бізнес-модулів) | ~3-4 KB |
| **Разом** | **~25-30 KB** |

ESP32 має 520 KB RAM. Firmware займає ~1 MB flash (з 1.5 MB app partition).
Ядро + всі модулі використовують ~5-6% від доступної RAM.

> Ці числа — оцінки. Точний розмір залежить від sdkconfig та кількості активних модулів.

## Changelog
- 2026-03-01 — Рефакторинг документації: виправлено defrost.req.heater → defrost_relay, WS broadcast 3s
- 2026-03-01 — Рефакторинг: виправлено data flow (SharedState замість Message Bus), оновлено HAL/Net структуру, jsmn замість nlohmann/json, додано modesp_mqtt, pcf8574 drivers, tests/host
