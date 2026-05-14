# Web UI — Svelte SPA architecture

> 📖 **Українською:** [documentation/uk/03-framework-reference/web-ui.md](../../uk/03-framework-reference/web-ui.md)

The WebUI is а **Svelte 4 single-page application** that runs entirely
in the browser and talks to the device through `/api/*` REST endpoints
and а WebSocket at `/ws`. It is **manifest-driven** — the framework's
build pipeline produces `ui.json`, served by the device at runtime, and
the SPA renders pages, cards, and widgets directly from that schema.
Adding a new state key or widget to a module manifest results in а new
UI element after rebuild — no JavaScript edits needed.

This page documents the SPA architecture for framework contributors and
module authors who need to understand how their manifest declarations
become UI. For the manifest schema reference see
[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md).

REQUIRES: Node.js 18+ for development. The pre-built bundle (`data/www/`)
ships in the data partition; rebuilding the WebUI is optional.

## Stack

| Layer | Choice | Notes |
|---|---|---|
| Framework | Svelte 4.2 | Compile-time reactivity, no virtual DOM, tiny runtime. |
| Bundler | Rollup 3 | `@rollup/plugin-{commonjs,node-resolve,terser}`, CSS-only plugin, livereload during dev. |
| State | Svelte stores | `writable` + `derived` from `svelte/store`. No Redux, no Vuex. |
| Routing | Internal | Page id is а variable in `App.svelte`; deep linking is not used. |
| i18n | Lazy-loaded JSON | 4 language packs (`uk` default, `en/de/pl` loaded on demand). |
| Build size | ~80 KB gzip | Single bundle with all widgets. |

## File layout

```
webui/
├── package.json            ← Rollup config + svelte deps
├── rollup.config.js
├── dev-server.js           ← local dev with /api proxy to device
├── scripts/deploy.js       ← gzip bundle → data/www/
├── public/                 ← static assets (icons, tokens.css)
└── src/
    ├── main.js             ← Svelte mount point
    ├── App.svelte          ← root component, page switch, login modal
    ├── pages/
    │   ├── Dashboard.svelte
    │   ├── DynamicPage.svelte      ← renders pages from ui.json
    │   ├── BindingsEditor.svelte
    │   └── bindings/                ← bindings sub-pages (OneWire discovery, etc.)
    ├── components/
    │   ├── Layout.svelte
    │   ├── Card.svelte
    │   ├── WidgetRenderer.svelte   ← widget type → component dispatch
    │   ├── widgets/                 ← 25 widget components
    │   ├── Icon.svelte
    │   ├── Clock.svelte
    │   ├── LoginModal.svelte
    │   └── Toast.svelte
    ├── stores/
    │   ├── state.js        ← SharedState mirror + WebSocket pump
    │   ├── ui.js           ← ui.json + translated pages
    │   ├── i18n.js         ← language selection + `t` helper
    │   ├── theme.js
    │   ├── toast.js
    │   ├── wifiForm.js
    │   └── mqttForm.js
    ├── lib/
    │   ├── api.js          ← apiGet/apiPost/apiUpload + HTTP Basic auth
    │   ├── websocket.js    ← auto-reconnect WS client
    │   ├── settings.js
    │   ├── visibility.js   ← visible_when evaluator
    │   ├── icons.js
    │   ├── chart.js        ← SVG line chart for DataLogger
    │   └── downsample.js
    └── i18n/
        ├── uk.js           ← default (built-in)
        ├── en.js
        ├── de.js
        └── pl.js
```

## Stores — the runtime state model

There are seven Svelte stores. They are the entire state shape of the
SPA. Components subscribe through `$store` reactivity.

### `state` — SharedState mirror

```js
import { state, stateKey, setStateKey } from './stores/state.js';

// Subscribe to all keys (rarely needed)
$: console.log($state['equipment.air_temp']);

// Subscribe to a single key (preferred)
const temp = stateKey('equipment.air_temp');
$: console.log($temp);

// Optimistic update (writes local value before server confirms)
setStateKey('thermo.setpoint', 22.5);
```

Implementation in `stores/state.js`:

```js
export const state = writable({});
export const wsConnected = writable(false);

export function initWebSocket() {
  wsClient = createWebSocket((data) => {
    if ('_ws_connected' in data) {
      wsConnected.set(data._ws_connected);
      return;
    }
    state.update(s => ({ ...s, ...data }));
  });
}
```

The store starts empty. `App.svelte` does а one-shot `GET /api/state`
for the initial snapshot, then `initWebSocket()` opens the WS that
streams delta updates. Every WS message is merged into `state` via
`state.update(s => ({ ...s, ...data }))`.

### `uiConfig` and `pages` — schema from the device

```js
import { uiConfig, pages, deviceName, navigateTo } from './stores/ui.js';
```

`loadUiConfig()` fetches `/api/ui` once at boot. The result is the
generated `ui.json` produced by `tools/generate_ui.py` from all module
manifests. Shape:

```json
{
  "device_name": "ModESP",
  "pages": [
    {
      "id": "thermostat",
      "title": "Thermostat",
      "icon": "thermometer",
      "cards": [
        {
          "title": "State",
          "widgets": [
            {"key": "thermo.temperature", "widget": "value"},
            {"key": "thermo.setpoint",    "widget": "slider"}
          ]
        }
      ]
    }
  ],
  "state_meta": { /* per-key type, units, min/max */ }
}
```

`pages` is а `derived` store that applies the loaded language pack
to titles, labels, units, and option lists. Widgets get a translated
copy without losing references to their raw `key`.

### `language` and `langPack` — lazy i18n

`stores/i18n.js` exposes а writable `language` (one of `'uk'`, `'en'`,
`'de'`, `'pl'`). The default is `'uk'` because the generator emits
Ukrainian strings into `ui.json` directly. Other languages are loaded
on demand:

```js
language.subscribe(lang => {
  loadLanguagePack(lang);   // fetch /i18n/{lang}.json, set langPack
});
```

Language packs are served by the device from `data/www/i18n/<lang>.json`.
Each pack is а flat key-value dictionary. Translation lookup tries the
structured key first (`thermo.setpoint.description`), then falls back
to direct string-to-string mapping (legacy compatibility).

Adding а language = drop а new JSON pack into the data partition +
extend the language selector. No SPA rebuild needed.

### Other stores

- `theme.js` — dark/light theme toggle, persisted to localStorage.
- `toast.js` — toast notification queue with auto-dismiss.
- `wifiForm.js`, `mqttForm.js` — sticky form state for settings
  pages so the user doesn't lose typed credentials on navigation.

## WebSocket flow

`lib/websocket.js`:

```js
export function createWebSocket(onMessage) {
  let retry = 1000;
  function connect() {
    ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onopen = () => { retry = 1000; onMessage({ _ws_connected: true }); };
    ws.onmessage = (e) => onMessage(JSON.parse(e.data));
    ws.onclose = () => {
      onMessage({ _ws_connected: false });
      setTimeout(connect, retry);
      retry = Math.min(retry * 2, 30000);
    };
  }
  connect();
}
```

- **Auto-reconnect** with exponential backoff: 1 s → 2 s → 4 s → ... →
  30 s cap.
- **Meta-events** `{ "_ws_connected": true/false }` flow through the
  same callback as data, so subscribers can show а disconnected
  indicator without а separate channel.
- **No outbound traffic** by default — writes go through `apiPost` to
  `/api/settings`. This keeps the WS path one-way and the server-side
  buffer small.

