# ModESP — BLE-Observer Sensor Drivers Spec (pvvx Xiaomi · HolyIOT 21011 · BTHome v2)

> Generated from adversarially-verified deep research (workflow `ble-sensors-research`,
> 2026-06-20). Decode formulas confirmed against primary firmware structs + ≥2 independent
> parsers; NimBLE API verifier-corrected against esp-nimble headers + v5.5 `blecent`.
>
> **Target:** ESP32-S3, ESP-IDF v5.5, NimBLE **passive observer** (never connects).
> `hardware_type: "ble"`, observer mode. Decode + dispatch adv payloads into SharedState.
>
> **NimBLE origin convention:** `ble_hs_adv_fields.svc_data_uuid16` points at the first UUID
> byte → **byte 0–1 = 16-bit UUID (LE), byte 2 = start of payload.**

---

## 1. Xiaomi LYWSD03MMC (pvvx / ATC custom firmware)
Dispatch on **service UUID + service-data length** (firmware emits several formats; you cannot
assume which). `0x181A` → pvvx-custom or ATC1441 (disambiguate by length); `0xFCD2` → BTHome (§3).
Device name `ATC_AABBCC` (last 3 MAC bytes). MAC embedded in 0x181A payload.

Length dispatch on `svc_data_uuid16_len` (incl. 2 UUID bytes) — **Likely, confirm by sniff**
(pvvx trims formats across versions, issue #727): `17`→pvvx-custom, `15`→ATC1441, `13`→ATC1441-enc.

### 1b. pvvx-custom (UUID 0x181A, LITTLE-ENDIAN) — **Confirmed**
| Field | Offset | Type | Decode |
|---|---|---|---|
| MAC | 2–7 | 6×u8 LE (reversed) | display = `data[7-i]` |
| temperature | 8–9 | s16 LE | `°C = int16_LE / 100` |
| humidity | 10–11 | u16 LE | `% = uint16_LE / 100` |
| battery_mV | 12–13 | u16 LE | `mV` |
| battery_% | 14 | u8 | 0..100 |
| counter | 15 | u8 | dedup |
| flags | 16 | u8 | GPIO trigger |

### 1c. ATC1441 (UUID 0x181A, BIG-ENDIAN numerics) — **Confirmed**
| Field | Offset | Type | Decode |
|---|---|---|---|
| MAC | 2–7 | 6×u8 BE | display order as-is |
| temperature | 8–9 | s16 **BE** | `°C = int16_BE / 10` |
| humidity | 10 | u8 | `%` |
| battery_% | 11 | u8 | 0..100 |
| battery_mV | 12–13 | u16 **BE** | `mV` |
| counter | 14 | u8 | (verifier fix: @14, not @15) |

### 1d. ATC1441 encrypted compact (len==13) — **Likely**
`temp_C=dec[0]/2-40`, `hum=dec[1]/2`, `batt%=dec[2]&0x7F`. AES-CCM, 16-byte bindkey first.
Only implement if fleet uses encryption; otherwise just don't misparse a 13-byte frame as §1c.

---

## 2. HolyIOT 21011 / YJ-21011 — ⚠️ STOCK = PRESENCE ONLY
**Confirmed:** stock firmware (nRF52810 + LIS2DH12) broadcasts **NO temp/humidity and no raw
accel** — the accelerometer is only a motion trigger for advertising. Stock = **presence + RSSI**.
To get sensor data you must **reflash to BTHome v2** (then handled by §3).

### 2a. Identify (stock) — **Confirmed (from manuals)**
- Local Name (0x09): `"holyiot"` (lowercase).
- Factory iBeacon UUID `FDA50693-A4E2-4FB1-AFCF-C6EB07647825`, **Major 10032** — strong fingerprint.
- MAC = random-static (top 2 bits `0b11`); **no OUI** — do not filter by OUI.
- Filter: `name=="holyiot"` AND (that UUID OR iBeacon prefix `02 15`) AND random-static addr.
- iBeacon layout (mfg-data after `1A FF`): company 0–1 LE (`4C 00` or `FF FF`), `02 15` @2–3,
  UUID @4–19, Major @20–21 **BE**, Minor @22–23 BE, TX@1m @24 s8. → emit presence + RSSI only.

### 2c. Reflashed → BTHome v2 (§3). Accelerometer → BTHome motion `0x21`. Recommended path for
actual sensing. Which objects appear = Must-sniff (depends on flashed build).

---

## 3. Generic BTHome v2 — **Confirmed (spec + ble_monitor)**
- Service Data UUID **`0xFCD2`** (on-air `D2 FC`) = definitive marker. **No MAC in payload** →
  identity = BLE source address (bind device→driver by MAC in config).
- Layout: `[0..1]=D2 FC`, `[2]=device-info`, `[3..]=ascending object-id TLVs`.
- device-info: `enc=b&1`, `version=(b>>5)&7` (==2), `trigger=(b>>2)&1`. `0x40`=v2/clear/regular,
  `0x44`=trigger, `0x41`=encrypted.

### Object table (decode = signed/unsigned LE int × factor) — **Confirmed**
| ID | Property | Type | Signed | Factor | Unit |
|---|---|---|---|---|---|
| 0x00 | packet_id | u8 | no | 1 | — |
| 0x01 | battery | u8 | no | 1 | % |
| 0x02 | temperature | s16 | **yes** | 0.01 | °C |
| 0x03 | humidity | u16 | no | 0.01 | % |
| 0x0C | voltage | u16 | no | 0.001 | V |
| 0x21 | motion | u8 | no | — | 0/1 |
| 0x3A | button | u8 | no | — | event code |
| 0x04 | pressure | u24 | no | 0.01 | hPa |

Parser: read id → fixed length by id → value → repeat. **Cache last-known per device** (pvvx may
alternate temp/hum vs batt/voltage frames — don't clear a channel just because absent this frame).
Encrypted (bit0=1): tail = `[counter:4 LE][MIC:4]`, AES-128-CCM, 16-byte bindkey,
`nonce = MAC ‖ D2 FC ‖ devinfo ‖ counter`; enforce strictly-increasing counter.

---

## 4. NimBLE passive scan (ESP-IDF v5.5, S3) — verifier-corrected
> Corrections: `ble_hs_adv_find_field` **does NOT exist** (use `ble_hs_adv_parse_fields`);
> `BLE_HS_ADV_FILTER_POLICY_USE_WL` **does NOT exist** (use literal `1`); `data` is **non-const
> `uint8_t*`**; fields point into the controller buffer — **copy out within the callback**
> (esp-idf #13814).

```c
struct ble_gap_disc_params dp = {0};
dp.passive = 1;            // observer, no SCAN_REQ
dp.filter_duplicates = 0;  // payloads change per-adv (temp/counter) — want every report
dp.itvl = 160;             // 160 * 0.625ms = 100 ms
dp.window = 48;            //  48 * 0.625ms =  30 ms  (30% duty for Wi-Fi coex)
dp.filter_policy = 0;
ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &dp, gap_event_cb, NULL);
```
GAP cb: `if (event->type==BLE_GAP_EVENT_DISC)` → `ble_hs_adv_parse_fields(&fields, d->data, d->length_data)`
→ if `fields.svc_data_uuid16` (uuid = `[0]|[1]<<8`): dispatch `0x181A`/`0xFCD2`; if `fields.mfg_data`:
dispatch `0xFF` (HolyIOT iBeacon). `d->addr.val[6]` LE; `d->rssi` int8 (127=unavailable).
AD types: `BLE_HS_ADV_TYPE_SVC_DATA_UUID16=0x16`, `BLE_HS_ADV_TYPE_MFG_DATA=0xff`.

**Coex duty cycle:** `window < itvl` so radio frees for Wi-Fi between windows. Start 30 ms/100 ms
(30%). Avoid `window==itvl` while Wi-Fi active. (Engineering guidance, NOT Espressif-published —
tune under real load.) Filtering by UUID/MAC is software-side; optional HW allow-list via
`ble_gap_wl_set` + `filter_policy=1`.

---

## 5. modesp BLE-central manager → driver dispatch → SharedState
1. **One central manager** owns the NimBLE host + single passive scan. GAP cb runs in NimBLE host
   task → **zero-heap** (no std::string/new/malloc); copy ≤31-byte payload to stack
   `etl::array<uint8_t,31>` (don't retain controller buffer).
2. **Registry keyed by MAC** via `bindings.json`: 6-byte addr → driver instance + format hint
   (`pvvx`/`atc1441`/`bthome`/`holyiot_ibeacon`) + optional bindkey. Unknown addrs dropped early.
3. **Dispatch** `(addr, rssi, uuid, payload, len)` → driver `on_advertisement()`: 0x181A→pvvx/ATC,
   0xFCD2→BTHome TLV (+ per-device cache), 0xFF iBeacon→presence/RSSI. Decode into `etl` types.
4. **Write SharedState**: `sensor.<device>.temperature/.humidity/.battery_pct/.battery_mv/.rssi/`
   `.motion/.last_seen_ms`. Mark stale after N missed intervals (don't clear mid-alternation).
5. **DataLogger/UI** unchanged: driver `manifest.json` declares `loggable.channels`/`.events`;
   generator wires registration. No generated files edited.
6. **Threading:** if SharedState writes can block, hand off via `etl::queue`/FreeRTOS queue to a
   modesp worker task; trivial decode can stay inline in the GAP cb.

Drivers: `drivers/ble_xiaomi_th` (pvvx/ATC), `drivers/ble_holyiot` (iBeacon presence + BTHome if
reflashed), generic `drivers/ble_bthome` (any 0xFCD2 device).
