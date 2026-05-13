/**
 * @file test_nvs_token.cpp
 * @brief NVS token smoke tests: magic bump, CRC16, serialize roundtrip.
 *
 * Focus on what Phase 2 introduces (magic SQTK→SCTK) і preserved invariants
 * (CRC16, 96-byte payload). Full deserialize roundtrip needs a loaded
 * scenario (modr buffer) — exercised у HIL test Phase 4.
 */

#include "modesp/scenario/nvs_token.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace modesp::scenario;

int main() {
    // 1. Magic constant matches 'SCTK' (bump from old 'SQTK'). Verified
    // byte-by-byte to catch endian regressions.
    {
        constexpr uint32_t expected = 0x4B544353;  // 'S' 'C' 'T' 'K' LE
        assert(SEQ_TOKEN_MAGIC == expected);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&SEQ_TOKEN_MAGIC);
        assert(bytes[0] == 'S');
        assert(bytes[1] == 'C');
        assert(bytes[2] == 'T');
        assert(bytes[3] == 'K');
    }

    // 2. Old 'SQTK' magic gets rejected. Crafts а token with old magic
    // (just enough header bytes to trigger magic check) — deserialize_token
    // returns INVALID_FILE.
    {
        uint8_t buf[SEQ_TOKEN_SIZE] = {0};
        const uint32_t old_magic = 0x4B545153;  // 'S' 'Q' 'T' 'K' LE
        std::memcpy(buf, &old_magic, sizeof(old_magic));
        // version bytes... irrelevant; magic check fires first
        SequenceRuntime sr;
        // sr.scenario.header() will return null since no scenario loaded —
        // but deserialize_token should fail at magic check before that.
        EngineError err = deserialize_token(buf, SEQ_TOKEN_SIZE, sr);
        assert(err == EngineError::INVALID_FILE);
    }

    // 3. CRC16-CCITT (XMODEM) golden vector. The single-character "A"
    // (0x41) → 0x58E5 per XMODEM checksum tables. Locks our impl
    // to industry standard.
    {
        const uint8_t test[] = {'A'};
        uint16_t crc = crc16_ccitt(test, 1);
        assert(crc == 0x58E5);
    }

    // 4. Token size invariant — 96 bytes exact (matches plan Q7 layout).
    {
        static_assert(sizeof(seq_token) == 96, "token size locked to 96");
        static_assert(SEQ_TOKEN_SIZE == 96, "SEQ_TOKEN_SIZE constant matches");
    }

    // 5. Bad-size buffer rejected (too small для full token)
    {
        uint8_t small[16] = {};
        SequenceRuntime sr;
        EngineError err = deserialize_token(small, sizeof(small), sr);
        // Implementation should detect undersized buffer і reject before
        // dereferencing — INVALID_FILE expected.
        assert(err == EngineError::INVALID_FILE);
    }

    std::printf("test_nvs_token: OK (5 cases)\n");
    return 0;
}
