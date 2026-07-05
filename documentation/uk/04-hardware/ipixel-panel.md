# iPixel LED-панель — підключення та керування

> 📖 **In English:** [documentation/en/04-hardware/ipixel-panel.md](../../en/04-hardware/ipixel-panel.md)

Практичний гайд: як підключити китайську RGB LED-матрицю **iPixel Color / LED_BLE 64×16** до ModESP по BLE та керувати нею — з WebUI (живлення, яскравість, ефект, своє повідомлення, колір) і програмно з інших модулів (текст-слоти). Це **how-to**; внутрішню будову див. у референсі [`drivers/ble_led_panel`](../03-framework-reference/drivers/ble_led_panel.md), [`modules/panel`](../03-framework-reference/modules/panel.md), [`components/modesp_ble`](../03-framework-reference/components/modesp_ble.md).

**ВИМАГАЄ:** ESP32-**S3** (BLE), увімкнений `modesp_ble` (роль CENTRAL), панель iPixel/LED_BLE у радіусі дії.

## Що знадобиться

- Панель **iPixel Color / LED_BLE 64×16** (ADV-ім'я виду `LED_BLE_XXXXXXXX`).
- Плата **ESP32-S3** (наприклад, `stand_s3`).
- Wi-Fi — для WebUI (вкладка iPixel).

> ADV-ім'я панелі впізнаєш у будь-якому BLE-сканері (nRF Connect) або в офіційному додатку iPixel — драйвер під'єднується за префіксом **`LED_BLE_`**.

## Крок 1 — Увімкнути BLE + CENTRAL (Kconfig)

`idf.py menuconfig` → меню **«ModESP BLE»**:

```
[*] CONFIG_MODESP_BLE_ENABLE      # спільний BLE-хост
[*] CONFIG_MODESP_BLE_CENTRAL     # підключення до панелі
```

Без `CENTRAL` драйвер не має чим під'єднатися до панелі.

## Крок 2 — Підписати панель у runtime (сторінка «Пристрої») і прив'язати роль

Панель — це **remote-пристрій BLE**, а не дротове залізо: її НЕ оголошують у `board.json`. Драйвер реєструє connect-префікс `LED_BLE` на BOOT, тож неприв'язана панель з'являється в уніфікованому скані `GET /api/ble/scan` ще до будь-якого біндінгу. Порядок: **скан → підписка → прив'язка ролі** (R4.3, R6.2).

1. **Скан + підписка.** WebUI → сторінка **«Пристрої»** → скан BLE → знайди свою панель за ADV-ім'ям (`LED_BLE_XXXXXXXX`) → **Підписати**. Підписка пишеться у `/data/devices.json` (runtime-only, gitignored) як `RemoteDeviceConfig{id, transport, identity, name}` — де `name` = ПОВНЕ ADV-ім'я панелі (її connect-таргет), `transport` авто-виводиться (`ble`), а `id` (напр. `led_panel`) — це стабільний хендл пристрою (R4.1, R4.3).

2. **Прив'язка ролі** — у `data/bindings.json` (шаблон — `boards/<board>/bindings.json`) прив'яжи роль до пристрою за його **`id`**:

```json
// bindings.json
{"hardware": "led_panel", "driver": "ble_led_panel", "role": "panel", "module": "panel"}
```

Біндінг посилається на device `id`, **ніколи** на ADV-ім'я/MAC — ідентичність живе на рядку пристрою в `devices.json`, не на ролі (R0.3). Роль `panel` оголошує сам `panel`-модуль (`requires: [{"role":"panel","capability":"panel"}]`), тож панель резолвиться за **capability `panel`**, а не за драйвером — модуль не знає, який транспорт її дає (R0.1, R1.2). Деталі полів — [board-config.md](board-config.md) і [bindings.md](bindings.md).

## Крок 3 — Підключити модулі

Контент і веб-керування дає `panel`-модуль; драйвер `ble_led_panel` тримає BLE-лінк.

```json
// project.json
"modules": [ "...", "panel" ]
```

CMake-залежність з'являється автоматично (`generated/modules.cmake` →
`main/CMakeLists.txt`) — ручних правок не потрібно.

Драйвер `ble_led_panel` опційний у menuconfig сам стає доступним (генератор додає toggle). Якщо плата прив'язує драйвер, вимкнений у menuconfig, білд падає з FATAL — узгодь: `python tools/drivers_sync.py --fix`.

## Крок 4 — Зібрати та прошити

```bash
idf.py build
idf.py flash monitor
```

