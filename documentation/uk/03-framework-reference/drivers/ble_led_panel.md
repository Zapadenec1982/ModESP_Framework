# `ble_led_panel` — BLE RGB LED-панель 64×16 (iPixel / LED_BLE)

> 📖 **In English:** [documentation/en/03-framework-reference/drivers/ble_led_panel.md](../../../en/03-framework-reference/drivers/ble_led_panel.md)

Драйвер керує китайською RGB LED-матрицею **iPixel Color / LED_BLE 64×16** через BLE. Це **actuator** з `hardware_type: ble` (`transport: ble`), який оголошує `capability: panel` і підключається до панелі **за ім'ям реклами (ADV-NAME)**, а не за MAC. Ідентичність (adv-name) живе на рядку пристрою в `ble_devices`, ніколи на біндінгу ролі ([R0.3](../rules.md)). Драйвер володіє **всім форматом дротового протоколу iPixel**: GATT-UUID'ами, control-байтами (живлення / яскравість), нативним кодером TEXT-кадру (гліфи + CRC32 + чанкінг) і фоновою render-задачею. BLE-лінк він отримує, реєструючи **connect-профіль** у загальному central-link seam хоста `modesp_ble` (`central_link.h`) — цей транспорт **не знає** жодного формату пристрою. Модуль [`panel`](../modules/panel.md) володіє **лише контентом** (що показувати) і керує драйвером через інтерфейс `IPanelPort`.

Драйвер носить **два капелюхи**:

- **`modesp::IActuatorDriver`** — «капелюх», завдяки якому DriverManager створює драйвер із біндінга й індексує за роллю (`panel`, модуль `panel`). `update()` лише логує фронт підключення; `set()`/`set_value()` фактично не використовуються — живлення/яскравість/текст веде модуль `panel` через `IPanelPort`.
- **`modesp::panel::IPanelPort`** — модуль `panel` резолвить той САМИЙ об'єкт за роллю (`find_actuator(role)->as_panel()` — capability-cast без RTTI) і подає контент (живлення / яскравість / текст) крізь нього.

Драйвер їде на спільному BLE-хості [`modesp_ble`](../../03-framework-reference/components/modesp_ble.md) (роль **CENTRAL**). Підключення до панелі ставить на паузу пасивний скан observer'а, потім відновлює його. Модулі ніколи не торкаються BLE напряму — I/O робить драйвер, модуль читає/пише SharedState.

ВИМАГАЄ: `modesp_ble` (CENTRAL), `modesp_hal`.

## Залізо (board.json)

Панель оголошується у `ble_devices` через **`name`** — це ADV-NAME, до якого драйвер під'єднується (а не MAC, як у пасивних сенсорів):

```json
"ble_devices": [
  { "id": "led_panel", "name": "LED_BLE_E6C5EBE2" }
]
```

| Поле | Опис |
|---|---|
| `id` | Локальний ідентифікатор пристрою, на нього посилається binding. |
| `name` | ADV-NAME панелі для підключення. Драйвер сканує префікс **`LED_BLE`** (`connect_name_prefix` у маніфесті). |

## Прив'язки (bindings.json)

```json
{"hardware": "led_panel", "driver": "ble_led_panel", "role": "panel", "module": "equipment"}
```

| Поле | Значення |
|---|---|
| `hardware` | `id` пристрою з `ble_devices`. |
| `driver` | `ble_led_panel`. |
| `role` | `panel`. |
| `module` | `equipment`. |

## Control-plane

Драйвер віддає свій `ConnectProfile` (префікс ADV-NAME + write/notify UUID) загальному central-link'у `modesp_ble`, який далі веде машину станів connect/discover/READY:

```
скан ADV-NAME "LED_BLE" ──▶ connect ──▶ discover
   write char fa02 (write-хендл) + notify char fa03 ──▶ READY
```

