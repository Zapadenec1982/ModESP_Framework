# `player` — audio player (alarm tones + WAV/MP3 clips)

> 📖 **In Ukrainian:** [documentation/uk/03-framework-reference/modules/player.md](../../../uk/03-framework-reference/modules/player.md)

Sound business module: generates alarm tones (sine via LUT + phase accumulator) and plays WAV/MP3 clips from LittleFS (`/data/audio/<name>`). Sources are reduced to 16-bit mono PCM, software volume is applied to it, and buffers are handed to the `audio` capability — today that is the [`max98357a`](../drivers/max98357a.md) amplifier over I²S, tomorrow any other sink. The module owns a dedicated playback task (FreeRTOS) that is the only thing touching the sink; `on_update()` merely enqueues requests and mirrors atomics into SharedState.

**Role = capability (R0.1).** `player` declares the role `audio_main` with `capability: "audio"` and never knows which driver provides it — resolution goes by role through the binding. The audio output is optional (`"optional": true`): without a binding the module substitutes a `NullSink` and silently accepts commands (nothing plays, the system does not crash).

## Data flow

```
player.play / player.btn_alarm / player.btn_beep
                    │
              (request queue)
                    │
            playback task ── Tone (sine LUT) ─┐
                          └─ Clip (WAV/MP3) ──┼─▶ 16-bit mono PCM ─▶ volume ─▶ IAudioSink.write()
                                              │                                        │
                    /data/audio/<name>.wav|.mp3                              max98357a (I²S) ─▶ speaker
                    │
      player.playing / player.clip  ◀── state mirror ── on_update()
```

Expected role in bindings: `audio_main` (capability `audio`, driver `max98357a`, `hardware_type: i2s_bus`). One I²S bus = one audio output — the sink is a singleton (see [`drivers/max98357a.md`](../drivers/max98357a.md)).

## Commands and control (web/MQTT, momentary)

Declared as module state keys (`access: readwrite`) — the generator wires them into web/MQTT automatically. Command keys are **self-resetting**: the module consumes them and returns them to empty/`false`.

| Key | Type | Description |
|---|---|---|
| `player.play` | string | What to play: `alarm` / `beep` / a WAV or MP3 clip name (without extension → `.wav`). |
| `player.stop` | bool | Stop playback. |
| `player.btn_alarm` | bool | Test: alarm tone (2 kHz, intermittent, safety-cap 120 s). |
| `player.btn_beep` | bool | Test: short beep (2 kHz, 200 ms). |

## Settings (web, persisted)

`access: readwrite`, `persist: true` — the generator wires them into web/NVS/MQTT.

| Key | Type | Def. | Description |
|---|---|---|---|
| `player.enabled` | bool | true | Sound on/off. Turning off on the fly stops the active clip. |
| `player.volume` | int 0-100 %, step 5 | 70 | Volume (**software** — the MAX98357A has no volume register; scaled while filling the buffer, 0-100 % → 0-256). |

## Indication (read-only state)

| Key | Description |
|---|---|
| `player.playing` | Playback state (`Playing`/`Silent`). |
| `player.clip` | Current/last clip or tone. |

## Clips, tones, priorities

- **Tones** are generated from a sine LUT — no asset needed. `alarm` is an intermittent gate (200 ms on / 400 ms period), `beep` is single 200 ms.
- **Clips** are streamed from `/data/audio/<name>`: the extension picks the decoder — `.mp3` → Helix decoder, otherwise `.wav` (16-bit mono PCM). Without an extension `.wav` is appended.
- **Priority:** an active `alarm` (`PRIO_ALARM`) is not preempted by lower requests; `stop` and equal/higher requests preempt it.

> ℹ️ **Note:** MP3 is compiled only with `CONFIG_MODESP_AUDIO_MP3` (def. `y`). When disabled, `.mp3` clips are rejected; WAV and tones still work.

## Bindings

The audio output is a wired I²S bus from board.json, bound to `max98357a`, role `audio_main`, module `player`.

### board.json

```json
"i2s_buses": [
  { "id": "i2s_0", "bclk": 15, "ws": 16, "dout": 7, "sd": 17, "sample_rate_hz": 16000 }
]
```

### bindings.json

```json
[
  { "hardware": "i2s_0", "driver": "max98357a", "role": "audio_main", "module": "player" }
]
```

`module` must name `player` (the role owner, R1.2/R3.4), otherwise the build fails. The capability is `audio`; role and channel match by capability only (R3.1). Put WAV/MP3 assets in `data/audio/`.

## Optionality

The module in `project.json` → always compiled (lightweight, inert without a sink). The **driver** itself is optional via `CONFIG_MODESP_DRIVER_MAX98357A`; the MP3 decoder — via `CONFIG_MODESP_AUDIO_MP3`. Without an audio binding — `NullSink` (commands are accepted, no sound).

## Common pitfalls

- **Silence, `NullSink` in the logs** — there is no bindings entry `audio_main`→`max98357a`, or the driver is disabled in menuconfig. Reconcile with `tools/drivers_sync.py --fix`.
- **Clip does not play** — the file is not in `/data/audio/` (check `data.bin` freshness: build via `run_build.ps1`, R8.1), or the WAV is not 16-bit mono PCM.
- **`.mp3` rejected** — `CONFIG_MODESP_AUDIO_MP3` is disabled.

## Source

- [`modules/player/manifest.json`](../../../../modules/player/manifest.json)
- [`modules/player/src/player_module.cpp`](../../../../modules/player/src/player_module.cpp) — playback task, tones, volume, decoder selection.
- [`modules/player/src/wav_decoder.cpp`](../../../../modules/player/src/wav_decoder.cpp) / [`mp3_decoder.cpp`](../../../../modules/player/src/mp3_decoder.cpp)
- [`drivers/max98357a.md`](../drivers/max98357a.md) — audio-output driver (`IAudioSink`, I²S).
- [`rules.md`](../rules.md) — R0.1 (role=capability), R3.1/R3.3/R3.4, R5.2.
- [`project-hierarchy.md`](../project-hierarchy.md) — peripheral route Module↔Role↔Device↔Binding.
