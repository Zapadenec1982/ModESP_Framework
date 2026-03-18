# ModESP v4 — DataLogger Module

Модуль багатоканального логування температури та подій обладнання.
Записує дані на LittleFS (flash), віддає через streaming HTTP JSON,
візуалізує у ChartWidget (SVG, Catmull-Rom smooth curves).

## Архітектура

- **6-channel dynamic logging:** канал `air` завжди активний, решта (evap, cond, setpoint, humidity, reserved) опціональні
- **TempRecord:** 16 bytes фіксований розмір — `timestamp` (uint32_t, 4B) + `ch[6]` (int16_t x6, 12B)
- **ChannelDef:** compile-time таблиця каналів — `id`, `state_key`, `enable_key`, `has_key` per channel
- **TEMP_NO_DATA sentinel:** `INT16_MIN` (-32768) — канал вимкнений або датчик відсутній
- **EventRecord:** 8 bytes — `timestamp` (uint32_t) + `event_type` (uint8_t) + 3B padding
- **Timestamp:** UNIX epoch якщо SNTP синхронізовано, інакше uptime в секундах
- **Пріоритет модуля:** LOW (ініціалізується останнім)
- **Edge-detect подій:** poll кожен update cycle, порівнює поточний стан з попереднім

## Канали

Порядок каналів у бінарному записі фіксований (compile-time `CHANNEL_DEFS[6]`):

| # | id         | state_key              | enable_key             | has_key                  |
|---|------------|------------------------|------------------------|--------------------------|
| 0 | `air`      | `equipment.air_temp`   | _(завжди)_             | _(завжди)_               |
| 1 | `evap`     | `equipment.evap_temp`  | `datalogger.log_evap`  | `equipment.has_evap_temp`|
| 2 | `cond`     | `equipment.cond_temp`  | `datalogger.log_cond`  | `equipment.has_cond_temp`|
| 3 | `setpoint` | `thermostat.setpoint`  | `datalogger.log_setpoint`| _(завжди)_             |
| 4 | `humidity` | `equipment.humidity`   | `datalogger.log_humidity`| `equipment.has_humidity`|
| 5 | _(reserved)_ | _(nullptr)_         | _(nullptr)_            | _(nullptr)_              |

**Логіка увімкнення каналу:**
- `enable_key == nullptr` — канал завжди увімкнений (air)
- Інакше: `toggle ON` (enable_key) **AND** `hardware present` (has_key або true якщо has_key == nullptr)
- Значення зберігаються як `int16_t` (температура x10, тобто 23.5C = 235)

## Зберігання (LittleFS)

### Файли

| Файл                   | Розмір запису | Опис                        |
|------------------------|-------------|-----------------------------|
| `/data/log/temp.bin`   | 16 bytes    | Поточний файл температури   |
| `/data/log/temp.old`   | 16 bytes    | Попередній (після ротації)   |
| `/data/log/events.bin` | 8 bytes     | Поточний файл подій         |
| `/data/log/events.old` | 8 bytes     | Попередній (після ротації)   |

### RAM буфери
- **Температура:** `etl::vector<TempRecord, 16>` — до 16 записів у пам'яті
- **Події:** `etl::vector<EventRecord, 32>` — до 32 подій у пам'яті
- **Flush:** кожні 10 хвилин (`FLUSH_INTERVAL_MS = 600000`) або при зупинці модуля (`on_stop`)

### Ротація
- **temp.bin:** max size = `retention_hours * 60 * sizeof(TempRecord)` (1 запис/хвилину при sample_interval=60s)
- **events.bin:** hard limit `EVENT_MAX_SIZE = 16384` bytes (16 KB)
- При перевищенні: `remove(*.old)` → `rename(*.bin → *.old)` — зберігається подвійний обсяг

### Auto-migration
При завантаженні (`on_init`) перевіряється розмір temp файлів: якщо `st_size % 16 != 0`
(старий формат 8 або 12 bytes), файли видаляються для коректної роботи з 16-байтним форматом.

## Події

10 типів подій, фіксуються по edge-detect (зміна стану):

