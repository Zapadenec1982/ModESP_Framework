"""
test_kc868a6.py — board-config + bindings-page tests (KC868-A6 + dev board).

Exercises the still-current board/bindings subsystems against the framework
template's CURRENT modules (whatever project.json lists — equipment, datalogger,
simple_thermo, abs_test, display):

1. PCF8574 driver manifests (pcf8574_relay, pcf8574_input) pass DriverManifestValidator.
2. Generator hardware-type config (VALID_HARDWARE_TYPES, BOARD_SECTION_TO_HW_TYPE)
   maps the I2C-expander sections.
3. Equipment manifest multi-driver roles cross-validate against the real drivers/.
4. boards/kc868a6/board.json + bindings.json — structure + clean validate_bindings.
5. UIJsonGenerator._bindings_page — roles carry hw_types/drivers arrays, and the
   hardware inventory reflects the board (expanders for KC868-A6, GPIO for dev).

Like test_modules.py, the active module set is read from project.json and brittle
totals are computed from the source data; only stable facts are pinned as literals.

NOTE vs the pre-framework version: this file used to load deleted refrigeration
modules (thermostat/defrost/protection) and assert on equipment roles that no
longer exist (compressor/door_contact/evap_*). Those modules and roles are gone.
The current equipment manifest exposes two multi-driver roles — air_temp
(ds18b20/ntc) and actuator_1 (relay/pcf8574_relay) — which carry the same
"multi-driver → multiple hw_types" behaviour the old compressor/air_temp tests
verified, so the intent is preserved against the roles that actually exist.
"""
import json
import sys
from collections import Counter
from pathlib import Path

import pytest

# Setup path for imports
TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import (
    DriverManifestValidator,
    UIJsonGenerator,
    FeatureResolver,
    cross_validate,
    validate_bindings,
    VALID_HARDWARE_TYPES,
    BOARD_SECTION_TO_HW_TYPE,
    load_capabilities,
    _driver_declared_capabilities,
)

PROJECT_ROOT = Path(__file__).parent.parent.parent
DRIVERS_DIR = PROJECT_ROOT / "drivers"
MODULES_DIR = PROJECT_ROOT / "modules"
BOARDS_DIR = PROJECT_ROOT / "boards"


def load_driver_manifest(name):
    with open(DRIVERS_DIR / name / "manifest.json", "r", encoding="utf-8") as f:
        return json.load(f)


def load_module_manifest(name):
    with open(MODULES_DIR / name / "manifest.json", "r", encoding="utf-8") as f:
        return json.load(f)


def load_board_file(board_name, filename):
    with open(BOARDS_DIR / board_name / filename, "r", encoding="utf-8") as f:
        return json.load(f)


def load_project():
    with open(PROJECT_ROOT / "project.json", "r", encoding="utf-8") as f:
        return json.load(f)


def load_all_drivers():
    """Завантажити всі реальні driver manifests, keyed by name."""
    drivers = {}
    for drv_dir in DRIVERS_DIR.iterdir():
        mf = drv_dir / "manifest.json"
        if mf.exists():
            with open(mf, "r", encoding="utf-8") as f:
                d = json.load(f)
                drivers[d["driver"]] = d
    return drivers


def _cap_index_from(all_drivers):
    """capability -> sorted driver names (the generator's P3 matcher index)."""
    idx = {}
    for dn, dm in all_drivers.items():
        for cap in set(_driver_declared_capabilities(dm)):
            idx.setdefault(cap, []).append(dn)
    return {c: sorted(v) for c, v in idx.items()}


# ═══════════════════════════════════════════════════════════════
#  Fixtures — follow project.json so the suite tracks the active set
# ═══════════════════════════════════════════════════════════════

@pytest.fixture(scope="module")
def project():
    return load_project()


@pytest.fixture(scope="module")
def active_modules(project):
    return list(project["modules"])


