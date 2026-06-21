# ble_led_panel — iPixel Color / LED_BLE 16×64 protocol + NimBLE connect (impl bible)

> From workflow `ipixel-protocol-from-working-code` (2026-06-21), derived from WORKING OSS
> (ToBiDi0410/iPixel-ESP32, yyewolf/go-ipxl, derkalle4, Pupariaa/Bk-Light-AppBypass) + verified
> against esp-nimble v5.5. **No btsnoop needed; the panel itself validates.**

## Transport (Confirmed-by-working-code)
- **Service `0x00FA`** = `000000fa-0000-1000-8000-00805f9b34fb` — **NOTE: 0x00FA, NOT 0xFA00!**
  (earlier docs/specs that said "0xFA00 family" were wrong about the service value).
- **Write char `0xFA02`** = `0000fa02-...`, **notify char `0xFA03`** = `0000fa03-...`.
  Confirmed by **lucagoc/pypixelcolor** (`lib/constants.py` WRITE_UUID/NOTIFY_UUID) + **DonKracho/ESPHome-component-iPixel-ble**
  (`ipixel_ble.h`: service `000000fa-...`, char `0000fa02-...`, notify `0000fa03-...`).
- **Discovery (HW-confirmed 2026-06-21):** do NOT filter by the service UUID — enumerate **ALL** characteristics
  (`ble_gattc_disc_all_chrs(1..0xffff)`) and match fa02/fa03 by char UUID. (A `disc_svc_by_uuid(0xFA00)` — wrong
  value — returns "service not found"; the service is 0x00FA. Char-wide discovery sidesteps the whole issue.)
- **Writes:** prefer **write-WITH-response** when fa02 advertises the Write property (0x08) — DonKracho's working
  ESP32 client uses `ESP_GATT_WRITE_TYPE_RSP`; fall back to write-no-response.
- Adv name prefix `LED_BLE_` (this panel: `LED_BLE_E6C5EBE2`). All multi-byte fields little-endian.
- Geometry 16×64 = device type 3 (byte `0x83`/131), 64×16, 1024 px.
- Frame shape: `[len u16 LE][opcode][subcmd][...]`.

## Control commands (hex → fa02) — Confirmed
| Cmd | Bytes |
|---|---|
| Screen ON | `05 00 07 01 01` |
| Screen OFF | `05 00 07 01 00` (no hw power-off opcode exists; this + brightness 0 = off; keeps BLE link) |
| Brightness | `05 00 04 80 <pct 5..100>` |
| Clear | `04 00 03 80` |
| Flip | `05 00 06 80 <1|0>` (Likely — derkalle4 only) |
| DIY mode enter | `05 00 04 01 01` |
| Set pixel (DIY) | `0A 00 05 01 00 RR GG BB XX YY` (after DIY enter; 1024 writes for full frame — slow) |
| Set time | `0B 00 01 80 YY MM DD WD HH MN SS` |

## Image upload = **PNG blob** (RESOLVED — not raw RGB888)
15-byte header + PNG file bytes, **write-WITH-response** (ACT1025 stable), MTU 512:
```
[0..1 total_len=png_len+15 u16LE][2 type=0x02][3..4 00 00][5..8 png_len u32LE]
[9..12 crc32(png) u32LE][13..14 buffer 00 65][15.. PNG bytes]
```
- CRC = `esp_rom_crc32_le(0, png, png_len)` (== zlib/PNG CRC-32, emit LE).
- PNG = 64×16 RGB. **Needs on-device PNG encoder (miniz/lodepng) OR host pre-encode.** Keep encode off the BLE hot path.
- Fallback if no render: write-without-response 500B/100ms chunks; vary buffer byte `0x65`→1..9; IDM- devices use no-CRC header.

## ACT1025 handshake (Confirmed) — optional, improves 16×64 reliability
Subscribe fa03 (CCCD `01 00`) first. TX fa02 (no-rsp): `08 00 01 80 HH MM SS 00` → `04 00 05 80` →
wait fa03 stage-1 ACK (`0C 00 01 80 81 06 32 00 00 01 00 01` or 64×16 alt `0B 00 01 80 83 06 32 00 00 01 00`)
→ (opt) `08 00 05 80 0B 03 07 02` → send frame → frame-ACK `05 00 02 00 03`. **Skip entirely if no ACK in 5s
(plain devices need none — iPixel-ESP32 connects with zero init).**

