# `amt630a` — I²C OSD/TFT відео-SoC як дисплей (capability `display`)

> 📖 **In English:** [.../en/.../amt630a](../../../en/03-framework-reference/drivers/amt630a.md)

Драйвер бекенду дисплея для відео-SoC **AMT630A** (плата ZCD-630A-4.3D): чіп
керує TFT-панеллю та композитним відео, а ESP32 накладає поверх нього OSD (текст,
меню, банери сповіщень) по I²C. Драйвер надає capability **`display`** через
адаптер `Amt630aPort`, що реалізує шов `IDisplayPort` (`present_main` /
`present_menu` / `present_edit` / `present_notice`). Модуль
[`display`](../modules/display.md) володіє лише *контентом* (що показати) і
викликає порт за роллю — самого чіпа, банків I²C чи послідовностей регістрів він
не знає (див. [R0.1](../rules.md#r01--роль--здатність-capability-ніколи-не-драйвер),
[ADR-002](../../../../docs/display/ADR-002-display-architecture.md)).

**Формат протоколу належить драйверу.** Уся розкладка регістрів AMT630A
(vendor-unlock, OSD-вікна, FONT RAM, палітра, PWM-підсвітка, CVBS-мукс,
no-signal backdrop, power-gate) живе в компоненті драйвера
(`osd::Amt630a` + `Amt630aPort`). Модуль подає семантичний intent
(`MainView` / `MenuView` / `EditView` / `Notice`), а порт вирішує *презентацію*
сам — велике головне значення у вікні W0 ×2, дрібні рядки у W1, банер сповіщення
кольором за рівнем (ADR-002 §6: «презентацію вирішує драйвер»).

`hardware_type` — `i2c_display`, `transport` — `wired`. Прив'язка йде через
`i2c_displays` у **board.json** (геометрія + I²C-шина), а не через MAC чи адресу —
`requires_address: false`. Драйвер опційний: SRCS гейтяться
`CONFIG_MODESP_DRIVER_AMT630A` (menu **«ModESP Drivers»**).

## Залізо

| Параметр | Значення |
|----------|----------|
| Чіп | AMT630A (OSD/TFT video-SoC) на платі ZCD-630A-4.3D |
| Панель | 480×272 TFT (overscan), композитне відео CVBS |
| Транспорт | I²C (`wired`), 6 device-адрес (банків): `0x58`/`0x59`/`0x5A`/`0x5B`/`0x5C`/`0x5F` |
| Пробіг присутності | ACK на банку OSD `0x5B` (`i2c_master_probe`) |
| Входи відео | 2 × CVBS (AV1/CVBS1, AV3/CVBS3; AV2 — junk) |
| Підсвітка | PWM0 duty (`FD42`/`FD1F`), 0–100 % |
| Power-gate | опційний load-switch на GPIO (0 мА у OFF) |
| Шрифт | кириличний RAM-шрифт 16×20, 1bpp, у FONT RAM чіпа |

## Capability і канали

Драйвер надає рівно одну capability — **`display`** (`provides.type: display`),
`settings: []` (жодних per-driver налаштувань у маніфесті). Каналів у сенсі
«температура/вологість» тут немає: `display` — це *вивідна* здатність, до якої
модуль підключається через `IDisplayPort`, а не набір іменованих state-key'ів.

Порт декларує свої можливості через `caps()` — модуль читає їх і адаптує UX:

| `DisplayCaps` | Значення | Що дає |
|---------------|----------|--------|
| `has_color` | `true` | палітра OSD (color1 червоний, color2 жовтий…) |
| `has_backlight` | `true` | `set_backlight(pct)` — PWM0 duty |
| `has_video_params` | `true` | `set_brightness` / `set_contrast` / `set_saturation` |
| `has_inputs` | `true`, `input_count = 2` | `select_input(n)` — CVBS-мукс |
| `has_backdrop` | `true` | `set_backdrop(SNOW/BLUE/BLACK)` — фон no-signal |
| `has_power` | `power_gpio >= 0` | `set_rail(on)` + неблокуюче chunked-відновлення OSD |

## Прив'язки

### board.json

Дисплей оголошується у `i2c_displays` — там живе шина, геометрія OSD-сітки
(`cols`/`rows`) та overscan-калібровка (`cal_x`/`cal_y` у пікселях):

```json
"i2c_buses": [
  { "id": "i2c_0", "sda": 6, "scl": 5, "freq_hz": 100000 }
],
"i2c_displays": [
  { "id": "disp_0", "bus": "i2c_0", "chip": "amt630a",
    "cols": 20, "rows": 10, "cal_x": -8, "cal_y": -8 }
]
```

### bindings.json

Один запис, що прив'язує дисплей до модуля `display` через роль-capability
`display_main`:

```json
{ "hardware": "disp_0", "driver": "amt630a", "role": "display_main", "module": "display" }
```

| Поле | Значення |
|------|----------|
| `hardware` | `id` дисплея з `i2c_displays` (`disp_0`) |
| `driver` | `amt630a` |
| `role` | `display_main` — оголошує модуль-власник `display` |
| `module` | `display` |

Опційний **power-gate**: додай `"power_gpio": <n>` у binding — драйвер підніме
живлення чіпа на старті і зможе знеструмлювати панель у sleep
(`caps().has_power` стане `true`). Без нього `set_rail()` — no-op з warning.

## Протокол (стисло)

Порт конфігурує OSD **поверх працюючої OEM-прошивки** — жодного off→on
відеобанків, щоб не зіпсувати робоче зображення. Ключові інваріанти реалізації:

- **DANGER-регістри заблоковано** (`is_danger`): PLL, SPI-flash піни, Tcon-банк
  `0x5C` — запис у них зависає чіп або псує flash; `amt_w` їх мовчки відкидає.
- **Вікна вмикаються ПІСЛЯ запису BGMAP** (`window_enable` в кінці `render`) —
  кадр з'являється атомарно, без блимання сміття на старті.
- **Кириличний шрифт** заливається у FONT RAM як 1bpp з релятивним
  `bitmap_start` (0x1C0-база); UTF-8 → кодпойнт → tile через `amt630a_cp_to_tile`.
- **Power-gate recovery неблокуючий** (`service()` крок за тіком): після
  cold-boot чіпа OSD губиться, тож порт chunked-реконфігурує (WAIT→SETUP→FONT),
  не блокуючи main-loop (монолітний reconfig ~5–7 с трипив би TWDT). Поки
  `busy()` — модуль чекає, потім повторно подає кадр.

Глибше — у дизайн-доках драйвера: розкладка регістрів
([AMT630A_control_reference](../../../../docs/amt630a/AMT630A_control_reference.md)),
модель драйвера ([AMT630A_driver_design](../../../../docs/amt630a/AMT630A_driver_design.md)),
sleep/power ([AMT630A_power_modes](../../../../docs/amt630a/AMT630A_power_modes.md)),
доставка сповіщень ([ADR-001](../../../../docs/amt630a/ADR-001-osd-notifications.md)).

## Фабрика і реєстрація

Одна точка реєстрації (R3.2): фабрика резолвить `i2c_display` та шину через HAL і
будує **singleton**-порт (zero heap, статичний на місці):

```cpp
IDisplayPort* amt630a_factory(const modesp::Binding& b, modesp::HAL& hal) {
    auto* dcfg = hal.find_i2c_display(b.hardware_id);          // board.json → геометрія
    auto* bus  = hal.find_i2c_bus(dcfg->bus_id);               // board.json → шина
    const int power_gpio = static_cast<int>(b.setting_or("power_gpio", -1.0f));
    static Amt630aPort port(bus->bus_handle, bus->freq_hz,
                            dcfg->cols, dcfg->rows, dcfg->cal_x, dcfg->cal_y, power_gpio);
    return &port;
}
MODESP_REGISTER_DISPLAY(amt630a, &amt630a_factory)
```

## Опційність (Kconfig)

```
CONFIG_MODESP_DRIVER_AMT630A   (menu «ModESP Drivers»)
```

Вимкнений драйвер не компілюється (SRCS-гейт, R5.2). Якщо board.json прив'язує
`amt630a`, а toggle вимкнений — білд падає з FATAL; узгодити
`python tools/drivers_sync.py --fix`.

## Що далі

- **[modules/display.md](../modules/display.md)** — модуль-власник ролі `display_main` (меню, редагування, сповіщення).
- **[ADR-002](../../../../docs/display/ADR-002-display-architecture.md)** — дворівнева архітектура: шов `IDisplayPort` + драйвер-адаптер.
- **[ADR-003](../../../../docs/display/ADR-003-amt630a-adr002-compliance.md)** — відповідність AMT630A-порту до ADR-002.
- **[rules.md](../rules.md)** — R0.1 (роль=capability), R3.2/R3.3 (маршрут периферії), R5.2 (опційність).
- **[project-hierarchy.md](../project-hierarchy.md)** — маршрут периферії Module↔Role↔Device↔Binding + інваріанти.
- **Сусідній backend:** [ble_led_panel](ble_led_panel.md) — інший вивідний драйвер (`IPanelPort`) через BLE.

## Джерела

- [`drivers/amt630a/manifest.json`](../../../../drivers/amt630a/manifest.json)
- [`drivers/amt630a/src/amt630a_port.cpp`](../../../../drivers/amt630a/src/amt630a_port.cpp) — адаптер `IDisplayPort` + фабрика/реєстрація
- [`drivers/amt630a/src/amt630a.cpp`](../../../../drivers/amt630a/src/amt630a.cpp) — чіп-контрол (I²C-банки, OSD, шрифт, CVBS, backdrop, PWM)
