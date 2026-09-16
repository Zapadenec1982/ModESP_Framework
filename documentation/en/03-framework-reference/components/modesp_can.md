# `modesp_can` — CAN (TWAI) transport, ModESP-CAN v1

> 📖 **Українською:** [documentation/uk/03-framework-reference/components/modesp_can.md](../../../uk/03-framework-reference/components/modesp_can.md)

`modesp_can` is the framework's wired transport for joining several ModESP
nodes over classic CAN 2.0B. It is a Kconfig-gated component and the **single
owner of the chip's TWAI node** (the new `esp_driver_twai` node API, ESP-IDF
v6). On top of raw CAN it speaks **ModESP-CAN v1** — a lightweight,
manifest-generated frame protocol designed in
[ADR-004](../../../../docs/can/ADR-004-can-transport-p6.md). It lives in
`components/modesp_can` — not in `modules/` — and is registered in `main.cpp`
as a `BaseModule("can", HIGH)` service with dependency injection
(`set_hal(&hal)` + `set_state(&app.state())`) before `init_all`.

The component bridges **two internal planes** of ModESP:

| Plane | Direction out | Direction in |
|---|---|---|
| **SharedState** (retained values) | Keys marked `can_export` in manifests are broadcast as STATE frames — on-change plus a full refresh every 5 s. | STATE frames from other nodes land in `CanRemoteCache`; the [`can_remote`](../drivers/can_remote.md) bridge driver turns them into ordinary roles. |
| **Message bus** (transient events) | A module publishes `MsgRemoteEvent{node=0, ...}`; the service encodes it as an EVENT frame if the event is declared in `can_events.out`. | A received EVENT frame declared in `can_events.in` is re-published locally as `MsgRemoteEvent{node=<src>, ...}` from the main-loop task. |

Consumer modules never see CAN: a role fed by a remote node looks exactly like
a role fed by a local sensor. That is the whole point — the transport proves
the P0–P5 "role = capability, device carries identity" model end-to-end.

## Build & configuration (Kconfig)

Menu **"ModESP CAN"** (`components/modesp_can/Kconfig`):

| Symbol | Default | Meaning |
|---|---|---|
| `CONFIG_MODESP_CAN_ENABLE` | n | Master switch. Off → the component contributes **zero sources** (empty `SRCS`, the `modesp_ble` pattern), so non-CAN builds carry no CAN code or flash. |
| `CONFIG_MODESP_CAN_SELF_TEST` | n | Bench-only no-ACK loopback mode — see [Self-test](#self-test-config_modesp_can_self_test). Never enable on a production bus. |

`REQUIRES`: `modesp_core`, `modesp_hal`. `PRIV_REQUIRES`: `esp_driver_twai`,
`esp_timer`, `nvs_flash`. The generated `can_registry.h` is included from
`generated/` (same single-source-of-truth mechanism as `mqtt_topics.h` in
`modesp_mqtt`).

Two more things every CAN node needs:

- a `can_buses` entry in board.json (pins + bitrate, below);
- an **external CAN transceiver** (SN65HVD230, TJA1051T/3, ...) between the
  MCU pins and the bus. The ESP32's TWAI controller has no physical layer —
  there is no transceiver-less wiring option for a real bus.

Bridge drivers with `hardware_type: "can"` (e.g. `can_remote`) get an
automatic `depends on MODESP_CAN_ENABLE` in their generated driver toggle
(`HW_TYPE_KCONFIG_DEPS` in tools/generate_ui.py:3244), so they cannot be
compiled without the component.

## Board resource (`can_buses`)

board.json declares pins and bitrate; the HAL stores this as **config only**
(`CanBusResource`, like `uart_buses`) — the TWAI peripheral itself is created
and owned by the component:

```json
"can_buses": [
  {"id": "can_0", "tx": 21, "rx": 22, "bitrate": 125000, "label": "CAN bus"}
]
```

| Field | Meaning |
|---|---|
| `id` | Hardware id of the bus. |
| `tx` / `rx` | GPIOs to the transceiver's TXD/RXD. |
| `bitrate` | Bits per second; default 125000. |

v1 uses the **first** bus entry (`hal.can_bus_at(0)`); the data structures
are ready for multi-bus chips (ESP32-P4) but the service does not iterate
them yet. A board without `can_buses` is not an error: the service sets
`can.status = "no_bus"` and stays idle.

## Frame format (ModESP-CAN v1)

### 29-bit arbitration ID

Every frame uses a 29-bit extended ID, packed by
components/modesp_can/include/modesp/can/can_codec.h:63:

```
[28:26] prio (3 bits, 0 = highest)   events=2, state=4, heartbeat=6
[25:24] kind (2 bits)                0=HEARTBEAT, 1=EVENT, 2=STATE, 3=CMD (reserved)
[23:16] src  (8 bits)                sender node-id, 1..253 (0 and 254/255 reserved)
[15:0]  id16 (16 bits)               key-id / event-id; for HEARTBEAT = magic 0x4D43 ("MC")
```

The CAN rule "two nodes must never transmit the same arbitration ID with
different payloads" holds by construction: `src` is part of the ID, so every
node owns its own ID space.

### STATE (kind=2)

A retained SharedState value. Payload: byte `[0]` = value type (bits 0–3:
0=f32, 1=i32, 2=bool) plus a VALID flag (bit 6); bytes `[1..4]` = the value,
little-endian (bool is 1 byte, DLC=2; f32/i32 are DLC=5). VALID=0 mirrors the
source's `<key>_ok` companion being `false` (e.g. a frozen EMA in
`equipment`) — the importer turns it into `is_healthy() = false`. Sent
on-change plus a full refresh every 5 s (retained emulation for late
listeners). **String keys are not exportable in v1** — they do not fit an
8-byte frame.

