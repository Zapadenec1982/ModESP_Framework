/**
 * @file can_service.h
 * @brief CanService — єдиний власник TWAI-ноди (транспорт ModESP-CAN v1, ADR-004).
 *
 * Мостить два внутрішні плани ModESP через CAN:
 *   - SharedState:  експорт ключів з can_registry.h (on-change + періодичний
 *                    refresh) і оновлення CanRemoteCache на RX;
 *   - шину повідомлень: MsgRemoteEvent (node==0 → TX; RX → publish локально).
 *
 * Потокова модель (наслідки нового esp_driver_twai node-API, IDF v6):
 *   - усі callback'и драйвера — ISR-контекст: RX кладе кадр у SPSC-ring
 *     (продюсер-ISR / консюмер-main-loop), TX-done звільняє слот пулу,
 *     error/state лише інкрементують атомарні лічильники/прапори;
 *   - ВСЯ логіка (декод, state_set, publish, recover) — у on_update на
 *     main-loop task; звідти ж twai_node_transmit (неблокуючий, timeout=0);
 *   - TX zero-copy: кадр+буфер живуть у статичному пулі до tx_done.
 *
 * Відмова ініціалізації CAN не фатальна для боту: can.status="error",
 * модуль лишається зареєстрованим (main loop живий, рестарт не потрібен).
 */

#pragma once

#include "sdkconfig.h"

#if defined(CONFIG_MODESP_CAN_ENABLE)

#include <stdint.h>
#include <atomic>

#include "modesp/base_module.h"
#include "modesp/can/can_codec.h"

// Opaque handle — esp_twai.h підключає лише .cpp (PRIV_REQUIRES esp_driver_twai).
// Ідентична typedef-декларація до esp_twai_types.h (повторна typedef легальна).
struct twai_node_base;
typedef struct twai_node_base* twai_node_handle_t;

namespace modesp {

class HAL;
class SharedState;
struct CanBusResource;

class CanService : public BaseModule {
public:
    CanService() : BaseModule("can", ModulePriority::HIGH) {}

    /// DI з main.cpp ДО init_all Phase 2: конфіг can_buses + version()-полінг стану.
    void set_hal(HAL* hal)            { hal_ = hal; }
    void set_state(SharedState* s)    { state_ = s; }

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_message(const etl::imessage& msg) override;
    void on_stop() override;

    uint8_t node_id() const { return node_id_; }
    bool    is_enabled() const { return node_ != nullptr; }

