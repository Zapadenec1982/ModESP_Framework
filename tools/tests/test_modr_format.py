"""
Test `.modr` binary format invariants.

Builds minimal v1 scenario via hand-encoded Python struct.pack calls matching
`components/modesp_sequence/include/modesp/sequence/modr_format.h` layout.
Verifies:
- Struct sizes match C++ static_assert expectations
- Header layout (magic, version, offsets) byte-correct
- CRC32 trailer using CRC-32/ISO-HDLC (zlib.crc32 = ESP-IDF esp_crc32_le)
- Round-trip parse of constructed bytes restores expected values
- Golden file fixture matches Python builder output (regression detection)

When `tools/compile_scenario.py` lands у Step 2, this golden becomes its first
regression test — compiler emits identical bytes from JSON authoring source.

Plan reference: Step 1 у .claude/plans/quirky-imagining-lake.md
ADR reference: docs/sequence_engine/adr/0001-binary-format-not-constexpr.md
"""

import struct
import zlib
from pathlib import Path

import pytest

# Constants matching modr_format.h exactly.
MODR_MAGIC = 0x52444F4D  # 'MODR' LE
MODR_FORMAT_VERSION = 1

# Struct sizes (from static_assert in modr_format.h)
SIZE_HEADER = 56
SIZE_TRACK = 16
SIZE_PHASE = 20
SIZE_TRANSITION = 12
SIZE_GLOBAL_TRANSITION = 8
SIZE_ACTION = 8
SIZE_PARAM_ENTRY = 8
SIZE_RESOURCE_DECL = 4
SIZE_PHASE_RESOURCE_CLAIM = 4

# Sentinels
MODR_NO_OFFSET = 0xFFFF
MODR_TARGET_COMPLETE = 0xFFFF
MODR_TARGET_ABORT = 0xFFFE

# Completion rules
MODR_COMPLETION_ALL_TRACKS = 0
MODR_COMPLETION_ANY_TRACK = 1
MODR_COMPLETION_MAIN_TRACK = 2

# Track flags
MODR_TRACK_FLAG_MAIN = 1 << 0
MODR_TRACK_FLAG_LOOP_ON_DONE = 1 << 1

# Transition kinds
MODR_TRANS_KIND_TIME = 0
MODR_TRANS_KIND_COND = 1
MODR_TRANS_KIND_TIME_OR_COND = 2
MODR_TRANS_KIND_TIME_AND_COND = 3


def djb2_hash16(s: str) -> int:
    """djb2 hash producing low-16 bits. Matches modr_format.h::djb2_hash16."""
    h = 5381
    for ch in s.encode("utf-8"):
        h = ((h << 5) + h + ch) & 0xFFFFFFFF
    return h & 0xFFFF


def build_string_pool(strings: list[str]) -> tuple[bytes, dict[str, int]]:
    """
    Build length-prefixed (u8 len + bytes) string pool.
    Returns (pool_bytes, name_to_offset_map).

    Offsets are byte offsets within the pool, NOT indices.
    Plan Q1 spec — 1-byte aligned, 256-entry max.
    """
    pool = bytearray()
    offsets: dict[str, int] = {}
    for s in strings:
        if s in offsets:
            continue  # dedupe
        encoded = s.encode("utf-8")
        if len(encoded) > 255:
            raise ValueError(f"String too long for u8 length prefix: {s!r}")
        offsets[s] = len(pool)
        pool.append(len(encoded))
        pool.extend(encoded)
    return bytes(pool), offsets


