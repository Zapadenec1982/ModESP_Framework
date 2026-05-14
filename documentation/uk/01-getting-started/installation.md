# Installation — toolchain і first build

> 📖 **In English:** [documentation/en/01-getting-started/installation.md](../../en/01-getting-started/installation.md)

Ця сторінка покриває setting up ESP-IDF toolchain, cloning repo,
І producing ваш first firmware binary. Після цього jump до
**[quickstart](quickstart.md)** щоб flash її.

Time budget: **20-40 minutes** depending на whether ESP-IDF already
installed.

## Prerequisites

- ESP32 device з USB-serial adapter (більшість dev boards мають це
  built у). Recommended: **ESP32-S3** з 4 MB+ flash. ESP32-WROOM works.
- USB cable з data lines (НЕ charge-only cable — common mistake).
- Operating system: Windows 10+, Linux, АБО macOS.
- **8 GB free disk space** (ESP-IDF + toolchain).
- Python 3.8+ (ESP-IDF installs власний).

## Step 1 — Install ESP-IDF

Фреймворк targets ESP-IDF **v5.x** (any 5.x.y; tested на 5.1, 5.2, 5.3).

**Windows (PowerShell, recommended):**

Use official installer: <https://dl.espressif.com/dl/esp-idf/>.
Він bundles Python, Git, і toolchain у one MSI. Pick
"Online installer" і choose v5.x.

Після install, відкрий "ESP-IDF 5.x PowerShell" з Start Menu — це
pre-configured shell з `$env:IDF_PATH` set.

**Linux / macOS:**

```bash
mkdir -p ~/esp && cd ~/esp
git clone --branch release/v5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32,esp32s3
```

Add activation step до shell rc:

```bash
echo '. $HOME/esp/esp-idf/export.sh' >> ~/.bashrc
```

Open new terminal — `idf.py --version` should work.

## Step 2 — Clone framework

```
git clone https://github.com/<your-org>/modesp-v4.git
cd modesp-v4
```

Repo contains:

- `components/` — framework libraries (modesp_*).
- `modules/` — business modules (recipe І non-recipe).
- `drivers/` — hardware drivers.
- `main/` — `main.cpp` з module wiring.
- `tools/` — Python build-time generators.
- `data/www/` — pre-built WebUI bundle.

## Step 3 — Install Python tooling

Build calls кілька Python tools (`generate_ui.py`, `compile_scenario.py`).
Вони use packages з `tools/requirements.txt`:

```
pip install -r tools/requirements.txt
```

(Use той Python який ESP-IDF activated. На Windows ESP-IDF
PowerShell pins it; на Linux/macOS activation script does.)

## Step 4 — First build

З repo root:

```
idf.py set-target esp32s3    # або esp32, esp32c3 — match ваше hardware
idf.py build
```

Це:

1. Generates UI / state metadata / MQTT topics з manifests.
2. Compiles всі `.modr` recipes.
3. Compiles entire project.
4. Builds LittleFS data image.

Total first-build time: **3-8 minutes** depending на CPU. Subsequent
incremental builds: **5-30 seconds**.

Expected output ends з:

```
Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p PORT flash
or
 python -m esptool ... write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x10000 build/<project>.bin 0x8000 build/partition_table/partition-table.bin
```

Якщо це бачите — toolchain healthy. Move до **[quickstart.md](quickstart.md)**.

## Step 5 — Editor setup (optional)

**VS Code:** install official **Espressif IDF** extension.
Configure: Command Palette → "ESP-IDF: Configure ESP-IDF Extension".
Extension wires IntelliSense, debugger, flash, І monitor
commands.

**CLion / other:** point CMake на project. ESP-IDF generates
`compile_commands.json` який більшість C/C++ editors recognize.

## Troubleshooting

**`idf.py: command not found`** — IDF environment not activated.
Run `~/esp/esp-idf/export.sh` АБО open ESP-IDF PowerShell.

**`Python is not installed correctly`** — typically WSL/MSYS Python
conflicts з ESP-IDF. Use ESP-IDF-bundled Python; close any
other terminal.

**Build hangs at "Building ETL..."** — `marcel-cd__etlcpp` 1.0.1 has
known `externalproject_add` bug. Fix: edit
`managed_components/marcel-cd__etlcpp/CMakeLists.txt` І comment out
`externalproject_add` block. (Див. engine rebuild changelog для context.)

**`fatal: could not read Username`** — Git asking for credentials
щоб fetch submodules. Configure GitHub credential helper або use
SSH URLs у `.gitmodules`.

**Disk space exhausted mid-build:** clean з `idf.py fullclean` І
re-check `build/` size перед retrying. Build artefacts run ~2 GB.

## Що далі

- **[quickstart.md](quickstart.md)** — flash І run reference scenario.
- **[concepts.md](concepts.md)** — 4 core mental models перед написанням
  першого module.
- **[02-module-author-guide/overview.md](../02-module-author-guide/overview.md)** —
  start writing.

## Source

- ESP-IDF docs: [Get Started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html).
- `tools/requirements.txt` у framework root.