### EVENT (kind=1)

A transient event: raw 0..8 payload bytes as declared in the manifest. On RX
the service publishes `MsgRemoteEvent{node=src, event_id, len, data}` to the
**local** message bus from the main-loop task (the bus is not thread-safe —
publishing from other tasks is forbidden). Only events present in the
generated registry are re-published; unknown event-ids are counted and
dropped.

### HEARTBEAT (kind=0, id16=0x4D43)

Sent every **1000 ms**. Payload: `[0]` protocol version (=1), `[1]` flags
(bit 0 = node in safe mode; currently always 0 — wired up with product
interlocks in phase C5), `[2..3]` registry CRC-16 (fleet firmware-divergence
diagnostics; the receiver records but does not act on it yet), `[4..7]`
uptime in seconds (LE).

### CMD (kind=3)

Reserved for addressed writes of rw-keys (v1.1). Received CMD frames are
ignored; in v1, commands travel as events.

## Stable IDs (djb2-fold16)

`id16 = fold16(djb2(name))` where `fold16(h) = (h ^ (h >> 16)) & 0xFFFF`, and
0 maps to 0x0001. State keys hash the **full key name**
(`equipment.air_temp`); events hash `<module>.<event>`. Hash-derived IDs are
self-describing across a heterogeneous fleet: two different firmwares derive
the same ID for the same name with no shared registry file.

The generator checks for collisions within each kind (STATE and EVENT are
separate ID spaces, thanks to the kind bits) and **fails the build** on a
clash; the escape hatch is an explicit `can_id` field on the colliding entry.

Two implementations of the hash exist and are wire-contract-critical
mirrors: `can_fold16` in tools/generate_ui.py:1097 (build time) and
`modesp::can::fold16` in
components/modesp_can/include/modesp/can/can_codec.h:93 (runtime, used by the
bridge driver to hash `binding.address`). Shared test vectors in
`tools/tests/test_can_registry.py` and `tests/host/test_can_codec.cpp` keep
them in sync — changing either breaks compatibility with already-flashed
nodes.

## Plane 1: SharedState export (`can_export`)

Marking what a node broadcasts is a manifest edit, not code. Two sources feed
the generated registry (`collect_can_registry`, tools/generate_ui.py:1118):

**(a) A `state` key** — add `can_export: true` (types float/int/bool only):

```json
"state": {
  "simple_thermo.setpoint": {"type": "float", "access": "readwrite", "can_export": true}
}
```

**(b) A `requires[]` role** — exports the runtime key `equipment.<role>`; the
frame type comes from the capability's `value_type` (float/bool only in v1),
so an explicit `capability` is mandatory:

```json
"requires": [
  {"role": "air_temp", "type": "sensor", "capability": "temperature", "can_export": true}
]
```

Both accept an optional `can_id` override. The generator emits
`generated/can_registry.h` — `CAN_EXPORT[]{key, id16, type}`, `CAN_EVENTS[]`,
`CAN_REGISTRY_CRC16` — rewritten on every build (even when empty, with a
sentinel row) so a stale header cannot outlive the last removed entry. Up to
32 export keys per node (`MAX_EXPORT_KEYS`, enforced by a `static_assert`).

