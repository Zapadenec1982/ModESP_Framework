# `ble_nrf_tilt` — BLE-маячок нахилу/орієнтації nRF52832

> 📖 **In English:** [.../en/.../ble_nrf_tilt](../../../en/03-framework-reference/drivers/ble_nrf_tilt.md)

Сенсорний драйвер для tilt/orientation-маячка на **nRF52832** (модуль HolyIOT
21011 + акселерометр LIS2DH12). Драйвер пасивно слухає BLE-рекламу через
**OBSERVER** спільного хоста `modesp_ble` — з'єднання не встановлюється. На відміну
від Xiaomi-датчиків, маячок рекламує **manufacturer data** (компанія `0xFFFF`), а не
service-data, тож декодер реєструється у пулі manufacturer-декодерів транспорту
(`register_adv_mfg_decoder`). Один фізичний пристрій дає **шість каналів**
(кут нахилу, прапорець нахилу, заряд, сирі осі X/Y/Z), кожен зі своєю **capability** —
канал обирається полем `address` у прив'язці.

**Формат даних належить драйверу.** Поля цього пристрою специфічні (кут, tilted,
батарея, сирі осі), тому драйвер тримає **власний per-MAC кеш** (`NrfTiltReading`),
а не спільний temp/hum/battery кеш BTHome-датчиків. Транспорт володіє лише радіо та
пасивним скануванням — формату пристрою він не знає — і роздає кожен кадр
manufacturer-data зареєстрованим декодерам. Декодер, що впізнав свій формат
(company `0xFFFF`, ver=1, рівно 15 байт, magic-маркер `0xA7` на `md[8]`), кладе
показник у кеш за MAC, який читає драйвер.

`hardware_type` — `ble`, `transport` — `ble`. **Ідентичність (MAC) живе на пристрої**
(`RemoteDeviceConfig{transport, identity, name}`), не на ролі (R0.3). Маячок
підписується у runtime через сторінку **Devices** (`/data/devices.json`), а прив'язка
йде за **device id**, ніколи за MAC (R4.3). Драйвер залежить від компонента
`modesp_ble`, який має бути ввімкнений (`CONFIG_MODESP_BLE_CENTRAL`), а сам драйвер —
опційний (`CONFIG_MODESP_DRIVER_BLE_NRF_TILT`).

## Здатність і канали

Роль ніколи не знає драйвера — вона оголошує **capability** (R0.1, R3.1). Один
фізичний пристрій живить кілька ролей; `address` у прив'язці обирає величину. Це
**фіксований enum** каналів (`address_channels`), а не сканування шини.

| `address` | Capability | Мітка (picker) | Значення |
|-----------|-----------|----------------|----------|
| `angle`   | `angle`     | *(з `capabilities.json`: Angle)* | Кут нахилу, ° (`-1` → недійсний кадр) |
| `tilted`  | `binary_in` | `Tilted (0/1)` | Прапорець нахилу: `1.0` / `0.0` |
| `battery` | `battery`   | *(з `capabilities.json`: Battery mV)* | Заряд, мВ |
| `ax`      | `accel`     | `Axis X (raw)` | Сира вісь X акселерометра |
| `ay`      | `accel`     | `Axis Y (raw)` | Сира вісь Y |
| `az`      | `accel`     | `Axis Z (raw)` | Сира вісь Z |

Мітки `angle`/`battery` **опущені** в маніфесті й деривуються з `capabilities.json`
(R1.3). `tilted` та три осі мають явні мітки: `binary_in`/`accel` — генеричні
здатності, тож драйвер іменує канали, щоб розрізнити їх у picker. Генератор віддає ці
канали в `role.channels_by_driver` — `<select>` каналу з'являється лише за 2+ каналів
однієї здатності (напр. три осі `accel`); одиничний канал авто-прив'язується (R3.5).

Пристрій вважається несправним (`is_healthy() == false`), якщо протягом `stale_ms`
(за замовчуванням 60 с) не надійшло жодного adv-кадру.

## Прив'язки

### devices.json (runtime)

Маячок — remote-пристрій: він з'являється в єдиному BLE-скані (`GET /api/ble/scan`)
ще до будь-якої прив'язки, бо декодер реєструється на BOOT. Оператор підписує його на
сторінці **Devices**; підписка пишеться пристроєм у `/data/devices.json` (R4.3 —
runtime-only, ніколи не build-вхід, gitignored):

```json
{
  "devices": [
    { "id": "tank_tilt", "transport": "ble", "identity": "e2:81:15:44:aa:03", "name": "Tank tilt" }
  ]
}
```

### bindings.json

Один запис на канал — кілька прив'язок того самого `hardware` (device id) з різними
`address` / `role`. `hardware` посилається на device id, MAC на біндінгу немає:

```json
[
  { "hardware": "tank_tilt", "driver": "ble_nrf_tilt", "role": "tank_angle",  "module": "equipment", "address": "angle" },
  { "hardware": "tank_tilt", "driver": "ble_nrf_tilt", "role": "tank_tipped", "module": "equipment", "address": "tilted" },
  { "hardware": "tank_tilt", "driver": "ble_nrf_tilt", "role": "tank_batt",   "module": "equipment", "address": "battery" }
]
```

`binding.module` мусить називати модуль з `project.json`, що є власником ролі (R3.4).
Модулі ніколи не звертаються до BLE напряму — I/O робить драйвер, модуль лише читає
SharedState (R3.3).

## Налаштування

Одне per-binding налаштування (маніфест `settings`), редагується у WebUI на сторінці
**Налаштування датчиків** (картка `nRF tilt: {{hardware_id}}`, `access_level: service`):

| Ключ | Тип | Дефолт | Діапазон | Опис |
|------|-----|--------|----------|------|
| `stale_ms` | int | `60000` | `5000`…`600000`, крок `1000` (мс) | Без adv-кадру довше — датчик несправний |

## Протокол

Маячок рекламує manufacturer data з компанією `0xFFFF` (спільний тестовий id), тож
драйвер робить збіг детермінованим: рівно 15 байт, `ver=1`, magic-маркер `0xA7` на
`md[8]` (константа з firmware nRF). Розкладка кадру (включно з 2-байтним префіксом
компанії):

```
[0..1]  FF FF            company
[2]     ver = 0x01
[3]     flags            bit0 = tilted
[4]     tilt_deg         0xFF = недійсний (angle → -1)
[5..6]  vbat_mV          LE
[7]     seq
[8]     0xA7             magic-маркер (детермінований gate)
[9..14] ax, ay, az       int16 LE — сирі осі акселерометра
```

Декодер також викликає `ble::report_seen(mac, rssi, "ble_nrf_tilt", "45° 2900mV")` —
короткий рядок поточних показників, щоб оператор міг розрізнити два nRF-датчики у
скані (нахили один — і дивись, у якого рядка кут змінюється наживо).

Байтовий розбір живе **у драйвері** — транспорт формату не знає. Ідентичність device id
→ MAC резолвиться з об'єднаного реєстру (devices.json ∪ board.json) через
`hal.find_ble_device`; порядок байтів display-MAC розвертається у NimBLE `addr.val`
(little-endian), тож фабрика й декодер ключать **той самий** слот кешу за MAC.

## Типові помилки

- **MAC на біндінгу.** Ідентичність — на пристрої (devices.json), не на ролі. MAC у
  bindings.json прив'язує роль до транспорту й порушує R0.3.
- **Прив'язка перед підпискою.** Спершу підпишіть пристрій у **Devices** (з'явиться в
  скані); лише тоді `hardware: <id>` резолвиться. Інакше — `not in board.json nor devices.json`.
- **Драйвер вимкнений у menuconfig.** Плата, що прив'язує вимкнений драйвер → FATAL на
  build-time. Узгодити: `python tools/drivers_sync.py --fix` (R8.3).
- **Пул кешу — 6 різних MAC.** `MAX_NRF_DEVICES = 6`; сьомий фізичний маячок не влізе в
  кеш. Прив'язок (каналів) — до `6 × 6`.
- **Не редагуйте `data/`.** board/bindings правте в `boards/<board>/`; `devices.json` не
  лишайте в `data/` під час білду (потрапить у data.bin і перезапише реальні підписки).

## Що далі

- **[bindings.md](../../04-hardware/bindings.md)** — синтаксис bindings.json і як
  `address` обирає канал.
- **[ble_xiaomi_th.md](ble_xiaomi_th.md)** — сусідній BLE-драйвер (service-data,
  спільний кеш) — контраст із manufacturer-data + власним кешем цього драйвера.
- **[ble_led_panel.md](ble_led_panel.md)** — BLE-актуатор (панель), інший напрям здатності.
- **[project-hierarchy.md](../project-hierarchy.md)** — маршрут периферії
  Module↔Role↔Device↔Binding + інваріанти.
- **[rules.md](../rules.md)** — R0.1 (роль=capability), R0.3 (ідентичність на пристрої),
  R3.5 (per-driver канали), R4.1 (`RemoteDeviceConfig`).

## Джерела

- [drivers/ble_nrf_tilt/manifest.json](../../../../drivers/ble_nrf_tilt/manifest.json) —
  capability/канали/налаштування/hardware_type/transport.
- [drivers/ble_nrf_tilt/src/ble_nrf_tilt_driver.cpp](../../../../drivers/ble_nrf_tilt/src/ble_nrf_tilt_driver.cpp) —
  декодер + власний кеш + фабрика + register-хук.
- [drivers/ble_nrf_tilt/include/ble_nrf_tilt_driver.h](../../../../drivers/ble_nrf_tilt/include/ble_nrf_tilt_driver.h) —
  `NrfTiltReading`, `Channel`, розкладка кадру.
- [components/modesp_ble/](../../../../components/modesp_ble/) — транспорт (OBSERVER,
  `adv_decoder.h`, `report_seen`).
