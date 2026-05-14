# Installation — toolchain і first build

> 📖 **Українською:** [documentation/uk/01-getting-started/installation.md](../../uk/01-getting-started/installation.md)

This page covers setting up the ESP-IDF toolchain, cloning the repo,
AND producing your first firmware binary. После this, jump to the
**[quickstart](quickstart.md)** to flash it.

Time budget: **20-40 minutes** depending на whether ESP-IDF is already
installed.

## Prerequisites

- An ESP32 device із USB-serial adapter (most dev boards have це built у).
  Recommended: **ESP32-S3** із 4 MB+ flash. ESP32-WROOM works.
- USB cable із data lines (NOT а charge-only cable — common mistake).
- Operating system: Windows 10+, Linux, OR macOS.
- **8 GB free disk space** (ESP-IDF + toolchain).
- Python 3.8+ (ESP-IDF installs its own).

## Step 1 — Install ESP-IDF

The framework targets ESP-IDF **v5.x** (any 5.x.y; tested on 5.1, 5.2, 5.3).

**Windows (PowerShell, recommended):**

Use the official installer: <https://dl.espressif.com/dl/esp-idf/>.
It bundles Python, Git, AND the toolchain into one MSI. Pick the
"Online installer" AND choose v5.x.

After install, открой "ESP-IDF 5.x PowerShell" from Start Menu — це
is а pre-configured shell із `$env:IDF_PATH` set.

**Linux / macOS:**

```bash
mkdir -p ~/esp && cd ~/esp
git clone --branch release/v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32,esp32s3
```

Add the activation step to your shell rc:

```bash
echo '. $HOME/esp/esp-idf/export.sh' >> ~/.bashrc
```

Open а new terminal — `idf.py --version` should work.

## Step 2 — Clone the framework

```
git clone https://github.com/<your-org>/modesp-v4.git
cd modesp-v4
```

The repo contains:

- `components/` — framework libraries (modesp_*).
- `modules/` — business modules (recipe AND non-recipe).
- `drivers/` — hardware drivers.
- `main/` — `main.cpp` із module wiring.
- `tools/` — Python build-time generators.
- `data/www/` — pre-built WebUI bundle.

## Step 3 — Install Python tooling

The build calls а few Python tools (`generate_ui.py`, `compile_scenario.py`).
Они use packages from `tools/requirements.txt`:

```
pip install -r tools/requirements.txt
```

(Use the Python that ESP-IDF activated. On Windows the ESP-IDF
PowerShell pins it; on Linux/macOS the activation script does.)

## Step 4 — First build

From the repo root:

```
idf.py set-target esp32s3    # or esp32, esp32c3 — match your hardware
idf.py build
```

This:

1. Generates UI / state metadata / MQTT topics from manifests.
2. Compiles all `.modr` recipes.
3. Compiles the entire project.
4. Builds the LittleFS data image.

Total first-build time: **3-8 minutes** depending on CPU. Subsequent
incremental builds: **5-30 seconds**.

Expected output ends із:

```
Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool ... write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x10000 build/<project>.bin 0x8000 build/partition_table/partition-table.bin
```

If you see це, the toolchain is healthy. Move to **[quickstart.md](quickstart.md)**.

## Step 5 — Editor setup (optional)

**VS Code:** install the official **Espressif IDF** extension.
Configure: Command Palette → "ESP-IDF: Configure ESP-IDF Extension".
The extension wires IntelliSense, debugger, flash, AND monitor
commands.

**CLion / other:** point CMake at the project. ESP-IDF generates а
`compile_commands.json` що most C/C++ editors recognize.

## Troubleshooting

**`idf.py: command not found`** — IDF environment not activated.
Run `~/esp/esp-idf/export.sh` OR open ESP-IDF PowerShell.

**`Python is not installed correctly`** — typically WSL/MSYS Python
conflicts із ESP-IDF's. Use the ESP-IDF-bundled Python; close any
other terminal.

**Build hangs at "Building ETL..."** — `marcel-cd__etlcpp` 1.0.1 has а
known `externalproject_add` bug. Fix: edit
`managed_components/marcel-cd__etlcpp/CMakeLists.txt` AND comment out
the `externalproject_add` block. (See engine rebuild changelog for context.)

**`fatal: could not read Username`** — Git is asking for credentials
to fetch submodules. Configure а GitHub credential helper або use
SSH URLs у `.gitmodules`.

**Disk space exhausted mid-build:** clean із `idf.py fullclean` AND
re-check `build/` size before retrying. Build artefacts run ~2 GB.

## Next steps

- **[quickstart.md](quickstart.md)** — flash AND run the reference scenario.
- **[concepts.md](concepts.md)** — 4 core mental models before you write
  your first module.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)** —
  start writing.

## Source

- ESP-IDF docs: [Get Started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html).
- `tools/requirements.txt` у the framework root.