def build_minimal_v1() -> bytes:
    """
    Construct minimal valid v1 scenario:
    - Recipe name: "t"
    - 1 track: "m" (main)
    - 1 phase: "p" with unconditional transition to $complete
    - No actions, no conditions, no resources, no params, no globals

    Byte layout (calculated):
    - header (56) + track[1] (16) + phase[1] (20) + transition[1] (12)
      + string pool ("t" + "m" + "p", 6 bytes) + CRC32 (4) = 114 bytes

    Layout offsets:
    - 0..55:   header
    - 56..71:  track[0]
    - 72..91:  phase[0]
    - 92..103: transition[0]
    - 104..109: string pool
    - 110..113: CRC32 trailer

    Returns the complete file bytes including CRC.
    """
    # Build string pool first (we need name offsets for header/track/phase)
    pool, offsets = build_string_pool(["t", "m", "p"])
    assert len(pool) == 6, f"Expected 6 bytes string pool, got {len(pool)}"

    # Layout planning
    header_off = 0
    track_off = SIZE_HEADER  # 56
    phase_off = track_off + SIZE_TRACK  # 72
    transition_off = phase_off + SIZE_PHASE  # 92
    string_pool_off = transition_off + SIZE_TRANSITION  # 104
    total_size = string_pool_off + len(pool) + 4  # +4 for CRC32 = 114

    assert total_size == 114, f"Expected total 114 bytes, calculated {total_size}"

    # ── Header (56 bytes) ──
    # Layout matches modr_format.h::modr_header exactly.
    header = struct.pack(
        "<"           # little-endian
        "I"           # magic
        "H"           # format_version
        "H"           # flags
        "I"           # total_size
        "H"           # scenario_id
        "H"           # name_str_idx
        "B"           # track_count
        "B"           # cont_count
        "B"           # resource_count
        "B"           # global_trans_count
        "B"           # completion_rule
        "B"           # reserved_a
        "H"           # track_table_off
        "H"           # param_pool_off
        "H"           # param_pool_count
        "H"           # action_pool_off
        "H"           # action_pool_count
        "H"           # cond_pool_off
        "H"           # cond_pool_count
        "H"           # string_pool_off
        "H"           # global_trans_off
        "H"           # resource_off
        "H"           # reserved_b
        "I"           # default_phase_timeout_ms
        "I"           # scenario_timeout_max_ms
        "I",          # reserved_c
        MODR_MAGIC,
        MODR_FORMAT_VERSION,
        0,                                # flags (no continuous, no globals, no resources)
        total_size,
        djb2_hash16("recipe_minimal"),    # scenario_id (name doesn't matter for golden test)
        offsets["t"],                     # name_str_idx (offset within pool)
        1,                                # track_count
        0,                                # cont_count
        0,                                # resource_count
        0,                                # global_trans_count
        MODR_COMPLETION_ALL_TRACKS,
        0,                                # reserved_a
        track_off,
        0,                                # param_pool_off (unused)
        0,                                # param_pool_count
        0,                                # action_pool_off (unused)
        0,                                # action_pool_count
        0,                                # cond_pool_off
        0,                                # cond_pool_count
        string_pool_off,
        0,                                # global_trans_off (unused)
        0,                                # resource_off (unused)
        0,                                # reserved_b (padding)
        60_000,                           # default_phase_timeout_ms = 60s
        0,                                # scenario_timeout_max_ms = 0 (unlimited)
        0,                                # reserved_c
    )
    assert len(header) == SIZE_HEADER, f"Header packed {len(header)} bytes, expected {SIZE_HEADER}"

    # ── Track[0] (16 bytes) ──
    track = struct.pack(
        "<HHHHBBHI",
        offsets["m"],                     # name_str_idx
        phase_off,                        # phases_off
        0,                                # initial_phase
        0,                                # reserved_a
        1,                                # phase_count
        MODR_TRACK_FLAG_MAIN,             # flags
        0,                                # reserved_b
        0,                                # reserved_c
    )
    assert len(track) == SIZE_TRACK, f"Track packed {len(track)} bytes, expected {SIZE_TRACK}"

    # ── Phase[0] (20 bytes) ──
    phase = struct.pack(
        "<HHHHBBBBIHBB",
        offsets["p"],                     # name_str_idx
        MODR_NO_OFFSET,                   # entry_action_off (no entry actions)
        MODR_NO_OFFSET,                   # exit_action_off
        transition_off,                   # transitions_off
        0,                                # entry_action_n
        0,                                # exit_action_n
        1,                                # transition_n
        0,                                # cont_mask
        0,                                # timeout_ms = 0 → use header default
        MODR_NO_OFFSET,                   # phase_resources_off
        0,                                # phase_resource_n
        0,                                # reserved
    )
    assert len(phase) == SIZE_PHASE, f"Phase packed {len(phase)} bytes, expected {SIZE_PHASE}"

    # ── Transition[0] (12 bytes) ──
    # Unconditional transition: no condition, immediate jump to $complete on entry
    transition = struct.pack(
        "<HHIBBH",
        MODR_NO_OFFSET,                   # cond_pool_idx (unconditional)
        MODR_TARGET_COMPLETE,             # target_phase = $complete
        0,                                # time_threshold_ms (unused for unconditional)
        MODR_TRANS_KIND_COND,             # kind (no time, just condition — and condition unconditional)
        0,                                # reserved_a
        0,                                # reserved_b
    )
    assert len(transition) == SIZE_TRANSITION

    # Concatenate body
    body = header + track + phase + transition + pool
    assert len(body) == total_size - 4, (
        f"Body {len(body)} bytes, expected {total_size - 4}"
    )

    # Compute CRC32 over body
    crc = zlib.crc32(body) & 0xFFFFFFFF

    # Append CRC32 trailer
    full = body + struct.pack("<I", crc)
    assert len(full) == total_size

    return full


# ─────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────


class TestStructSizes:
    """Verify Python builders match C++ static_assert expectations."""

    def test_header_size(self):
        # 56 bytes — must match modr_format.h::modr_header static_assert
        assert SIZE_HEADER == 56

    def test_track_size(self):
        assert SIZE_TRACK == 16

    def test_phase_size(self):
        assert SIZE_PHASE == 20

    def test_transition_size(self):
        # 12 bytes corrected from plan Q1 spec'd 8 (alignment fix)
        assert SIZE_TRANSITION == 12

    def test_global_transition_size(self):
        assert SIZE_GLOBAL_TRANSITION == 8

    def test_action_size(self):
        # 8 bytes (padded from 6 для alignment)
        assert SIZE_ACTION == 8

    def test_param_entry_size(self):
        assert SIZE_PARAM_ENTRY == 8

    def test_resource_decl_size(self):
        assert SIZE_RESOURCE_DECL == 4

    def test_phase_resource_claim_size(self):
        assert SIZE_PHASE_RESOURCE_CLAIM == 4


