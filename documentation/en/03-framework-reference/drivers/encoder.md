# `encoder` — incremental rotary encoder

> 📖 **Українською:** [documentation/uk/03-framework-reference/drivers/encoder.md](../../../uk/03-framework-reference/drivers/encoder.md)

Incremental rotary encoder — a quadrature (two-pin A/B) sensor that
reports how far and which way a knob has turned. The driver decodes the
CLK/DT signal on each tick and publishes a **signed detent counter** as a
float. Consumers read the counter and track its delta between reads to
detect rotation; the counter never resets, so no steps are lost.

The driver registers as a `sensor` with `hardware_type: encoder` and
`requires_address: false`. One sensor per bound encoder.

The push-button found on most encoder modules (KY-040 and friends) is
**not** part of this driver — it is a plain contact. Bind the SW pin to
the [`digital_input`](digital_input.md) driver as a separate role. This
keeps `encoder` a single-signal sensor, like every other driver.

REQUIRES: `modesp_hal`. No external dependencies.

## Board resource

A quadrature encoder needs **two** pins, so it gets its own board.json
resource type (the one-id-one-pin resources can't express a 2-pin
device):

```json
"encoders": [
  {"id": "enc_0", "clk": 25, "dt": 26, "pull": "up", "label": "Menu encoder"}
]
```

| Field  | Meaning |
|--------|---------|
| `id`   | Hardware id referenced by the binding. |
| `clk`  | Channel A GPIO. |
| `dt`   | Channel B GPIO. |
| `pull` | `"up"` (default) or `"down"`. Most mechanical encoders are wired to GND and need pull-ups. |

The HAL configures both pins as inputs; the driver never touches raw
pins. See [04-hardware/board-config.md](../../04-hardware/board-config.md).

## Bindings

The "encoder with button" is wired as **two** bindings — rotation
(`encoder`) and the click (`digital_input`):

```json
{
  "manifest_version": 1,
  "bindings": [
    {"hardware": "enc_0",  "driver": "encoder",       "role": "menu_nav",    "module": "equipment"},
    {"hardware": "enc_sw", "driver": "digital_input", "role": "menu_select", "module": "equipment"}
  ]
}
```

Resulting state keys:

- `equipment.menu_nav` (float) — accumulated detent count, signed.
- `equipment.menu_select` (bool) — button level.

`enc_sw` here is an ordinary `gpio_inputs` entry in board.json.

## Settings

| Key | Type | Default | Notes |
|---|---|---|---|
| `steps_per_detent` | int | 4 | Quadrature transitions per physical click. KY-040-style modules emit a full cycle (4) per detent; set 1 or 2 for finer encoders. Persisted. |
| `invert` | bool | false | Swap rotation direction (CW ↔ CCW). Persisted. |

## Provides

`{"type": "float"}` — the running, signed detent counter, published to
`equipment.<role>`. It is an **absolute** position, not a per-tick delta:
turn one detent clockwise and it goes from `0` to `1`; turn back and it
returns to `0`.

## Pattern: consumed by business module

A consumer keeps the last value it saw and acts on the difference:

```cpp
// in your BaseModule::on_update:
float pos = state.get_or("equipment.menu_nav", 0.0f);
int32_t delta = (int32_t)pos - last_pos_;     // last_pos_ is a member
if (delta != 0) {
    last_pos_ = (int32_t)pos;
    // delta > 0 → clockwise, delta < 0 → counter-clockwise
    menu_move(delta);
}
bool click;
if (state.get("equipment.menu_select", click) && click) {
    menu_select();
}
```

Edge detect and acceleration belong in the consumer — the driver
publishes the **raw position**.

## How it works

The decoder reads the 2-bit quadrature state `(CLK << 1) | DT` each tick
and looks the transition up in a 16-entry table indexed by
`(prev << 2) | curr`. Each valid Gray-code transition is ±1 quarter-step;
invalid transitions (contact bounce, a skipped state) map to 0 and are
ignored. Quarter-steps accumulate until a full `steps_per_detent` is
reached, then the signed counter ticks by one.

Polling at the ~100 Hz tick is enough for a hand-turned knob. For a
machine-driven encoder spinning fast enough to alias at 100 Hz, write a
PCNT-peripheral variant instead.

## Discovery

None. Bindings declared manually.

## Common pitfalls

**Counts double or half-speed:** `steps_per_detent` doesn't match your
encoder. If one click moves the counter by 4, set it to 4 (or whatever
your encoder emits per detent); if it skips clicks, lower it.

**Reversed direction:** wiring CLK/DT swapped, or you just want the other
convention — set `invert: true` rather than re-soldering.

**Floating pins at boot:** without pull-ups the decoder sees noise. Set
`pull: "up"` in board.json (and check the module has its own resistors if
the GPIO is input-only, 34–39 on classic ESP32).

**Fast spins skip steps:** 100 Hz polling can alias a quickly-spun
encoder. Acceptable for menu knobs; not for high-rate shaft sensing.

## UI surface

None in the shipped manifest. Add a cards block in your fork to expose
`<binding_id>.steps_per_detent` / `<binding_id>.invert` if operators need
to tune them in the field.

## Why it's a good driver to read

- Shows how a **multi-pin** device gets its own board.json resource and
  HAL finder (`find_encoder`) — the pattern for any sensor that the
  one-id-one-pin resources can't express.
- Clean single-signal `sensor` contract despite decoding state across
  ticks: all the state machine lives in the driver, the consumer sees one
  float.

## Next steps

- **[drivers/digital_input.md](digital_input.md)** — the companion driver
  for the encoder's push-button.
- **[writing-a-driver.md](../../02-module-author-guide/writing-a-driver.md)** —
  driver authoring guide (factory + registration).
- **[modules/equipment.md](../modules/equipment.md)** — how sensors become
  `equipment.<role>` keys.

## Source

- [`drivers/encoder/manifest.json`](../../../../drivers/encoder/manifest.json)
- [`drivers/encoder/include/encoder_driver.h`](../../../../drivers/encoder/include/encoder_driver.h)
- [`drivers/encoder/src/encoder_driver.cpp`](../../../../drivers/encoder/src/encoder_driver.cpp)
