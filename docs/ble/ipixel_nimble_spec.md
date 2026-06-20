# ModESP — iPixel Color 16×64 BLE Panel Driver + ESP-IDF NimBLE/Wi-Fi Coexistence Spec

> Generated from adversarially-verified deep research (workflow `ble-panel-research`,
> 2026-06-20). Part 2 (ESP-IDF config) verified verbatim against `release/v5.5` Kconfig
> source. Part 1 (panel protocol) cross-confirmed across multiple reverse-engineering repos.
>
> **Scope:** ESP32-S3 NimBLE-**central** driver controlling an iPixel Color (`LED_BLE_*`,
> silkscreen `TR2304H183-13`) 16×64 RGB LED matrix, running alongside Wi-Fi STA + NimBLE
> peripheral roles under ESP-IDF v5.5.
>
> **Confidence legend:** **Confirmed** = ≥2 independent codebases agree · **Likely** = single
> working-code source · **Must-sniff** = sources conflict / unverified; capture on real HW first.

---

## PART 1 — iPixel Color / TR2304H183 16×64 BLE Protocol

### 1.0 Device identity
- `TR2304H183-13` is an internal PCB/batch code, not a catalog SKU. Identify by **app (iPixel
  Color, Android pkg `com.wifiled.ipixels`) + BLE adv name `LED_BLE_*` + device-type byte**,
  never by silkscreen. (Confirmed)
- Member of the **iDotMatrix / fa02-fa03 protocol family**. `LED_BLE_*` uses the same transport
  as `IDM-*` panels but **diverges at higher-level opcodes** (clock/GIF broken on the wide
  panel — §1.7). (Confirmed)
- Sources: cagcoach/ha-ipixel-color · yyewolf/go-ipxl · derkalle4/python3-idotmatrix-library ·
  Pupariaa/Bk-Light-AppBypass · ToBiDi0410/iPixel-ESP32 (existing ESP32 port).

### 1.1 BLE UUIDs — Confirmed
| Role | UUID | Properties | Handle |
|------|------|------------|--------|
| Parent service | *not published — discover at runtime* | — | Must-sniff |
| **Write** (host→device) | `0000fa02-0000-1000-8000-00805f9b34fb` | Write / Write-Without-Response | ~`0x0006` |
| **Notify** (device→host) | `0000fa03-0000-1000-8000-00805f9b34fb` | Notify | ~`0x0009` |
| CCCD | `0x2902` on fa03 → write `01 00` | | ~`0x000a` |

Connect flow: scan adv name prefix `LED_BLE_` → connect → discover fa02/fa03 → enable fa03
notifications (CCCD `01 00`) → write commands to fa02.

### 1.2 Frame format — Confirmed
```
[LEN_LO][LEN_HI][CMD_LO][CMD_HI][DATA...]   ; LEN = TOTAL frame byte count (LE), incl. header
```
Opcode class: `0x01xx` = set/DIY, `0x80xx` = config/query.

### 1.3 Power & brightness — Confirmed
| Command | Bytes | Opcode |
|---------|-------|--------|
| Power ON | `05 00 07 01 01` | `0x0107` |
| Power OFF | `05 00 07 01 00` | `0x0107` |
| Brightness (NN=1..100 → 0x01..0x64) | `05 00 04 80 NN` | `0x8004` |
| Flip normal / upside-down | `05 00 06 80 00` / `05 00 06 80 01` | `0x8006` |
| Reset (2 frames) | `04 00 03 80` then `05 00 04 80 50` | `0x8003` |

Brightness pre-scaling (Likely): image/text bytes pre-scaled `out = clamp(byte*brightness/100)`
before upload (go-ipxl). A first driver can rely on `0x8004` alone and skip pre-scaling.

### 1.4 Handshake — Must-sniff (control plane works without it)
- Power/brightness/flip: **no handshake, no pairing/bonding** — write immediately after CCCD
  enable. (Confirmed)
- Device-info/geometry query (write fa02 → size/type byte on fa03) implemented in go-ipxl
  `device_info.go`, not reproduced verbatim. 16×64 *should* report device-type `0x83` = Type3,
  **64 wide × 16 high** (wide strip, NOT 16-tall portrait). (Likely)
