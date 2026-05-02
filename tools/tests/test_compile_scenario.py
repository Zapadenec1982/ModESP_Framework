"""
Tests для tools/compile_scenario.py — JSON manifest "scenario" section → .modr binary.

Coverage у Step 2a (this commit):
- KnownActionRegistry загружає valid registry, detects hash mismatches/collisions
- Compiler reads manifest, validates schema, emits well-formed binary
- CRC32 trailer correct
- Schema validation rejects malformed scenarios з proper error code
- Module type / scenario section requirements enforced
- Transition target resolution ($complete, $abort, named phases)

Step 2b will add: feature parity tests (actions, conditions, parameters,
multi-track, resources, global transitions), property tests з Hypothesis,
round-trip tests vs Step 1 golden.

Plan reference: Step 2 у .claude/plans/quirky-imagining-lake.md
ADR reference: docs/sequence_engine/adr/0004-recipe-as-manifest.md
"""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path

import pytest

# Add tools/ до path для imports
import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from compile_scenario import (  # noqa: E402
    CompileError,
    KnownActionRegistry,
    ScenarioCompiler,
    StringPool,
    djb2_hash16,
    MODR_MAGIC,
    MODR_FORMAT_VERSION,
)


REPO_ROOT = Path(__file__).parent.parent.parent
KNOWN_ACTIONS_PATH = REPO_ROOT / "tools" / "known_actions.json"
SCHEMA_PATH = REPO_ROOT / "tools" / "scenario_schema.json"


# ─────────────────────────────────────────────────────────────────────
# Fixtures
# ─────────────────────────────────────────────────────────────────────


@pytest.fixture
def registry() -> KnownActionRegistry:
    return KnownActionRegistry.load(KNOWN_ACTIONS_PATH)


@pytest.fixture
def schema() -> dict:
    return json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))


@pytest.fixture
def compiler(registry, schema) -> ScenarioCompiler:
    return ScenarioCompiler(registry, schema)


def make_minimal_manifest() -> dict:
    """Smallest valid recipe manifest для compilation tests."""
    return {
        "manifest_version": 1,
        "module": "recipe_min",
        "module_type": "recipe",
        "version": "1.0.0",
        "priority": 5,
        "state": {},
        "scenario": {
            "default_phase_timeout_ms": 60000,
            "completion_rule": "all_tracks_complete",
            "tracks": [
                {
                    "name": "main",
                    "flags": ["main_track"],
                    "phases": [
                        {
                            "name": "p",
                            "transitions": [{"to": "$complete"}]
                        }
                    ]
                }
            ]
        }
    }


def write_manifest(tmp_path: Path, manifest: dict) -> Path:
    p = tmp_path / "manifest.json"
    p.write_text(json.dumps(manifest), encoding="utf-8")
    return p


# ─────────────────────────────────────────────────────────────────────
# KnownActionRegistry tests
# ─────────────────────────────────────────────────────────────────────


