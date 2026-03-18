"""
test_kc868a6.py — тести для підтримки KC868-A6 (Phase 12a)

Перевіряє:
1. Нові driver manifests (pcf8574_relay, pcf8574_input) проходять валідацію
2. Equipment manifest з multi-driver — cross-validation
3. Board JSON для KC868-A6 — структура та конфігурація
4. Generator _bindings_page — hw_types масив для multi-driver ролей
5. Generator з KC868-A6 board+bindings — повний цикл генерації
"""
import json
import sys
from pathlib import Path

import pytest

# Setup path for imports
TOOLS_DIR = Path(__file__).parent.parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from generate_ui import (
    DriverManifestValidator,
    ManifestValidator,
    UIJsonGenerator,
    FeatureResolver,
    FeaturesConfigGenerator,
    cross_validate,
    VALID_HARDWARE_TYPES,
    BOARD_SECTION_TO_HW_TYPE,
)

PROJECT_ROOT = Path(__file__).parent.parent.parent
DRIVERS_DIR = PROJECT_ROOT / "drivers"
MODULES_DIR = PROJECT_ROOT / "modules"
DATA_DIR = PROJECT_ROOT / "data"
BOARDS_DIR = PROJECT_ROOT / "boards"


def load_driver_manifest(name):
    path = DRIVERS_DIR / name / "manifest.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_module_manifest(name):
    path = MODULES_DIR / name / "manifest.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_data_file(name):
    path = DATA_DIR / name
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_board_file(board_name, filename):
    path = BOARDS_DIR / board_name / filename
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_project():
    path = PROJECT_ROOT / "project.json"
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def load_all_drivers():
    """Завантажити всі реальні driver manifests."""
    drivers = {}
    for drv_dir in DRIVERS_DIR.iterdir():
        mf = drv_dir / "manifest.json"
        if mf.exists():
            with open(mf, "r", encoding="utf-8") as f:
                d = json.load(f)
                drivers[d["driver"]] = d
    return drivers


def load_all_manifests():
    """Завантажити всі module manifests."""
    return [
        load_module_manifest("equipment"),
        load_module_manifest("thermostat"),
        load_module_manifest("defrost"),
        load_module_manifest("protection"),
        load_module_manifest("datalogger"),
    ]


# ═══════════════════════════════════════════════════════════════
#  Driver manifest validation
# ═══════════════════════════════════════════════════════════════

class TestPCF8574DriverManifests:
    """Нові driver manifests проходять валідацію."""

    def test_pcf8574_relay_valid(self):
        """pcf8574_relay manifest проходить валідацію."""
        v = DriverManifestValidator()
        manifest = load_driver_manifest("pcf8574_relay")
        result = v.validate(manifest, "pcf8574_relay/manifest.json")
        assert result is True
        assert len(v.errors) == 0

    def test_pcf8574_relay_is_actuator(self):
        """pcf8574_relay — категорія actuator."""
        manifest = load_driver_manifest("pcf8574_relay")
        assert manifest["category"] == "actuator"

    def test_pcf8574_relay_hw_type(self):
        """pcf8574_relay — hardware_type i2c_expander_output."""
        manifest = load_driver_manifest("pcf8574_relay")
        assert manifest["hardware_type"] == "i2c_expander_output"
        assert manifest["hardware_type"] in VALID_HARDWARE_TYPES

    def test_pcf8574_input_valid(self):
        """pcf8574_input manifest проходить валідацію."""
        v = DriverManifestValidator()
        manifest = load_driver_manifest("pcf8574_input")
        result = v.validate(manifest, "pcf8574_input/manifest.json")
        assert result is True
        assert len(v.errors) == 0

    def test_pcf8574_input_is_sensor(self):
        """pcf8574_input — категорія sensor."""
        manifest = load_driver_manifest("pcf8574_input")
        assert manifest["category"] == "sensor"

    def test_pcf8574_input_hw_type(self):
        """pcf8574_input — hardware_type i2c_expander_input."""
        manifest = load_driver_manifest("pcf8574_input")
        assert manifest["hardware_type"] == "i2c_expander_input"
        assert manifest["hardware_type"] in VALID_HARDWARE_TYPES


# ═══════════════════════════════════════════════════════════════
#  VALID_HARDWARE_TYPES та BOARD_SECTION_TO_HW_TYPE
# ═══════════════════════════════════════════════════════════════

class TestHardwareTypeMappings:
    """Нові hardware types додані в конфігурацію генератора."""

    def test_i2c_expander_output_in_valid_types(self):
        assert "i2c_expander_output" in VALID_HARDWARE_TYPES

    def test_i2c_expander_input_in_valid_types(self):
        assert "i2c_expander_input" in VALID_HARDWARE_TYPES

    def test_expander_outputs_section_mapped(self):
        assert "expander_outputs" in BOARD_SECTION_TO_HW_TYPE
        assert BOARD_SECTION_TO_HW_TYPE["expander_outputs"] == "i2c_expander_output"

    def test_expander_inputs_section_mapped(self):
        assert "expander_inputs" in BOARD_SECTION_TO_HW_TYPE
        assert BOARD_SECTION_TO_HW_TYPE["expander_inputs"] == "i2c_expander_input"