- BK-Light ACT1025 (16×64) image-upload ACK sequence differs from 32×32; 16×64 bytes not
  extracted. 32×32 reference (diff only): `HANDSHAKE_FIRST=08 00 01 80 0E 06 32 00`,
  `HANDSHAKE_SECOND=04 00 05 80`, `ACK1=0C 00 01 80 81 06 32 00 00 01 00 01`,
  `ACK2=08 00 05 80 0B 03 07 02`, `ACK3=05 00 02 00 03`.

### 1.5 Full-frame image upload — Must-sniff (sources CONFLICT — do not ship blind)
Two incompatible encodings:
- **Encoding A — PNG-in-frame** (derkalle4, go-ipxl, cagcoach): rasterize matrix → PNG → wrap →
  chunk. Header: `[LEN_2LE][02 00][OPT=00][FRAMELEN_4LE][CRC32_4LE][BUFFER 01..09][PNG...]`.
  CRC32 = zlib/IEEE over (brightness-scaled) payload, LE. **Inconsistency:** derkalle4 `image.py`
  uses NO CRC32 + different chunk header; go-ipxl includes CRC32.
- **Encoding B — raw RGB888** (yewolf RE blog): raw RGB bytes, no PNG.

→ Implement **both behind a flag**; select via one-time HCI capture (§1.8). Note: PNG encoding on
  ESP32 needs a deflate/PNG encoder (miniz); raw-RGB for 64×16 = 3072 bytes is far simpler.

Chunking: default MTU 23 → 20-byte ATT payload, stack auto-fragments, no app-level sequence byte.
Logical chunk: derkalle4 4096 B (+16 B hdr); iPixel docs cite 12 KB threshold; pace writes ~10 ms.

### 1.6 Scrolling text — Likely (structure) / Must-sniff (enums)
TYPE_TEXT (tag `00 01`), same length+CRC32 framing. Host rasterizes glyphs (firmware renders no
fonts). derkalle4 `text.py`: header `[LEN_2LE][03 00 00][PAYLOAD_LEN_4LE][CRC32_4LE][00 00 12]`,
meta `[num_chars_2LE][00 01][text_mode 0..8][speed][color_mode][R][G][B][bg_mode][bgR][bgG][bgB]`,
glyphs 16×32 1-bit, separated by `05 FF FF FF` / `02 FF FF FF`. Enum values + separator =
Must-sniff.

