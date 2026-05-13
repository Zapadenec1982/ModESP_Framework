# ModESP v4 -- Svelte WebUI

## Stack

- **Svelte 4** (4.2.18) + **Rollup 3** bundler
- Deployed як gzipped bundle на LittleFS ESP32
- Bundle size: **63KB JS gz + 13KB CSS gz** (76KB total)
- Target: ESP32 HTTP server (port 80), SPA shell в `data/www/index.html`
- Zero runtime dependencies -- тільки devDependencies (svelte, rollup, terser, etc.)

## Архiтектура

```
webui/
├── src/
│   ├── main.js                  # Entry point (mount App)
│   ├── App.svelte               # Root: load ui.json, init WS, route pages
│   ├── stores/                  # Svelte stores (reactive state)
│   │   ├── state.js             # SharedState mirror (WebSocket)
│   │   ├── ui.js                # UI schema from GET /api/ui
│   │   ├── theme.js             # Light/Dark toggle
│   │   ├── i18n.js              # UA/EN переклади
│   │   ├── toast.js             # Toast повiдомлення (success/error/warn)
│   │   └── wifiForm.js          # WiFi form state (SSID, password)
│   ├── lib/                     # Утилiти
│   │   ├── api.js               # apiGet, apiPost, apiUpload
│   │   ├── websocket.js         # Auto-reconnecting WS client
│   │   ├── visibility.js        # isVisible() для visible_when
│   │   ├── downsample.js        # Min/Max bucket downsampling
│   │   ├── chart.js             # SVG math (catmullRomPath, buildSmoothSegments)
│   │   └── icons.js             # Inline Lucide SVG paths (13 iконок)
│   ├── components/              # Загальнi компоненти
│   │   ├── Layout.svelte        # Sidebar (desktop) + bottom tabs (mobile)
│   │   ├── Card.svelte          # Collapsible card wrapper
│   │   ├── GroupAccordion.svelte # Responsive accordion (desktop open / mobile collapsed)
│   │   ├── Clock.svelte         # Годинник на Dashboard (поточний час)
│   │   ├── Icon.svelte          # SVG icon renderer
│   │   ├── MiniChart.svelte     # Sparkline температури в Dashboard tiles
│   │   ├── Toast.svelte         # Toast повiдомлення (save/error feedback)
│   │   └── WidgetRenderer.svelte # Dispatch widget по типу
│   │   └── widgets/             # 21 widget компонентiв
│   └── pages/                   # 7 page компоненти
│       ├── Dashboard.svelte     # Bento-card overview (Premium Redesign R1)
│       ├── DynamicPage.svelte   # Рендер будь-якої ui.json page
│       └── BindingsEditor.svelte # Equipment bindings + OneWire scan
│           (+ bindings/: BindingCard, EquipmentStatus, OneWireDiscovery, OneWirePicker)
├── scripts/
│   └── deploy.js                # Gzip (level 9) + copy to data/www/
├── rollup.config.js             # IIFE output, terser, css-only
└── package.json                 # npm run build / deploy
```

## Stores

### state.js -- дзеркало SharedState

- `state` -- writable store, мiстить всi ключi SharedState як `{key: value}`
- `wsConnected` -- writable boolean, статус WebSocket з'єднання
- `initWebSocket()` -- створює auto-reconnecting WS client
- `setStateKey(key, value)` -- оптимiстичне оновлення (до пiдтвердження сервера)
- `stateKey(key)` -- derived store для одного ключа

**Потiк даних:**
1. `onMount` -> `apiGet('/api/state')` -> початковий стан
2. `initWebSocket()` -> WS `/ws` -> real-time оновлення через `state.update()`
3. Widgets змiнюють значення -> `apiPost('/api/settings')` -> сервер -> WS broadcast -> store

### ui.js -- UI schema

