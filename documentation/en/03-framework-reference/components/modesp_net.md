# `modesp_net` — WiFi, HTTP, WebSocket

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_net.md](../../../uk/03-framework-reference/components/modesp_net.md)

`modesp_net` provides three modules що handle the network surface:
WiFiService (STA + AP fallback), HttpService (~30 REST endpoints + static
serving), і WsService (WebSocket real-time state broadcast). Together
они expose the entire framework to external clients — WebUI, MQTT
helpers, CLI tools.

Business modules don't interact із це layer directly; instead their
manifests declare UI / state keys, і this component renders them. Це
page documents what's exposed і how the pipeline fits together.

REQUIRES: `modesp_core`, `modesp_services`, `modesp_hal`,
`esp_wifi esp_http_server esp_netif esp_event nvs_flash`.

## Component layout

```
components/modesp_net/include/modesp/net/
├── wifi_service.h
├── http_service.h
└── ws_service.h
```

## `WiFiService` — connection management

```cpp
class WiFiService : public modesp::BaseModule {
public:
    WiFiService();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    // Diagnostic
    bool is_connected() const;
    const char* current_ssid() const;
    int8_t current_rssi() const;
    bool is_ap_mode() const;
};
```

### Connection logic

1. **Boot:** reads SSID/password з NVS (`wifi.ssid`, `wifi.password`).
2. **STA attempt:** tries to join configured SSID. If success → connected,
   acquire DHCP, register mDNS hostname.
3. **AP fallback:** if STA fails after ~30 s OR no credentials у NVS,
   opens an AP `ModESP-<MAC>` із known password (default `12345678`).
   Captive-portal-style — user joins, opens browser, navigates to
   `http://192.168.4.1/`, enters credentials.
4. **STA recovery:** STA disconnect events trigger reconnect із
   exponential backoff. After ~5 minutes of failure, опционально fall back
   to AP (Kconfig flag).

### State keys

| Key | Type | Notes |
|---|---|---|
| `wifi.connected` | bool | true якщо STA joined; false если AP-only або disconnected. |
| `wifi.ssid` | string | Current SSID (persisted). |
| `wifi.password` | string | (persisted, mqtt_subscribe disabled — never exposed). |
| `wifi.rssi` | int | Signal strength dBm. |
| `wifi.ip` | string | Current IP address. |
| `wifi.ap_mode` | bool | true якщо у AP fallback. |
| `wifi.last_connect_s` | int | Seconds since last successful connect. |

### HTTP endpoints (handled у HttpService)

| Method + path | Purpose |
|---|---|
| `GET /api/wifi/scan` | Probe nearby SSIDs; returns array of `{ssid, rssi, secured}`. |
| `POST /api/wifi` | Save new credentials і reconnect. |
| `GET /api/wifi/ap` | Current AP fallback config. |
| `POST /api/wifi/ap` | Update AP fallback (SSID, password). |

### mDNS

After successful STA join, hostname `modesp-<deviceid>.local` becomes
resolvable on local network. WebUI accessible via
`http://modesp-a1b2c3.local/` instead of hunting для IP.

## `HttpService` — REST API і static file serving

```cpp
class HttpService : public modesp::BaseModule {
public:
    HttpService();

    void set_state(SharedState* state);
    void set_config(ConfigService*);
    void set_modules(ModuleManager*);
    // ... more setters для dependencies ...
    void set_scenario_engine(modesp::scenario::Engine*);

    httpd_handle_t server() const;
};
```

Dependency injection happens у main.cpp (HTTP needs access to almost
every other service). Inits після всі dependencies INITIALISED у Phase 3
(LOW priority).

### REST endpoints

~30 endpoints total. Categorised:

**State і diagnostics:**
- `GET /api/state` — full SharedState snapshot as JSON.
- `GET /api/modules` — module list із states.
- `GET /api/board` — board.json contents.
- `GET /api/bindings` — bindings.json contents.
- `POST /api/bindings` — save new bindings (requires reload).
- `GET /api/ui` — generated UI schema.
- `GET /api/log` — recent log buffer.
- `GET /api/log/summary` — short summary.

**Settings (writable state):**
- `POST /api/settings` — single-shot writes (1-8 keys at once).

**Network:**
- `GET /api/wifi/scan`, `POST /api/wifi`, `GET/POST /api/wifi/ap`.
- `GET /api/mqtt`, `POST /api/mqtt` — broker config.
- `GET /api/time`, `POST /api/time` — manual time AND timezone.

**Hardware:**
- `GET /api/onewire/scan` — discover OneWire sensors на bound buses.
- `POST /api/drivers/<type>/scan` — driver-specific discovery.

**OTA:**
- `GET /api/ota` — current partition і version.
- `POST /api/ota` — start firmware upload (multipart).
- `POST /api/ota/confirm` — mark pending firmware stable.
- `POST /api/ota/rollback` — boot із previous partition.

**Backup і restore:**
- `GET /api/backup` — dump persistent state і config як JSON.
- `POST /api/restore` — apply backup.

**System:**
- `POST /api/restart` — reboot the device.
- `POST /api/factory-reset` — erase NVS і reboot.
- `GET /api/auth`, `POST /api/auth` — credentials change.

**Scenario:**
- `GET /api/scenario/list`, `GET /api/scenario/info?handle=N`
- `POST /api/scenario/load`, `start`, `pause`, `resume`, `abort`,
  `unload`.

### Authentication

HTTP Basic Auth. Default credentials: `admin` / `modesp` (stored у NVS
`auth` namespace). Change через `POST /api/auth` або WebUI System →
Auth.

