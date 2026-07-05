# `simple_thermo` — еталонний ON/OFF термостат

> 📖 **In English:** [documentation/en/03-framework-reference/modules/simple_thermo.md](../../../en/03-framework-reference/modules/simple_thermo.md)

`simple_thermo` — це еталонний бізнес-модуль фреймворку: мінімальний термостат з гістерезисом, який читає температуру із SharedState, застосовує логіку уставки та диференціалу і керує бінарним виходом. Постачається з фреймворком насамперед як **перший модуль, який варто вивчити** при ознайомленні зі структурою типового бізнес-модуля.

Близько 150 рядків C++ та маніфест приблизно на 100 рядків. Найкраще читати одразу після `quickstart.md` і `02-module-author-guide/overview.md`.

ВИМАГАЄ: `modesp_core`. Читає `equipment.air_temp`; записує `simple_thermo.output` (бінарний запит нагріву).

## Поведінка

ON/OFF із симетричним гістерезисом (зоною нечутливості):

```
Output := ON  якщо temp < (setpoint - differential)
Output := OFF якщо temp >= setpoint
Output := без змін у решті випадків (у межах зони нечутливості)
```

Початковий стан OFF. Уставка та диференціал налаштовуються у часі виконання та зберігаються між перезавантаженнями.

## Ключі стану

| Ключ | Тип | Примітки |
|---|---|---|
| `simple_thermo.temperature` | float | Поточне показання (дзеркалить equipment.air_temp). |
| `simple_thermo.setpoint` | float | Уставка користувача (5-40 °C, за замовчуванням 22). Зберігається. |
| `simple_thermo.differential` | float | Гістерезис (0,5-5 °C, за замовчуванням 1). Зберігається. |
| `simple_thermo.state` | string | `"off"` / `"heating"` / `"idle"`. |
| `simple_thermo.output` | bool | Запит нагріву — підключайте до актуатора. |

`setpoint` і `differential` приймають запис через MQTT (`mqtt_subscribe: true`).

## Як це підключається

Читає `equipment.air_temp` (надається Equipment Manager + драйвером здатності `temperature`), записує `simple_thermo.output`. Модуль не знає, хто дає температуру (ds18b20 / NTC / віддалений канал) — лише споживає значення за ключем стану (R0.1). Щоб фактично керувати реле, направте `simple_thermo.output` до ролі актуатора через вашу бізнес-логіку АБО додатковий модуль, що знає про прив'язки.

Типовий цикл:

1. Equipment Manager читає сенсор DS18B20 → записує `equipment.air_temp`.
2. simple_thermo читає `equipment.air_temp`, застосовує гістерезис, оновлює `simple_thermo.output`.
3. (Ви налаштовуєте якийсь зв'язок між `simple_thermo.output` і `equipment.req_<heater_role>` — або простий модуль, АБО Equipment Manager, налаштований дзеркалити ці ключі.)

## Огляд джерел C++

Заголовок (`modules/simple_thermo/include/simple_thermo_module.h`, ~28 рядків):

```cpp
class SimpleThermoModule : public modesp::BaseModule {
public:
    SimpleThermoModule();
    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
private:
    bool heating_ = false;
};
```

Реалізація (~55 рядків): прямолінійний гістерезис у `on_update`. Читайте код напряму — це найпростіший підклас BaseModule у кодовій базі.

## Інтерфейс користувача (автоматично згенерований)

Маніфест оголошує дві картки:

1. **State** (тільки для читання): значення температури, рядок стану, індикатор виходу.
2. **Settings**: повзунок уставки, числове введення диференціалу.

Сторінка WebUI **"Thermostat"** з цими картками.

## Топіки MQTT

Публікує:
- `<base>/simple_thermo/temperature`
- `<base>/simple_thermo/state`
- `<base>/simple_thermo/output`
- `<base>/simple_thermo/setpoint`

Приймає:
- `<base>/cmd/simple_thermo.setpoint`
- `<base>/cmd/simple_thermo.differential`

## Інтеграція з DataLogger

Автоматично логуються:
- Канал `simple_thermo.temperature` (температура, ввімкнено за замовчуванням).
- Подія `simple_thermo.output` (id 30, обидва фронти, "Heating ON" / "Heating OFF").

## Чому це гарний перший приклад

- Маніфест охоплює state, mqtt, loggable, ui — усі типові секції.
- Клас C++ **тривіальний** (~50 рядків) — легко читати.
- Демонструє патерн гістерезису, придатний у багатьох сферах.
- Показує потік даних `equipment.* → simple_thermo.* → equipment.req_*`.
- Патерн збереження уставки.

Зрозумівши цей модуль, напишіть власну варіацію з іншою керуючою логікою АБО підтримкою кількох зон. Структура переноситься.

## Що далі

- **[02-module-author-guide/writing-a-module.md](../../02-module-author-guide/writing-a-module.md)** — покрокова інструкція з використанням цього патерну.
- **[modules/equipment.md](equipment.md)** — провайдер сенсорів вище за потоком.
- **[modules/datalogger.md](datalogger.md)** — споживач даних нижче за потоком.

## Джерела

- [`modules/simple_thermo/manifest.json`](../../../../modules/simple_thermo/manifest.json)
- [`modules/simple_thermo/include/simple_thermo_module.h`](../../../../modules/simple_thermo/include/simple_thermo_module.h)
- [`modules/simple_thermo/src/simple_thermo_module.cpp`](../../../../modules/simple_thermo/src/simple_thermo_module.cpp)
