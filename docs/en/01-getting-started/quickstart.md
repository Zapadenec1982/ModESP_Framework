# Quickstart

> 📖 **Українською:** [docs/uk/01-getting-started/quickstart.md](../../uk/01-getting-started/quickstart.md)

Goal: flash а ModESP-based firmware onto а real ESP32, see the WebUI live,
load and run the reference scenario `abs_test` через HTTP API — all у under
10 minutes.

This guide assumes you already have ESP-IDF v5.0+ installed. If not, see
[installation.md](installation.md) first.

## What you'll need

- An **ESP32-WROOM-32** dev board (other variants like S3 / C3 work but
  haven't been calibrated against the 65 KB heap budget).
- A **USB cable** (data, not just power).
- The IP address of your home Wi-Fi и SSID/password ready.
- ESP-IDF environment activated у your shell (`$IDF_PATH` set, tools у
  PATH, Python venv active).

## Step 1 — Build і flash

```bash
git clone https://github.com/Zapadenec1982/ModESP_Framework
cd ModESP_Framework

# Build (first run takes ~5 minutes; subsequent < 30 seconds incremental)
idf.py build

# Flash + monitor on COM port (Windows). Replace з your serial port.
idf.py -p COM15 flash monitor
```

On Linux/macOS the port is `/dev/ttyUSB0` or `/dev/ttyACM0`. Press `Ctrl+]`
to exit the monitor.

You should see boot logs ending з something like:

```
I (12345) main: Phase 3: Initializing HTTP + WebSocket...
I (12350) wifi_service: connected, IP=192.168.1.85
I (12355) http: HTTP server started on port 80
```

Note the IP address — you'll need it.

## Step 2 — Configure Wi-Fi (if not pre-flashed)

On the first boot the device starts а Wi-Fi AP named `ModESP-XXXXXX`
(MAC suffix). Connect to it (password `12345678` by default), then open
[http://192.168.4.1/](http://192.168.4.1/) and enter your home Wi-Fi
credentials. The device reboots and joins your network.

After it joins, watch the monitor log для the assigned IP.

## Step 3 — Open the WebUI

In а browser navigate to `http://<device-ip>/`. You should see the ModESP
landing page з system pages (Dashboard, Network, Firmware, System) і а
"Тест" page from the bundled `abs_test` recipe.

**Default credentials** for HTTP API і protected endpoints: `admin` /
`modesp`. Change them у the System page → Auth section.

## Step 4 — Run the reference scenario

`abs_test` is а minimal two-track scenario shipped with the firmware. Main
track cycles через `phase_a → phase_b → phase_c → $complete`; watcher
track waits for main to enter `phase_c` then completes. Use it to
verify the scenario engine is alive.

```bash
# Replace 192.168.1.85 з your device IP throughout.
# Credentials default to admin/modesp (HTTP Basic Auth).

# 1. Load the .modr from LittleFS
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/load \
     -H "Content-Type: application/json" \
     -d '{"path": "/data/scenarios/abs_test.modr"}'
# → {"handle": 1}

# 2. Start it
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/start \
     -H "Content-Type: application/json" \
     -d '{"handle": 1}'
# → {"ok": true}

# 3. Watch progress — mirror keys у /api/state are live-updated
curl -u admin:modesp http://192.168.1.85/api/state | python -m json.tool | grep abs_test
# {
#   "abs_test.scenario_state": "running",
#   "abs_test.scenario_elapsed_s": 3,
#   "abs_test.main_phase_name": "phase_b",
#   ...
# }

# 4. Wait ~7 seconds for completion, then check
curl -u admin:modesp "http://192.168.1.85/api/scenario/info?handle=1"
# → {"state":"completed", "elapsed_s":7, "tracks":[...]}

# 5. Unload to free the slot
curl -u admin:modesp -X POST http://192.168.1.85/api/scenario/unload \
     -H "Content-Type: application/json" \
     -d '{"handle": 1}'
```

The WebUI's **Тест** page shows the same state у real time via WebSocket
updates — switch to it during steps 2-4 і watch `main_phase_name` /
`watcher_phase_name` advance live.

## What just happened?

- **`abs_test.modr`** was compiled at build time by `tools/compile_scenario.py`
  from [`modules/abs_test/manifest.json`](../../../modules/abs_test/manifest.json)
  і bundled into the LittleFS image (`data/scenarios/`).
- **HTTP `/api/scenario/load`** invoked `Engine::load_path()` which reads
  the file, validates magic + CRC + action hashes, and parks it у а slot.
- **HTTP `/api/scenario/start`** invoked `Engine::start()` which initialises
  both tracks і transitions the scenario to `running`.
- **Engine tick (100 Hz)** stepped both tracks через their phase machines.
  Mirror keys were written every tick by `mirror::publish()` so the WebUI
  saw live state.
- **Track 2 (watcher)** waited for the condition
  `state_key_eq{key:"abs_test.main_phase_name", value:"phase_c"}` — а
  cross-track sync example using only SharedState as the rendezvous point.

## Next steps

- Read **[concepts.md](concepts.md)** для the four key ideas (manifest-driven,
  modules, scenarios, SharedState).
- Read **[Module Author Guide → overview.md](../02-module-author-guide/overview.md)**
  to start writing your own module.
- Read **[scenario-engine/00_overview.md](../03-framework-reference/scenario-engine/00_overview.md)**
  для а deeper look at the recipe runtime.

## Troubleshooting

**Device boots but Wi-Fi never connects:** check the monitor log для
`wifi_service: SSID="..." not found` or auth failures. Re-enter credentials
via the AP fallback (long press boot button to force AP mode).

**`load` returns 400 with `"error": "invalid_file"`:** the .modr probably
wasn't included у the LittleFS image. Verify із `idf.py build` output that
`data/scenarios/abs_test.modr` was packed (look для "Adding File:
scenarios\\abs_test.modr"). If missing, re-run `python tools/compile_scenario.py`
and rebuild.

**`load` returns 401 Unauthorized:** missing HTTP Basic Auth — add
`-u admin:modesp` to your curl command, or change credentials via the
System page → Auth section first.

**`start` returns 400 `"resource_contended"`:** another scenario is holding
the same resource. `abs_test` declares no resources so this shouldn't happen;
if it does, check `/api/scenario/list` and unload anything still loaded.

**Mirror keys stuck at `idle` after start:** scenario engine wasn't ticked.
Check the monitor для `Phase 2: Initializing WiFi + business modules...`
followed by registration messages including `scenario`. If not present,
`scenario_engine` wasn't registered у `main.cpp` (Phase 3 wiring missing —
file а bug).