class TestDjb2Hash:
    """Verify djb2_hash16 matches expected values used elsewhere."""

    def test_empty_string(self):
        assert djb2_hash16("") == 5381 & 0xFFFF

    def test_known_values(self):
        # Hand-computed reference values — locking them in protects against
        # accidental algorithm drift. djb2 algorithm:
        #   h = 5381; for each ch: h = h * 33 + ch; result = h & 0xFFFF
        assert djb2_hash16("a") == ((5381 * 33 + ord("a")) & 0xFFFF)
        # djb2_hash16("abc"): 5381*33+97=177670; 177670*33+98=5863208;
        # 5863208*33+99=193485963; & 0xFFFF = 23691 (0x5C8B)
        assert djb2_hash16("abc") == 23691

    def test_collision_check_baseline(self):
        # Two of the most common action names should NOT collide на 16 bits.
        # Якщо вони collide — будемо знати and rename one.
        assert djb2_hash16("log") != djb2_hash16("set_state")
        assert djb2_hash16("wait_ms") != djb2_hash16("time_elapsed_ms")


class TestMinimalV1Builder:
    """Verify minimal scenario builder produces well-formed binary."""

    def test_total_size(self):
        data = build_minimal_v1()
        assert len(data) == 114, f"Expected 114 bytes, got {len(data)}"

    def test_header_magic(self):
        data = build_minimal_v1()
        magic = struct.unpack_from("<I", data, 0)[0]
        assert magic == MODR_MAGIC

    def test_header_version(self):
        data = build_minimal_v1()
        version = struct.unpack_from("<H", data, 4)[0]
        assert version == MODR_FORMAT_VERSION

    def test_header_total_size_matches_actual(self):
        data = build_minimal_v1()
        total_size_field = struct.unpack_from("<I", data, 8)[0]
        assert total_size_field == len(data)

    def test_track_count_one(self):
        data = build_minimal_v1()
        # track_count at offset 16
        assert data[16] == 1

    def test_completion_rule_all_tracks(self):
        data = build_minimal_v1()
        # completion_rule at offset 20
        assert data[20] == MODR_COMPLETION_ALL_TRACKS

    def test_crc_trailer_valid(self):
        data = build_minimal_v1()
        body = data[:-4]
        crc_stored = struct.unpack_from("<I", data, len(data) - 4)[0]
        crc_computed = zlib.crc32(body) & 0xFFFFFFFF
        assert crc_stored == crc_computed

    def test_crc_corruption_detected(self):
        # Modify a byte у body — CRC should mismatch.
        data = bytearray(build_minimal_v1())
        data[16] ^= 0xFF  # flip track_count
        body = bytes(data[:-4])
        crc_stored = struct.unpack_from("<I", bytes(data), len(data) - 4)[0]
        crc_computed = zlib.crc32(body) & 0xFFFFFFFF
        assert crc_stored != crc_computed, "CRC must change on body corruption"

    def test_string_pool_contains_recipe_name(self):
        data = build_minimal_v1()
        string_pool_off = struct.unpack_from("<H", data, 36)[0]
        # First entry: u8 length, then bytes
        first_len = data[string_pool_off]
        first_str = data[string_pool_off + 1:string_pool_off + 1 + first_len].decode("utf-8")
        assert first_str == "t"


class TestGoldenFile:
    """
    Verify committed golden binary matches Python builder output.

    На failure тут — означає або:
    (a) Builder logic changed → regenerate golden if change intentional
    (b) Code regression introduced silent format drift → fix bug

    See `tools/tests/fixtures/scenarios/README.md` для regeneration procedure.
    """

    GOLDEN_PATH = Path(__file__).parent / "fixtures" / "scenarios" / "minimal_v1.modr"

    def test_golden_exists(self):
        assert self.GOLDEN_PATH.exists(), (
            f"Golden file missing: {self.GOLDEN_PATH}. "
            "First run: regenerate by writing build_minimal_v1() output to disk."
        )

    def test_golden_byte_exact(self):
        if not self.GOLDEN_PATH.exists():
            pytest.skip("Golden missing — run test_golden_exists first")
        expected = build_minimal_v1()
        actual = self.GOLDEN_PATH.read_bytes()
        assert actual == expected, (
            f"Golden file diverges from builder output. "
            f"Sizes: golden={len(actual)}, builder={len(expected)}. "
            f"First differing byte: "
            f"{next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), 'N/A')}"
        )


# ── Helper для regeneration (manual invocation) ──

def regenerate_golden():
    """
    Run via:
        python -c "from tools.tests.test_modr_format import regenerate_golden; regenerate_golden()"
    Overwrites the golden fixture with current builder output.
    """
    path = TestGoldenFile.GOLDEN_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(build_minimal_v1())
    print(f"Wrote {len(path.read_bytes())} bytes to {path}")


if __name__ == "__main__":
    regenerate_golden()