- `uiConfig` -- повна JSON schema з `GET /api/ui`
- `pages` -- derived store, автоматично перекладається при змiнi мови
- `stateMeta` -- metadata для валiдацiї (min, max, step)
- `deviceName` -- назва пристрою для `document.title`
- `loadUiConfig()` -- async завантаження при старті

### theme.js -- тема оформлення

- `theme` -- writable (`'dark'` | `'light'`)
- Iнiцiалiзацiя: `localStorage` -> `prefers-color-scheme` -> `'dark'` (default)
- Persist: `localStorage.setItem('modesp-theme', value)`
- Застосування: `document.documentElement.setAttribute('data-theme', v)`
- `toggleTheme()` -- перемикання dark/light
- CSS custom properties в `App.svelte`: `--bg`, `--card`, `--fg`, `--accent`, etc.

### toast.js -- toast повiдомлення

- `toasts` -- writable store, масив `{id, msg, type}` повiдомлень
- `toastSuccess(msg)` -- зелений toast (3с)
- `toastError(msg)` -- червоний toast (5с)
- `toastWarn(msg)` -- жовтий toast (4с)
- Auto-remove по таймеру (setTimeout → filter)
- Компонент `Toast.svelte` рендерить стек повiдомлень

### wifiForm.js -- стан WiFi форми

- `wifiSsid` -- writable, поточний SSID
- `wifiPassword` -- writable, пароль
- Використовується WifiSave та WifiScan widgets

### i18n.js -- iнтернацiоналiзацiя

- **2 рiвнi перекладу:**
  1. `uk`/`en` словники (~128 ключiв) -- UI-елементи (кнопки, статуси, повiдомлення)
  2. `uiEn` (~130 ключiв) -- переклад ui.json текстiв (сторiнки, картки, описи, options)
- `language` -- writable (`'uk'` | `'en'`)
- `t` -- derived store: `$t['app.loading']` -> локалiзований текст
- `uiEn` -- map UA->EN для текстiв з маніфестiв
- Iнiцiалiзацiя: `localStorage` -> `navigator.language` (uk/ru -> uk, iнше -> en)
- `toggleLanguage()` -- перемикання UK/EN

## Widget System

`WidgetRenderer.svelte` отримує `widget` config та `value`, дiспатчить на компонент по `widget.widget` типу.

### 21 widget типiв

| Widget | Тип | Опис |
|--------|-----|------|
| `ValueWidget` | `value` | Readonly значення (температура, час, etc.) |
| `SliderWidget` | `slider` | Повзунок з min/max/step |
| `NumberInput` | `number_input` | Числове поле вводу |
| `IndicatorWidget` | `indicator` | Кольоровий iндикатор стану (on/off, alarm) |
| `StatusText` | `status_text` | Текстовий статус з кольоровим фоном |
| `ToggleWidget` | `toggle` | Перемикач boolean |
| `SelectWidget` | `select` | Dropdown з options (per-option disabled) |
| `TextInput` | `text_input` | Текстове поле вводу |
| `PasswordInput` | `password_input` | Поле пароля з show/hide |
| `ButtonWidget` | `button` | Кнопка з confirm дiалогом |
| `ChartWidget` | `chart` | SVG графiк температури (multi-channel) |
| `FirmwareUpload` | `firmware_upload` | OTA upload з progress bar |
| `FileUploadWidget` | `file_upload` | Загальний file upload |
| `WifiSave` | `wifi_save` | Збереження WiFi STA credentials |
| `WifiScan` | `wifi_scan` | Сканування WiFi мереж |
| `ApSave` | `ap_save` | Збереження WiFi AP налаштувань |
| `MqttSave` | `mqtt_save` | Збереження MQTT config |
| `TimeSave` | `time_save` | Збереження часу/NTP |
| `DatetimeInput` | `datetime_input` | Поле вводу дати/часу |
| `TimezoneSelect` | `timezone_select` | Вибiр часового поясу |
| `DefrostToggle` | `defrost_toggle` | Ручний запуск/зупинка розморозки |

