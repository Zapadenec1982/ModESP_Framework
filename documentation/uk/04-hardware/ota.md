# OTA — оновлення прошивки через ефір

> 📖 **In English:** [documentation/en/04-hardware/ota.md](../../en/04-hardware/ota.md)

ModESP v4 підтримує unattended over-the-air firmware updates з automatic
rollback якщо new image fails to come up. Built на ESP-IDF
`esp_https_ota` І dual-app partition scheme.

REQUIRES: 4 MB+ flash, partitions configured для OTA (default `partitions.csv`
includes both app АND data partitions).

## Partition layout

```
Name        Type    SubType   Offset    Size
nvs         data    nvs       0x9000    0x6000     (24 KB)
otadata     data    ota       0xF000    0x2000     (8 KB)
phy_init    data    phy       0x11000   0x1000     (4 KB)
ota_0       app     ota_0     0x20000   0x1E0000   (1.875 MB)
ota_1       app     ota_1     0x200000  0x1E0000   (1.875 MB)
littlefs    data    spiffs    0x3E0000  0x20000    (128 KB)
```

Два `ota_*` слоти = два app images. `otadata` tracks який slot active.
На boot bootloader reads `otadata` І jumps до selected slot. Якщо
running app aborts перед `esp_ota_mark_app_valid`, bootloader falls
back до іншого slot на next boot.

## Upload flow

WebUI System → Firmware → Update АБО HTTP:

```
POST /api/ota
Content-Type: multipart/form-data
Content-Length: 1900000

<binary firmware image>
```

Що відбувається:

1. HTTP handler opens inactive partition (`esp_ota_begin`).
2. Streams chunks straight з request body до flash, validating
   header (magic + chip type).
3. На `EOF` finalizes write (`esp_ota_end`).
4. Marks new partition як pending boot (`esp_ota_set_boot_partition`).
5. Returns `{"ok":true, "next": "ota_1", "size": 1879040}`.
6. WebUI prompts user confirm reboot.

New image **pending** — ще не validated. На reboot:

1. Bootloader sees pending І boots з нового slot.
2. App initializes; `App::on_start` (після successful manager init)
   calls `esp_ota_mark_app_valid_cancel_rollback()`.
3. Якщо app reached цей call, new image стає **valid**;
   otherwise bootloader sees no validation marker on next reboot
   І falls back до previous slot automatically.

## Confirm і rollback

```
POST /api/ota/confirm   # explicit user-driven valid mark
POST /api/ota/rollback  # boot OTHER partition immediately
```

`confirm` — no-op якщо app already self-validated (default).
Useful якщо хочете gated rollouts: ship з validation flag що
defaults to false, force user click "Confirm" у WebUI.

`rollback` swaps boot partition І reboots. Use коли new
firmware comes up technically successfully але exhibits bugs які
self-validation can't detect.

## State keys

| Key | Notes |
|---|---|
| `ota.active_partition` | `"ota_0"` або `"ota_1"`. |
| `ota.next_partition` | Де pending image is staged. |
| `ota.app_version` | З `CONFIG_APP_PROJECT_VER` (Kconfig). |
| `ota.idf_version` | ESP-IDF version baked in. |
| `ota.last_update_unix` | Коли last successful update completed. |

## HTTP endpoint reference

| Method + path | Body | Purpose |
|---|---|---|
| `GET /api/ota` | — | Returns active/next partition info і versions. |
| `POST /api/ota` | binary | Stream firmware. Returns size і target slot. |
| `POST /api/ota/confirm` | — | Mark pending firmware valid (cancel rollback). |
| `POST /api/ota/rollback` | — | Boot previous partition; immediate reboot. |

Auth: HTTP Basic, same admin credentials як rest API.

## Security

- HTTP Basic Auth — only built-in gate. **Не expose Port 80
  до Internet** без putting OTA за VPN, firewall, або
  reverse proxy з additional auth.
- Optional: enable secure boot (`CONFIG_SECURE_BOOT`) І flash
  encryption у ESP-IDF Kconfig. Це додає cryptographic integrity на
  bootloader level. Mandatory у production де device physically
  accessible.
- Optional: signed firmware images. Configure через `signing` Kconfig
  options. Bootloader rejects images без valid signature.

## Memory і performance

- HTTP server holds ~4 KB buffer для streaming chunks; flashing
  uses NO additional heap.
- Flash write speed: ~150 KB/s на typical ESP32-S3 (limited by SPI
  flash erase time, не WiFi).
- Total update time для ~1.8 MB firmware: ~12-15 seconds.

## Common pitfalls

**Partial upload → corruption:** HTTP handler discards будь-який incomplete
upload; inactive partition left untouched (aborted
`esp_ota_begin/end` cycle ніколи marks new slot як boot target).
Retry upload.

**WiFi loss mid-update:** якщо user browser disconnects, HTTP
request aborts на device side. Same як partial upload — clean
state. Retry.

**Self-validation never fires:** якщо ваші modifications break перед
validation hook (наприклад assertion у Phase 1 init), new firmware
boots, crashes, І bootloader rolls back at next attempt. Use
serial console щоб see crash output.

**Pending boot у production:** ніколи не deploy з automatic rollback
disabled. Always allow at least 1 reboot cycle pass перед declaring
new firmware "stable".

**Sketchy networks:** OTA через busy public WiFi може time out. Run
update з stable LAN connection.

## Що далі

- **[deployment.md](deployment.md)** — initial flash, factory reset,
  monitor workflow.
- **[03-framework-reference/components/modesp_net.md](../03-framework-reference/components/modesp_net.md)** —
  HTTP handlers що back OTA endpoints.

## Source

- [`components/modesp_net/src/http_service.cpp`](../../../components/modesp_net/src/http_service.cpp) —
  OTA handlers around `/api/ota*` paths.
- `partitions.csv` у project root.