@pytest.fixture(scope="module")
def all_manifests(active_modules):
    return [load_module_manifest(m) for m in active_modules]


@pytest.fixture(scope="module")
def equipment():
    return load_module_manifest("equipment")


@pytest.fixture(scope="module")
def all_drivers():
    return load_all_drivers()


# ═══════════════════════════════════════════════════════════════
#  Driver manifest validation — PCF8574 (I2C expander) drivers
# ═══════════════════════════════════════════════════════════════

class TestPCF8574DriverManifests:
    """Нові driver manifests проходять валідацію."""

    def test_pcf8574_relay_valid(self):
        v = DriverManifestValidator()
        manifest = load_driver_manifest("pcf8574_relay")
        assert v.validate(manifest, "pcf8574_relay/manifest.json") is True
        assert v.errors == []

    def test_pcf8574_relay_is_actuator(self):
        manifest = load_driver_manifest("pcf8574_relay")
        assert manifest["category"] == "actuator"

    def test_pcf8574_relay_hw_type(self):
        manifest = load_driver_manifest("pcf8574_relay")
        assert manifest["hardware_type"] == "i2c_expander_output"
        assert manifest["hardware_type"] in VALID_HARDWARE_TYPES

    def test_pcf8574_input_valid(self):
        v = DriverManifestValidator()
        manifest = load_driver_manifest("pcf8574_input")
        assert v.validate(manifest, "pcf8574_input/manifest.json") is True
        assert v.errors == []

    def test_pcf8574_input_is_sensor(self):
        manifest = load_driver_manifest("pcf8574_input")
        assert manifest["category"] == "sensor"

    def test_pcf8574_input_hw_type(self):
        manifest = load_driver_manifest("pcf8574_input")
        assert manifest["hardware_type"] == "i2c_expander_input"
        assert manifest["hardware_type"] in VALID_HARDWARE_TYPES


# ═══════════════════════════════════════════════════════════════
#  VALID_HARDWARE_TYPES та BOARD_SECTION_TO_HW_TYPE
# ═══════════════════════════════════════════════════════════════

class TestHardwareTypeMappings:
    """I2C-expander hardware types присутні в конфігурації генератора."""

    def test_i2c_expander_output_in_valid_types(self):
        assert "i2c_expander_output" in VALID_HARDWARE_TYPES

    def test_i2c_expander_input_in_valid_types(self):
        assert "i2c_expander_input" in VALID_HARDWARE_TYPES

    def test_expander_outputs_section_mapped(self):
        assert BOARD_SECTION_TO_HW_TYPE["expander_outputs"] == "i2c_expander_output"

    def test_expander_inputs_section_mapped(self):
        assert BOARD_SECTION_TO_HW_TYPE["expander_inputs"] == "i2c_expander_input"


# ═══════════════════════════════════════════════════════════════
#  Equipment manifest — multi-driver support
# ═══════════════════════════════════════════════════════════════

class TestEquipmentMultiDriver:
    """Equipment manifest з масивами драйверів (одна роль → кілька драйверів)."""

    def test_air_temp_multi_driver(self, equipment):
        """air_temp is a TEMPERATURE capability (fillable by ds18b20/ntc/BLE — no driver list)."""
        req = next(r for r in equipment["requires"] if r["role"] == "air_temp")
        assert req["capability"] == "temperature"
        assert "driver" not in req   # role = capability, never a driver

    def test_actuator_1_multi_driver(self, equipment):
        """actuator_1 is a RELAY_OUT capability (fillable by relay/pcf8574_relay — no driver list)."""
        req = next(r for r in equipment["requires"] if r["role"] == "actuator_1")
        assert req["capability"] == "relay_out"
        assert "driver" not in req

    def test_cross_validate_with_all_drivers(self, equipment, all_drivers):
        """Cross-validation проходить з усіма реальними drivers без помилок."""
        errors, warnings = [], []
        cross_validate([equipment], all_drivers, errors, warnings)
        assert errors == [], f"Errors: {errors}"

    def test_cross_validate_multi_driver_category(self, equipment, all_drivers):
        """Multi-driver: всі драйвери однієї ролі мають однакову категорію."""
        for req in equipment["requires"]:
            drivers = req.get("driver", [])
            if isinstance(drivers, str):
                drivers = [drivers]
            if len(drivers) <= 1:
                continue
            categories = {all_drivers[d]["category"] for d in drivers if d in all_drivers}
            assert len(categories) == 1, \
                f"Role '{req['role']}' has mixed categories: {categories}"