## Pages

### Dashboard (`Dashboard.svelte`)

Bento-card layout (Premium Redesign R1):
- **Main temp-card**: велика температура з gradient + heroBreath glow animation, setpoint slider,
  hero-badges (COOLING / DEFROST / NIGHT), відображає `thermostat.display_temp`
- **Metrics row**: evap_temp та cond_temp tiles з кольоровим border-left (frost/warn)
- **Protection summary**: shield icon + red/green status badge + alarm code
- **Equipment grid**: 4-cell 2-column, color-coded badges (cyan=compressor, orange=defrost,
  frost=evap_fan, green=cond_fan), progress bar animations

### DynamicPage (`DynamicPage.svelte`)

- Рендерить будь-яку сторiнку з `ui.json` по `pageId`
- Cards з `visible_when` перевiркою через `isVisible()`
- Widgets в кожнiй card через `WidgetRenderer`
- Пiдтримка collapse/expand анiмацiї для cards

### BindingsEditor (`BindingsEditor.svelte`)

- Спецiальна сторiнка для Equipment bindings (не з ui.json)
- Роздiли: sensors, actuators
- OneWire Discovery: сканування шини, список пристроїв, призначення ролей
- Save -> Restart -> `equipment.has_*` state keys оновлюються

### Layout (`Layout.svelte`)

- Desktop: sidebar з навiгацiєю + header з статусом
- Mobile: bottom tabs навiгацiя
- Header: device name, WS connection indicator, theme toggle, language toggle
- Alarm banner при `protection.alarm_active === true`
- Сторiнки генеруються з `$pages` (ui.json) + hardcoded bindings/dashboard

## Runtime Visibility

### visible_when (cards та widgets)

Картки та вiджети можуть мати умову `visible_when` в ui.json:

```json
{"key": "defrost.type", "eq": 2}
{"key": "thermostat.fan_mode", "neq": 0}
{"key": "thermostat.night_mode", "in": [1, 2, 3]}
```

`isVisible(vw, $state)` -- утилiта в `lib/visibility.js`:
- `eq` -- значення рiвне
- `neq` -- значення не рiвне
- `in` -- значення входить в масив
- Якщо `vw` вiдсутнiй -- елемент завжди видимий

### requires_state (per-option disabled)

Select widget options можуть мати `requires_state`:
```json
{"value": 2, "label": "Гарячий газ", "requires_state": "equipment.has_defrost_relay"}
```
Якщо `$state[requires_state]` === `false` -- option показується як disabled з hint.

## Theme System

CSS custom properties в `:root` та `:root[data-theme="light"]` в `App.svelte`:

| Property | Dark | Light |
|----------|------|-------|
| `--bg` | `#0f172a` | `#f8fafc` |
| `--card` | `#1e293b` | `#ffffff` |
| `--fg` | `#f1f5f9` | `#0f172a` |
| `--fg-muted` | `#94a3b8` | `#64748b` |
| `--accent` | `#3b82f6` | `#2563eb` |
| `--success` | `#22c55e` | `#16a34a` |
| `--warning` | `#f59e0b` | `#d97706` |
| `--error` | `#ef4444` | `#dc2626` |

Body transition: `background-color 0.3s, color 0.3s` для плавного перемикання.

## Анiмацiї

- **Page transitions:** `fly({x: 20})` in + `fade` out через Svelte `{#key}`
- **Loading:** `fade` in, error screen `scale({start: 0.95})`
- **Cards:** slide collapse/expand анiмацiя
- **Values:** flash при змiнi значення (CSS animation)
- **Staggered entrance:** cards з'являються послiдовно при завантаженнi сторiнки

## Lib утилiти

