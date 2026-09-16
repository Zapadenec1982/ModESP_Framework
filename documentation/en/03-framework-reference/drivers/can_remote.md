# `can_remote` — remote ModESP-node key over CAN

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/can_remote.md](../../../uk/03-framework-reference/drivers/can_remote.md)

Bridge driver for the CAN transport: it makes a SharedState key **exported by
another ModESP node** (ModESP-CAN v1,
[ADR-004](../../../../docs/can/ADR-004-can-transport-p6.md)) look like an
ordinary local sensor. The driver does no bus I/O itself — the
[`modesp_can`](../components/modesp_can.md) component receives STATE frames
and fills `CanRemoteCache`; the driver just subscribes to one `(node, key)`
pair at factory time and reads the cached value in `read()`. A consumer
module binds a role by capability and never learns the value crossed a wire.

The driver registers as a `sensor` with `hardware_type: can`,
`transport: "can"`, `requires_address: true` and `multiple_per_bus: true`.
One driver instance per bound remote key; one remote **device** (node) feeds
as many roles as you bind — one binding per key. This is the CAN mirror of
[`ble_xiaomi_th`](ble_xiaomi_th.md): identity lives on the device, the
binding's `address` selects *what* from that device feeds the role.

REQUIRES: the `modesp_can` component (`CONFIG_MODESP_CAN_ENABLE`). The
generated driver toggle carries `depends on MODESP_CAN_ENABLE`, so the driver
auto-disables when the component is off.

## Board resource

A remote node is not a wired resource — it is a row in the transport-generic
`remote_devices` section (board.json seed, or added at runtime via
`/data/devices.json`; ConfigService merges both):

```json
"remote_devices": [
  {"id": "node_kitchen", "transport": "can", "identity": "12", "name": "Kitchen controller"}
]
```

| Field | Meaning |
|---|---|
| `id` | Logical device name referenced by `hardware` in bindings — never the address itself (R0.3). |
| `transport` | Must be `"can"` — the factory rejects anything else. |
| `identity` | The remote node's **node-id, 1..253**, as a string. Decimal (`"12"`) or hex (`"0x0C"`) both parse. |
| `name` | Human label for UI; optional. |

The local node additionally needs a `can_buses` entry (pins + bitrate) and a
CAN transceiver — see
[components/modesp_can.md](../components/modesp_can.md). Every node listed in
`remote_devices` with `transport: "can"` is liveness-tracked from boot and
gets a `remote.<node-id>.online` state key.

## Bindings

`address` names the **remote key** — the full SharedState key name as the
*source* node exports it (`can_export` in its manifest). The key-id on the
wire is `fold16(djb2(address))`, computed by the same hash the source node's
generator used, so matching happens with no shared registry file:

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "node_kitchen", "driver": "can_remote", "role": "kitchen_temp", "address": "equipment.air_temp",   "module": "equipment"},
    {"hardware": "node_kitchen", "driver": "can_remote", "role": "kitchen_door", "address": "equipment.door_open",  "module": "equipment"}
  ]
}
```

If the source node declared the key with an explicit `can_id` override, the
name hash will not match — pass the id itself as hex instead:

```json
{"hardware": "node_kitchen", "driver": "can_remote", "role": "kitchen_temp", "address": "0x1234", "module": "equipment"}
```

Bound to the `equipment` module, each role publishes the usual pair:

- `equipment.kitchen_temp` (float) — the remote value;
- `equipment.kitchen_temp_ok` (bool) — health flag, driven by
  `is_healthy()` below.

## Settings

| Key | Type | Default | Notes |
|---|---|---|---|
| `stale_ms` | int | 5000 | No fresh STATE frame for longer → unhealthy. Range 500–600000, step 500. Persisted. |

## Provides

`{"type": "float"}` — the remote key's value, published to
`equipment.<role>`. The wire carries three types; `read()` flattens them:
f32 passes through, i32 is cast to float, bool becomes `0.0` / `1.0`.

## Pattern: consumed by business module

The consumer reads the role key exactly as it would for a local sensor —
health-gate first, then act. For hard interlocks you can additionally gate on
the node's liveness key:

```cpp
// in your BaseModule::on_update:
float t;
bool  t_ok = state.get_or("equipment.kitchen_temp_ok", false);
if (t_ok && state.get("equipment.kitchen_temp", t)) {
    control_loop(t);
} else {
    fail_safe();   // node offline, frame stale, or source marked it invalid
}

