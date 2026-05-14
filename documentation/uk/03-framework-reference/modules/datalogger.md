# DataLogger module

> 📖 **In English:** [documentation/en/03-framework-reference/modules/datalogger.md](../../../en/03-framework-reference/modules/datalogger.md)

`datalogger` — це універсальний модуль, який реєструє значення SharedState у часі ТА записує дискретні події. Дані зберігаються у файлах LittleFS із налаштовуваним терміном зберігання. WebUI використовує віджет графіка для відображення нещодавньої історії; HTTP API надає дані зовнішнім споживачам.

Модуль керується маніфестом — секція `loggable` у маніфесті вашого бізнес-модуля оголошує, які ключі є каналами (безперервні), А ЯКІ — подіями (на фронтах). DataLogger знаходить їх під час збірки та записує дані за власним розкладом.

ВИМАГАЄ: `modesp_core`, `modesp_services`, розділ LittleFS.

## Що він робить

- **Логування каналів:** робить вибірку зазначених ключів стану з налаштовуваним користувачем інтервалом (за замовчуванням 60 с, діапазон 30-300). Зберігає записи `(timestamp, value)` компактно у LittleFS.
- **Логування подій:** фіксує висхідні/спадні/обидва фронти на булевих ключах. Зберігає записи `(timestamp, event_id)`.
- **Зберігання:** видаляє найстаріші записи, коли загальна кількість перевищує `datalogger.retention_hours` (за замовчуванням 48, діапазон 12-168).
- **Доступ через WebUI:** віджет графіка запитує `/api/datalogger/series?key=X&window=1h`.
- **Експорт CSV:** `GET /api/datalogger/export?key=X&from=...&to=...`.

## Ключі стану

| Ключ | Тип | Примітки |
|---|---|---|
| `datalogger.enabled` | bool | Головний перемикач. За замовчуванням true. |
| `datalogger.retention_hours` | int | 12-168, за замовчуванням 48. Зберігається. |
| `datalogger.sample_interval` | int | 30-300 с, за замовчуванням 60. Зберігається. |
| `datalogger.records_count` | int | Загальна кількість записів каналів у буфері. |
| `datalogger.events_count` | int | Загальна кількість записів подій. |
| `datalogger.flash_used` | int | Спожиті байти (КБ). |
| `datalogger.log_<channel>` | bool | Прапорець увімкнення для кожного каналу. |

Поканальні прапорці увімкнення (`log_evap`, `log_cond`, `log_setpoint` тощо) автоматично генеруються з прапорця `default` зареєстрованих каналів — якщо у маніфесті вказано `"default": true`, відповідний `log_<channel>` за замовчуванням true.

## Як модулі оголошують канали та події

Маніфест бізнес-модуля містить:

```json
"loggable": {
  "channels": {
    "simple_thermo.temperature": {
      "type": "temperature",
      "label": "Temperature",
      "default": true
    }
  },
  "events": {
    "simple_thermo.output": {
      "id": 30,
      "edge": "both",
      "label_on": "Heating ON",
      "label_off": "Heating OFF"
    }
  }
}
```

Генератор об'єднує секції `loggable` усіх модулів і створює `datalogger_channels.h` та `datalogger_events.h` із зведеними списками. DataLogger зчитує їх під час компіляції, А його `on_update` робить вибірку та фіксацію відповідно.