- **write** характеристика `fa02` (прив'язана як write-хендл), **notify** характеристика `fa03` (підписана).
- `update()` драйвера лише **логує** фронт підключення; `set()` (живлення) — no-op: живленням володіє модуль.
- Живлення (ON/OFF) та яскравість (0..100 %) **кодує сам драйвер** (`set_power` / `set_brightness`), а *викликає* їх модуль [`panel`](../modules/panel.md) через `IPanelPort`. Модуль — єдиний писар контенту і повторно застосовує power+brightness на кожному (пере)підключенні (скидання sentinel), тож налаштування переживають реконект, без гонки на фронті.

## Команди (control-байти)

Байти незмінні — кодує і надсилає їх **драйвер** (`set_power` / `set_brightness`), які модуль `panel` викликає через `IPanelPort`:

| Дія | Байти |
|---|---|
| Power ON | `05 00 07 01 01` |
| Power OFF | `05 00 07 01 00` |
| Яскравість | `05 00 04 80 <pct>` (`<pct>` = 0..100) |

## Розподіл відповідальності: драйвер vs модуль

Драйвер володіє **всім форматом дротового протоколу** (UUID'и, control-байти, кодер тексту, шрифт, render-задача) і реалізує **`IPanelPort`**. Модуль [`panel`](../modules/panel.md) володіє **лише контентом** — що показувати (текст, іконки, кольори, ефект) — і подає його через `IPanelPort`; він повністю BLE-агностичний. Два seam'и, що зустрічаються в драйвері:

```cpp
// до транспорту (modesp::ble, central_link.h) — драйвер реєструє профіль і пише крізь лінк:
struct ConnectProfile {
    const char*       name_prefix;   // префікс ADV-NAME ("LED_BLE")
    const ble_uuid_t* write_uuid;    // fa02 — write-хендл
    const ble_uuid_t* notify_uuid;   // fa03 — підписка
    CentralNotifyCb   on_notify;     // приймач notify (тут nullptr)
    void*             ctx;
};
ICentralLink* register_connect_profile(const ConnectProfile&);  // виклик із фабрики
class ICentralLink {
    bool connected() const;
    bool write(const uint8_t* data, uint16_t len, bool with_response);
    bool write_frame(bool (*body)(ICentralLink*, void*), void* arg);  // атомарний багаточанковий кадр
};

// до модуля (modesp::panel::IPanelPort) — модуль викликає, драйвер кодує байти:
bool connected() const;
void set_power(bool on);                       // кодує 05 00 07 01 <on>
void set_brightness(int pct);                  // кодує 05 00 04 80 <pct>
void show_text(const char* s, uint8_t r, uint8_t g, uint8_t b,
               uint8_t anim, uint8_t speed, uint8_t rainbow);  // enqueue → render-задача
```

У фабриці драйвер реєструє `ConnectProfile` (через `register_connect_profile`) і повертається як звичайний актуатор; модуль `panel` резолвить той самий об'єкт за роллю (`find_actuator(role)->as_panel()`). Модуль читає `panel.power` (bool), `panel.brightness` (int %), `panel.anim` (int 0..7 ефект) і `panel.rotate` (bool) з SharedState (їх задає веб-вкладка «iPixel» / MQTT) і викликає `set_power` / `set_brightness` / `show_text` на `IPanelPort` — жодного control-байта сам не кодує і BLE не торкається. Таке розділення дає єдиного писаря контенту без гонки на фронті підключення, а драйвер тримає фізичний BLE-лінк живим незалежно від того, що вирішує показати модуль.

## Опційність (Kconfig)

CENTRAL-роль (підключення до панелі) вмикається у menu **«ModESP BLE»**:

```
CONFIG_MODESP_BLE_ENABLE     (master — спільний BLE-хост)
CONFIG_MODESP_BLE_CENTRAL    (connect до панелі)
```

Без `CONFIG_MODESP_BLE_CENTRAL` central-connect шлях (`central_link.h`) недоступний — драйверу немає де зареєструвати профіль і взяти лінк. Сам BLE-хост (`modesp_ble`) лінкується у `main` лише при `CONFIG_MODESP_BLE_ENABLE`.

## Що далі

- **[modules/panel.md](../modules/panel.md)** — модуль контенту панелі (годинник/темп/вологість, іконки, кольори, нативна анімація).
- **[components/modesp_ble.md](../components/modesp_ble.md)** — спільний BLE-хост (OBSERVER / CENTRAL / PERIPHERAL).
- **[`docs/ble/panel_protocol.md`](../../../../docs/ble/panel_protocol.md)** — байтовий протокол керування панеллю.

## Джерела

- [`drivers/ble_led_panel/manifest.json`](../../../../drivers/ble_led_panel/manifest.json)
- [`drivers/ble_led_panel/include/ble_led_panel_driver.h`](../../../../drivers/ble_led_panel/include/ble_led_panel_driver.h)
- [`drivers/ble_led_panel/src/ble_led_panel_driver.cpp`](../../../../drivers/ble_led_panel/src/ble_led_panel_driver.cpp)