At runtime the service polls `SharedState::version()` each tick (the
`MqttService` pattern) and diffs against a per-key last-sent cache; a change
sends a STATE frame. A full refresh pass runs every 5 s, spread across ticks
by a cursor. TX is budgeted at 4 frames per tick (≤400 frames/s at the
100 Hz main loop).

Importing the other node's values is the bridge driver's job — see
[drivers/can_remote.md](../drivers/can_remote.md).

## Plane 2: events (`MsgRemoteEvent`)

One symmetric envelope, `modesp/can/can_messages.h`
(`msg_id::CAN_REMOTE_EVENT` = 60):

```cpp
struct MsgRemoteEvent : etl::message<msg_id::CAN_REMOTE_EVENT> {
    uint8_t  node     = 0;    // 0 = "send to the bus"; else = source node
    uint16_t event_id = 0;    // stable ID from generated/can_registry.h
    uint8_t  len      = 0;    // 0..8 payload bytes
    uint8_t  data[8]  = {};
};
```

- **TX:** a module publishes the envelope with `node == 0`. `CanService`
  catches it in `on_message` and encodes an EVENT frame — but only if the
  event_id is declared `outbound` in the registry (whitelist); anything else
  is logged and dropped.
- **RX:** a received EVENT frame becomes the same envelope with
  `node == <src>`, published locally from the main-loop task; consumers
  filter by `event_id`.

Events are declared in the manifest's top-level `can_events` block: `out`
names are written **without dots** (the `<module>.` prefix is added
automatically), `in` names are the fully-qualified `<module>.<event>` of the
source node:

```json
"can_events": {
  "out": {"defrost_started": {}},
  "in":  {"panel.button_pressed": {"payload": "u8"}}
}
```

`can_messages.h` is deliberately **not** gated by `CONFIG_MODESP_CAN_ENABLE`:
a module may publish the envelope unconditionally — without the component it
simply has no subscriber (zero cost).

## Node id

Every node needs an address 1..253. Resolution order
(can_service.cpp:124):

1. **NVS override** — namespace `can`, key `node_id` (u8). Set it when two
   boards collide or when you want stable, human-chosen addresses.
2. **MAC-derived default** — `djb2(MAC) % 253 + 1`. Two devices of the same
   board model get different addresses with no board.json edit (the same
   philosophy as the MQTT `device_id`). A collision between two nodes out of
   ~250 values is possible — that is what the NVS override is for.

board.json deliberately does **not** carry the node id: it describes the
board *model*, and two devices of one model must differ.

## Heartbeat & liveness

- Each node sends a HEARTBEAT every **1 s**.
- A remote node is considered offline after **3.5 s** of silence (any frame
  counts); the liveness sweep runs every 500 ms.
- Each tracked node gets a state key **`remote.<node-id>.online`** (bool) —
  available to interlocks and scenarios as an ordinary SharedState key.
  Nodes listed in `remote_devices` are tracked from boot (starting offline);
  unknown nodes heard on the bus are tracked too, until the 16-slot pool
  fills (a discovery seed).
- When a node goes offline, **all its cached subscriptions are invalidated
  immediately**, so a bridge driver's `is_healthy()` drops at once instead of
  waiting out `stale_ms`.

## State keys (`can.*`)

| Key | Type | Meaning |
|---|---|---|
| `can.status` | string | `ok`, `no_bus` (no `can_buses` in board.json), `error` (init failed — node stays alive, CAN idle), `bus_off`, `recovering` (node re-created after bus-off; `ok` only after a real `tx_done` confirms frames flow again). |
| `can.node_id` | int | This node's resolved address. |
| `can.tx_count` | int | Frames transmitted. Silent set (`track_change=false`) — no WS delta spam. |
| `can.rx_count` | int | Frames received (ISR counter). Silent. |
| `can.err_count` | int | Bus error events. Silent. |
| `can.rx_dropped` / `can.tx_dropped` | int | RX ring / TX queue overflow counters; published only once non-zero. Silent. |
| `can.bus_off` | bool | Bus-off condition active. |
| `can.loopback_ok` | bool | Self-test builds only: first own frame made the full TX→RX round trip. |
| `remote.<n>.online` | bool | Liveness of remote node `<n>` (see above). |

## Threading model

All four callbacks of the new TWAI driver run in **ISR context**, and there
is no blocking-RX API — so the component keeps a strict split:

- **RX:** `on_rx_done` copies the frame (id + dlc + up to 8 bytes) into a
  16-slot static SPSC ring — producer ISR, consumer main loop. A full ring
  drops the frame and counts it; nothing blocks.