# ═══════════════════════════════════════════════════════════════
#  Board JSON — KC868-A6
# ═══════════════════════════════════════════════════════════════

class TestBoardKC868A6:
    """Валідація boards/kc868a6/board.json."""

    @pytest.fixture(scope="class")
    def board(self):
        return load_board_file("kc868a6", "board.json")

    def test_board_name(self, board):
        assert board["board"] == "kc868_a6"

    def test_manifest_version(self, board):
        assert board["manifest_version"] == 1

    def test_has_i2c_bus(self, board):
        assert len(board["i2c_buses"]) == 1
        bus = board["i2c_buses"][0]
        assert bus["sda"] == 4
        assert bus["scl"] == 15

    def test_has_relay_expander(self, board):
        relay_exp = next(e for e in board["i2c_expanders"] if e["id"] == "relay_exp")
        assert relay_exp["address"] == "0x24"
        assert relay_exp["chip"] == "pcf8574"

    def test_has_input_expander(self, board):
        input_exp = next(e for e in board["i2c_expanders"] if e["id"] == "input_exp")
        assert input_exp["address"] == "0x22"
        assert input_exp["chip"] == "pcf8574"

    def test_6_relay_outputs(self, board):
        assert len(board["expander_outputs"]) == 6

    def test_relays_active_low(self, board):
        """Критично: всі реле active_high=false (active-LOW)."""
        for out in board["expander_outputs"]:
            assert out["active_high"] is False, \
                f"Relay '{out['id']}' must be active_high=false (active-LOW)"

    def test_6_digital_inputs(self, board):
        assert len(board["expander_inputs"]) == 6

    def test_inputs_inverted(self, board):
        """Opto-isolated inputs inverted (active-LOW)."""
        for inp in board["expander_inputs"]:
            assert inp["invert"] is True, \
                f"Input '{inp['id']}' should have invert=true"

    def test_2_onewire_buses(self, board):
        assert len(board["onewire_buses"]) == 2
        gpios = {bus["gpio"] for bus in board["onewire_buses"]}
        assert gpios == {32, 33}

    def test_4_adc_channels(self, board):
        assert len(board["adc_channels"]) == 4


# ═══════════════════════════════════════════════════════════════
#  Bindings JSON — KC868-A6 (structure + clean validate_bindings)
# ═══════════════════════════════════════════════════════════════

class TestBindingsKC868A6:
    """Валідація boards/kc868a6/bindings.json проти board + drivers."""

    @pytest.fixture(scope="class")
    def board(self):
        return load_board_file("kc868a6", "board.json")

    @pytest.fixture(scope="class")
    def bindings(self):
        return load_board_file("kc868a6", "bindings.json")

    def test_manifest_version(self, bindings):
        assert bindings["manifest_version"] == 1

    def test_uses_expander_relay_driver(self, bindings):
        """Реле прив'язані до pcf8574_relay (I2C expander)."""
        relay_rows = [b for b in bindings["bindings"] if b["driver"] == "pcf8574_relay"]
        assert relay_rows, "expected at least one pcf8574_relay binding"
        for b in relay_rows:
            assert b["hardware"].startswith("relay_")

    def test_has_air_temp_onewire(self, bindings):
        """air_temp прив'язаний до ds18b20 на OneWire шині."""
        air = next(b for b in bindings["bindings"] if b["role"] == "air_temp")
        assert air["driver"] == "ds18b20"
        assert air["hardware"].startswith("ow_")

    def test_validate_bindings_clean(self, board, bindings, all_drivers):
        """bindings ↔ board ↔ driver узгоджені — нема помилок валідації."""
        errors, warnings = [], []
        validate_bindings(board, bindings, all_drivers, errors, warnings)
        assert errors == [], f"errors: {errors}"