Every API endpoint calls `check_auth(req)` first; missing/wrong
credentials → 401 Unauthorized.

Auth can be disabled via NVS flag (`auth.enabled = false`), useful у
development. Production: leave enabled.

### Static file serving

After all API endpoints, а wildcard handler serves files from `/data/www/`:

- `/` → `/data/www/index.html` (WebUI entry).
- `/bundle.js.gz` → gzipped Svelte bundle.
- `/bundle.css.gz` → gzipped styles.
- `/i18n/<lang>.json` → translation packs.

Wildcard handler must be registered LAST (after API endpoints). Otherwise
the `/*` pattern shadows specific routes.

## `WsService` — WebSocket state broadcast

```cpp
class WsService : public modesp::BaseModule {
public:
    WsService();

    void set_state(SharedState* state);
    void set_http_server(httpd_handle_t server);
};
```

WebSocket endpoint: `/api/ws`. WebUI's frontend connects here on load AND
maintains the connection для real-time state updates.

### Broadcast logic

Every ~500 ms, WsService:

1. Calls `state.for_each_changed_and_clear()`.
2. If changes ≤ 32 keys, sends а delta payload:
   ```json
   {"type": "delta", "values": {"key1": v1, "key2": v2, ...}}
   ```
3. If overflow (`needs_full_broadcast()`), sends full snapshot:
   ```json
   {"type": "snapshot", "values": {<all keys>}}
   ```
4. New clients що connect mid-session get а snapshot on join.

### Incoming messages

```json
{"type": "set", "key": "thermo.setpoint", "value": 23.5}
```

Equivalent to `POST /api/settings {"thermo.setpoint": 23.5}` але without
а full HTTP round-trip. Used by interactive widgets (sliders) для low
latency.

### Capacity

Max ~4 concurrent WebSocket clients (Kconfig). Each holds ~2 KB у
httpd's per-client buffer. Exceeds budget on busy devices.

## Wiring у main.cpp

```cpp
static modesp::WiFiService wifi_service;
static modesp::HttpService http_service;
static modesp::WsService   ws_service;

// Phase 2: register WiFi
app.modules().register_module(wifi_service);

// Phase 3: register HTTP + WS із dependency injection
http_service.set_state(&app.state());
http_service.set_config(&config_service);
http_service.set_modules(&app.modules());
http_service.set_wifi(&wifi_service);
http_service.set_persist(&persist_service);
http_service.set_hal(&hal);
http_service.set_datalogger(&datalogger);
http_service.set_scenario_engine(&scenario_engine);

ws_service.set_state(&app.state());

app.modules().register_module(http_service);
app.modules().register_module(ws_service);
app.modules().init_all(app.state());

// Step 9: connect WS handler to HTTP server, register wildcard last
if (http_service.server()) {
    ws_service.set_http_server(http_service.server());
    http_service.register_static_handler();   // MUST be last
}
```

The setter pattern is verbose but explicit — every dependency visible
у main.cpp. Avoids hidden global state.

## State keys (network category)

| Key | Notes |
|---|---|
| `wifi.connected` | STA connected. |
| `wifi.ssid` / `wifi.ip` / `wifi.rssi` | Connection details. |
| `wifi.ap_mode` | AP fallback active. |
| `http.requests_count` | Total HTTP requests served (debug). |
| `http.ws_clients` | Currently connected WS clients. |

## Performance і memory

| Service | RAM | Task |
|---|---|---|
| WiFiService | ~3 KB | runs on main + ESP-IDF WiFi tasks. |
| HttpService | ~8 KB (httpd buffer) | httpd has own task. |
| WsService | ~4 KB (frame buffers) | WS frames у httpd worker. |

Total ~15 KB allocated для network surface. Each WS client adds ~2 KB.

Max URI handlers: 64 (Kconfig `CONFIG_HTTPD_MAX_URI_HANDLERS`). Bumped
from default 48 для scenario API + WS coexistence.

## Common pitfalls

**WS doesn't connect:** check `wifi.connected = true` first. If WiFi OK
але WS fails, look для `httpd_register_uri_handler` failure у logs —
likely max URI handlers exceeded.

**Static handler shadowing API:** `register_static_handler()` MUST be
called LAST. Otherwise the `/*` wildcard matches everything including
`/api/*`.

**401 Unauthorized у browser:** clear browser auth cache, re-enter
credentials. Default `admin/modesp`; reset via factory-reset або direct
NVS edit.

**`/api/state` slow:** snapshotting takes ~30 ms with 96 entries. Don't
poll це endpoint frequently — use WS for real-time, REST for one-shots.

**WiFi reconnect storm у logs:** AP fallback enabled + STA SSID gone
results у repeated mode switching. Disable AP fallback for stable
deployments OR ensure а deterministic AP password.

## Next steps

- **[components/modesp_mqtt.md](modesp_mqtt.md)** — MQTT publish/subscribe
  layer on top of network.
- **[components/modesp_aws.md](modesp_aws.md)** — AWS IoT alternative
  cloud backend.
- **[02-module-author-guide/ui-widgets.md](../../02-module-author-guide/ui-widgets.md)**
  — what `/api/ui` exposes і how widgets render.
- **[02-module-author-guide/debugging.md](../../02-module-author-guide/debugging.md)**
  — using `/api/state`, `/api/modules`, `/api/log` to debug.

## Source

- [`components/modesp_net/include/modesp/net/`](../../../../components/modesp_net/include/modesp/net/)
  — public headers.
- [`components/modesp_net/src/`](../../../../components/modesp_net/src/)
  — implementations. `http_service.cpp` is the largest file (~2000 LOC)
  housing all REST handlers.