Див. [02-module-author-guide/manifest.md](../../02-module-author-guide/manifest.md#section-loggable-service-modules).

## Формат зберігання

Розділ LittleFS (за замовчуванням 960 КБ):

```
/data/
└── datalogger/
    ├── channels.bin          бінарні записи (timestamp + channel_id + value)
    ├── events.bin            бінарні записи (timestamp + event_id + flags)
    └── ... (ротовані файли для очищення згідно терміну зберігання)
```

Компактне бінарне кодування:
- Запис каналу: 12 байтів (4 timestamp + 1 channel_id + 4 float value + 3 padding).
- Запис події: 8 байтів (4 timestamp + 2 event_id + 1 edge_type + 1 padding).

48-годинний буфер при вибірці кожні 60 с = ~3000 записів на канал. 6 каналів = ~18000 записів = ~216 КБ. Вміщається із запасом.

## HTTP API

| Endpoint | Призначення |
|---|---|
| `GET /api/datalogger/series?key=X&window=1h` | JSON: `[{t, v}, ...]` відфільтровано за вікном часу. |
| `GET /api/datalogger/events?from=T&to=T` | JSON-список подій. |
| `GET /api/datalogger/export?key=X&from=T&to=T` | Завантаження CSV. |
| `POST /api/datalogger/clear` | Очистити всі логи. |
| `POST /api/datalogger/settings` | Оновити прапорці увімкнення / інтервали. |

Рядки вікон: `10m` / `1h` / `24h` / `7d` / `30d` (обмежено терміном зберігання).

## Інтеграція з віджетом графіка

Маніфест UI:

```json
{
  "key": "equipment.air_temp",
  "widget": "chart",
  "window": "1h",
  "height": 200
}
```

Віджет запитує endpoint серії та малює лінійний графік SVG. Кілька серій ще не підтримуються у MVP (Stage 1.5).

## Продуктивність і пам'ять

| Ресурс | Вартість |
|---|---|
| Кільцевий буфер у RAM | ~8 КБ (кешуються найсвіжіші записи) |
| Пакет запису LittleFS | ~1 КБ на скидання (кожні 5 хвилин) |
| CPU на такт | < 0,5 мс типово |

Записи з вибірки обмежені за частотою — фактичні записи у flash відбуваються приблизно кожні 5 хв (налаштовується). Обмеження частоти у стилі NVS захищає ресурс flash.

## Типові шаблони використання

### Додавання нового каналу

У маніфесті вашого бізнес-модуля:

```json
"loggable": {
  "channels": {
    "my_module.energy_kwh": {
      "type": "kwh",
      "label": "Energy",
      "default": true
    }
  }
}
```

Після `idf.py build` `datalogger_channels.h` міститиме новий канал. DataLogger почне робити вибірку при наступному перезавантаженні.

### Програмний запит

```python
import requests
data = requests.get(
    "http://192.168.1.85/api/datalogger/series",
    params={"key": "equipment.air_temp", "window": "24h"},
    auth=("admin", "modesp"),
).json()
# data = [{"t": 1700000000, "v": 22.5}, ...]
```

## Типові помилки

**Нестабільні ID подій:** не перенумеровуйте ID подій у маніфестах. Існуючі файли LittleFS кодують старі ID; перенумерування призводить до неправильних міток.

**Часта вибірка зношує flash:** інтервал вибірки менше 30 с заборонений не просто так. Витривалість flash — приблизно 100 тис. циклів стирання на сектор. При вибірці кожні 30 с прогнозований термін служби прошивки — десятиліття. При 1 с — лише місяці.

**Забуто увімкнути поканальні перемикачі:** навіть якщо канал оголошено у маніфесті, прапорець `datalogger.log_<channel>` керує тим, чи фактично робиться вибірка. WebUI показує ці перемикачі.

**Віджет графіка показує порожньо:** перевірте, чи ключ зареєстровано у `datalogger.channels`. Якщо секцію `loggable` бізнес-модуля було додано посеред розробки, datalogger може ще не мати жодних записів. Зачекайте 1-2 інтервали вибірки.

## Що далі

- **[02-module-author-guide/manifest.md](../../02-module-author-guide/manifest.md#section-loggable-service-modules)** — синтаксис оголошення у маніфесті.
- **[02-module-author-guide/ui-widgets.md](../../02-module-author-guide/ui-widgets.md)** — довідник віджета графіка.

## Джерела

- [`modules/datalogger/`](../../../../modules/datalogger/) — реалізація.
- [`generated/datalogger_channels.h`](../../../../generated/datalogger_channels.h) — автоматично згенерований список каналів.
- [`generated/datalogger_events.h`](../../../../generated/datalogger_events.h) — автоматично згенерований список подій.
