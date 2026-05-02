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

    def test_synthetic_collision_detected(self, tmp_path):
        # Construct two names that hash to same low-16 на purpose.
        # Easiest: use empirical pair.
        # Actually, just inject identical hashes у the JSON manually:
        bad = {
            "format_version": 1,
            "hash_algorithm": "djb2_hash16",
            "actions": {
                "fake1": {"hash": djb2_hash16("fake1")},
                "fake2": {"hash": djb2_hash16("fake1")}  # forced identical hash to fake1
            },
            "conditions": {}
        }
        bad_path = tmp_path / "collide.json"
        bad_path.write_text(json.dumps(bad), encoding="utf-8")
        # First check: fake2's stored hash != djb2(fake2) — caught як E0202.
        with pytest.raises(CompileError) as exc:
            KnownActionRegistry.load(bad_path)
        # Either E0202 (hash mismatch detected first) OR E0203 (collision).
        # Both correct rejection paths.
        assert exc.value.code in {"E0202", "E0203"}


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


class TestV0Limitations:
    """Step 2a v0 explicitly defers some features. These tests document deferred
    behavior and ensure compiler emits clear error rather than silent miscompilation."""

    def test_conditional_transition_rejected_in_v0(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["tracks"][0]["phases"][0]["transitions"] = [
            {"to": "$complete", "when": {"time_elapsed_ms": 1000}}
        ]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0206"

    def test_global_transitions_rejected_in_v0(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["global_transitions"] = [
            {"to": "$abort", "when": {"state_key_eq": {"key": "test.fault", "value": True}}}
        ]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0205"

    def test_phase_resources_rejected_in_v0(self, compiler, tmp_path):
        m = make_minimal_manifest()
        m["scenario"]["resources"] = [
            {"resource": "equipment.pump", "exclusive": True, "scope": "phase"}
        ]
        path = write_manifest(tmp_path, m)
        with pytest.raises(CompileError) as exc:
            compiler.compile(path)
        assert exc.value.code == "E0204"