### 1.7 Known-broken on this panel — do NOT ship without HW test
- **Clock** (`0x0106`) — non-functional on `LED_BLE`/iPixel 16×64 (derkalle4 issue #26).
- **GIF/animation** (`0x0003`) — non-functional on wide panel.

### 1.8 nRF Connect / btsnoop capture steps (resolves all Must-sniff in one session)
**Method 1 — Android btsnoop:** Developer Options → "Enable Bluetooth HCI snoop log" → use iPixel
Color app, do ONE isolated action at a time (brightness; solid-red image; "AB" text; clock; GIF),
~3 s apart → `adb pull /sdcard/btsnoop_hci.log` → Wireshark filter
`btatt.opcode.method == 0x12 || 0x52` on handle `0x0006`.
- Image encoding A vs B: if payload starts `89 50 4E 47` → PNG (A); if `3072` bytes of `FF 00 00`
  → raw RGB (B). Record presence of CRC32 after FRAMELEN.
- Device-info: first Write to `0x0006` after connect + its Notification on `0x0009`; byte 4 =
  device-type.
- Text enums: diff slow-vs-fast / red-vs-green captures.

**Method 2 — nRF Connect manual:** Scanner → filter `LED_BLE` → Connect → enable notify on
`0xFA03` (record parent service UUID) → write `0xFA02` as **Write Command** → `05 00 07 01 01`
(on), `05 00 04 80 32` (50%).

### 1.9 MINIMAL first driver (ship only Confirmed control plane)
```
1. Scan adv name prefix "LED_BLE_" → connect (NimBLE central)
2. Discover service → fa02 (write), fa03 (notify)
3. Write fa03 CCCD = 01 00
4. Power ON   → fa02: 05 00 07 01 01
5. Brightness → fa02: 05 00 04 80 NN   (NN = 0x01..0x64)
6. Flip       → fa02: 05 00 06 80 00 / 01
7. Power OFF  → fa02: 05 00 07 01 00
```
All writes = Write-Without-Response, no auth, no handshake. Add image (A+B behind flag) and text
**after** the §1.8 capture.

---

## PART 2 — ESP-IDF v5.5 / ESP32-S3: NimBLE (peripheral+central) + Wi-Fi Coexistence

> All symbols verified verbatim against `release/v5.5` Kconfig. Two symbols in the raw research
> were INVALID — corrected here (§2.3).

### 2.1 Verified config (drive via `MODESP_BLE_ENABLE` Kconfig select + component defaults)
```ini
# Bluetooth host = NimBLE (default host is Bluedroid → REQUIRED)
CONFIG_BT_ENABLED=y
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
# Roles: dual-role (peripheral+central) is STOCK default — no special flag
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
# Sum of simultaneous peripheral+central links (default 3 on S3, range 1-9)
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096
CONFIG_BT_CTRL_BLE_MAX_ACT=6
# Wi-Fi/BLE software coexistence (default-y when both stacks on; set explicit)
CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y
# Core pinning (S3 dual-core): radio on core 0, Wi-Fi on core 1.
# NOTE: these are CHOICE-option symbols (=y), not assignable ints.
CONFIG_BT_CTRL_PINNED_TO_CORE_0=y
CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y
CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1=y
# OPTIONAL: NimBLE host heap → PSRAM (N16R8). Controller DMA buffers MUST stay internal.
# CONFIG_SPIRAM=y
# CONFIG_SPIRAM_USE_MALLOC=y
# CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y
```
S3 controller uses `BT_CTRL_*` namespace (NOT `BT_LE_*` — those are C6/H2/C5/P4).

### 2.2 Coexistence support matrix (S3, v5.5 — official)
| Wi-Fi × BLE | Verdict |
|---|---|
| **STA** Scan/Connecting/Connected × BLE any | **Y — stable** ✅ |
| **SoftAP** Connecting/Connected × BLE any | **C1 — unstable** ⚠️ avoid |

Keep Wi-Fi modem-sleep on; do not force `WIFI_PS_NONE` (recommended for stability).

### 2.3 Corrections vs raw research (do not regress)
1. ❌ `CONFIG_ESP_WIFI_TASK_CORE_ID=1` invalid (hidden derived int) → ✅ `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1=y`.
2. ❌ `*_PINNED_TO_CORE_CHOICE` are CHOICE blocks, not keys → ✅ use `_0=y` / `_1=y` options.
3. "modem sleep mandatory for coex" overstated — Kconfig `select` is real, but guide frames it as for *stable* mode only.

### 2.4 Footprint (verify with `idf.py size` — no official per-config table)
- Flash: NimBLE ≈ 167 KB smaller than Bluedroid; BLE portion low-hundreds of KB. Non-issue on 16 MB.
- RAM: ~47 KB IRAM + ~14 KB DRAM static; ~88 KB heap @ init (~149 KB total). Scales with
  `MAX_CONNECTIONS` + ACL/MSYS buffer counts.
- PSRAM: host heap + BT BSS OK in PSRAM; controller DMA/radio buffers MUST stay internal.

### 2.5 Init-order pitfalls
1. **`nvs_flash_init()` FIRST** (stores PHY calibration for both radios — #1 coex boot failure if late).
2. Wi-Fi: `esp_netif_init` → `esp_event_loop_create_default` → `esp_wifi_init` → `set_mode(STA)` → `esp_wifi_start`.
3. NimBLE: `nimble_port_init` → configure `ble_hs_cfg` → `nimble_port_freertos_init(host_task)`.
4. Optional `esp_coex_preference_set(...)` after both up (`PREFER_BALANCE` default; `PREFER_BT` to protect the panel link).
- Scan-while-advertising (OSD peripheral + scan for `LED_BLE_*`) supported; bounded by `MAX_CONNECTIONS`. Ref `examples/bluetooth/nimble/ble_multi_conn`.
- Edge case GitHub #17871: S3 STA disconnect on some Android-13+ hotspots only when BLE active — field stability note, not a blocker.

### Net guidance
- **Build now (zero HW risk):** §2.1 sdkconfig + §1.9 minimal driver (connect+power+brightness+flip).
- **One HW capture (§1.8) gates the rest:** resolves PNG-vs-raw-RGB, CRC32, geometry, text enums.
- **Do not ship** clock/GIF without HW test (broken on this panel class).
- Code refs for ESP32 port: DonKracho `ESPHome-component-iPixel-ble`, go-ipxl `packet_builder.go`, ToBiDi0410/iPixel-ESP32.