# ═══════════════════════════════════════════════════════════════
#  Equipment manifest — multi-driver support
# ═══════════════════════════════════════════════════════════════

class TestEquipmentMultiDriver:
    """Equipment manifest з масивами драйверів."""

    @pytest.fixture
    def equipment(self):
        return load_module_manifest("equipment")

    @pytest.fixture
    def all_drivers(self):
        return load_all_drivers()

    def test_compressor_has_multi_driver(self, equipment):
        """Compressor role має масив драйверів [relay, pcf8574_relay]."""
        req = next(r for r in equipment["requires"] if r["role"] == "compressor")
        drivers = req["driver"]
        assert isinstance(drivers, list)
        assert "relay" in drivers
        assert "pcf8574_relay" in drivers

    def test_door_contact_has_multi_driver(self, equipment):
        """door_contact має масив [digital_input, pcf8574_input]."""
        req = next(r for r in equipment["requires"] if r["role"] == "door_contact")
        drivers = req["driver"]
        assert isinstance(drivers, list)
        assert "digital_input" in drivers
        assert "pcf8574_input" in drivers

    def test_air_temp_multi_driver(self, equipment):
        """air_temp підтримує ds18b20 та ntc."""
        req = next(r for r in equipment["requires"] if r["role"] == "air_temp")
        drivers = req["driver"]
        assert isinstance(drivers, list)
        assert "ds18b20" in drivers
        assert "ntc" in drivers

    def test_cross_validate_with_all_drivers(self, equipment, all_drivers):
        """Cross-validation проходить з усіма реальними drivers."""
        errors = []
        warnings = []
        cross_validate([equipment], all_drivers, errors, warnings)
        assert len(errors) == 0, f"Errors: {errors}"

    def test_cross_validate_multi_driver_category(self, all_drivers):
        """Multi-driver: всі драйвери однієї категорії."""
        equipment = load_module_manifest("equipment")
        for req in equipment["requires"]:
            drivers = req.get("driver", [])
            if isinstance(drivers, str):
                drivers = [drivers]
            if len(drivers) <= 1:
                continue
            categories = set()
            for drv_name in drivers:
                if drv_name in all_drivers:
                    categories.add(all_drivers[drv_name]["category"])
            # Всі драйвери однієї ролі повинні мати однакову категорію
            assert len(categories) == 1, \
                f"Role '{req['role']}' has mixed categories: {categories}"


# ═══════════════════════════════════════════════════════════════
#  Board JSON — KC868-A6
# ═══════════════════════════════════════════════════════════════

class TestBoardKC868A6:
    """Валідація board_kc868a6.json."""

    @pytest.fixture
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
        exps = board["i2c_expanders"]
        relay_exp = next(e for e in exps if e["id"] == "relay_exp")
        assert relay_exp["address"] == "0x24"
        assert relay_exp["chip"] == "pcf8574"

    def test_has_input_expander(self, board):
        exps = board["i2c_expanders"]
        input_exp = next(e for e in exps if e["id"] == "input_exp")
        assert input_exp["address"] == "0x22"
        assert input_exp["chip"] == "pcf8574"

    def test_6_relay_outputs(self, board):
        outputs = board["expander_outputs"]
        assert len(outputs) == 6

    def test_relays_active_low(self, board):
        """Критично: всі реле active_high=false (active-LOW)."""
        for out in board["expander_outputs"]:
            assert out["active_high"] is False, \
                f"Relay '{out['id']}' must be active_high=false (active-LOW)"

    def test_6_digital_inputs(self, board):
        inputs = board["expander_inputs"]
        assert len(inputs) == 6

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
#  Bindings JSON — KC868-A6
# ═══════════════════════════════════════════════════════════════

class TestBindingsKC868A6:
    """Валідація bindings_kc868a6.json."""

    @pytest.fixture
    def bindings(self):
        return load_board_file("kc868a6", "bindings.json")

    def test_manifest_version(self, bindings):
        assert bindings["manifest_version"] == 1

    def test_has_compressor(self, bindings):
        comp = next(b for b in bindings["bindings"] if b["role"] == "compressor")
        assert comp["driver"] == "pcf8574_relay"
        assert comp["hardware"] == "relay_1"

    def test_has_air_temp(self, bindings):
        air = next(b for b in bindings["bindings"] if b["role"] == "air_temp")
        assert air["driver"] == "ds18b20"

    def test_has_door_contact(self, bindings):
        door = next(b for b in bindings["bindings"] if b["role"] == "door_contact")
        assert door["driver"] == "pcf8574_input"


# ═══════════════════════════════════════════════════════════════
#  Generator — _bindings_page з multi-driver
# ═══════════════════════════════════════════════════════════════