| Файл | Опис |
|------|------|
| `api.js` | `apiGet`, `apiPost`, `apiUpload` -- HTTP helpers з error handling |
| `websocket.js` | Auto-reconnecting WS client (exponential backoff 1s..30s) |
| `visibility.js` | `isVisible(vw, state)` -- перевiрка visible_when умов |
| `downsample.js` | Min/Max bucket downsampling для ChartWidget (max 720 точок) |
| `chart.js` | SVG math: `catmullRomPath()`, `buildSmoothSegments()`, `tempRange()`, `computeTimeLabels/TempLabels()` -- shared мiж ChartWidget та MiniChart |
| `icons.js` | Inline Lucide SVG paths (13 iконок: home, snowflake, wifi, flame, etc.) |

## Build та Deploy

```bash
# Розробка з hot-reload
cd webui && npm run dev

# Production build
cd webui && npm run build
# -> dist/bundle.js, dist/bundle.css

# Build + gzip + deploy до ESP32
cd webui && npm run deploy
# -> data/www/bundle.js.gz, data/www/bundle.css.gz, data/www/index.html
```

**Rollup config:**
- Input: `src/main.js`
- Output: IIFE format (`dist/bundle.js`)
- Plugins: svelte, css-only, node-resolve, commonjs, terser (production)
- Sourcemaps тiльки в dev mode

**Deploy script** (`scripts/deploy.js`):
- Gzip level 9 через Node.js `zlib` (кросплатформний)
- Копiює `bundle.js.gz`, `bundle.css.gz`, `index.html` в `data/www/`

**SPA shell** (`data/www/index.html`):
- Мiнiмальний HTML з `<link>` на `/bundle.css` та `<script>` на `/bundle.js`
- ESP32 HTTP server вiддає `.gz` файли з `Content-Encoding: gzip`

## Premium Redesign R1 (2026-03-07)

### GroupAccordion

Компонент `GroupAccordion.svelte` реалізує responsive accordion для груп налаштувань:

- **Desktop (≥768px):** відкрито за замовчуванням, collapsible = false
- **Mobile (<768px):** collapsed, summary chips показують поточні значення
- `collapsible` prop — контролює режим; `defaultOpen` — поведінка за замовчуванням
- Стан зберігається в `sessionStorage` / `localStorage` між навігацією
- Slide transition (Svelte built-in) + icon color `var(--accent)`

### Dashboard Bento-card Layout

Замість плоского tile layout — ієрархічний bento-card дизайн:

| Зона | Вміст | Стиль |
|------|-------|-------|
| Main temp-card | Температура + setpoint slider + COOLING/DEFROST/NIGHT badges | Gradient + heroBreath animation |
| Metrics row | evap_temp, cond_temp tiles | Кольоровий border-left |
| Protection summary | shield icon + status + alarm code | Red/green badge |
| Equipment grid | compressor, defrost, evap_fan, cond_fan | Color-coded 2×2 grid |

### System / Network Pages

- **System page:** wide status card вгорі, firmware/runtime у двох колонках
- **Network page:** WiFi + MQTT у власних картках з icons
- **Uptime:** ClockWidget форматує як HH:MM:SS замість сирих секунд

### Icons

- `lib/icons.js`: inline Lucide SVG paths (shield, flame, snowflake, wifi, thermometer та ін.)
- Protection page: shield icon (SVG inline)
- Equipment status: color-coded badges замість SVG icons (cyan/orange/frost/green)

### Статистика R1

- **41 Svelte файл** (7 pages + 34 components/widgets)
- **63KB JS gz + 13KB CSS gz** (76KB total; 43KB→63KB після R1)

## Changelog

- 2026-03-07 -- Premium Redesign R1: GroupAccordion responsive, bento-card Dashboard, System/Network
  pages restructure, uptime HH:MM:SS. Bundle 44KB→76KB (63KB JS + 13KB CSS gz).
- 2026-03-01 -- Оновлено requires_state приклад (defrost_relay), додано компоненти Toast/MiniChart/Clock.
  Додано stores toast.js, wifiForm.js. Додано lib/chart.js.
- 2026-02-25 -- Створено
