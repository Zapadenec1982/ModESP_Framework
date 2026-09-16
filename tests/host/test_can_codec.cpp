/**
 * @file test_can_codec.cpp
 * @brief ModESP-CAN v1: кодек кадрів + кеш віддалених вузлів (host, doctest).
 *
 * Хеш/CRC-вектори СПІЛЬНІ з tools/tests/test_can_registry.py — обидві
 * реалізації (Python-генератор і цей C++) мають збігатися, інакше підписка
 * драйвера-моста не зійдеться з ID вузла-відправника. Значення зафіксовані
 * назавжди: це wire-контракт протоколу (ADR-004).
 */

#include "doctest.h"

#include "modesp/can/can_codec.h"
#include "modesp/can/can_remote_cache.h"

using namespace modesp;
using namespace modesp::can;

TEST_SUITE("CanCodec") {

// ── Хеші (дзеркало Python can_djb2_32 / can_fold16) ──

TEST_CASE("djb2: канонічні значення") {
    CHECK(djb2_32("") == 5381u);
    CHECK(djb2_32("a") == 5381u * 33u + uint8_t('a'));
}

TEST_CASE("fold16: wire-контракт — значення зафіксовані назавжди") {
    CHECK(fold16("equipment.air_temp") == 0xE753);
    CHECK(fold16("simple_thermo.temperature") == 0x2B0B);
    CHECK(fold16("keypad.cmd_start") == 0x4184);
}

TEST_CASE("fold16: нуль зарезервовано") {
    CHECK(fold16("") >= 1);
    CHECK(fold16("equipment.x") >= 1);
}

TEST_CASE("crc16_ccitt: канонічний вектор") {
    const uint8_t v[] = {'1','2','3','4','5','6','7','8','9'};
    CHECK(crc16_ccitt(v, sizeof(v)) == 0x29B1);
}

// ── 29-бітний ID ──

TEST_CASE("pack/unpack ID: roundtrip усіх полів") {
    FrameId f{PRIO_EVENT, FrameKind::EVENT, 42, 0xBEEF};
    uint32_t raw = pack_id(f);
    CHECK((raw >> 29) == 0);   // вміщується у 29 біт
    FrameId u = unpack_id(raw);
    CHECK(u.prio == PRIO_EVENT);
    CHECK(u.kind == FrameKind::EVENT);
    CHECK(u.src == 42);
    CHECK(u.id16 == 0xBEEF);
}

TEST_CASE("pack ID: пріоритет домінує в арбітражі") {
    // Менший prio → менший ID → виграє арбітраж CAN
    uint32_t ev = pack_id({PRIO_EVENT, FrameKind::EVENT, 200, 0xFFFF});
    uint32_t st = pack_id({PRIO_STATE, FrameKind::STATE, 1, 0x0001});
    uint32_t hb = pack_id({PRIO_HEARTBEAT, FrameKind::HEARTBEAT, 1, HEARTBEAT_MAGIC});
    CHECK(ev < st);
    CHECK(st < hb);
}

// ── STATE payload ──

TEST_CASE("STATE f32: roundtrip + DLC") {
    StatePayload p;
    p.type = ValType::F32; p.valid = true; p.f32 = -18.5f;
    uint8_t buf[8];
    size_t dlc = encode_state(p, buf);
    CHECK(dlc == 5);
    CHECK((buf[0] & STATE_VALID_BIT) != 0);

    StatePayload out;
    REQUIRE(decode_state(buf, dlc, out));
    CHECK(out.type == ValType::F32);
    CHECK(out.valid);
    CHECK(out.f32 == doctest::Approx(-18.5f));
}

TEST_CASE("STATE i32: roundtrip від'ємного значення") {
    StatePayload p;
    p.type = ValType::I32; p.valid = true; p.i32 = -123456;
    uint8_t buf[8];
    size_t dlc = encode_state(p, buf);
    CHECK(dlc == 5);

    StatePayload out;
    REQUIRE(decode_state(buf, dlc, out));
    CHECK(out.type == ValType::I32);
    CHECK(out.i32 == -123456);
}

TEST_CASE("STATE bool: компактний DLC=2") {
    StatePayload p;
    p.type = ValType::BOOL; p.valid = true; p.b = true;
    uint8_t buf[8];
    size_t dlc = encode_state(p, buf);
    CHECK(dlc == 2);

    StatePayload out;
    REQUIRE(decode_state(buf, dlc, out));
    CHECK(out.type == ValType::BOOL);
    CHECK(out.b);
}

TEST_CASE("STATE: VALID-біт переносить несправність джерела") {
    StatePayload p;
    p.type = ValType::F32; p.valid = false; p.f32 = 3.0f;
    uint8_t buf[8];
    size_t dlc = encode_state(p, buf);

    StatePayload out;
    REQUIRE(decode_state(buf, dlc, out));
    CHECK_FALSE(out.valid);
    CHECK(out.f32 == doctest::Approx(3.0f));   // значення все одно їде (діагностика)
}

TEST_CASE("STATE decode: зіпсовані кадри відкидаються") {
    uint8_t buf[8] = {};
    StatePayload out;
    CHECK_FALSE(decode_state(buf, 0, out));            // порожній
    CHECK_FALSE(decode_state(buf, 1, out));            // лише тип
    buf[0] = 0x0F;                                     // невідомий тип
    CHECK_FALSE(decode_state(buf, 5, out));
    buf[0] = uint8_t(ValType::F32);
    CHECK_FALSE(decode_state(buf, 3, out));            // короткий для f32
}

// ── HEARTBEAT payload ──

TEST_CASE("HEARTBEAT: roundtrip") {
    HeartbeatPayload hb;
    hb.flags = HB_FLAG_SAFE_MODE;
    hb.registry_crc16 = 0xABCD;
    hb.uptime_s = 86400;
    uint8_t buf[8];
    CHECK(encode_heartbeat(hb, buf) == 8);

    HeartbeatPayload out;
    REQUIRE(decode_heartbeat(buf, 8, out));
    CHECK(out.proto_ver == PROTO_VER);
    CHECK(out.flags == HB_FLAG_SAFE_MODE);
    CHECK(out.registry_crc16 == 0xABCD);
    CHECK(out.uptime_s == 86400u);
}

TEST_CASE("HEARTBEAT: короткий кадр відкидається") {
    uint8_t buf[8] = {};
    HeartbeatPayload out;
    CHECK_FALSE(decode_heartbeat(buf, 7, out));
}

} // TEST_SUITE CanCodec