class TestBindingsPageMultiDriver:
    """Generator _bindings_page емітує hw_types для multi-driver ролей."""

    @pytest.fixture
    def generator_output(self):
        """Запускає генератор з KC868-A6 board та bindings."""
        manifests = load_all_manifests()
        driver_manifests = load_all_drivers()
        project = load_project()
        equipment = load_module_manifest("equipment")

        board = load_board_file("kc868a6", "board.json")
        bindings = load_board_file("kc868a6", "bindings.json")

        gen = UIJsonGenerator()
        resolver = FeatureResolver(bindings, equipment)
        result = gen.generate(project, manifests, driver_manifests, board, bindings, resolver)
        return result

    def test_bindings_page_exists(self, generator_output):
        """Сторінка bindings генерується."""
        pages = generator_output["pages"]
        bp = next((p for p in pages if p["id"] == "bindings"), None)
        assert bp is not None

    def test_compressor_role_has_hw_types(self, generator_output):
        """Compressor role має hw_types масив."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        roles = bp["roles"]
        comp = next(r for r in roles if r["role"] == "compressor")
        assert "hw_types" in comp
        assert "gpio_output" in comp["hw_types"]
        assert "i2c_expander_output" in comp["hw_types"]

    def test_compressor_role_has_drivers(self, generator_output):
        """Compressor role має drivers масив."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        roles = bp["roles"]
        comp = next(r for r in roles if r["role"] == "compressor")
        assert "drivers" in comp
        assert "relay" in comp["drivers"]
        assert "pcf8574_relay" in comp["drivers"]

    def test_air_temp_multi_driver_role(self, generator_output):
        """air_temp (multi driver) має drivers та hw_types."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        roles = bp["roles"]
        air = next(r for r in roles if r["role"] == "air_temp")
        assert "ds18b20" in air["drivers"]
        assert "ntc" in air["drivers"]
        assert "onewire_bus" in air["hw_types"]
        assert "adc_channel" in air["hw_types"]

    def test_hardware_includes_expander_outputs(self, generator_output):
        """Hardware inventory включає expander_outputs."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        hardware = bp["hardware"]
        hw_types = {h["hw_type"] for h in hardware}
        assert "i2c_expander_output" in hw_types

    def test_hardware_includes_expander_inputs(self, generator_output):
        """Hardware inventory включає expander_inputs."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        hardware = bp["hardware"]
        hw_types = {h["hw_type"] for h in hardware}
        assert "i2c_expander_input" in hw_types

    def test_hardware_relay_count(self, generator_output):
        """6 relays in hardware inventory."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        hardware = bp["hardware"]
        relays = [h for h in hardware if h["hw_type"] == "i2c_expander_output"]
        assert len(relays) == 6

    def test_hardware_input_count(self, generator_output):
        """6 digital inputs in hardware inventory."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        hardware = bp["hardware"]
        inputs = [h for h in hardware if h["hw_type"] == "i2c_expander_input"]
        assert len(inputs) == 6


# ═══════════════════════════════════════════════════════════════
#  Generator з dev board — backward compatibility
# ═══════════════════════════════════════════════════════════════

class TestDevBoardBackwardCompat:
    """Dev board (cold_room_dev_v1) продовжує працювати з multi-driver."""

    @pytest.fixture
    def generator_output(self):
        manifests = load_all_manifests()
        driver_manifests = load_all_drivers()
        project = load_project()
        equipment = load_module_manifest("equipment")

        board = load_board_file("dev", "board.json")
        bindings = load_board_file("dev", "bindings.json")

        gen = UIJsonGenerator()
        resolver = FeatureResolver(bindings, equipment)
        return gen.generate(project, manifests, driver_manifests, board, bindings, resolver)

    def test_bindings_page_exists(self, generator_output):
        pages = generator_output["pages"]
        bp = next((p for p in pages if p["id"] == "bindings"), None)
        assert bp is not None

    def test_compressor_has_hw_types(self, generator_output):
        """Compressor role має hw_types навіть з dev board."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        roles = bp["roles"]
        comp = next(r for r in roles if r["role"] == "compressor")
        assert "hw_types" in comp
        assert "gpio_output" in comp["hw_types"]
        assert "i2c_expander_output" in comp["hw_types"]

    def test_gpio_hardware_present(self, generator_output):
        """Dev board має gpio_output hardware."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        hardware = bp["hardware"]
        hw_types = {h["hw_type"] for h in hardware}
        assert "gpio_output" in hw_types

    def test_no_expander_hardware(self, generator_output):
        """Dev board НЕ має expander hardware."""
        pages = generator_output["pages"]
        bp = next(p for p in pages if p["id"] == "bindings")
        hardware = bp["hardware"]
        hw_types = {h["hw_type"] for h in hardware}
        assert "i2c_expander_output" not in hw_types
        assert "i2c_expander_input" not in hw_types