# ═══════════════════════════════════════════════════════════════
#  Generator — _bindings_page з KC868-A6 board (multi-driver roles)
# ═══════════════════════════════════════════════════════════════

class TestBindingsPageKC868A6:
    """Generator _bindings_page емітує hw_types/drivers + hardware inventory."""

    @pytest.fixture(scope="class")
    def bindings_page(self, project, all_manifests, equipment, all_drivers):
        board = load_board_file("kc868a6", "board.json")
        bindings = load_board_file("kc868a6", "bindings.json")
        resolver = FeatureResolver(bindings, equipment)
        caps = load_capabilities()
        cap_index = _cap_index_from(all_drivers)
        out = UIJsonGenerator().generate(
            project, all_manifests, all_drivers, board, bindings, resolver, caps, cap_index)
        bp = next((p for p in out["pages"] if p["id"] == "bindings"), None)
        assert bp is not None, "bindings page missing"
        return bp

    def _role(self, bindings_page, role):
        return next(r for r in bindings_page["roles"] if r["role"] == role)

    def test_bindings_page_exists(self, bindings_page):
        assert bindings_page["id"] == "bindings"

    def test_actuator_role_multi_hw_types(self, bindings_page):
        """actuator_1 (relay|pcf8574_relay) → gpio_output + i2c_expander_output."""
        act = self._role(bindings_page, "actuator_1")
        assert "gpio_output" in act["hw_types"]
        assert "i2c_expander_output" in act["hw_types"]

    def test_actuator_role_multi_drivers(self, bindings_page):
        act = self._role(bindings_page, "actuator_1")
        assert "relay" in act["drivers"]
        assert "pcf8574_relay" in act["drivers"]

    def test_air_temp_multi_driver_role(self, bindings_page):
        """air_temp (temperature capability) is eligible for ds18b20|ntc AND the BLE temp channel."""
        air = self._role(bindings_page, "air_temp")
        assert "ds18b20" in air["drivers"]
        assert "ntc" in air["drivers"]
        assert "ble_xiaomi_th" in air["drivers"]     # capability unifies transports
        assert "onewire_bus" in air["hw_types"]
        assert "adc_channel" in air["hw_types"]

    def test_air_temp_addr_drivers_is_per_driver(self, bindings_page):
        """requires_address is PER-DRIVER: a temperature role's addr_drivers must list ONLY the
        BLE channel driver, never the address-free wired ds18b20/ntc — else the default board's
        wired air_temp binding (no address) becomes un-saveable (P3 review, CRITICAL)."""
        air = self._role(bindings_page, "air_temp")
        assert "ble_xiaomi_th" in air["addr_drivers"]
        assert "ds18b20" not in air["addr_drivers"]
        assert "ntc" not in air["addr_drivers"]

    def test_air_temp_channels_by_driver(self, bindings_page):
        """channels_by_driver is PER-DRIVER: the BLE temp driver exposes its temperature channel;
        the address-free wired ds18b20/ntc contribute none (a wired pick needs no channel). The
        channel label derives from capabilities.json — no hardcoded per-driver label."""
        air = self._role(bindings_page, "air_temp")
        cbd = air.get("channels_by_driver", {})
        assert [c["value"] for c in cbd.get("ble_xiaomi_th", [])] == ["temperature"]
        assert cbd["ble_xiaomi_th"][0]["label"] == "Temperature, °C"   # from capabilities.json
        assert "ds18b20" not in cbd    # single-value wired driver → no channel enum
        assert "ntc" not in cbd

    def test_roles_match_equipment_requires(self, bindings_page, equipment):
        """Кожна equipment-роль присутня на сторінці bindings."""
        page_roles = {r["role"] for r in bindings_page["roles"]}
        equip_roles = {r["role"] for r in equipment["requires"]}
        assert equip_roles.issubset(page_roles)

    def test_hardware_includes_expander_outputs(self, bindings_page):
        hw_types = {h["hw_type"] for h in bindings_page["hardware"]}
        assert "i2c_expander_output" in hw_types

    def test_hardware_includes_expander_inputs(self, bindings_page):
        hw_types = {h["hw_type"] for h in bindings_page["hardware"]}
        assert "i2c_expander_input" in hw_types

    def test_hardware_relay_count(self, bindings_page):
        """6 relays (i2c_expander_output) у інвентарі обладнання."""
        relays = [h for h in bindings_page["hardware"]
                  if h["hw_type"] == "i2c_expander_output"]
        assert len(relays) == 6

    def test_hardware_input_count(self, bindings_page):
        """6 digital inputs (i2c_expander_input) у інвентарі обладнання."""
        inputs = [h for h in bindings_page["hardware"]
                  if h["hw_type"] == "i2c_expander_input"]
        assert len(inputs) == 6

    def test_hardware_inventory_matches_board(self, bindings_page):
        """Інвентар == сума всіх board-секцій (computed, не хардкод)."""
        board = load_board_file("kc868a6", "board.json")
        expected = Counter()
        for section, hw_type in BOARD_SECTION_TO_HW_TYPE.items():
            expected[hw_type] += len(board.get(section, []))
        actual = Counter(h["hw_type"] for h in bindings_page["hardware"])
        assert actual == expected


