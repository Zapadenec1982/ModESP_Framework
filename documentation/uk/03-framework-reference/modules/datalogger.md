# DataLogger module

> 📖 **In English:** [documentation/en/03-framework-reference/modules/datalogger.md](../../../en/03-framework-reference/modules/datalogger.md)

`datalogger` — general-purpose module що logs SharedState values over
time AND records discrete events. Data зберігаються у LittleFS files з
configurable retention. WebUI використовує chart widget щоб plot recent
history; HTTP API exposes data для external consumers.

Manifest-driven — `loggable` секція вашого business module декларує які
keys — channels (continuous) AND які — events (edge-triggered).
DataLogger discovers їх при build time, writes records на власному
schedule.

REQUIRES: `modesp_core`, `modesp_services`, LittleFS partition.

## Що він робить

- **Channel logging:** samples specified state keys з user-configurable
  interval (default 60 с, range 30-300). Stores `(timestamp, value)`
  records compactly у LittleFS.
- **Event logging:** captures rising/falling/both edges на boolean keys.
  Stores `(timestamp, event_id)` records.
- **Retention:** drops oldest records once total exceeds
  `datalogger.retention_hours` (default 48, range 12-168).
- **WebUI access:** chart widget queries
  `/api/datalogger/series?key=X&window=1h`.
- **CSV export:** `GET /api/datalogger/export?key=X&from=...&to=...`.

## State keys

| Key | Type | Notes |
|---|---|---|
| `datalogger.enabled` | bool | Master enable. Default true. |
| `datalogger.retention_hours` | int | 12-168, default 48. Persisted. |
| `datalogger.sample_interval` | int | 30-300 с, default 60. Persisted. |
| `datalogger.records_count` | int | Total channel records у buffer. |
| `datalogger.events_count` | int | Total event records. |
| `datalogger.flash_used` | int | Bytes consumed (KB). |
| `datalogger.log_<channel>` | bool | Per-channel enable flag. |

Per-channel enables (`log_evap`, `log_cond`, `log_setpoint`, тощо)
auto-generated з registered channels' `default` flag — якщо manifest
сказав `"default": true`, the corresponding `log_<channel>` defaults
true.

## Як модулі declare channels і events

Manifest business module включає:

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

Generator merges усі modules' `loggable` секції і emits
`datalogger_channels.h` і `datalogger_events.h` з consolidated lists.
DataLogger reads це при compile time AND його `on_update` samples /
captures accordingly.

Див. [02-module-author-guide/manifest.md](../../02-module-author-guide/manifest.md#section-loggable-service-modules).

## Storage format

LittleFS partition (default 960 KB):

```
/data/
└── datalogger/
    ├── channels.bin          binary records (timestamp + channel_id + value)
    ├── events.bin            binary records (timestamp + event_id + flags)
    └── ... (rotated files для retention pruning)
```

Compact binary encoding:
- Channel record: 12 bytes (4 timestamp + 1 channel_id + 4 float value + 3 padding).
- Event record: 8 bytes (4 timestamp + 2 event_id + 1 edge_type + 1 padding).

48-hour buffer at 60 с sampling = ~3000 records per channel. 6 channels
= ~18000 records = ~216 KB. Fits comfortably.

## HTTP API

| Endpoint | Purpose |
|---|---|
| `GET /api/datalogger/series?key=X&window=1h` | JSON: `[{t, v}, ...]` filtered by time window. |
| `GET /api/datalogger/events?from=T&to=T` | JSON event list. |
| `GET /api/datalogger/export?key=X&from=T&to=T` | CSV download. |
| `POST /api/datalogger/clear` | Wipe всі logs. |
| `POST /api/datalogger/settings` | Update enabled flags / intervals. |

Window strings: `10m` / `1h` / `24h` / `7d` / `30d` (limited by
retention).

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

Widget queries series endpoint, рендерить SVG line chart. Multi-series
ще не supported у MVP (Stage 1.5).

## Performance і memory

| Resource | Cost |
|---|---|
| In-RAM ring buffer | ~8 KB (most recent records cached) |
| LittleFS write batch | ~1 KB per flush (кожні 5 хвилин) |
| Per-tick CPU | < 0.5 мс типово |

Sample writes throttled — actual flash writes happen кожні ~5 хв
(configurable). NVS-style debounce protects flash endurance.

## Common patterns

### Adding new channel

У business module's manifest:

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

Після `idf.py build`, `datalogger_channels.h` includes new channel.
DataLogger samples його next reboot.

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

**Event IDs not stable:** Не renumber event IDs у маніфестах. Existing
LittleFS files encode old IDs; renumbering causes wrong labels.

**High-rate sampling burns flash:** sample interval < 30 с не allowed
для reason. Flash endurance ~100k erase cycles per sector. При 30 с
sampling, plausible firmware lifetime — decades. При 1 с — just months.

**Забутий enable per-channel toggles:** навіть якщо channel declared у
manifest, `datalogger.log_<channel>` flag controls чи actually sampled.
WebUI exposes toggles.

**Chart widget shows blank:** check key registered у
`datalogger.channels`. Якщо business module's loggable section був added
mid-development, datalogger may не мати any records yet. Wait 1-2 sample
intervals.

## Що далі

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
