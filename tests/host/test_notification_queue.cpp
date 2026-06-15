/**
 * @file test_notification_queue.cpp
 * @brief HOST unit tests для NotificationQueue (ADR-001).
 */

#include <string>

#include "doctest.h"
#include "display/notification_queue.h"

using namespace modesp::display;

static int lvl(NoticeLevel l) { return static_cast<int>(l); }

TEST_CASE("NotificationQueue: push shows active banner") {
    NotificationQueue q;
    q.push(0, 0, "hello");
    CHECK(q.consume_dirty());
    REQUIRE(q.has_active());
    CHECK(std::string(q.active().text.c_str()) == "hello");
    CHECK(lvl(q.active().level) == lvl(NoticeLevel::INFO));
    CHECK_FALSE(q.consume_dirty());   // лише раз
}

TEST_CASE("NotificationQueue: TTL expires and clears") {
    NotificationQueue q;
    q.push(1, 100, "warn");
    q.consume_dirty();
    REQUIRE(q.has_active());

    q.tick(50);
    CHECK(q.has_active());
    CHECK_FALSE(q.consume_dirty());   // ще активний, без змін

    q.tick(60);                        // 50+60 > 100 → expired
    CHECK_FALSE(q.has_active());
    CHECK(q.consume_dirty());
}

TEST_CASE("NotificationQueue: ttl=0 stays until replaced") {
    NotificationQueue q;
    q.push(2, 0, "alarm");
    q.consume_dirty();
    q.tick(100000);
    CHECK(q.has_active());
    CHECK(lvl(q.active().level) == lvl(NoticeLevel::ALARM));
}

TEST_CASE("NotificationQueue: higher level preempts") {
    NotificationQueue q;
    q.push(0, 0, "info");
    q.consume_dirty();
    q.push(2, 0, "ALARM");
    CHECK(q.consume_dirty());
    REQUIRE(q.has_active());
    CHECK(std::string(q.active().text.c_str()) == "ALARM");
    CHECK(lvl(q.active().level) == lvl(NoticeLevel::ALARM));
}

TEST_CASE("NotificationQueue: lower level does not preempt active") {
    NotificationQueue q;
    q.push(2, 0, "ALARM");
    q.consume_dirty();
    q.push(0, 0, "info");
    // активним лишається ALARM
    CHECK_FALSE(q.consume_dirty());
    CHECK(std::string(q.active().text.c_str()) == "ALARM");
}

TEST_CASE("NotificationQueue: overflow drops lowest, keeps new alarm") {
    NotificationQueue q;
    for (int i = 0; i < (int)NotificationQueue::CAP; ++i) q.push(0, 0, "low");
    q.consume_dirty();
    q.push(2, 0, "ALARM");   // черга повна level-0 → новий ALARM витісняє один low
    REQUIRE(q.has_active());
    CHECK(lvl(q.active().level) == lvl(NoticeLevel::ALARM));
    CHECK(std::string(q.active().text.c_str()) == "ALARM");
}

TEST_CASE("NotificationQueue: after alarm expires, lower banner resurfaces") {
    NotificationQueue q;
    q.push(0, 0, "info");          // infinite info
    q.push(2, 100, "alarm");       // alarm with TTL
    q.consume_dirty();
    CHECK(std::string(q.active().text.c_str()) == "alarm");

    q.tick(150);                    // alarm expires
    CHECK(q.consume_dirty());
    REQUIRE(q.has_active());
    CHECK(std::string(q.active().text.c_str()) == "info");   // info повертається
}

// ── додано після ревью (cov-2/3/4) ──

TEST_CASE("NotificationQueue: equal level keeps FIFO (earliest seq)") {
    NotificationQueue q;
    q.push(1, 0, "first");
    q.consume_dirty();
    q.push(1, 0, "second");
    CHECK_FALSE(q.consume_dirty());                          // active не змінився
    CHECK(std::string(q.active().text.c_str()) == "first");
}

TEST_CASE("NotificationQueue: overflow keeps higher; equal-level new is dropped") {
    NotificationQueue q;
    const char* names[8] = {"t0","t1","t2","t3","t4","t5","t6","t7"};
    for (int i = 0; i < (int)NotificationQueue::CAP; ++i) q.push(0, 0, names[i]);
    q.consume_dirty();
    CHECK(std::string(q.active().text.c_str()) == "t0");     // earliest seq активний
    q.push(1, 0, "hi");                                       // витісняє один level-0
    CHECK(q.consume_dirty());
    CHECK(std::string(q.active().text.c_str()) == "hi");

    NotificationQueue q2;
    for (int i = 0; i < (int)NotificationQueue::CAP; ++i) q2.push(1, 0, "x");
    q2.consume_dirty();
    q2.push(1, 0, "y");                                       // не > найнижчого → дроп
    CHECK_FALSE(q2.consume_dirty());
}

TEST_CASE("NotificationQueue: ALL finite entries age, not only active") {
    NotificationQueue q;
    q.push(0, 50,  "low");      // прихований нижчий, finite
    q.push(2, 100, "alarm");    // активний, finite
    q.consume_dirty();
    q.tick(60);                 // low(50)<60 → стерто; alarm(100)>60 → лишається active
    REQUIRE(q.has_active());
    CHECK(std::string(q.active().text.c_str()) == "alarm");
    q.tick(60);                 // alarm(40)<60 → стерто; low уже застарів → черга порожня
    CHECK_FALSE(q.has_active()); // якби low старів лише активним — він би тут «воскрес»
}