# ═══════════════════════════════════════════════════════════════
#  Generator — dev board (GPIO, no expanders) backward compatibility
# ═══════════════════════════════════════════════════════════════

class TestDevBoardBackwardCompat:
    """Dev board (template_dev_v1) продовжує працювати з multi-driver ролями."""

    @pytest.fixture(scope="class")
    def bindings_page(self, project, all_manifests, equipment, all_drivers):
        board = load_board_file("dev", "board.json")
        bindings = load_board_file("dev", "bindings.json")
        resolver = FeatureResolver(bindings, equipment)
        caps = load_capabilities()
        cap_index = _cap_index_from(all_drivers)
        out = UIJsonGenerator().generate(
            project, all_manifests, all_drivers, board, bindings, resolver, caps, cap_index)
        bp = next((p for p in out["pages"] if p["id"] == "bindings"), None)
        assert bp is not None, "bindings page missing"
        return bp

    def test_bindings_page_exists(self, bindings_page):
        assert bindings_page["id"] == "bindings"

    def test_actuator_role_still_multi_hw_types(self, bindings_page):
        """Роль actuator_1 несе обидва hw_types навіть на GPIO-only платі."""
        act = next(r for r in bindings_page["roles"] if r["role"] == "actuator_1")
        assert "gpio_output" in act["hw_types"]
        assert "i2c_expander_output" in act["hw_types"]

    def test_gpio_hardware_present(self, bindings_page):
        """Dev board має gpio_output hardware."""
        hw_types = {h["hw_type"] for h in bindings_page["hardware"]}
        assert "gpio_output" in hw_types

    def test_no_expander_hardware(self, bindings_page):
        """Dev board НЕ має expander hardware."""
        hw_types = {h["hw_type"] for h in bindings_page["hardware"]}
        assert "i2c_expander_output" not in hw_types
        assert "i2c_expander_input" not in hw_types

    def test_validate_bindings_clean(self, all_drivers):
        board = load_board_file("dev", "board.json")
        bindings = load_board_file("dev", "bindings.json")
        errors, warnings = [], []
        validate_bindings(board, bindings, all_drivers, errors, warnings)
        assert errors == [], f"errors: {errors}"