Після старту в лозі: `panel connected (power/brightness/effect/text driven by the panel module)`. Панель вмикається, і на ній починає ротуватися годинник / температура / вологість (за наявності сенсорів).

## Керування з WebUI (вкладка «iPixel»)

Відкрий WebUI (IP плати у браузері) → вкладка **iPixel**. Контроли керуються одразу (live) і зберігаються між перезавантаженнями:

| Контрол | Що робить |
|---|---|
| **Живлення** | ON / OFF панелі |
| **Яскравість** | 5..100 % |
| **Ротація** | Авто (годинник/темп/вологість) або Пауза (утримати кадр) |
| **Ефект** | Анімація тексту `0..7` (див. нижче) |
| **Повідомлення** | Свій текст (до 31 символа) — показується **замість** ротації; порожнє поле → ротація повертається |
| **Колір** | Колір повідомлення (нативний пікер) |
| **Слоти модулів** | Read-only перегляд тексту, що пишуть інші модулі (Крок нижче) |

Тими самими ключами можна керувати по **MQTT** (крім текстових: `panel.power` / `panel.brightness` / `panel.rotate` / `panel.anim`).

### Ефекти анімації (`anim`)

HW-підтверджено на панелі:

| `anim` | Ефект | `anim` | Ефект |
|---|---|---|---|
| 0 | статика | 4 | згори→вниз |
| 1 | скрол справа→наліво | 5 | блимання |
| 2 | скрол зліва→направо | 6 | дихання |
| 3 | знизу→вгору | 7 | drop-in (порядкове складання) |

## Вивід тексту з інших модулів (слоти-API)

Будь-який модуль може вивести свій рядок на панель через **5 спільних текст-слотів**. Це тонка конвенція над SharedState — слоти це звичайні рядкові ключі, тож пишеш їх власним `state_set`:

```cpp
#include "panel_text.h"
// ... зсередини методу модуля (on_update / on_init):
state_set(modesp::panel_text::slot(0), "ALARM");     // вивести у слот 0
state_set(modesp::panel_text::slot(1), "DEFROST");
state_set(modesp::panel_text::slot(0), "");          // очистити слот 0
```

Непорожні слоти **ротуються на екрані** (білим) поряд із сенсорами. Порожній (`""`) слот не показується. Повний опис — [modules/panel § API виводу тексту](../03-framework-reference/modules/panel.md#api-виводу-тексту-слоти-модулів).

## Типові помилки

- **Панель не знаходиться** — панель не підписана на сторінці «Пристрої» (немає рядка в `/data/devices.json`), або роль `panel` не прив'язана до її `id` у `bindings.json`, або вимкнений `CONFIG_MODESP_BLE_CENTRAL`, або панель поза радіусом. Пересканай у «Пристроях» і перевір підписку.
- **Build FATAL про вимкнений драйвер** — плата прив'язує `ble_led_panel`, але його вимкнено в menuconfig. `python tools/drivers_sync.py --fix`.
- **Повідомлення не очищується у WebUI** — стерти весь текст і клікнути поза полем (commit на blur/Enter); порожнє значення повертає ротацію.
- **Текст обрізається на 31 символі** — це стеля рядка стану (`etl::string<32>`); панель скролить до 31 символа.
- **`idf.py` скаржиться на версію тулчейна** — отруєний `build/` після зміни версії IDF; `idf.py fullclean` і збери знову.

## Що далі

- **[modules/panel.md](../03-framework-reference/modules/panel.md)** — референс контент-модуля (іконки, кольори, ефекти, слоти-API).
- **[drivers/ble_led_panel.md](../03-framework-reference/drivers/ble_led_panel.md)** — драйвер (BLE-лінк, control-plane).
- **[components/modesp_ble.md](../03-framework-reference/components/modesp_ble.md)** — спільний BLE-хост (observer / central / peripheral).
- **[bindings.md](bindings.md)** — прив'язки драйверів до залоза.

## Джерела

- [`boards/stand_s3/board.json`](../../../boards/stand_s3/board.json), [`boards/stand_s3/bindings.json`](../../../boards/stand_s3/bindings.json)
- [`drivers/ble_led_panel/`](../../../drivers/ble_led_panel/)
- [`modules/panel/`](../../../modules/panel/)
- [`modules/panel/include/panel_text.h`](../../../modules/panel/include/panel_text.h) — слоти-API
- [`docs/ble/panel_protocol.md`](../../../docs/ble/panel_protocol.md) — байтовий протокол панелі