// optional interlock on the whole node (node-id 12 in this example):
bool kitchen_online = state.get_or("remote.12.online", false);
```

Nothing in the consumer mentions CAN, the node-id, or the remote key name —
swap the binding to a local `ntc` sensor and the module runs unchanged.

## How it works

At factory time (boot, drivers/can_remote/src/can_remote_driver.cpp:65):

1. `hal.find_remote_device(hardware)` resolves the device row; the factory
   verifies `transport == "can"` and parses `identity` as a node-id 1..253.
2. `address` becomes a 16-bit key-id: `fold16(djb2(address))`, or the literal
   value for the `0x....` hex form.
3. `CanRemoteCache::instance().subscribe(node, key_id)` returns a pointer to
   a cache slot (idempotent for the same pair); the driver keeps it.

At runtime the driver is passive — `update()` is a no-op. `CanService`
drains received STATE frames on the main-loop task and refreshes the cache
slot (value, VALID bit, timestamp). `read()` returns the cached value;
`is_healthy()` is true only when **all three** hold:

- the last frame carried VALID=1 (the source's `<key>_ok` was true);
- the frame is fresher than `stale_ms`;
- the node is online (heartbeat within 3.5 s).

When a node times out, the cache invalidates all its subscriptions
immediately, so health drops at once instead of waiting out `stale_ms`.
Everything runs on the single main-loop task — no locks, no heap.

Freshness expectations: the source sends on-change **plus** a full refresh
every 5 s, so a healthy-but-quiet key is re-confirmed at least every 5 s.

## Discovery

None yet. Node-ids and remote key names are declared manually; there is no
CAN bus scan or node browser in the WebUI (planned as ADR-004 phase C5 —
export-table announcement + a devices page). The driver also declares no
per-channel `capability` yet, so the role matcher falls back to type-based
matching (ADR-004 open question 10.3).

## Common pitfalls

**Role never becomes healthy:** the source node must actually export the key
— `can_export: true` on the state key (or `requires[]` role) in *its*
manifest, and its firmware rebuilt since. Also check `remote.<n>.online`: if
it never goes `true`, the problem is the bus/heartbeat, not the key.

**`address` must match the exported name exactly:** the match is a hash of
the full key string (`equipment.air_temp`), so a typo or a shortened name
silently subscribes to a key-id nobody transmits.

**Source key uses `can_id` override:** the name hash no longer matches. Use
the hex form — `"address": "0x1234"` — with the same id the source manifest
declares.

**`identity` is not a node-id:** a MAC string in `identity` is a BLE habit;
for CAN it must parse as an integer 1..253 or the factory refuses the
binding at boot (check the log for `can_remote` errors).

**`stale_ms` below 5000 flaps:** a key whose value does not change is only
re-sent by the 5 s full refresh, so a tighter `stale_ms` makes health
oscillate between refreshes. Lower it only for keys you know change (and are
therefore re-sent) faster.

**Pools are finite:** 8 driver instances (`MAX_CAN_REMOTE`) and 16 cache
subscriptions (`CanRemoteCache::MAX_SUBSCRIPTIONS`). The factory returns
`nullptr` with a "pool exhausted" log line when you run out.

## UI surface

One card per binding on the service-level **"Налаштування датчиків"** page
(`page_id: driver_settings`): "CAN remote: {{hardware_id}}" with a
`number_input` for `stale_ms`.

## Next steps

This driver is the C2/C4 skeleton of ADR-004: it is exercised on a single
bench via the component's self-test loopback, but the two-node end-to-end run
(phase C3/C4 — real bus, transceivers, WebUI devices page for CAN) is still
ahead.

- **[components/modesp_can.md](../components/modesp_can.md)** — the transport
  component: frame format, heartbeat/liveness, `can_export`, Kconfig.
- **[04-hardware/board-config.md](../../04-hardware/board-config.md)** — the
  `remote_devices` and `can_buses` sections.
- **[ble_xiaomi_th.md](ble_xiaomi_th.md)** — the BLE bridge driver this one
  mirrors (same device/identity/address pattern, different transport).
- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)**
  — driver authoring guide (factory + registration).

## Source

- [`drivers/can_remote/manifest.json`](../../../../drivers/can_remote/manifest.json)
- [`drivers/can_remote/include/can_remote_driver.h`](../../../../drivers/can_remote/include/can_remote_driver.h)
- [`drivers/can_remote/src/can_remote_driver.cpp`](../../../../drivers/can_remote/src/can_remote_driver.cpp)