| EventType              | Код | Джерело state key              | Тригер           |
|------------------------|-----|--------------------------------|------------------|
| `EVENT_COMPRESSOR_ON`  | 1   | `equipment.compressor`         | false → true     |
| `EVENT_COMPRESSOR_OFF` | 2   | `equipment.compressor`         | true → false     |
| `EVENT_DEFROST_START`  | 3   | `defrost.active`               | false → true     |
| `EVENT_DEFROST_END`    | 4   | `defrost.active`               | true → false     |
| `EVENT_ALARM_HIGH`     | 5   | `protection.high_temp_alarm`   | false → true     |
| `EVENT_ALARM_LOW`      | 6   | `protection.low_temp_alarm`    | false → true     |
| `EVENT_ALARM_CLEAR`    | 7   | high/low alarm                 | true → false     |
| `EVENT_DOOR_OPEN`      | 8   | `equipment.door_open`          | false → true     |
| `EVENT_DOOR_CLOSE`     | 9   | `equipment.door_open`          | true → false     |
| `EVENT_POWER_ON`       | 10  | —                              | при `on_init()`  |

## HTTP API

### GET /api/log?hours=N

Streaming chunked JSON v3. Параметр `hours` (default = 24) визначає глибину вибірки.

**Потік обробки:**
1. Scan temp файлів (old + current) + RAM буфер — визначити канали з даними (`ch_has_data[]`)
2. Побудувати масив `active_idx[]` — індекси каналів де є хоча б один не-TEMP_NO_DATA запис
3. Streaming output: header → temp records → events → footer

**Формат JSON v3:**
```json
{
  "channels": ["air", "evap", "setpoint"],
  "temp": [
    [1708800000, 235, -120, 200],
    [1708800060, 237, null, 200]
  ],
  "events": [
    [1708800100, 1],
    [1708800500, 2]
  ]
}
```

- `channels` — масив ID активних каналів (тільки ті що мають дані)
- `temp` — масив `[timestamp, v0, v1, ...]`, значення = int16 raw (x10) або `null` для TEMP_NO_DATA
- `events` — масив `[timestamp, event_type]`

### GET /api/log/summary

Компактна статистика без даних:
```json
{
  "hours": 48,
  "temp_count": 2880,
  "event_count": 42,
  "flash_kb": 95,
  "channels": 3
}
```

## ChartWidget (SVG)

Svelte компонент `webui/src/components/widgets/ChartWidget.svelte` — повністю динамічний по каналах.

### Основні характеристики
- **SVG viewBox:** 720 x 280 pixels
- **Padding:** top=20, right=16, bottom=40, left=50
- **Smooth curves:** Catmull-Rom spline → SVG cubic Bezier (`catmullRomPath()`)
- **Null handling:** розрив кривої при `null` значеннях (сегментація)

### Канали та кольори (PALETTE)
| Індекс | Канал    | Колір    | i18n ключ          |
|--------|----------|----------|--------------------|
| 0      | air      | `#3b82f6`| `chart.ch_air`     |
| 1      | evap     | `#10b981`| `chart.ch_evap`    |
| 2      | cond     | `#f59e0b`| `chart.ch_cond`    |
| 3      | setpoint | `#f97316`| `chart.ch_setpoint`|
| 4      | humidity | `#8b5cf6`| `chart.ch_humidity`|

### Toggle checkboxes
- Відображаються коли є більше 1 каналу з даними
- Кожен checkbox має колір каналу (`accent-color: var(--ch-color)`)
- Приховані канали не впливають на масштабування осей

### Downsample
- Утиліта `webui/src/lib/downsample.js` — Min/Max bucket алгоритм
- `MAX_POINTS = 720` — максимум точок у SVG
- По кожному бакету вибираються точки min та max по primary каналу (зазвичай air)
- Зберігає форму кривої та піки температури