The server side is documented in
[components/modesp_net.md](components/modesp_net.md#wsservice--websocket-state-broadcast).

## HTTP API client (`lib/api.js`)

Four exports: `apiGet`, `apiPost`, `apiUpload`, `needsLogin`.

### Authentication

The device uses HTTP Basic Auth (default `admin/modesp`). The SPA
keeps credentials in `sessionStorage` under the key `modesp_auth`:

```js
export function setAuth(user, pass) {
  const creds = btoa(user + ':' + pass);
  sessionStorage.setItem(AUTH_KEY, creds);
}
```

`apiPost` injects `Authorization: Basic <creds>` on every request.
`apiGet` does not, because GETs on the device side are public for the
state surface (only writes require auth in the default config).

A 401 response sets the `needsLogin` writable store; the root `App.svelte`
component watches it and shows the `LoginModal`. After successful login
the modal calls `setAuth` and the failed request is not retried — the
user simply repeats their action.

### Upload

`apiUpload(url, file, onProgress)` is а thin `XMLHttpRequest` wrapper
because `fetch` doesn't expose upload progress. Used by firmware OTA
and certificate uploads.

## Page model

```
App.svelte
├── Layout.svelte                ← header, drawer, page switch
│   └── currentPage = 'dashboard' | 'bindings' | <page-id-from-ui.json>
└── (rendered route)
    ├── Dashboard.svelte         ← fixed first page, summary cards
    ├── BindingsEditor.svelte    ← fixed config page
    └── DynamicPage.svelte       ← all manifest-defined pages
```

`DynamicPage` looks up the requested page id in the `pages` store and
walks its cards/widgets, delegating to `WidgetRenderer` for each widget.

### `WidgetRenderer` — the widget dispatch

```svelte
<script>
  export let widget;
  // ... maps widget.widget string → Svelte component ...
</script>
{#if widget.widget === 'slider'}
  <SliderWidget {widget} />
{:else if widget.widget === 'value'}
  <ValueWidget {widget} />
{:else if ...}
{/if}
```

The mapping is а switch over `widget.widget`. All widget components
share the same prop interface: they receive the widget definition
object as `widget` and look up their value(s) through `stateKey`.

## Widget catalog

25 widget components in `src/components/widgets/`. They fall into four
groups:

**Display:** `Value`, `Indicator`, `StatusText`, `Chart`, `MiniChart`.
Read-only — visualise а single state key.

**Input:** `Slider`, `NumberInput`, `Toggle`, `Select`, `TextInput`,
`PasswordInput`, `DatetimeInput`. Two-way bound — `POST /api/settings`
on commit.

**Action:** `Button`, `ActionsGrid`. Trigger а specific HTTP endpoint
without bound state.

**Specialised:** `WifiScan`, `WifiSave`, `ApSave`, `MqttSave`, `CloudSave`,
`TimeSave`, `TimezoneSelect`, `AuthSave`, `FirmwareUpload`, `FileUpload`,
`CertUpload`, `DefrostToggle`. Each one bundles а small form + а
dedicated endpoint, so the WidgetRenderer can drop them into а
generic card from а simple manifest entry without the manifest needing
to describe the full form layout.

See [02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)
for which type goes with which state type, and which props each accepts.

## `visible_when` evaluation

Manifest entries can carry а `visible_when` expression that hides the
card or widget unless the condition holds:

```json
{
  "title": "Defrost",
  "visible_when": {"thermo.defrost_supported": true},
  "widgets": [...]
}
```

`lib/visibility.js` evaluates these expressions client-side against the
`$state` store. Supported operators: equality, `not`, `and`, `or`, plus
shorthand for list-of-allowed-values (`{"key": ["a", "b"]}` matches if
the value is `"a"` or `"b"`). The grammar is intentionally limited —
complex predicates belong in С++ modules, not in manifest.

## Build and dev workflow

```bash
cd webui

# install deps
npm install

# dev server with hot reload (proxies /api/* and /ws to ESP32)
npm run dev          # ws://192.168.1.85/ws by default, override via env

# production bundle
npm run build        # outputs bundle.js + bundle.css

# deploy: gzip and copy to data/www/, ready for `idf.py flash_data`
npm run deploy
```

The pre-built bundle is checked into git (`data/www/`). Most module
authors never need to rebuild the WebUI — adding а new state key or
widget through а manifest works against the existing bundle.

You only rebuild the WebUI when you change Svelte components themselves
(а new widget type, а tweak to layout/theme, а bug fix in `lib/`).

## Performance characteristics

| Metric | Value |
|---|---|
| Bundle size | ~80 KB gzip (single chunk) |
| Initial paint | <1 s on а typical home router |
| Idle WS traffic | <1 KB/s steady-state (delta-only) |
| Memory | ~5-8 MB JS heap in а typical session |
| Mobile-friendly | Yes — responsive layout, touch-sized inputs |

The SPA is intentionally а single bundle. Code-splitting would add
HTTP round-trips that hurt cold start on the device's modest HTTP
server, and the current bundle is small enough that the trade-off
doesn't pay off.

## Common pitfalls

**State not updating after а write:** the SPA does optimistic updates
via `setStateKey`. If the server rejects (validation error), you'll
see your value briefly then it reverts on the next WS delta. Check the
toast for the error message.

**Page is empty after а manifest change:** the SPA caches `ui.json`
in memory but reloads on every page refresh. Hard refresh (Ctrl+Shift+R)
forces а re-fetch. If the new page still doesn't appear, check the
device's `GET /api/ui` directly — the generator may have failed silently
on rebuild.

**Language pack missing strings:** `tr()` falls back to the original UK
string when а translation key is missing. Look at the displayed text —
if it's still Ukrainian after switching language, that string is just
missing from the active language pack. Add it to `data/www/i18n/<lang>.json`
and `flash_data`.

**WS shows disconnected after а while:** check the device's
`/api/log` — if it's freezing the main task, the WS broadcast loop
falls behind. The SPA reconnects automatically once the device is
responsive again.

**Auth modal appears after а 401:** the SPA forwards all 401 responses
through `needsLogin`. After successful login the user has to retry
their action manually — failed requests are not auto-retried, to keep
the auth flow predictable.

## Why Svelte, not React/Vue

Three reasons. **Bundle size:** Svelte has no runtime virtual DOM; the
compiled output is direct DOM manipulation, so a full SPA with 25
widgets fits in ~80 KB gzip — manageable for an embedded HTTP server.
**Reactivity model:** `$store` is straightforward to teach to С++
developers who are the primary contributors. **Zero ecosystem
churn:** Svelte 4 has been stable for а year and the project doesn't
need any of the React-ecosystem features it lacks.

The trade-off is а smaller component library compared to React. The
framework writes its own widgets from scratch, which matches the
manifest-driven philosophy anyway — the catalog is what the framework
needs, no more.

## Next steps

- **[02-module-author-guide/ui-widgets.md](../02-module-author-guide/ui-widgets.md)** —
  what to declare in а manifest to get а widget rendered.
- **[components/modesp_net.md](components/modesp_net.md)** — server side
  of WebSocket + HTTP API.
- **[05-tools/generate_ui.md](../05-tools/generate_ui.md)** — how the
  `ui.json` schema gets generated from manifests.

## Source

- [`webui/src/App.svelte`](../../../webui/src/App.svelte) — root.
- [`webui/src/stores/`](../../../webui/src/stores/) — all stores.
- [`webui/src/lib/`](../../../webui/src/lib/) — API, WebSocket,
  visibility, chart utilities.
- [`webui/src/components/widgets/`](../../../webui/src/components/widgets/) —
  25 widget components.
- [`webui/package.json`](../../../webui/package.json) — Rollup config.