class TestKnownActionRegistry:
    def test_loads_valid_registry(self, registry):
        # Built-ins per plan: 3 actions, 13 conditions
        assert len(registry.actions) == 3
        assert len(registry.conditions) == 13

    def test_known_action_lookup(self, registry):
        assert registry.is_known_action("log")
        assert registry.is_known_action("set_state")
        assert registry.is_known_action("wait_ms")
        assert not registry.is_known_action("nonexistent")

    def test_known_condition_lookup(self, registry):
        assert registry.is_known_condition("time_elapsed_ms")
        assert registry.is_known_condition("state_key_eq")
        assert registry.is_known_condition("time_of_day_eq")
        assert registry.is_known_condition("all_of")
        assert not registry.is_known_condition("nonexistent")

    def test_hash_mismatch_rejected(self, tmp_path):
        bad = {
            "format_version": 1,
            "hash_algorithm": "djb2_hash16",
            "actions": {"log": {"hash": 9999}},  # wrong hash
            "conditions": {}
        }
        bad_path = tmp_path / "bad.json"
        bad_path.write_text(json.dumps(bad), encoding="utf-8")
        with pytest.raises(CompileError) as exc:
            KnownActionRegistry.load(bad_path)
        assert exc.value.code == "E0202"
        assert "hash mismatch" in exc.value.message

    def test_real_collision_detected(self, tmp_path):
        # Brute-force found pair: 'aa' і 'gjq' both djb2_hash16 to 0x7727.
        # Verified: djb2_hash16('aa') == djb2_hash16('gjq') == 0x7727.
        # Це genuine algorithmic collision, не synthetic hash injection.
        h_aa = djb2_hash16("aa")
        h_gjq = djb2_hash16("gjq")
        assert h_aa == h_gjq == 0x7727, "test invariant: brute-forced collision pair"

        bad = {
            "format_version": 1,
            "hash_algorithm": "djb2_hash16",
            "actions": {
                "aa": {"hash": h_aa},   # both stored hashes correct (no E0202)
                "gjq": {"hash": h_gjq}, # but they collide на 0x7727 (E0203)
            },
            "conditions": {}
        }
        bad_path = tmp_path / "real_collision.json"
        bad_path.write_text(json.dumps(bad), encoding="utf-8")
        with pytest.raises(CompileError) as exc:
            KnownActionRegistry.load(bad_path)
        # Both stored hashes correct → E0202 не fires. E0203 (collision) IS the path.
        assert exc.value.code == "E0203", (
            f"Expected E0203 collision detection, got {exc.value.code}: {exc.value.message}"
        )
        assert "0x7727" in exc.value.message or "30503" in exc.value.message  # 0x7727 = 30503


# ─────────────────────────────────────────────────────────────────────
# Schema validation tests
# ─────────────────────────────────────────────────────────────────────