// ═══════════════════════════════════════════════════════════════

TEST_SUITE("CanRemoteCache") {

// Кеш — singleton; кожен кейс починає з clear()
struct CacheFixture {
    CanRemoteCache& c = CanRemoteCache::instance();
    CacheFixture() { c.clear(); }
};

TEST_CASE_FIXTURE(CacheFixture, "subscribe: ідемпотентність + межі") {
    const CanReading* r1 = c.subscribe(10, 0x1234);
    REQUIRE(r1 != nullptr);
    CHECK(c.subscribe(10, 0x1234) == r1);      // та сама пара → той самий слот
    CHECK(c.subscribe(0, 0x1234) == nullptr);  // node 0 — invalid
    CHECK(c.subscribe(254, 0x1234) == nullptr);
    CHECK(c.subscribe(10, 0) == nullptr);      // key-id 0 — зарезервовано
}

TEST_CASE_FIXTURE(CacheFixture, "state frame оновлює лише свою підписку") {
    const CanReading* r = c.subscribe(10, 0x1234);
    REQUIRE(r);

    StatePayload p;
    p.type = ValType::F32; p.valid = true; p.f32 = 21.5f;
    c.on_state_frame(10, 0x1234, p, 1'000'000);
    CHECK(r->valid);
    CHECK(r->f32 == doctest::Approx(21.5f));
    CHECK(r->last_us == 1'000'000);

    // Інший key того ж вузла / той самий key іншого вузла — не чіпають кеш
    p.f32 = 99.0f;
    c.on_state_frame(10, 0x9999, p, 2'000'000);
    c.on_state_frame(11, 0x1234, p, 2'000'000);
    CHECK(r->f32 == doctest::Approx(21.5f));
}

TEST_CASE_FIXTURE(CacheFixture, "liveness: heartbeat → online, тиша → offline + invalid") {
    const CanReading* r = c.subscribe(10, 0x1234);
    REQUIRE(r);
    CHECK_FALSE(c.node_online(10));   // ще не бачили

    c.on_heartbeat(10, 0, 1'000'000);
    CHECK(c.tick(1'100'000, 3'500'000));    // перехід → online (changed)
    CHECK(c.node_online(10));

    StatePayload p;
    p.type = ValType::BOOL; p.valid = true; p.b = true;
    c.on_state_frame(10, 0x1234, p, 1'200'000);
    CHECK(r->valid);

    // 3.5 c тиші → offline, підписки invalid (failsafe без очікування stale_ms)
    CHECK(c.tick(1'200'000 + 3'600'000, 3'500'000));
    CHECK_FALSE(c.node_online(10));
    CHECK_FALSE(r->valid);

    // Без переходів — tick не рапортує змін
    CHECK_FALSE(c.tick(1'200'000 + 3'700'000, 3'500'000));
}

TEST_CASE_FIXTURE(CacheFixture, "register_node: трекінг ще до першого кадру") {
    c.register_node(20);
    CHECK(c.node_count() == 1);
    REQUIRE(c.node_at(0));
    CHECK(c.node_at(0)->node == 20);
    CHECK(c.node_at(0)->registered);
    CHECK_FALSE(c.node_at(0)->online);
}

TEST_CASE_FIXTURE(CacheFixture, "heartbeat незнайомого вузла трекається (discovery-lite)") {
    c.on_heartbeat(30, HB_FLAG_SAFE_MODE, 500'000);
    CHECK(c.node_count() == 1);
    REQUIRE(c.node_at(0));
    CHECK(c.node_at(0)->node == 30);
    CHECK_FALSE(c.node_at(0)->registered);
    CHECK(c.node_at(0)->hb_flags == HB_FLAG_SAFE_MODE);
}

} // TEST_SUITE CanRemoteCache
