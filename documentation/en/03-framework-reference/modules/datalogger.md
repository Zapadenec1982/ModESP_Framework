# DataLogger module

> 📖 **Українською:** [documentation/uk/03-framework-reference/modules/datalogger.md](../../../uk/03-framework-reference/modules/datalogger.md)

`datalogger` is а general-purpose module that logs SharedState values
over time AND records discrete events. Data is stored у LittleFS files
з configurable retention. WebUI uses а chart widget to plot recent
history; HTTP API exposes the data for external consumers.

It's manifest-driven — your business module's `loggable` section declares
which keys are channels (continuous) AND which are events (edge-triggered).
DataLogger discovers them at build time, writes records on its own
schedule.

REQUIRES: `modesp_core`, `modesp_services`, LittleFS partition.

## What it does

- **Channel logging:** samples specified state keys at а user-configurable
  interval (default 60 s, range 30-300). Stores `(timestamp, value)`
  records compactly у LittleFS.
- **Event logging:** captures rising/falling/both edges на boolean keys.
  Stores `(timestamp, event_id)` records.
- **Retention:** drops oldest records once total exceeds
  `datalogger.retention_hours` (default 48, range 12-168).
- **WebUI access:** chart widget queries `/api/datalogger/series?key=X&window=1h`.
- **CSV export:** `GET /api/datalogger/export?key=X&from=...&to=...`.

## State keys

| Key | Type | Notes |
|---|---|---|
| `datalogger.enabled` | bool | Master enable. Default true. |
| `datalogger.retention_hours` | int | 12-168, default 48. Persisted. |
| `datalogger.sample_interval` | int | 30-300 s, default 60. Persisted. |
| `datalogger.records_count` | int | Total channel records у buffer. |
| `datalogger.events_count` | int | Total event records. |
| `datalogger.flash_used` | int | Bytes consumed (KB). |
| `datalogger.log_<channel>` | bool | Per-channel enable flag. |

Per-channel enables (`log_evap`, `log_cond`, `log_setpoint`, etc.) are
auto-generated з registered channels' `default` flag — if the manifest
said `"default": true`, the corresponding `log_<channel>` defaults true.

## How modules declare channels і events

A business module's manifest includes:

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

The generator merges all modules' `loggable` sections і emits
`datalogger_channels.h` і `datalogger_events.h` із the consolidated lists.
DataLogger reads these at compile time AND its `on_update` samples /
captures accordingly.

See [02-module-author-guide/manifest.md](../../02-module-author-guide/manifest.md#section-loggable-service-modules).

## Storage format

LittleFS partition (default 960 KB):

```
/data/
└── datalogger/
    ├── channels.bin          binary records (timestamp + channel_id + value)
    ├── events.bin            binary records (timestamp + event_id + flags)
    └── ... (rotated files for retention pruning)
```

Compact binary encoding:
- Channel record: 12 bytes (4 timestamp + 1 channel_id + 4 float value + 3 padding).
- Event record: 8 bytes (4 timestamp + 2 event_id + 1 edge_type + 1 padding).

48-hour buffer at 60 s sampling = ~3000 records per channel. 6 channels
= ~18000 records = ~216 KB. Fits comfortably.

## HTTP API

| Endpoint | Purpose |
|---|---|
| `GET /api/datalogger/series?key=X&window=1h` | JSON: `[{t, v}, ...]` filtered by time window. |
| `GET /api/datalogger/events?from=T&to=T` | JSON event list. |
| `GET /api/datalogger/export?key=X&from=T&to=T` | CSV download. |
| `POST /api/datalogger/clear` | Wipe all logs. |
| `POST /api/datalogger/settings` | Update enabled flags / intervals. |

Window strings: `10m` / `1h` / `24h` / `7d` / `30d` (limited by retention).

## Chart widget integration

UI manifest:

```json
{
  "key": "equipment.air_temp",
  "widget": "chart",
  "window": "1h",
  "height": 200
}
```

Widget queries the series endpoint, renders SVG line chart. Multi-series
not yet supported у MVP (Stage 1.5).

## Performance і memory

| Resource | Cost |
|---|---|
| In-RAM ring buffer | ~8 KB (most recent records cached) |
| LittleFS write batch | ~1 KB per flush (every 5 minutes) |
| Per-tick CPU | < 0.5 ms typical |

Sample writes throttled — actual flash writes happen every ~5 min
(configurable). NVS-style debounce protects flash endurance.

## Common patterns

### Adding а new channel

In your business module's manifest:

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

After `idf.py build`, `datalogger_channels.h` includes the new channel.
DataLogger samples it next reboot.

### Querying programmatically

```python
import requests
data = requests.get(
    "http://192.168.1.85/api/datalogger/series",
    params={"key": "equipment.air_temp", "window": "24h"},
    auth=("admin", "modesp"),
).json()
# data = [{"t": 1700000000, "v": 22.5}, ...]
```

## Common pitfalls

**Event IDs not stable:** Don't renumber event IDs у manifests. Existing
LittleFS files encode the old IDs; renumbering causes wrong labels.

**High-rate sampling burns flash:** sample interval < 30 s isn't allowed
для а reason. Flash endurance ~100k erase cycles per sector. At 30 s
sampling, plausible firmware lifetime is decades. At 1 s, just months.

**Forgetting to enable per-channel toggles:** even if а channel is
declared у manifest, `datalogger.log_<channel>` flag controls whether
it's actually sampled. WebUI exposes the toggles.

**Chart widget shows blank:** check the key is registered у
`datalogger.channels`. If а business module's loggable section was added
mid-development, datalogger may not have any records yet. Wait 1-2
sample intervals.

## Next steps

- **[02-module-author-guide/manifest.md](../../02-module-author-guide/manifest.md#section-loggable-service-modules)** —
  manifest declaration syntax.
- **[02-module-author-guide/ui-widgets.md](../../02-module-author-guide/ui-widgets.md)** —
  chart widget reference.

## Source

- [`modules/datalogger/`](../../../../modules/datalogger/) — implementation.
- [`generated/datalogger_channels.h`](../../../../generated/datalogger_channels.h)
  — auto-generated channel list.
- [`generated/datalogger_events.h`](../../../../generated/datalogger_events.h)
  — auto-generated event list.