- **TX:** the driver queue is **zero-copy** — it stores pointers — so frames
  live in a 12-slot static pool until `on_tx_done` releases the slot.
  `twai_node_transmit(..., 0)` is non-blocking; a full queue increments a
  counter instead of waiting (the main loop must never block: TWDT is 5 s
  with `trigger_panic`).
- **Errors:** `on_error` / `on_state_change` only bump atomic counters and
  flags. All real work — decode, `state_set`, `publish`, bus-off recovery —
  happens in `on_update` on the main-loop task. Recovery **re-creates the TWAI
  node** after a 1 s backoff (the IDF driver never fires `tx_done` for the
  frame aborted by bus-off, so the node teardown is the only safe way to
  reclaim the TX pool); status goes `bus_off` → `recovering` → `ok`, where
  `ok` requires an actual `tx_done` on the new node — a stuck-dominant bus
  keeps cycling `bus_off`/`recovering` instead of falsely reporting `ok`.
- **Init failure is not fatal** (a confirmed boot-killer pattern from the
  2026-07-07 review): any error path sets `can.status` and returns `true`, so
  the device boots and the rest of the firmware runs; CAN just stays idle.

Zero heap in the hot path: ring, TX pool, subscription cache and node table
are all fixed-size statics.

Software filtering: ESP32/S3 offer a single mask filter, which cannot express
"heartbeats from everyone + my keys + my events". v1 accepts all extended
frames and dispatches in software — at 250 kbps that is at most ~2000
frames/s with tens of bytes of work each. A hardware filter is a later
optimization, if measurements ever demand it.

## Self-test (`CONFIG_MODESP_CAN_SELF_TEST`)

Sets the node's `enable_self_test` + `enable_loopback` flags: own frames come
back **without ACK**, so the full TX→RX path (encode → transmit → ISR → ring
→ decode) is verifiable on **one** bench device with no transceiver and no
second node. `can.loopback_ok` goes `true` after the first own frame returns.

> ⚠️ **Warning:** never enable self-test on a production bus — no-ACK
> transmission violates CAN semantics for every other node.

## v1 limits

Honest list of what is *not* there:

- **No string keys** — 8-byte frames; fragmentation is a separate decision.
- **No CMD frames** — kind=3 is reserved; addressed rw-writes are v1.1.
- **Software filtering** — a CPU tax on a heavily loaded bus (see above).
- **First bus only** — multi-bus chips are data-ready but not serviced.
- **No third-party interop** — this is a homogeneous-fleet protocol by
  choice; a CANopen/Cyphal implementation could later replace the component
  behind the same driver-bridge seam (ADR-004 phase C6).
- **Registry CRC is diagnostic only** — carried in heartbeats, not yet
  compared or acted on by receivers.

## Next steps

Phases C3+ of ADR-004 need two physical nodes with transceivers and are still
ahead: state mirroring and liveness on a real bus (C3), events end-to-end
(C3b), the bridge-driver role end-to-end plus a WebUI devices page for CAN
(C4), liveness in product interlocks and discovery (C5).

- **[drivers/can_remote.md](../drivers/can_remote.md)** — the bridge driver
  that turns a remote node's key into a local role.
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** — the
  `can_buses` and `remote_devices` sections.
- **[ADR-004](../../../../docs/can/ADR-004-can-transport-p6.md)** — the full
  architecture decision, deviations from the external design doc, open
  questions.

## Sources

- [`components/modesp_can/include/modesp/can/can_codec.h`](../../../../components/modesp_can/include/modesp/can/can_codec.h) — pure, host-testable frame codec.
- [`components/modesp_can/include/modesp/can/can_service.h`](../../../../components/modesp_can/include/modesp/can/can_service.h) / [`src/can_service.cpp`](../../../../components/modesp_can/src/can_service.cpp) — the service.
- [`components/modesp_can/include/modesp/can/can_remote_cache.h`](../../../../components/modesp_can/include/modesp/can/can_remote_cache.h) / [`src/can_remote_cache.cpp`](../../../../components/modesp_can/src/can_remote_cache.cpp) — remote-node cache.
- [`components/modesp_can/include/modesp/can/can_messages.h`](../../../../components/modesp_can/include/modesp/can/can_messages.h) — the `MsgRemoteEvent` envelope.
- [`components/modesp_can/Kconfig`](../../../../components/modesp_can/Kconfig) / [`CMakeLists.txt`](../../../../components/modesp_can/CMakeLists.txt)
- `tools/generate_ui.py` — `collect_can_registry` / `generate_can_registry_header` / `can_fold16`.