## Scrolling text = native type-4 route (Confirmed, no PNG)
Open (no-rsp, ~60ms apart): `08 00 01 80 HH MM SS 00` / `04 00 05 80` / `05 00 12 80 07` / `07 00 08 80 01 00 <CH>`.
Text packet header like image but data-type `00 01`, route `00 65`; body = per-glyph 8×10 bitmaps **bit-reversed per byte**,
fg/bg RGB, effect code (0 fixed,1 scroll-left,2 right,5 blink,6 breathe). Chunk 509B no-rsp. Rotate host-side for 64×16.

## NimBLE GATT-client connect (ESP-IDF v5.5, verified) — funcs in `host/ble_gatt.h`
Sequence (all in NimBLE host task):
```
sync_cb → ble_hs_id_infer_auto → ble_gap_disc (scan, match name "LED_BLE_")
 → BLE_GAP_EVENT_DISC match → ble_gap_disc_cancel → ble_gap_connect(own,&addr,30000,NULL,cb,NULL)
 → BLE_GAP_EVENT_CONNECT(status 0) → ble_gattc_exchange_mtu(conn,NULL,NULL)
     → ble_gattc_disc_svc_by_uuid(conn,&0xFA00,on_svc)
 → on_svc EDONE → ble_gattc_disc_chrs_by_uuid(conn,start,end,&0xFA02,on_chr_wr)  // store chr->val_handle
     → EDONE → disc_chrs_by_uuid(&0xFA03,on_chr_nt) → EDONE → ble_gattc_disc_all_dscs(conn,fa03,end,on_dsc)
 → on_dsc match 0x2902 → ble_gattc_write_flat(conn,cccd,{01 00},2,cb)  // enable notify, WITH response
 → run handshake → commands
 → BLE_GAP_EVENT_DISCONNECT → rescan
```
Key signatures (verified vs esp-nimble v5.5):
- `int ble_gap_connect(uint8_t own_addr_type, const ble_addr_t *peer, int32_t dur_ms, const struct ble_gap_conn_params*, ble_gap_event_fn*, void*)`
- `int ble_gattc_disc_svc_by_uuid(uint16_t conn, const ble_uuid_t*, ble_gatt_disc_svc_fn*, void*)`
- `int ble_gattc_disc_chrs_by_uuid(uint16_t conn, uint16_t start, uint16_t end, const ble_uuid_t*, ble_gatt_chr_fn*, void*)`
- `int ble_gattc_disc_all_dscs(uint16_t conn, uint16_t start, uint16_t end, ble_gatt_dsc_fn*, void*)`
- `int ble_gattc_write_no_rsp_flat(uint16_t conn, uint16_t attr_handle, const void*, uint16_t len)` — control (fire-and-forget)
- `int ble_gattc_write_flat(uint16_t conn, uint16_t attr_handle, const void*, uint16_t len, ble_gatt_attr_fn*, void*)` — image (with response)
- Result structs: `ble_gatt_svc{start_handle,end_handle,uuid}`, `ble_gatt_chr{def_handle,val_handle,properties,uuid}`,
  `ble_gatt_dsc{handle,uuid}`, `ble_gatt_error{status,att_handle}`. `err->status==BLE_HS_EDONE(14)` = discovery complete.
- Events: CONNECT=0, DISCONNECT=1, NOTIFY_RX=12. `event->notify_rx.{om,attr_handle,conn_handle}`.
- CONCURRENCY: connecting requires pausing the observer scan (ble_gap_disc_cancel) then connect; resume scan after. Plan the scan↔connect interplay (single NimBLE host shared with sensor observer + peripheral advertising).

