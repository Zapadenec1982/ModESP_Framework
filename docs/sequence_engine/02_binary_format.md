# 02 — `.modr` Binary Format

**Status:** placeholder. **Заповнюється у Step 1** (modr_format.h commit).

## Заповнюється

- Magic, version, endianness, alignment
- Header layout (56 bytes) — byte-by-byte fields
- Track table (16 bytes per track)
- Phase struct (16 bytes per phase)
- Transition struct (8 bytes)
- Global transition struct (8 bytes)
- Action struct (6 bytes)
- Param entry struct (8 bytes)
- Resource decl struct (4 bytes)
- String pool format (length-prefixed entries, 1-byte aligned)
- CRC32 trailer (CRC-32/ISO-HDLC)
- Validation rules table — referenced by `compile_scenario.py` і `modr_loader.cpp`
- Maximum sizes: `MODR_MAX_SIZE = 16 KB`, `MAX_TRACKS_PER_SCENARIO = 6`, etc.

## Reference

- See ADR-0001 для motivation за binary blob (vs constexpr).
- See plan Q1 для current spec до Step 1 implementation.
