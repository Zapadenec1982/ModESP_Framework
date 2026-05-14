# Deployment — flash, monitor, factory reset

> 📖 **In English:** [documentation/en/04-hardware/deployment.md](../../en/04-hardware/deployment.md)

Ця сторінка покриває workflow для getting firmware **на** physical
ESP32 device, live console workflow, І як recover від
broken configuration. Для over-the-air updates already-deployed
devices, див. **[ota.md](ota.md)**.

REQUIRES: ESP-IDF v5.x toolchain. Див. **[../01-getting-started/installation.md](../01-getting-started/installation.md)**.

## Build → flash → monitor

```
idf.py build
idf.py -p COM4 flash
idf.py -p COM4 monitor
```

(`COM4` на Windows; `/dev/ttyUSB0` на Linux; `/dev/cu.usbserial-*`
на macOS.)

`flash` writes three regions:

1. **Bootloader** (`build/bootloader/bootloader.bin`).
2. **Partition table** (`build/partition_table/partition-table.bin`).
3. **App** (`build/<project>.bin`) — у `ota_0`.

LittleFS data partition (`build/data/*.bin`) needs extra step:

```
idf.py build flash_data
```

або як частина full deployment:

```
idf.py build flash flash_data monitor
```

`flash_data` — custom CMake target (див. project root `CMakeLists.txt`)
що builds LittleFS image з `data/` І writes його до `littlefs`
partition.

## Що у data partition

```
data/
├── www/               # static WebUI bundle (HTML/JS/CSS)
├── scenarios/         # *.modr compiled recipes
├── i18n/              # translation packs
└── (other static assets)
```

`generate_ui.py` І `compile_scenario.py` populate parts of це
у build directory; rest checked у git.

## Monitor — live serial console

```
idf.py monitor
```

Shows everything logged через `ESP_LOGI`/`ESP_LOGW`/etc., color-coded
по log level. Ctrl-] щоб exit.

Useful key bindings inside monitor:

- `Ctrl-T Ctrl-X` — exit І reset device.
- `Ctrl-T Ctrl-F` — backtrace decode наступної panic.
- `Ctrl-T Ctrl-Y` — toggle output filters (наприклад hide info-level).

Для long-running deployments use `picocom` І log до file:

```
picocom -b 115200 /dev/ttyUSB0 | tee modesp.log
```

## Factory reset

Two paths.

**Через WebUI:** System → Factory reset → confirm. Issues:

```
POST /api/factory-reset
```

Що це робить:

1. Erases `nvs` partition (`nvs_flash_erase()`).
2. Reboots.

Після reboot всі persisted state defaults (no WiFi credentials, no
saved configuration, default auth). Device enters AP fallback
бо `wifi.ssid` empty.

**З command line:**

```
idf.py -p COM4 erase-flash
idf.py -p COM4 flash flash_data
```

`erase-flash` wipes EVERYTHING (bootloader, partition table, app, NVS,
LittleFS). Use only коли потрібен guaranteed-clean device — наприклад
при changing partition layouts.

## Common workflows

### First flash на fresh device

```
idf.py build
idf.py -p COM4 erase-flash
idf.py -p COM4 flash flash_data monitor
```

Після WiFi setup у AP fallback АБО через initial credentials, device
reachable на local network через `modesp-<id>.local`.

### Updating just data partition (наприклад new WebUI bundle)

```
idf.py build flash_data
```

App keeps running unchanged. New data available immediately
(LittleFS mounted at boot).

### Updating just firmware

```
idf.py build flash monitor
```

Skips data partition. App reboots з new firmware; data preserved.

### Smoke-test cycle

```
idf.py build && idf.py -p COM4 flash flash_data monitor
```

Single line. Кожен component takes fixed time:

- Build (incremental): 5-30 s.
- Flash app: ~5 s.
- Flash data: ~3 s.
- Boot: ~2 s до first WiFi attempt.

Total ~15 s edit-to-running.

### Working з multiple devices на одному host

ESP-IDF picks up `ESPPORT` environment variable. Set per device:

```
$env:ESPPORT="COM4"   # PowerShell
export ESPPORT=/dev/ttyUSB0   # bash
```

Then `idf.py flash` and friends pick port automatically.

## Crash diagnostics

Якщо device gets stuck у boot loop:

1. **Monitor it:** panic dump prints до serial. Note PC І
   backtrace addresses.
2. **Decode backtrace:** `idf.py monitor` does it automatically.
   Якщо only raw log, run:
   ```
   addr2line -e build/<project>.elf 0x40012345 0x40012abc
   ```
3. **Common causes:**
   - Stack overflow у new module — bump `CONFIG_MAIN_TASK_STACK_SIZE`.
   - Bad pointer dereferenced у driver — check `bindings.json` для
     mismatched types.
   - Watchdog timeout — module's `on_update` is blocking. Не block у
     main task; див. **[02-module-author-guide/best-practices.md](../02-module-author-guide/best-practices.md)**.

## Common pitfalls

**Wrong serial port:** double-check `COM*` / `/dev/ttyUSB*` через
Device Manager / `dmesg`. `idf.py -p` overrides будь-який environment.

**Driver missing:** USB-to-serial adapter (CH340, CP210x) needs OS
drivers. Install з manufacturer's site якщо `idf.py` can't open
port.

**Flash too slow / fails:** drop baud rate з `idf.py -b 460800 flash`
(default usually 921600). Cheap USB-serial adapters can't sustain
top speed.

**LittleFS corruption:** якщо WebUI returns 404 після factory reset,
ви forgot `flash_data`. Run it.

**Build cache stale після partition table change:** clean з `idf.py
fullclean` then full rebuild. Partition tables не завжди picked
up by incremental builds.

## Що далі

- **[ota.md](ota.md)** — updating deployed devices.
- **[../01-getting-started/quickstart.md](../01-getting-started/quickstart.md)** —
  end-to-end quickstart що uses ці commands.
- **[../02-module-author-guide/debugging.md](../02-module-author-guide/debugging.md)** —
  module-level debugging з running device.

## Source

- `CMakeLists.txt` у project root — custom `flash_data` target.
- `partitions.csv` — partition layout.
- ESP-IDF docs: [Build System](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html).
