# Deployment — flash, monitor, factory reset

> 📖 **Українською:** [documentation/uk/04-hardware/deployment.md](../../uk/04-hardware/deployment.md)

This page covers the workflow for getting firmware **onto** а physical
ESP32 device, the live console workflow, AND how to recover from а
broken configuration. For over-the-air updates of already-deployed
devices, see **[ota.md](ota.md)**.

REQUIRES: ESP-IDF v5.x toolchain. See **[../01-getting-started/installation.md](../01-getting-started/installation.md)**.

## Build → flash → monitor

```
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor
```

(`COM4` on Windows; `/dev/ttyUSB0` on Linux; `/dev/cu.usbserial-*`
on macOS.)

`flash` writes three regions:

1. **Bootloader** (`build/bootloader/bootloader.bin`).
2. **Partition table** (`build/partition_table/partition-table.bin`).
3. **App** (`build/<project>.bin`) — into `ota_0`.

The LittleFS data partition (`build/data/*.bin`) needs an extra step:

```
idf.py build flash_data
```

or as part of full deployment:

```
idf.py build flash flash_data monitor
```

`flash_data` is а custom CMake target (see project root `CMakeLists.txt`)
that builds the LittleFS image із `data/` AND writes it to the `littlefs`
partition.

## What's у the data partition

```
data/
├── www/               # static WebUI bundle (HTML/JS/CSS)
├── scenarios/         # *.modr compiled recipes
├── i18n/              # translation packs
└── (other static assets)
```

`generate_ui.py` AND `compile_scenario.py` populate parts of это
у the build directory; the rest is checked into git.

## Monitor — live serial console

```
idf.py monitor
```

Shows everything logged via `ESP_LOGI`/`ESP_LOGW`/etc., color-coded по
log level. Ctrl-] to exit.

Useful key bindings inside monitor:

- `Ctrl-T Ctrl-X` — exit AND reset device.
- `Ctrl-T Ctrl-F` — backtrace decode of the next panic.
- `Ctrl-T Ctrl-Y` — toggle output filters (е.g. hide info-level).

For long-running deployments, use `picocom` AND log to file:

```
picocom -b 115200 /dev/ttyUSB0 | tee modesp.log
```

## Factory reset

Two paths.

**From WebUI:** System → Factory reset → confirm. Issues:

```
POST /api/factory-reset
```

What it does:

1. Erases the `nvs` partition (`nvs_flash_erase()`).
2. Reboots.

After reboot, all persisted state defaults (no WiFi credentials, no
saved configuration, default auth). The device enters AP fallback
because `wifi.ssid` is empty.

**From command line:**

```
idf.py -p COM4 erase-flash
idf.py -p COM4 flash flash_data
```

`erase-flash` wipes EVERYTHING (bootloader, partition table, app, NVS,
LittleFS). Use only when you need а guaranteed-clean device — for
example, when changing partition layouts.

## Common workflows

### First flash on а fresh device

```
idf.py build
idf.py -p COM4 erase-flash
idf.py -p COM4 flash flash_data monitor
```

After WiFi setup у the AP fallback OR via initial credentials, the
device is reachable on the local network through `modesp-<id>.local`.

### Updating just the data partition (е.g. new WebUI bundle)

```
idf.py build flash_data
```

App keeps running unchanged. The new data is available immediately
(LittleFS is mounted at boot).

### Updating just the firmware

```
idf.py build flash monitor
```

Skips data partition. App reboots із the new firmware; data is
preserved.

### Smoke-test cycle

```
idf.py build && idf.py -p COM4 flash flash_data monitor
```

Single line. Each component takes а fixed time:

- Build (incremental): 5-30 s.
- Flash app: ~5 s.
- Flash data: ~3 s.
- Boot: ~2 s до first WiFi attempt.

Total ~15 s edit-to-running.

### Working із multiple devices on one host

ESP-IDF picks up `ESPPORT` environment variable. Set it per device:

```
$env:ESPPORT="COM4"   # PowerShell
export ESPPORT=/dev/ttyUSB0   # bash
```

Then `idf.py flash` and friends pick the port automatically.

## Crash diagnostics

If the device gets stuck у а boot loop:

1. **Monitor it:** the panic dump prints to serial. Note the PC AND
   backtrace addresses.
2. **Decode the backtrace:** `idf.py monitor` does it automatically.
   If you only have the raw log, run:
   ```
   addr2line -e build/<project>.elf 0x40012345 0x40012abc
   ```
3. **Common causes:**
   - Stack overflow у а new module — bump `CONFIG_MAIN_TASK_STACK_SIZE`.
   - Bad pointer dereferenced у a driver — check `bindings.json` for
     mismatched types.
   - Watchdog timeout — а module's `on_update` is blocking. Don't
     block у the main task; see **[02-module-author-guide/best-practices.md](../02-module-author-guide/best-practices.md)**.

## Common pitfalls

**Wrong serial port:** double-check `COM*` / `/dev/ttyUSB*` із
Device Manager / `dmesg`. `idf.py -p` overrides any environment.

**Driver missing:** USB-to-serial adapter (CH340, CP210x) needs OS
drivers. Install from manufacturer's site if `idf.py` can't open the
port.

**Flash too slow / fails:** drop baud rate із `idf.py -b 460800 flash`
(default usually 921600). Cheap USB-serial adapters can't sustain
top speed.

**LittleFS corruption:** if WebUI returns 404 after factory reset, you
forgot `flash_data`. Run it.

**Build cache stale after partition table change:** clean із `idf.py
fullclean` then full rebuild. Partition tables aren't always picked
up by incremental builds.

## Next steps

- **[ota.md](ota.md)** — updating deployed devices.
- **[../01-getting-started/quickstart.md](../01-getting-started/quickstart.md)** —
  end-to-end quickstart that uses these commands.
- **[../02-module-author-guide/debugging.md](../02-module-author-guide/debugging.md)** —
  module-level debugging із the running device.

## Source

- `CMakeLists.txt` у project root — custom `flash_data` target.
- `partitions.csv` — partition layout.
- ESP-IDF docs: [Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html).
