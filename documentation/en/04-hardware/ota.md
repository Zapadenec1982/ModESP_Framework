# OTA — over-the-air firmware update

> 📖 **Українською:** [documentation/uk/04-hardware/ota.md](../../uk/04-hardware/ota.md)

ModESP v4 supports unattended over-the-air firmware updates із automatic
rollback if the new image fails to come up. Built on ESP-IDF's
`esp_https_ota` AND dual-app partition scheme.

REQUIRES: 4 MB+ flash, partitions configured for OTA (default `partitions.csv`
includes both AND data partitions).

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

Two `ota_*` slots = two app images. `otadata` tracks which slot is
active. On boot the bootloader reads `otadata` AND jumps to the
selected slot. If the running app aborts before `esp_ota_mark_app_valid`,
the bootloader falls back to the other slot on next boot.

## Upload flow

WebUI System → Firmware → Update OR HTTP:

```
POST /api/ota
Content-Type: multipart/form-data
Content-Length: 1900000

<binary firmware image>
```

What happens:

1. HTTP handler opens the inactive partition (`esp_ota_begin`).
2. Streams chunks straight from the request body to flash, validating
   header (magic + chip type).
3. On `EOF`, finalizes the write (`esp_ota_end`).
4. Marks the new partition as pending boot (`esp_ota_set_boot_partition`).
5. Returns `{"ok":true, "next": "ota_1", "size": 1879040}`.
6. WebUI prompts user to confirm reboot.

The new image is **pending** — not yet validated. On reboot:

1. Bootloader sees pending AND boots from new slot.
2. App initializes; `App::on_start` (after successful manager init)
   calls `esp_ota_mark_app_valid_cancel_rollback()`.
3. If the app reached this call, the new image becomes **valid**;
   otherwise the bootloader sees no validation marker on next reboot
   AND falls back to the previous slot automatically.

## Confirm and rollback

```
POST /api/ota/confirm   # explicit user-driven valid mark
POST /api/ota/rollback  # boot the OTHER partition immediately
```

`confirm` is а no-op if the app already self-validated (the default).
Useful if you want gated rollouts: ship із а validation flag that
defaults to false, force the user to click "Confirm" у WebUI.

`rollback` swaps the boot partition AND reboots. Use when а new
firmware comes up technically successfully but exhibits bugs that
self-validation can't detect.

## State keys

| Key | Notes |
|---|---|
| `ota.active_partition` | `"ota_0"` or `"ota_1"`. |
| `ota.next_partition` | Where а pending image is staged. |
| `ota.app_version` | From `CONFIG_APP_PROJECT_VER` (Kconfig). |
| `ota.idf_version` | ESP-IDF version baked у. |
| `ota.last_update_unix` | When last successful update completed. |

## HTTP endpoint reference

| Method + path | Body | Purpose |
|---|---|---|
| `GET /api/ota` | — | Returns active/next partition info AND versions. |
| `POST /api/ota` | binary | Stream firmware. Returns size AND target slot. |
| `POST /api/ota/confirm` | — | Mark pending firmware valid (cancel rollback). |
| `POST /api/ota/rollback` | — | Boot previous partition; immediate reboot. |

Auth: HTTP Basic, same admin credentials as the rest of the API.

## Security

- HTTP Basic Auth is the only built-in gate. **Don't expose Port 80
  to the Internet** without putting OTA behind а VPN, firewall, or
  reverse proxy із additional auth.
- Optional: enable secure boot (`CONFIG_SECURE_BOOT`) AND flash
  encryption у ESP-IDF Kconfig. These add cryptographic integrity at
  the bootloader level. Mandatory у production where the device is
  physically accessible.
- Optional: signed firmware images. Configure via `signing` Kconfig
  options. The bootloader rejects images без а valid signature.

## Memory і performance

- HTTP server holds а ~4 KB buffer для streaming chunks; flashing
  uses NO additional heap.
- Flash write speed: ~150 KB/s on typical ESP32-S3 (limited by SPI
  flash erase time, not WiFi).
- Total update time for ~1.8 MB firmware: ~12-15 seconds.

## Common pitfalls

**Partial upload → corruption:** HTTP handler discards any incomplete
upload; the inactive partition is left untouched (an aborted
`esp_ota_begin/end` cycle never marks the new slot as boot target).
Retry the upload.

**WiFi loss mid-update:** if the user's browser disconnects, HTTP
request aborts on the device side. Same as partial upload — clean
state. Retry.

**Self-validation never fires:** if your modifications break before
the validation hook (е.g. assertion у Phase 1 init), the new firmware
boots, crashes, AND the bootloader rolls back on next attempt. Use
the serial console to see the crash output.

**Pending boot in production:** never deploy із automatic rollback
disabled. Always allow at least 1 reboot cycle to pass before declaring
а new firmware "stable".

**Sketchy networks:** OTA over а busy public WiFi може time out. Run
the update із а stable LAN connection.

## Next steps

- **[deployment.md](deployment.md)** — initial flash, factory reset,
  monitor workflow.
- **[03-framework-reference/components/modesp_net.md](../03-framework-reference/components/modesp_net.md)** —
  HTTP handlers що back the OTA endpoints.

## Source

- [`components/modesp_net/src/http_service.cpp`](../../../components/modesp_net/src/http_service.cpp) —
  OTA handlers around the `/api/ota*` paths.
- `partitions.csv` у the project root.
