/**
 * @file can_service.cpp
 * @brief CanService — транспорт ModESP-CAN v1 поверх esp_driver_twai (ADR-004).
 */

#include "sdkconfig.h"

#if defined(CONFIG_MODESP_CAN_ENABLE)

#include <stdio.h>
#include <stdlib.h>

#include "modesp/can/can_service.h"
#include "modesp/can/can_remote_cache.h"
#include "modesp/can/can_messages.h"
#include "modesp/shared_state.h"
#include "modesp/hal/hal.h"
#include "can_registry.h"

#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"

#include "etl/string.h"

namespace modesp {

static const char* TAG = "can";

using gen::can::CAN_EXPORT;
using gen::can::CAN_EXPORT_COUNT;
using gen::can::CAN_EVENTS;
using gen::can::CAN_EVENT_COUNT;
using gen::can::CanValType;

static_assert(CAN_EXPORT_COUNT <= 32,
              "CAN_EXPORT_COUNT перевищив MAX_EXPORT_KEYS — підніміть межу в can_service.h");

// ── TX-пул: кадр+буфер живуть до tx_done (черга драйвера тримає ВКАЗІВНИКИ,
//    zero-copy — stack-локальний twai_frame_t був би use-after-free) ──
namespace {
struct TxSlot {
    twai_frame_t      frame = {};
    uint8_t           data[8] = {};
    std::atomic<bool> in_use{false};
};
TxSlot s_tx_pool[12];
constexpr size_t S_TX_POOL_N = sizeof(s_tx_pool) / sizeof(s_tx_pool[0]);
} // namespace

// ═══════════════════════════════════════════════════════════════
// ISR trampolines — мінімум роботи, жодних blocking API
// ═══════════════════════════════════════════════════════════════

static bool s_on_rx_done(twai_node_handle_t node,
                         const twai_rx_done_event_data_t*, void* ctx) {
    return static_cast<CanService*>(ctx)->isr_on_rx(node);
}

static bool s_on_tx_done(twai_node_handle_t,
                         const twai_tx_done_event_data_t* edata, void* ctx) {
    static_cast<CanService*>(ctx)->isr_on_tx_done(edata->done_tx_frame);
    return false;
}

static bool s_on_state_change(twai_node_handle_t,
                              const twai_state_change_event_data_t* edata, void* ctx) {
    static_cast<CanService*>(ctx)->isr_on_state_change(edata->new_sta == TWAI_ERROR_BUS_OFF);
    return false;
}

static bool s_on_error(twai_node_handle_t,
                       const twai_error_event_data_t*, void* ctx) {
    static_cast<CanService*>(ctx)->isr_on_error();
    return false;
}

bool CanService::isr_on_rx(twai_node_handle_t node) {
    // SPSC: продюсер-ISR. Кадр можна прочитати ЛИШЕ тут (receive_from_isr поза
    // rx-callback'ом повертає INVALID_STATE) — тому навіть при повному ring'у
    // приймаємо у scratch і дропаємо з лічильником.
    static uint8_t scratch[8];

    uint32_t head = rx_head_.load(std::memory_order_relaxed);
    uint32_t tail = rx_tail_.load(std::memory_order_acquire);
    bool     drop = (head - tail) >= RX_RING;

    RxSlot& slot = rx_ring_[head % RX_RING];

    twai_frame_t f = {};
    f.buffer     = drop ? scratch : slot.data;
    f.buffer_len = 8;
    if (twai_node_receive_from_isr(node, &f) != ESP_OK) {
        return false;
    }
    if (f.header.rtr) {
        // Remote-кадри в ModESP-CAN v1 не визначені; HAL не копіює їх payload,
        // тож у буфері лишились би байти попереднього кадру — відкидаємо.
        return false;
    }
    if (drop) {
        rx_dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    slot.raw_id = f.header.id;
    slot.dlc    = uint8_t(f.header.dlc <= 8 ? f.header.dlc : 8);   // класичний CAN: dlc == байти
    rx_count_.fetch_add(1, std::memory_order_relaxed);
    rx_head_.store(head + 1, std::memory_order_release);
    return false;
}

void CanService::isr_on_tx_done(const void* done_frame) {
    tx_done_count_.fetch_add(1, std::memory_order_relaxed);
    // Мапимо вказівник кадру назад на слот пулу
    for (size_t i = 0; i < S_TX_POOL_N; i++) {
        if (&s_tx_pool[i].frame == done_frame) {
            s_tx_pool[i].in_use.store(false, std::memory_order_release);
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Ініціалізація
// ═══════════════════════════════════════════════════════════════

uint8_t CanService::resolve_node_id() {
    // 1) NVS-override (ключ "node_id" у namespace "can")
    nvs_handle_t nvs;
    if (nvs_open("can", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t v  = 0;
        bool    ok = (nvs_get_u8(nvs, "node_id", &v) == ESP_OK);
        nvs_close(nvs);
        if (ok && v >= can::NODE_MIN && v <= can::NODE_MAX) {
            return v;
        }
    }
    // 2) MAC-derived default: два пристрої однієї плати отримують різні адреси
    //    без правки board.json (аналогія mqtt device_id). Колізія 2 вузлів
    //    з ~250 значень можлива — тоді ставиться NVS-override.
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t h = 5381;
    for (int i = 0; i < 6; i++) h = h * 33u + mac[i];
    return uint8_t(h % (can::NODE_MAX - can::NODE_MIN + 1) + can::NODE_MIN);
}

// Створення + enable TWAI-ноди з конфігом bus_. Використовується on_init
// І відновленням після bus-off: ноду ПЕРЕСТВОРЮЄМО, бо драйвер не викликає
// tx_done для кадру, перерваного bus-off (twai_hal_v1.c скидає TX_BUFF_OCCUPIED
// без події) — слот пулу інакше тік би назавжди. Delete ноди скидає чергу
// драйвера, тож reset усього пулу тут безпечний.
bool CanService::start_node() {
    for (size_t i = 0; i < S_TX_POOL_N; i++) {
        s_tx_pool[i].in_use.store(false, std::memory_order_release);
    }

    twai_onchip_node_config_t cfg = {};
    cfg.io_cfg.tx                = bus_->tx;
    cfg.io_cfg.rx                = bus_->rx;
    cfg.io_cfg.quanta_clk_out    = GPIO_NUM_NC;
    cfg.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    cfg.bit_timing.bitrate       = bus_->bitrate;
    cfg.tx_queue_depth           = TX_POOL;
    cfg.fail_retry_cnt           = -1;   // класична CAN-семантика: ретрай до успіху
#if defined(CONFIG_MODESP_CAN_SELF_TEST)
    // Стендовий режим: власні кадри повертаються без ACK — повний TX→RX цикл
    // на одному пристрої без трансивера.
    cfg.flags.enable_self_test = 1;
    cfg.flags.enable_loopback  = 1;
#endif

    esp_err_t err = twai_new_node_onchip(&cfg, &node_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip: %s", esp_err_to_name(err));
        node_ = nullptr;
        return false;
    }

    twai_event_callbacks_t cbs = {};
    cbs.on_tx_done      = s_on_tx_done;
    cbs.on_rx_done      = s_on_rx_done;
    cbs.on_state_change = s_on_state_change;
    cbs.on_error        = s_on_error;
    err = twai_node_register_event_callbacks(node_, &cbs, this);
    if (err == ESP_OK) {
        // Явний accept-all (id=0, mask=0): фільтрація — software у on_update.
        // 1 mask-фільтр ESP32/S3 не виражає гетерогенний набір (heartbeat усіх
        // вузлів + вибрані ключі + події) — див. ADR-004 §4.4.
        twai_mask_filter_config_t fcfg = {};
        err = twai_node_config_mask_filter(node_, 0, &fcfg);
    }
    if (err == ESP_OK) {
        err = twai_node_enable(node_);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "callbacks/enable: %s", esp_err_to_name(err));
        twai_node_delete(node_);
        node_ = nullptr;
        return false;
    }
    return true;
}

bool CanService::on_init() {
    // Відмова CAN не мусить вбивати боот (блокер рев'ю 2026-07-07: fail у init
    // до TWDT-підписки → мертвий пристрій). Будь-яка помилка → status-ключ +
    // return true (модуль живий, idle).
    if (!hal_ || !state_) {
        ESP_LOGW(TAG, "set_hal()/set_state() не викликано — CAN вимкнено");
        state_set("can.status", "error");
        return true;
    }

    bus_ = hal_->can_bus_at(0);
    if (!bus_) {
        ESP_LOGI(TAG, "board.json без can_buses — CAN idle");
        state_set("can.status", "no_bus");
        return true;
    }

    node_id_ = resolve_node_id();

    if (!start_node()) {
        state_set("can.status", "error");
        return true;
    }

    // Вузли з реєстру пристроїв (board.json remote_devices + /data/devices.json,
    // transport "can"): трекаємо liveness одразу — remote.<n>.online стартує false.
    auto& cache = CanRemoteCache::instance();
    for (size_t i = 0; i < hal_->remote_device_count(); i++) {
        const RemoteDeviceConfig* dev = hal_->remote_device_at(i);
        if (!dev || dev->transport != "can") continue;
        // endptr-перевірка: runtime /data/devices.json не проходить генераторну
        // валідацію — '1x'/'1 2' не повинні тихо ставати вузлом 1
        char* end = nullptr;
        long nid = strtol(dev->identity.c_str(), &end, 0);
        if (end && *end == '\0' && end != dev->identity.c_str() &&
            nid >= can::NODE_MIN && nid <= can::NODE_MAX) {
            cache.register_node(uint8_t(nid));
        } else {
            ESP_LOGW(TAG, "remote device '%s': identity '%s' не є node-id 1..253",
                     dev->id.c_str(), dev->identity.c_str());
        }
    }
    update_liveness(esp_timer_get_time(), /*force_publish=*/true);

    ESP_LOGI(TAG, "CAN up: node-id=%u, '%s' TX=%d RX=%d @ %lu bps, %u keys, %u events, CRC 0x%04X",
             node_id_, bus_->id.c_str(), (int)bus_->tx, (int)bus_->rx,
             (unsigned long)bus_->bitrate,
             (unsigned)CAN_EXPORT_COUNT, (unsigned)CAN_EVENT_COUNT,
             gen::can::CAN_REGISTRY_CRC16);

    state_set("can.status", "ok");
    state_set("can.node_id", int32_t(node_id_));
    refresh_pending_ = true;   // перший full-refresh — одразу після старту
    refresh_cursor_  = 0;
    return true;
}

void CanService::on_stop() {
    if (node_) {
        twai_node_disable(node_);
        twai_node_delete(node_);
        node_ = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════
// TX
// ═══════════════════════════════════════════════════════════════

bool CanService::transmit(const can::FrameId& fid, const uint8_t* payload, size_t dlc) {
    if (!node_ || dlc > 8) return false;

    // Слот шукається лише з main-loop task (єдиний продюсер); звільняє tx_done ISR.
    TxSlot* slot = nullptr;
    for (size_t i = 0; i < S_TX_POOL_N; i++) {
        bool expected = false;
        if (s_tx_pool[i].in_use.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel)) {
            slot = &s_tx_pool[i];
            break;
        }
    }
    if (!slot) {
        tx_queue_full_++;
        return false;
    }

    memcpy(slot->data, payload, dlc);
    slot->frame            = {};
    slot->frame.header.id  = can::pack_id(fid);
    slot->frame.header.ide = 1;   // 29-бітний extended ID
    slot->frame.header.dlc = uint16_t(dlc);
    slot->frame.buffer     = slot->data;
    slot->frame.buffer_len = dlc;

    // Неблокуючий transmit: TWDT 5 с — блокуватись у main loop не можна
    // (та сама пастка, що підтверджена рев'ю для blocking MQTT publish).
    esp_err_t err = twai_node_transmit(node_, &slot->frame, 0);
    if (err != ESP_OK) {
        slot->in_use.store(false, std::memory_order_release);
        tx_queue_full_++;
        return false;
    }
    tx_count_++;
    return true;
}

void CanService::send_heartbeat() {
    can::HeartbeatPayload hb;
    hb.registry_crc16 = gen::can::CAN_REGISTRY_CRC16;
    hb.uptime_s       = uptime_s_;
    hb.flags          = 0;   // safe_mode-прапор вузла — разом з interlocks (C5)

    uint8_t buf[8];
    size_t  dlc = can::encode_heartbeat(hb, buf);
    transmit({can::PRIO_HEARTBEAT, can::FrameKind::HEARTBEAT, node_id_,
              can::HEARTBEAT_MAGIC}, buf, dlc);
}

CanService::SendResult CanService::try_send_export(size_t idx, bool force) {
    const auto& exp = CAN_EXPORT[idx];
    auto v = state_get(exp.key);
    if (!v.has_value()) return SendResult::SKIPPED;   // ключ ще не з'явився

    can::StatePayload p;
    switch (exp.type) {
        case CanValType::F32: {
            const float* f = etl::get_if<float>(&v.value());
            if (!f) return SendResult::SKIPPED;
            p.type = can::ValType::F32; p.f32 = *f;
            break;
        }
        case CanValType::I32: {
            const int32_t* iv = etl::get_if<int32_t>(&v.value());
            if (!iv) return SendResult::SKIPPED;
            p.type = can::ValType::I32; p.i32 = *iv;
            break;
        }
        case CanValType::BOOL: {
            const bool* b = etl::get_if<bool>(&v.value());
            if (!b) return SendResult::SKIPPED;
            p.type = can::ValType::BOOL; p.b = *b;
            break;
        }
        default:
            return SendResult::SKIPPED;
    }

    // VALID = компаньйон "<key>_ok" (заморожене EMA у equipment → invalid);
    // ключ без компаньйона — завжди valid.
    p.valid = true;
    etl::string<40> ok_key(exp.key);
    ok_key += "_ok";
    if (ok_key.size() <= MODESP_MAX_KEY_LENGTH) {
        auto ok_v = state_get(ok_key.c_str());
        if (ok_v.has_value()) {
            const bool* okb = etl::get_if<bool>(&ok_v.value());
            if (okb) p.valid = *okb;
        }
    }

    LastSent& ls = last_sent_[idx];
    bool changed = !ls.sent || ls.valid != p.valid;
    switch (p.type) {
        case can::ValType::F32:  changed = changed || ls.f32 != p.f32; break;
        case can::ValType::I32:  changed = changed || ls.i32 != p.i32; break;
        case can::ValType::BOOL: changed = changed || ls.b   != p.b;   break;
    }
    if (!force && !changed) return SendResult::SKIPPED;

    uint8_t buf[8];
    size_t  dlc = can::encode_state(p, buf);
    if (!transmit({can::PRIO_STATE, can::FrameKind::STATE, node_id_, exp.id}, buf, dlc)) {
        return SendResult::QUEUE_FULL;
    }
    ls.sent  = true;
    ls.valid = p.valid;
    ls.f32   = p.f32;
    ls.i32   = p.i32;
    ls.b     = p.b;
    return SendResult::SENT;
}

void CanService::publish_states() {
    if (CAN_EXPORT_COUNT == 0) return;

    size_t budget = TX_MAX_PER_TICK;

    if (refresh_pending_) {
        // Повний refresh: retained-емуляція для пізніх слухачів, розкладена
        // по тіках. Курсор рухається і по пропущених ключах (skip не має
        // стопорити прохід); черга повна — продовжимо з цього ж місця.
        while (refresh_cursor_ < CAN_EXPORT_COUNT && budget > 0) {
            SendResult r = try_send_export(refresh_cursor_, /*force=*/true);
            if (r == SendResult::QUEUE_FULL) return;
            if (r == SendResult::SENT) budget--;
            refresh_cursor_++;
        }
        if (refresh_cursor_ >= CAN_EXPORT_COUNT) {
            refresh_pending_ = false;
            refresh_cursor_  = 0;
        }
        return;
    }

    // On-change: version()-полінг SharedState (патерн MqttService).
    uint32_t ver = state_->version();
    if (ver == last_state_version_) return;
    last_state_version_ = ver;

    for (size_t i = 0; i < CAN_EXPORT_COUNT && budget > 0; i++) {
        SendResult r = try_send_export(i, /*force=*/false);
        if (r == SendResult::QUEUE_FULL) {
            // Не встигли — наступний тік дошле (last_sent не оновлено)
            last_state_version_ = ver - 1;
            return;
        }
        if (r == SendResult::SENT) budget--;
    }
    if (budget == 0) {
        // Бюджет тіка вичерпано, а хвіст ключів міг лишитись неперевіреним —
        // на тихій шині version() більше не зміниться і хвіст завис би до
        // 5-секундного refresh. Перескануємо наступним тіком (дешево: уже
        // надіслані ключі відфільтрує last_sent-кеш).
        last_state_version_ = ver - 1;
    }
}

// ═══════════════════════════════════════════════════════════════
// RX (main loop)
// ═══════════════════════════════════════════════════════════════

void CanService::drain_rx(int64_t now_us) {
    uint32_t head = rx_head_.load(std::memory_order_acquire);
    uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
    while (tail != head) {
        handle_frame(rx_ring_[tail % RX_RING], now_us);
        tail++;
        rx_tail_.store(tail, std::memory_order_release);
    }
}

void CanService::handle_frame(const RxSlot& slot, int64_t now_us) {
    can::FrameId fid = can::unpack_id(slot.raw_id);

    if (fid.src == node_id_) {
        // Власний кадр (loopback self-test) — TX→RX цикл підтверджено
        if (!loopback_seen_) {
            loopback_seen_ = true;
#if defined(CONFIG_MODESP_CAN_SELF_TEST)
            state_set("can.loopback_ok", true);
            ESP_LOGI(TAG, "self-test: власний кадр повернувся (TX→RX цикл OK)");
#endif
        }
        return;
    }
    if (fid.src < can::NODE_MIN || fid.src > can::NODE_MAX) return;

    auto& cache = CanRemoteCache::instance();

    switch (fid.kind) {
        case can::FrameKind::HEARTBEAT: {
            can::HeartbeatPayload hb;
            if (fid.id16 == can::HEARTBEAT_MAGIC &&
                can::decode_heartbeat(slot.data, slot.dlc, hb)) {
                cache.on_heartbeat(fid.src, hb.flags, now_us);
            }
            break;
        }
        case can::FrameKind::STATE: {
            can::StatePayload p;
            if (can::decode_state(slot.data, slot.dlc, p)) {
                cache.on_state_frame(fid.src, fid.id16, p, now_us);
            }
            break;
        }
        case can::FrameKind::EVENT: {
            // У локальну шину — лише оголошені (can_events.in) події.
            // publish() з main-loop task — єдиний безпечний контекст шини.
            for (size_t i = 0; i < CAN_EVENT_COUNT; i++) {
                if (CAN_EVENTS[i].id != fid.id16) continue;
                MsgRemoteEvent ev;
                ev.node     = fid.src;
                ev.event_id = fid.id16;
                ev.len      = slot.dlc;
                memcpy(ev.data, slot.data, ev.len);
                publish(ev);
                return;
            }
            rx_unknown_++;
            break;
        }
        case can::FrameKind::CMD:
            // Зарезервовано (адресний запис rw-ключів, v1.1)
            break;
    }
}

// ═══════════════════════════════════════════════════════════════
// Події з локальної шини → CAN (MsgRemoteEvent із node==0)
// ═══════════════════════════════════════════════════════════════

void CanService::on_message(const etl::imessage& msg) {
    if (msg.get_message_id() != msg_id::CAN_REMOTE_EVENT || !node_) return;
    const auto& ev = static_cast<const MsgRemoteEvent&>(msg);
    if (ev.node != 0) return;   // прийнята (не наша) подія

    // Whitelist: лише події, оголошені outbound у can_registry.h
    for (size_t i = 0; i < CAN_EVENT_COUNT; i++) {
        if (CAN_EVENTS[i].id == ev.event_id && CAN_EVENTS[i].outbound) {
            transmit({can::PRIO_EVENT, can::FrameKind::EVENT, node_id_, ev.event_id},
                     ev.data, ev.len <= 8 ? ev.len : 8);
            return;
        }
    }
    ESP_LOGW(TAG, "event 0x%04X не оголошено у can_events.out — не надіслано", ev.event_id);
}

// ═══════════════════════════════════════════════════════════════
// Головний цикл
// ═══════════════════════════════════════════════════════════════

void CanService::update_liveness(int64_t now_us, bool force_publish) {
    auto& cache = CanRemoteCache::instance();
    bool changed = cache.tick(now_us, NODE_TIMEOUT_US);
    if (!changed && !force_publish) return;

    // remote.<n>.online для кожного трекнутого вузла (ключ за номером вузла —
    // короткий і стабільний; мапінг на назву пристрою — справа webui/devices).
    for (size_t i = 0; i < cache.node_count(); i++) {
        const auto* n = cache.node_at(i);
        if (!n) continue;
        char key[32];
        snprintf(key, sizeof(key), "remote.%u.online", n->node);
        state_set(key, n->online);
    }
}

void CanService::publish_counters() {
    // Діагностика — тихі set'и (не тригерять WS delta і version())
    state_set("can.tx_count", int32_t(tx_count_), false);
    state_set("can.rx_count", int32_t(rx_count_.load(std::memory_order_relaxed)), false);
    state_set("can.err_count", int32_t(bus_err_count_.load(std::memory_order_relaxed)), false);
    uint32_t dropped = rx_dropped_.load(std::memory_order_relaxed);
    if (dropped) state_set("can.rx_dropped", int32_t(dropped), false);
    if (tx_queue_full_) state_set("can.tx_dropped", int32_t(tx_queue_full_), false);
}

void CanService::on_update(uint32_t dt_ms) {
    if (!node_) return;

    int64_t now_us = esp_timer_get_time();

    uptime_acc_ms_ += dt_ms;
    while (uptime_acc_ms_ >= 1000) { uptime_acc_ms_ -= 1000; uptime_s_++; }

    // 1) RX: дренаж ring'а (декод + кеш + події) — усе на main-loop task
    drain_rx(now_us);

    // 2) bus-off: відновлення з backoff'ом (неблокуючий). twai_node_recover()
    //    лише СТАРТУЄ відновлення (128×11 recessive біт — на stuck-dominant
    //    шині не завершиться ніколи), а кадр, перерваний bus-off, не отримує
    //    tx_done → його слот пулу тік би назавжди. Тому: ноду перестворюємо
    //    (drop черги + reset пулу в start_node), статус "recovering", і "ok" —
    //    лише після РЕАЛЬНОГО tx_done на новій ноді. Ключі — на перехід,
    //    інакше кожен тік бив би у tracked-зміни (WS delta + version()).
    if (bus_off_.load(std::memory_order_relaxed)) {
        if (!bus_off_reported_) {
            bus_off_reported_ = true;
            recovering_ = false;
            state_set("can.bus_off", true);
            state_set("can.status", "bus_off");
        }
        busoff_timer_ms_ += dt_ms;
        if (busoff_timer_ms_ >= BUSOFF_RECOVER_MS) {
            busoff_timer_ms_ = 0;
            ESP_LOGW(TAG, "bus-off — перестворення TWAI-ноди");
            twai_node_disable(node_);
            twai_node_delete(node_);
            node_ = nullptr;
            if (start_node()) {
                bus_off_.store(false, std::memory_order_relaxed);
                bus_off_reported_ = false;
                recovering_        = true;
                txdone_at_recover_ = tx_done_count_.load(std::memory_order_relaxed);
                state_set("can.status", "recovering");
            }
            // start_node fail → лишаємось у bus-off гілці, ретрай за backoff
        }
        // Liveness вузлів веде далі (шина мертва → вузли стануть offline)
        liveness_timer_ms_ += dt_ms;
        if (liveness_timer_ms_ >= LIVENESS_TICK_MS) {
            liveness_timer_ms_ = 0;
            update_liveness(now_us, false);
        }
        return;   // TX до відновлення шини безглуздий
    }
    busoff_timer_ms_ = 0;

    // 2б) підтвердження відновлення: перший tx_done на новій ноді означає, що
    //     кадр реально пішов у шину (в self-test — завершився без ACK).
    if (recovering_ &&
        tx_done_count_.load(std::memory_order_relaxed) != txdone_at_recover_) {
        recovering_ = false;
        state_set("can.bus_off", false);
        state_set("can.status", "ok");
        ESP_LOGI(TAG, "bus-off відновлено (tx_done підтверджено)");
    }

    // 3) heartbeat
    hb_timer_ms_ += dt_ms;
    if (hb_timer_ms_ >= HEARTBEAT_MS) {
        hb_timer_ms_ = 0;
        send_heartbeat();
    }

    // 4) стан: періодичний повний refresh (retained-емуляція) + on-change
    refresh_timer_ms_ += dt_ms;
    if (refresh_timer_ms_ >= STATE_REFRESH_MS) {
        refresh_timer_ms_ = 0;
        refresh_pending_  = true;
        refresh_cursor_   = 0;
    }
    publish_states();

    // 5) liveness вузлів
    liveness_timer_ms_ += dt_ms;
    if (liveness_timer_ms_ >= LIVENESS_TICK_MS) {
        liveness_timer_ms_ = 0;
        update_liveness(now_us, false);
    }

    // 6) лічильники
    counters_timer_ms_ += dt_ms;
    if (counters_timer_ms_ >= COUNTERS_MS) {
        counters_timer_ms_ = 0;
        publish_counters();
    }
}

} // namespace modesp

#endif // CONFIG_MODESP_CAN_ENABLE