class TestSchemaValidation:
    def test_minimal_manifest_compiles(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        path = write_manifest(tmp_path, manifest)
        result = compiler.compile(path)
        assert result.module_name == "recipe_min"
        assert len(result.blob) > 0

    def test_missing_module_field(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        del manifest["module"]
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0103"

    def test_wrong_module_type(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        manifest["module_type"] = "module"  # not "recipe"
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0104"

    def test_missing_scenario_section(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        del manifest["scenario"]
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0105"

    def test_schema_rejects_missing_required_field(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        del manifest["scenario"]["completion_rule"]
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0101"
        assert "completion_rule" in exc.value.message

    def test_schema_rejects_invalid_completion_rule(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        manifest["scenario"]["completion_rule"] = "wrong_rule"
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0101"

    def test_schema_rejects_empty_tracks(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        manifest["scenario"]["tracks"] = []
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0101"

    def test_schema_rejects_too_many_tracks(self, compiler, tmp_path):
        manifest = make_minimal_manifest()
        # 7 tracks > MAX_TRACKS_PER_SCENARIO (6)
        manifest["scenario"]["tracks"] = [
            {"name": f"t{i}", "phases": [{"name": "p", "transitions": [{"to": "$complete"}]}]}
            for i in range(7)
        ]
        path = write_manifest(tmp_path, manifest)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0101"


# ─────────────────────────────────────────────────────────────────────
# Compilation correctness tests
# ─────────────────────────────────────────────────────────────────────


class TestCompiledBinaryIntegrity:
    def test_starts_with_magic(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        assert data[0:4] == b"MODR"
        assert struct.unpack_from("<I", data, 0)[0] == MODR_MAGIC

    def test_version_field(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        version = struct.unpack_from("<H", data, 4)[0]
        assert version == MODR_FORMAT_VERSION

    def test_total_size_matches(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        total_size_field = struct.unpack_from("<I", data, 8)[0]
        assert total_size_field == len(data)

    def test_crc32_valid(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        crc_computed = zlib.crc32(data[:-4]) & 0xFFFFFFFF
        assert crc_stored == crc_computed

    def test_scenario_id_derived_from_module_name(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        scenario_id = struct.unpack_from("<H", data, 12)[0]
        assert scenario_id == djb2_hash16("recipe_min")

    def test_track_count_correct(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        assert data[16] == 1  # track_count

    def test_completion_rule_all(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        assert data[20] == 0  # MODR_COMPLETION_ALL_TRACKS = 0

    def test_completion_rule_any(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["completion_rule"] = "any_track_complete"
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        assert data[20] == 1

    def test_completion_rule_main(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["completion_rule"] = "main_track_complete"
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        assert data[20] == 2

    def test_main_track_flag_set(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        # track_count at 16 → track table starts after header (offset 56)
        # track[0].flags at offset 56 + 9 (field offset within track struct)
        track_flags = data[56 + 9]
        assert track_flags & 0x01  # MODR_TRACK_FLAG_MAIN

    def test_default_phase_timeout_persisted(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["default_phase_timeout_ms"] = 12345
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # default_phase_timeout_ms at offset 44 (4 bytes)
        timeout = struct.unpack_from("<I", data, 44)[0]
        assert timeout == 12345


# ─────────────────────────────────────────────────────────────────────
# Transition target resolution
# ─────────────────────────────────────────────────────────────────────


class TestTransitionTargets:
    def test_complete_target(self, compiler, tmp_path):
        # Minimal manifest already has $complete; verify encoded as 0xFFFF
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        # phase[0] at offset 56+16=72; transitions_off at 72+6 (2 bytes)
        trans_off = struct.unpack_from("<H", data, 72 + 6)[0]
        # transition[0] target_phase at trans_off + 2 (2 bytes)
        target = struct.unpack_from("<H", data, trans_off + 2)[0]
        assert target == 0xFFFF  # MODR_TARGET_COMPLETE

    def test_abort_target(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["transitions"] = [{"to": "$abort"}]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        trans_off = struct.unpack_from("<H", data, 72 + 6)[0]
        target = struct.unpack_from("<H", data, trans_off + 2)[0]
        assert target == 0xFFFE  # MODR_TARGET_ABORT

    def test_named_phase_target(self, compiler, tmp_path):
        m = make_minimal_manifest()
        # Two phases: p1 -> p2 -> $complete
        m["scenario"]["tracks"][0]["phases"] = [
            {"name": "p1", "transitions": [{"to": "p2"}]},
            {"name": "p2", "transitions": [{"to": "$complete"}]}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # phase[0] is at 72, phase[1] at 72+20=92
        # phase[0].transitions_off
        trans_off_p1 = struct.unpack_from("<H", data, 72 + 6)[0]
        # transition[0] target = index 1 (phase p2)
        target = struct.unpack_from("<H", data, trans_off_p1 + 2)[0]
        assert target == 1

    def test_unknown_target_rejected(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["transitions"] = [{"to": "nonexistent"}]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0207"
        assert "nonexistent" in exc.value.message


# ─────────────────────────────────────────────────────────────────────
# String pool tests
# ─────────────────────────────────────────────────────────────────────


class TestStringPool:
    def test_intern_dedupe(self):
        pool = StringPool()
        a = pool.intern("foo")
        b = pool.intern("foo")
        assert a == b

    def test_intern_distinct(self):
        pool = StringPool()
        a = pool.intern("foo")
        b = pool.intern("bar")
        assert a != b

    def test_layout_length_prefixed(self):
        pool = StringPool()
        pool.intern("ab")
        data = pool.to_bytes()
        # First byte = length (2), then "ab" (2 bytes)
        assert data == b"\x02ab"

    def test_too_long_string_rejected(self):
        pool = StringPool()
        with pytest.raises(CompileError) as exc:
            pool.intern("x" * 256)
        assert exc.value.code == "E0301"


# ─────────────────────────────────────────────────────────────────────
# Error format tests
# ─────────────────────────────────────────────────────────────────────


class TestErrorFormat:
    def test_compile_error_format(self):
        e = CompileError("E0101", "test msg", file="x.json", line=5, col=10)
        assert e.format() == "x.json:5:10: error[E0101]: test msg"

    def test_default_location(self):
        e = CompileError("E0301", "test")
        assert "?:0:0:" in e.format() or e.format().startswith("?:")
        assert "[E0301]" in e.format()


# ─────────────────────────────────────────────────────────────────────
# v0 limitations explicitly tested
# ─────────────────────────────────────────────────────────────────────


class TestUniqueness:
    """Track і phase names must be unique. JSON Schema's uniqueItems doesn't apply
    to arrays of objects, so explicit semantic check (E0208) у compiler."""

    def test_duplicate_track_names_rejected(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"] = [
            {"name": "main", "flags": ["main_track"], "phases": [
                {"name": "p", "transitions": [{"to": "$complete"}]}]},
            {"name": "main", "phases": [  # duplicate!
                {"name": "p", "transitions": [{"to": "$complete"}]}]}
        ]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0208"
        assert "duplicate track" in exc.value.message
        assert "main" in exc.value.message

    def test_duplicate_phase_names_within_track_rejected(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"] = [
            {"name": "p", "transitions": [{"to": "$complete"}]},
            {"name": "p", "transitions": [{"to": "$complete"}]},  # duplicate
        ]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0208"
        assert "duplicate phase" in exc.value.message

    def test_same_phase_name_in_different_tracks_allowed(self, compiler, tmp_path):
        # Name "p" у track A AND track B — це legitimate, no error.
        m = make_minimal_manifest()
        m["scenario"]["tracks"] = [
            {"name": "a", "flags": ["main_track"], "phases": [
                {"name": "p", "transitions": [{"to": "$complete"}]}]},
            {"name": "b", "phases": [
                {"name": "p", "transitions": [{"to": "$complete"}]}]}  # OK — different track
        ]
        path = write_manifest(tmp_path, m)
        result = compiler.compile(path)
        assert result.module_name == "recipe_min"


class TestMultiTrackBinary:
    """Multi-track recipes mais different layout (per-track phase tables, transition
    arrays interleaved). Step 2a code handles цей correctly але was previously untested."""

    def _build_two_track_recipe(self) -> dict:
        return {
            "manifest_version": 1,
            "module": "recipe_mt",
            "module_type": "recipe",
            "version": "1.0.0",
            "priority": 5,
            "state": {},
            "scenario": {
                "default_phase_timeout_ms": 60000,
                "completion_rule": "all_tracks_complete",
                "tracks": [
                    {"name": "a", "flags": ["main_track"], "phases": [
                        {"name": "p1", "transitions": [{"to": "p2"}]},
                        {"name": "p2", "transitions": [{"to": "$complete"}]}
                    ]},
                    {"name": "b", "phases": [
                        {"name": "q", "transitions": [{"to": "$complete"}]}
                    ]}
                ]
            }
        }

    def test_two_tracks_compile(self, compiler, tmp_path):
        path = write_manifest(tmp_path, self._build_two_track_recipe())
        result = compiler.compile(path)
        assert result.blob[16] == 2  # track_count

    def test_per_track_phase_arrays_dont_overlap(self, compiler, tmp_path):
        path = write_manifest(tmp_path, self._build_two_track_recipe())
        data = compiler.compile(path).blob
        track_table_off = struct.unpack_from("<H", data, 22)[0]

        # Track A
        ta_phases_off = struct.unpack_from("<H", data, track_table_off + 2)[0]
        ta_phase_count = data[track_table_off + 8]
        ta_end = ta_phases_off + 20 * ta_phase_count

        # Track B
        tb_phases_off = struct.unpack_from("<H", data, track_table_off + 16 + 2)[0]
        tb_phase_count = data[track_table_off + 16 + 8]
        tb_end = tb_phases_off + 20 * tb_phase_count

        # Ranges must not overlap
        assert ta_end <= tb_phases_off, (
            f"Track A phases [{ta_phases_off}..{ta_end}) overlaps з track B [{tb_phases_off}..{tb_end})"
        )

    def test_named_phase_target_within_track_resolves(self, compiler, tmp_path):
        path = write_manifest(tmp_path, self._build_two_track_recipe())
        data = compiler.compile(path).blob
        # Track A, phase[0] = "p1" з transition to "p2" (index 1)
        track_table_off = struct.unpack_from("<H", data, 22)[0]
        ta_phases_off = struct.unpack_from("<H", data, track_table_off + 2)[0]
        # phase[0].transitions_off
        p1_trans_off = struct.unpack_from("<H", data, ta_phases_off + 6)[0]
        # transition[0].target_phase
        target = struct.unpack_from("<H", data, p1_trans_off + 2)[0]
        assert target == 1, f"Expected p2 index 1, got {target}"

    def test_main_track_flag_preserved_for_first_track(self, compiler, tmp_path):
        path = write_manifest(tmp_path, self._build_two_track_recipe())
        data = compiler.compile(path).blob
        track_table_off = struct.unpack_from("<H", data, 22)[0]
        # Track A flags at offset+9
        ta_flags = data[track_table_off + 9]
        assert ta_flags & 0x01  # MODR_TRACK_FLAG_MAIN
        # Track B has no main_track flag
        tb_flags = data[track_table_off + 16 + 9]
        assert (tb_flags & 0x01) == 0

    def test_crc32_valid_multi_track(self, compiler, tmp_path):
        path = write_manifest(tmp_path, self._build_two_track_recipe())
        data = compiler.compile(path).blob
        crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        crc_computed = zlib.crc32(data[:-4]) & 0xFFFFFFFF
        assert crc_stored == crc_computed


class TestConditionalTransitions:
    """Step 2b.1: lift v0 'unconditional only' limitation. Conditions encoded
    у cond_pool; transitions reference them via cond_pool_idx. Pure time
    thresholds use kind=TIME з time_threshold_ms (no cond_pool entry consulted
    at runtime). Composite conditions (all_of/any_of/not) recursive."""

    MODR_TRANS_KIND_TIME = 0
    MODR_TRANS_KIND_COND = 1
    MODR_TRANS_KIND_UNCONDITIONAL = 4

    def _make_recipe_with_when(self, when):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["transitions"] = [
            {"to": "$complete", "when": when}
        ]
        return m

    def test_pure_time_elapsed_uses_kind_time(self, compiler, tmp_path):
        """{time_elapsed_ms: T} alone → kind=TIME, no cond_pool entry consumed."""
        m = self._make_recipe_with_when({"time_elapsed_ms": 5000})
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # phase[0] at offset 56+16=72, transitions_off at +6
        trans_off = struct.unpack_from("<H", data, 72 + 6)[0]
        # transition[0]: cond_pool_idx (2), target (2), time_threshold (4), kind (1)
        cond_idx = struct.unpack_from("<H", data, trans_off)[0]
        threshold = struct.unpack_from("<I", data, trans_off + 4)[0]
        kind = data[trans_off + 8]
        assert kind == self.MODR_TRANS_KIND_TIME
        assert threshold == 5000
        assert cond_idx == 0xFFFF  # MODR_NO_OFFSET — engine doesn't lookup cond_pool

    def test_state_key_eq_uses_kind_cond(self, compiler, tmp_path):
        m = self._make_recipe_with_when(
            {"state_key_eq": {"key": "test.input", "value": 42}}
        )
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        trans_off = struct.unpack_from("<H", data, 72 + 6)[0]
        cond_idx = struct.unpack_from("<H", data, trans_off)[0]
        kind = data[trans_off + 8]
        assert kind == self.MODR_TRANS_KIND_COND
        assert cond_idx == 0  # First condition у pool

        # Verify cond_pool_count і params populated
        cond_pool_count = struct.unpack_from("<H", data, 34)[0]
        assert cond_pool_count == 1
        param_pool_count = struct.unpack_from("<H", data, 26)[0]
        assert param_pool_count == 2  # key + value

    def test_composite_all_of_emits_children_first(self, compiler, tmp_path):
        # Composite з 2 children: time_elapsed_ms + state_key_gt
        m = self._make_recipe_with_when({
            "all_of": [
                {"time_elapsed_ms": 1000},
                {"state_key_gt": {"key": "test.signal", "value": 5}}
            ]
        })
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        cond_pool_count = struct.unpack_from("<H", data, 34)[0]
        # 3 conditions у pool: time_elapsed_ms, state_key_gt, all_of (parent)
        assert cond_pool_count == 3

        # Transition references the LAST condition (composite parent)
        trans_off = struct.unpack_from("<H", data, 72 + 6)[0]
        cond_idx = struct.unpack_from("<H", data, trans_off)[0]
        assert cond_idx == 2  # all_of is last

        # all_of's params reference indices 0 і 1
        cond_pool_off = struct.unpack_from("<H", data, 32)[0]
        param_pool_off = struct.unpack_from("<H", data, 24)[0]
        # all_of cond entry at cond_pool_off + 2*8 = +16
        all_of_param_idx = struct.unpack_from("<H", data, cond_pool_off + 16 + 2)[0]
        all_of_param_n = data[cond_pool_off + 16 + 4]
        assert all_of_param_n == 2
        # First param = i32 value 0 (index of first child)
        first_child_value = struct.unpack_from("<I", data, param_pool_off + all_of_param_idx * 8 + 4)[0]
        assert first_child_value == 0

    def test_composite_any_of(self, compiler, tmp_path):
        m = self._make_recipe_with_when({
            "any_of": [
                {"state_key_eq": {"key": "a", "value": 1}},
                {"state_key_eq": {"key": "b", "value": 2}}
            ]
        })
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # 3 cond entries: 2 state_key_eq + 1 any_of
        cond_pool_count = struct.unpack_from("<H", data, 34)[0]
        assert cond_pool_count == 3

    def test_composite_not(self, compiler, tmp_path):
        m = self._make_recipe_with_when({
            "not": {"state_key_eq": {"key": "x", "value": True}}
        })
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        cond_pool_count = struct.unpack_from("<H", data, 34)[0]
        assert cond_pool_count == 2  # state_key_eq + not (parent)

    def test_unconditional_when_absent(self, compiler, tmp_path):
        m = make_minimal_manifest()  # no `when` у transitions
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        trans_off = struct.unpack_from("<H", data, 72 + 6)[0]
        kind = data[trans_off + 8]
        assert kind == self.MODR_TRANS_KIND_UNCONDITIONAL
        # No cond_pool emitted — count = 0
        cond_pool_count = struct.unpack_from("<H", data, 34)[0]
        assert cond_pool_count == 0

    def test_state_key_in_range(self, compiler, tmp_path):
        m = self._make_recipe_with_when(
            {"state_key_in_range": {"key": "temp", "min": 20.0, "max": 30.0}}
        )
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        cond_pool_count = struct.unpack_from("<H", data, 34)[0]
        param_pool_count = struct.unpack_from("<H", data, 26)[0]
        assert cond_pool_count == 1
        assert param_pool_count == 3  # key + min + max

    def test_state_key_changed(self, compiler, tmp_path):
        m = self._make_recipe_with_when({"state_key_changed": {"key": "edge.signal"}})
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        param_pool_count = struct.unpack_from("<H", data, 26)[0]
        assert param_pool_count == 1  # key only

    def test_time_of_day_eq(self, compiler, tmp_path):
        m = self._make_recipe_with_when({"time_of_day_eq": {"hh": 6, "mm": 30}})
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        param_pool_count = struct.unpack_from("<H", data, 26)[0]
        assert param_pool_count == 2  # hh + mm

    def test_crc32_valid_with_conditions(self, compiler, tmp_path):
        m = self._make_recipe_with_when(
            {"all_of": [
                {"time_elapsed_ms": 1000},
                {"state_key_gt": {"key": "x", "value": 5}}
            ]}
        )
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        crc_computed = zlib.crc32(data[:-4]) & 0xFFFFFFFF
        assert crc_stored == crc_computed


class TestActionInvocations:
    """Step 2b.2: lift v0 'no entry/exit actions' limitation. Phase actions encoded
    у action_pool, з phase.entry_action_off / exit_action_off pointing to first
    action's index у pool. action.param_idx points into shared param_pool."""

    def _make_recipe_with_actions(self, entry=None, exit=None):
        m = make_minimal_manifest()
        if entry is not None:
            m["scenario"]["tracks"][0]["phases"][0]["entry"] = entry
        if exit is not None:
            m["scenario"]["tracks"][0]["phases"][0]["exit"] = exit
        return m

    def test_log_action_compiles(self, compiler, tmp_path):
        m = self._make_recipe_with_actions(entry=[
            {"action": "log", "params": {"msg": "hello"}}
        ])
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob

        # action_pool_count at offset 30
        action_count = struct.unpack_from("<H", data, 30)[0]
        assert action_count == 1
        # phase[0].entry_action_off (offset 0..1 у phase struct)
        # phase[0] at 56+16=72 (header + 1 track)
        entry_off = struct.unpack_from("<H", data, 72 + 2)[0]
        entry_n = data[72 + 8]
        assert entry_off == 0  # first entry у action_pool
        assert entry_n == 1

    def test_set_state_action_three_params(self, compiler, tmp_path):
        m = self._make_recipe_with_actions(entry=[
            {"action": "set_state", "params": {"key": "test.x", "type": "bool", "value": True}}
        ])
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob

        action_count = struct.unpack_from("<H", data, 30)[0]
        param_count = struct.unpack_from("<H", data, 26)[0]
        assert action_count == 1
        assert param_count == 3  # key + type + value

    def test_multiple_entry_actions(self, compiler, tmp_path):
        m = self._make_recipe_with_actions(entry=[
            {"action": "log", "params": {"msg": "first"}},
            {"action": "log", "params": {"msg": "second"}},
            {"action": "wait_ms", "params": {"ms": 500}}
        ])
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob

        action_count = struct.unpack_from("<H", data, 30)[0]
        assert action_count == 3
        entry_n = data[72 + 8]
        assert entry_n == 3

    def test_entry_and_exit_actions(self, compiler, tmp_path):
        m = self._make_recipe_with_actions(
            entry=[{"action": "set_state", "params": {"key": "x", "type": "bool", "value": True}}],
            exit=[{"action": "set_state", "params": {"key": "x", "type": "bool", "value": False}}]
        )
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob

        action_count = struct.unpack_from("<H", data, 30)[0]
        assert action_count == 2  # 1 entry + 1 exit
        entry_off = struct.unpack_from("<H", data, 72 + 2)[0]
        exit_off = struct.unpack_from("<H", data, 72 + 4)[0]
        entry_n = data[72 + 8]
        exit_n = data[72 + 9]
        assert entry_off == 0
        assert exit_off == 1
        assert entry_n == 1
        assert exit_n == 1

    def test_no_actions_emits_no_offset(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        entry_off = struct.unpack_from("<H", data, 72 + 2)[0]
        exit_off = struct.unpack_from("<H", data, 72 + 4)[0]
        assert entry_off == 0xFFFF  # MODR_NO_OFFSET
        assert exit_off == 0xFFFF

    def test_unknown_action_allowed_runtime_registered(self, compiler, tmp_path):
        # Per Step 2b.2 design — unknown actions accepted (могла би register
        # domain module runtime). Strict mode added later.
        m = self._make_recipe_with_actions(entry=[
            {"action": "domain.custom_action", "params": {}}
        ])
        path = write_manifest(tmp_path, m)
        # Should compile without error
        result = compiler.compile(path)
        assert result.module_name == "recipe_min"

    def test_action_param_count_validation(self, compiler, tmp_path):
        # log expects exactly 1 param (msg). Sending 0 should fail.
        m = self._make_recipe_with_actions(entry=[
            {"action": "log", "params": {}}  # missing msg
        ])
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0222"

    def test_string_param_interned_in_pool(self, compiler, tmp_path):
        m = self._make_recipe_with_actions(entry=[
            {"action": "log", "params": {"msg": "interned_string_xyz"}}
        ])
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # The param value points to string pool offset; verify string actually present.
        assert b"interned_string_xyz" in data

    def test_crc32_valid_with_actions(self, compiler, tmp_path):
        m = self._make_recipe_with_actions(entry=[
            {"action": "log", "params": {"msg": "test"}},
            {"action": "set_state", "params": {"key": "k", "type": "i32", "value": 42}}
        ])
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        crc_computed = zlib.crc32(data[:-4]) & 0xFFFFFFFF
        assert crc_stored == crc_computed


class TestGlobalTransitions:
    """Step 2b.3: lift v0 'no global transitions' limitation. Globals evaluated
    each tick before per-phase transitions, sorted by priority descending.
    Always target $abort з scope ∈ {abort_scenario, abort_only_main_track}."""

    def test_global_transition_compiles(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["global_transitions"] = [
            {"to": "$abort", "when": {"state_key_eq": {"key": "test.fault", "value": True}},
             "priority": 200}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # global_trans_count at offset 19
        assert data[19] == 1
        # global_trans_off at offset 38..39
        gt_off = struct.unpack_from("<H", data, 38)[0]
        assert gt_off > 0  # populated

    def test_global_flag_set(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["global_transitions"] = [
            {"to": "$abort", "when": {"state_key_eq": {"key": "x", "value": True}}}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # MODR_FLAG_HAS_GLOBALS = 1<<1 = 2
        flags = struct.unpack_from("<H", data, 6)[0]
        assert flags & 0x02

    def test_global_to_must_be_abort(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["global_transitions"] = [
            {"to": "phase_x", "when": {"state_key_eq": {"key": "x", "value": True}}}
        ]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0223"

    def test_globals_sorted_by_priority_descending(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["global_transitions"] = [
            {"to": "$abort", "when": {"state_key_eq": {"key": "low", "value": True}}, "priority": 50},
            {"to": "$abort", "when": {"state_key_eq": {"key": "high", "value": True}}, "priority": 200},
            {"to": "$abort", "when": {"state_key_eq": {"key": "mid", "value": True}}, "priority": 100},
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        gt_off = struct.unpack_from("<H", data, 38)[0]
        # Each global_trans 8 bytes; priority at offset +5
        priorities = [data[gt_off + i * 8 + 5] for i in range(3)]
        assert priorities == [200, 100, 50]  # descending

    def test_main_track_scope(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["global_transitions"] = [
            {"to": "$abort", "when": {"state_key_eq": {"key": "x", "value": True}},
             "scope": "abort_only_main_track"}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        gt_off = struct.unpack_from("<H", data, 38)[0]
        scope = data[gt_off + 6]  # scope field at offset +6
        assert scope == 1  # MODR_GLOBAL_SCOPE_MAIN_TRACK


class TestPhaseScopeResources:
    """Step 2b.3: lift v0 'no phase-scope resources' limitation. Each phase has
    its own claim array; engine claims at phase entry, releases at exit."""

    def test_phase_resources_compile(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["phase_resources"] = [
            {"resource": "equipment.pump", "exclusive": True}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob

        # phase[0] at offset 72; phase_resources_off at +16, phase_resource_n at +18
        pr_off = struct.unpack_from("<H", data, 72 + 16)[0]
        pr_n = data[72 + 18]
        assert pr_n == 1
        assert pr_off != 0xFFFF  # actual offset, not sentinel

        # Verify claim contents
        resource_hash = struct.unpack_from("<H", data, pr_off)[0]
        # djb2_hash16("equipment.pump")
        expected_hash = djb2_hash16("equipment.pump")
        assert resource_hash == expected_hash
        exclusive = data[pr_off + 2]
        assert exclusive == 1

    def test_no_phase_resources_emits_no_offset(self, compiler, tmp_path):
        path = write_manifest(tmp_path, make_minimal_manifest())
        data = compiler.compile(path).blob
        pr_off = struct.unpack_from("<H", data, 72 + 16)[0]
        pr_n = data[72 + 18]
        assert pr_off == 0xFFFF
        assert pr_n == 0

    def test_resources_flag_set_for_phase_scope(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["phase_resources"] = [
            {"resource": "equipment.x", "exclusive": True}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        flags = struct.unpack_from("<H", data, 6)[0]
        assert flags & 0x04  # MODR_FLAG_HAS_RESOURCES

    def test_multiple_phases_distinct_offsets(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"] = [
            {"name": "p1", "transitions": [{"to": "p2"}],
             "phase_resources": [{"resource": "a.x"}, {"resource": "a.y"}]},
            {"name": "p2", "transitions": [{"to": "$complete"}],
             "phase_resources": [{"resource": "b.z"}]}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        # phase[0] at 72, phase[1] at 72+20=92
        p1_off = struct.unpack_from("<H", data, 72 + 16)[0]
        p1_n = data[72 + 18]
        p2_off = struct.unpack_from("<H", data, 92 + 16)[0]
        p2_n = data[92 + 18]
        assert p1_n == 2
        assert p2_n == 1
        # p2 starts after p1's 2 claims (4 bytes each = 8 bytes apart)
        assert p2_off == p1_off + 8

    def test_crc32_valid_with_globals_and_phase_resources(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["phase_resources"] = [
            {"resource": "equipment.pump"}
        ]
        m["scenario"]["global_transitions"] = [
            {"to": "$abort", "when": {"state_key_eq": {"key": "x", "value": True}}}
        ]
        path = write_manifest(tmp_path, m)
        data = compiler.compile(path).blob
        crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        crc_computed = zlib.crc32(data[:-4]) & 0xFFFFFFFF
        assert crc_stored == crc_computed