## On-HW validation (panel is the test)
1. PNG depth/compression the firmware accepts (solid-red 64×16 RGB first).
2. buffer byte 0x65 vs 1..9. 3. handshake needed? (skip if no fa03 ACK). 4. flip cmd. 5. with-vs-without response for image.

---

# PART 3 — Native TEXT (Increment 2) — impl spec, from lucagoc/pypixelcolor (authoritative)

Text is rendered **host/firmware-side to per-char glyph bitmaps** (the panel has no usable built-in font
for arbitrary strings — pypixelcolor rasterizes with PIL+TTF). On ESP32 we embed a bitmap font and emit
glyph blocks — **NO PNG encoder needed** (the user's chosen path). Write to **fa02**, chunked, write-WITH-response.

## 3.1 Text frame (15-byte header + payload)
```
[0..1]  total_length u16 LE = len(payload) + 15
[2..3]  00 01                = text command marker
[4]     has_next            = 0x00 (single/first) ; 0x02 (continuation chunk)
[5..8]  payload_size u32 LE  = len(payload)
[9..12] crc32 u32 LE         = esp_rom_crc32_le(0, payload, len)   (== zlib/binascii.crc32 over payload)
[13]    0x00
[14]    save_slot            = slot id (Bk-Light used 0x65)
[15..]  payload
```

## 3.2 payload
```
[0]      num_chars (1)
[1..13]  properties (13 bytes): [0..2 reserved][3..5 anim/speed/rainbow][6..8 fg RGB][9 bg_enable][10..12 bg RGB]
[14..]   glyph blocks (concatenated, one per char)
```

## 3.3 glyph block (per char) — pypixelcolor encoding.py / image_processing.py
```
[0]      0x02         (char block; 0x09 = emoji)
[1..3]   RGB          (per-char color, 3 bytes)
[4..]    bitmap: rows top→bottom; each row MSB-first (bit15 = LEFTMOST pixel), big-endian.
         Monospace cell, 16 tall. Width 8 (16x8 → 1 byte/row → 16 bytes) or 16 (16x16 → 2 bytes/row → 32 bytes).
         DonKracho confirms the panel supports 16x8 / 16x16 / 32x16 monospace matrices.
```
**OPEN DETAIL (resolve on HW or by reading encoding.py fully):** exact cell size implied by the 0x02 block +
whether glyph width is fixed per type or derived from byte count. Start with **16x8** (8 chars across 64px —
ideal for a temperature readout); if garbage, try 16x16.

## 3.4 Font — REUSE tools/gen_osd_font.py (already MSB-first bit15=left!)
`render_glyph_1bpp()` in gen_osd_font.py renders glyphs as 16-bit words **MSB-first, bit15 = leftmost pixel**
(`w |= 1 << (15 - x)`) — the EXACT bit order the panel + pypixelcolor use (`line_value |= 1 << (15 - x)`).
Add a `--target panel` that emits 16×16 (and/or 16×8) 1bpp glyphs for ASCII 0x20–0x7E + ° into
`generated/panel_font_data.h`. Run e.g. `python tools/gen_osd_font.py --target panel --ttf <monospace.ttf>`.
This removes the only real friction (on-device glyphs) by reusing proven, format-matched code.

## 3.5 Handshake: set_time first (also returns display size)
TX fa02: `08 00 01 80 HH MM SS 00` → 11-byte ACK on fa03 with display model/size (~byte 5). Send once on READY.

## 3.6 On-device flow (Increment 2)
1. (opt) set_time on READY → geometry.
2. Show text: build payload (num_chars + 13B props with fg/bg/effect + glyph blocks from embedded font) →
   prepend 15B header with crc32(payload) → write to fa02 (GATT segments at MTU; with-response).
3. Driver/SharedState hook: a `panel.text` key (or formatted equipment.room_temp) → render + send on change (de-dupe).

## 3.7 Confirmed control commands (cross-checked pypixelcolor set_power.py/set_brightness.py)
Power ON `05 00 07 01 01` / OFF `05 00 07 01 00`; Brightness `05 00 04 80 <0..100>`. UUIDs: **service 0x00FA**
(`000000fa-…`), write `0000fa02`, notify `0000fa03`.
