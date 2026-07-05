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
- **WebUI access:** chart widget queries `/api/log?hours=N` (streaming chunked JSON).

## State keys

| Key | Type | Notes |
|---|---|---|
| `datalogger.enabled` | bool | Master enable. Default true. |
| `datalogger.retention_hours` | int | 12-168, default 48. Persisted. |
| `datalogger.sample_interval` | int | 30-300 s, default 60. Persisted. |
| `datalogger.records_count` | int | Total channel records у buffer. |
| `datalogger.events_count` | int | Total event records. |
| `datalogger.flash_used` | int | Bytes consumed (KB). |

Per-channel enable toggles are NOT keys of this module. Each channel in
`loggable.channels` may declare its own `enable_key` (a state key on the
owning module); the generator emits it as `LOG_CHANNELS[].enable_key`. A
channel with no `enable_key` is always on. The channel's `default` flag only
seeds the starting value of that `enable_key`. An optional `requires`
(→ `requires_key`) additionally gates the channel on hardware presence.

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

LittleFS partition, directory `/data/log/`:

```
/data/log/
├── temp.bin      active temperature file (append-only)
├── temp.old      rotated previous temperature file
├── events.bin    active events file (append-only)
└── events.old    rotated previous events file
```

Rotation is single-stage: when the active file exceeds its limit (temperature:
`retention_hours` × records/hour × 16 bytes; events: a hard 16 KB), it is
renamed to `.old` (the previous `.old` is dropped).

Compact binary encoding:
- Temperature record (`TempRecord`, 16 bytes): 4 timestamp + 6 channels × int16 (value ×10; `TEMP_NO_DATA` = INT16_MIN when the channel isn't logged). ALL channels live in one record, not one record per channel.
- Event record (`EventRecord`, 8 bytes): 4 timestamp + 1 event_type + 3 padding.

48-hour buffer at 60 s sampling = ~2880 records (one record carries all
channels) = ~46 KB. Fits comfortably.

## HTTP API

| Endpoint | Purpose |
|---|---|
| `GET /api/log?hours=N` | Streaming chunked JSON of all active channels + events, filtered to the last `hours` hours (`hours<=0` = everything). |
| `GET /api/log/summary` | Compact counters: `{"hours","temp_count","event_count","flash_kb","channels"}`. |

The module self-registers as the history source (`log_source::set(this)` in
`on_init`); `HttpService` reads the slot — no `set_datalogger()` in
`main.cpp`. Settings (interval, retention, enable) are driven through the
ordinary state endpoint from the manifest; there are no separate log routes.

`/api/log` format (JSON v3):

```json
{"channels":["air","evap","setpoint"],
 "temp":[[1700000000,225,180,220], ...],
 "events":[[1700000000,10], ...]}
```

`channels` lists only channel ids that have at least one value; temperature
values are integers ×10 (`22.5°C` → `225`) or `null` when that record has no
value for the channel.

## Chart widget integration

UI manifest (as in `datalogger/manifest.json` itself):

```json
{
  "key": "datalogger.chart",
  "widget": "chart",
  "label": "Temperature chart",
  "data_source": "/api/log",
  "default_hours": 24
}
```

Widget queries `/api/log`, renders an SVG line chart for each active channel
in the response.

## Performance і memory

| Resource | Cost |
|---|---|
| In-RAM buffers (temp 16 × 16 B + events 32 × 8 B) | ~0.5 KB |
| LittleFS write batch | append of buffered records (every 10 minutes) |
| Per-tick CPU | < 0.5 ms typical |

Sample writes throttled — actual flash writes happen every 10 min
(`FLUSH_INTERVAL_MS`), plus a forced flush when the RAM buffer fills and on
`on_stop`. NVS-style debounce protects flash endurance.

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
    "http://192.168.1.85/api/log",
    params={"hours": 24},
    auth=("admin", "modesp"),
).json()
# data = {"channels": ["air", ...],
#         "temp": [[1700000000, 225, ...], ...],   # values ×10 or null
#         "events": [[1700000000, 10], ...]}
```

## Common pitfalls

**Event IDs not stable:** Don't renumber event IDs у manifests. Existing
LittleFS files encode the old IDs; renumbering causes wrong labels.

**High-rate sampling burns flash:** sample interval < 30 s isn't allowed
для а reason. Flash endurance ~100k erase cycles per sector. At 30 s
sampling, plausible firmware lifetime is decades. At 1 s, just months.

**Forgetting to enable per-channel toggles:** if a channel declares an
`enable_key`, that state key (on the owning module) controls whether it's
actually sampled; a channel with no `enable_key` is always on. If the channel
has a `requires`, it also needs the hardware present.

**Chart widget shows blank:** check the key is registered in the business
module's `loggable.channels` (which feeds `datalogger_channels.h`). If a
loggable section was added mid-development, datalogger may not have any
records yet. Wait 1-2 sample intervals.

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