    // ── ISR-only entry points (кличуть file-static трамплінй з can_service.cpp;
    //    публічні лише тому, що трамплінам потрібен доступ — не для модулів) ──
    bool isr_on_rx(twai_node_handle_t node);
    void isr_on_tx_done(const void* done_frame);
    void isr_on_state_change(bool bus_off) {
        if (bus_off) bus_off_.store(true, std::memory_order_relaxed);
    }
    void isr_on_error() {
        bus_err_count_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    // ── Розміри/періоди (статичні пули; zero-heap у hot-path) ──
    // RX_RING 32: один 10 мс тік має вміщати worst-case насиченої шини 250 kbps
    // (~2000 кадрів/с ≈ 20/тік) — 16 давало стелю 1600 кадрів/с і губило події.
    static constexpr size_t   RX_RING           = 32;
    static constexpr size_t   TX_POOL           = 12;
    static constexpr size_t   MAX_EXPORT_KEYS   = 32;    // guard: static_assert у .cpp
    static constexpr uint32_t HEARTBEAT_MS      = 1000;
    static constexpr int64_t  NODE_TIMEOUT_US   = 3500 * 1000LL;   // 3.5 × heartbeat
    static constexpr uint32_t LIVENESS_TICK_MS  = 500;
    static constexpr uint32_t STATE_REFRESH_MS  = 5000;  // retained-емуляція
    static constexpr uint32_t COUNTERS_MS       = 1000;
    static constexpr uint32_t BUSOFF_RECOVER_MS = 1000;  // backoff перед recover
    static constexpr size_t   TX_MAX_PER_TICK   = 4;     // ≤400 кадрів/с на 100 Гц

    struct RxSlot {
        // twai_frame_t тут не тримаємо (header без esp_twai.h) — ISR копіює
        // розібраний мінімум: raw id + dlc + data.
        uint32_t raw_id  = 0;
        uint8_t  dlc     = 0;
        uint8_t  data[8] = {};
    };

    enum class SendResult : uint8_t { SENT, SKIPPED, QUEUE_FULL };

    // ── main-loop side ──
    bool       start_node();   // створення+enable TWAI-ноди з bus_ (init і re-create після bus-off)
    void       drain_rx(int64_t now_us);
    void       handle_frame(const RxSlot& slot, int64_t now_us);
    void       publish_states();
    SendResult try_send_export(size_t idx, bool force);
    void       send_heartbeat();
    bool       transmit(const can::FrameId& fid, const uint8_t* payload, size_t dlc);
    void       update_liveness(int64_t now_us, bool force_publish);
    void       publish_counters();
    uint8_t    resolve_node_id();

    HAL*               hal_     = nullptr;
    SharedState*       state_   = nullptr;
    twai_node_handle_t node_    = nullptr;
    uint8_t            node_id_ = 0;
    const CanBusResource* bus_  = nullptr;   // конфіг шини (живе у HAL)

    // RX ring: ISR пише head, main loop читає tail (монотонні лічильники,
    // індексація % RX_RING).
    RxSlot                rx_ring_[RX_RING];
    std::atomic<uint32_t> rx_head_{0};
    std::atomic<uint32_t> rx_tail_{0};   // atomic: ISR читає для drop-перевірки

    // Лічильники (ISR інкрементує; main loop публікує раз на секунду, silent)
    std::atomic<uint32_t> rx_count_{0};
    std::atomic<uint32_t> rx_dropped_{0};
    std::atomic<uint32_t> tx_done_count_{0};
    std::atomic<uint32_t> bus_err_count_{0};
    std::atomic<bool>     bus_off_{false};
    uint32_t              tx_count_      = 0;
    uint32_t              tx_queue_full_ = 0;
    uint32_t              rx_unknown_    = 0;

    // Відновлення після bus-off: нода ПЕРЕСТВОРЮЄТЬСЯ (drop черги драйвера +
    // повний reset TX-пулу — інакше слот кадру, перерваного bus-off, тік би
    // назавжди: tx_done для нього не приходить). "ok" — лише після реального
    // tx_done на новій ноді; до того статус "recovering".
    bool     recovering_        = false;
    uint32_t txdone_at_recover_ = 0;

    // Кеш «останнє надіслане» на кожен експортований ключ (детект змін поверх
    // version()-полінгу SharedState — патерн MqttService; деструктивний
    // for_each_changed_and_clear належить WsService і тут НЕ використовується).
    struct LastSent {
        bool    sent  = false;
        bool    valid = false;
        float   f32   = 0.0f;
        int32_t i32   = 0;
        bool    b     = false;
    };
    LastSent last_sent_[MAX_EXPORT_KEYS];

    uint32_t last_state_version_ = 0;
    size_t   refresh_cursor_     = 0;      // розкладає повний refresh по тіках
    bool     refresh_pending_    = false;

    // Таймери (мс, від dt_ms у on_update)
    uint32_t hb_timer_ms_       = 0;
    uint32_t liveness_timer_ms_ = 0;
    uint32_t refresh_timer_ms_  = 0;
    uint32_t counters_timer_ms_ = 0;
    uint32_t busoff_timer_ms_   = 0;

    uint32_t uptime_s_      = 0;
    uint32_t uptime_acc_ms_ = 0;
    bool     loopback_seen_ = false;
    bool     bus_off_reported_ = false;
};

} // namespace modesp

#endif // CONFIG_MODESP_CAN_ENABLE