### Zones (кольорові зони подій)
- **Compressor:** зелені напівпрозорі прямокутники (`#22c55e`, opacity 0.15) — EVENT_COMPRESSOR_ON → OFF
- **Defrost:** помаранчеві прямокутники (`#f97316`, opacity 0.2) — EVENT_DEFROST_START → END
- **Alarm markers:** червоні вертикальні лінії (`#ef4444`) — EVENT_ALARM_HIGH / LOW
- **Power-on markers:** сірі штрихові вертикальні лінії (`#64748b`) — EVENT_POWER_ON

### Setpoint dual-mode
- Якщо `setpoint` логується як канал → polyline в графіку (динамічне значення)
- Якщо `setpoint` НЕ в `channels` → горизонтальна штрихова лінія з live `$state['thermostat.setpoint']`

### Tooltip
- Mouse/touch → знаходить найближчу точку по timestamp
- Показує значення всіх видимих каналів + час
- Dot + background rect + text

### Real-time оновлення
- Кожні `sample_interval` секунд додається нова точка з WebSocket state (`appendLivePoint()`)
- Точки за межами вікна (`hours`) автоматично видаляються
- Full refresh кожні 5 хвилин (`setInterval(loadData, 300000)`)

### CSV export
- Client-side download — zero навантаження на ESP32
- Формат: `timestamp,datetime,air,evap,...` + секція `# Events`
- Файл: `datalog_{hours}h.csv`

### Event list
- `<details>` секція під графіком (collapsed за замовчуванням)
- Останні 50 подій у зворотному порядку
- Кольорове маркування: alarm (червоний), defrost (помаранчевий), power (сірий)
- Час у форматі `DD.MM HH:MM:SS`

### Period switcher
- Кнопки 24h / 48h — перезавантажують дані з API
- Кнопка CSV — завантажує файл

## State keys

| State key                    | Тип   | Access    | Persist | Default | Опис                              |
|------------------------------|-------|-----------|---------|---------|-----------------------------------|
| `datalogger.enabled`         | bool  | readwrite | yes     | true    | Логування увімкнено               |
| `datalogger.retention_hours` | int   | readwrite | yes     | 48      | Глибина зберігання (год), 12-168  |
| `datalogger.sample_interval` | int   | readwrite | yes     | 60      | Інтервал семплювання (с), 30-300  |
| `datalogger.log_evap`        | bool  | readwrite | yes     | false   | Логувати T випарника              |
| `datalogger.log_cond`        | bool  | readwrite | yes     | false   | Логувати T конденсатора           |
| `datalogger.log_setpoint`    | bool  | readwrite | yes     | false   | Логувати уставку                  |
| `datalogger.log_humidity`    | bool  | readwrite | yes     | false   | Логувати вологість                |
| `datalogger.records_count`   | int   | read      | no      | —       | Записів температури               |
| `datalogger.events_count`    | int   | read      | no      | —       | Кількість подій                   |
| `datalogger.flash_used`      | int   | read      | no      | —       | Flash використано (KB)            |

**MQTT publish:** `records_count`, `events_count`, `flash_used`
**MQTT subscribe:** `enabled`, `retention_hours`, `sample_interval`, `log_evap`, `log_cond`, `log_setpoint`, `log_humidity`

## UI сторінка

Сторінка "Графік" (`page_id: chart`, `icon: chart`, `order: 3`) містить 4 карточки:

1. **Температура** — ChartWidget з `data_source: /api/log`
2. **Статистика** — value widgets: records_count, events_count, flash_used
3. **Канали логування** (collapsible) — toggle widgets з `visible_when` для evap/cond/humidity
4. **Налаштування** (collapsible) — toggle enabled, number_input retention_hours та sample_interval

## Файли модуля

```
modules/datalogger/
├── manifest.json                    # State keys, UI, MQTT
├── include/datalogger_module.h      # ChannelDef, TempRecord, EventRecord, EventType
├── src/datalogger_module.cpp        # Реалізація (~520 рядків)
└── CMakeLists.txt

webui/src/
├── components/widgets/ChartWidget.svelte   # SVG chart (~640 рядків)
└── lib/downsample.js                       # Min/Max bucket downsampling
```

## Changelog
- 2026-02-25 — Створено (Phase 14b)
