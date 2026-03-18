/**
 * @file ws_service.h
 * @brief WebSocket service for real-time state delta broadcast
 *
 * Registers a /ws endpoint on the HTTP server.
 * Periodically broadcasts only CHANGED state keys (delta) to all clients.
 * New clients receive full state immediately on connect.
 *
 * Max 3 simultaneous WS clients.
 */

#pragma once

#include "modesp/base_module.h"
#include "esp_http_server.h"
#include "freertos/semphr.h"

namespace modesp {

class SharedState;

class WsService : public BaseModule {
public:
    WsService() : BaseModule("ws", ModulePriority::LOW) {}

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;

    void set_state(SharedState* state) { state_ = state; }
    void set_http_server(httpd_handle_t server);  // Реєструє WS handler якщо ще не зареєстрований

private:
    httpd_handle_t server_ = nullptr;
    SharedState* state_ = nullptr;
    bool ws_registered_ = false;

    // Connected clients (3 active browsers)
    static constexpr size_t MAX_WS_CLIENTS = 3;
    int client_fds_[MAX_WS_CLIENTS] = {-1, -1, -1};

    // BUG-021: mutex для client_fds_ (доступ з main loop + httpd потоків)
    StaticSemaphore_t clients_mutex_buf_;
    SemaphoreHandle_t clients_mutex_ = nullptr;

    // Delta broadcast timing (delta payloads ~200B → можна частіше)
    uint32_t broadcast_timer_ = 0;
    static constexpr uint32_t BROADCAST_INTERVAL_MS = 1500;

    // Heartbeat (ping) + cleanup
    // Must be well under httpd recv_wait_timeout (30s) so the PONG
    // response from the browser resets the socket idle timer in time.
    uint32_t heartbeat_timer_ = 0;
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 20000;

    void register_ws_handler();
    void broadcast_state();          // Delta — тільки змінені ключі
    void send_full_state_to(int fd); // Повний state для нового клієнта
    void send_to_all(const char* data, size_t len);
    void send_ping_to_all();
    void add_client(int fd);
    bool remove_client(int fd);  // returns true if fd was tracked
    bool has_client(int fd) const;
    void cleanup_dead_clients();

    // Returns true if fd is still a valid WebSocket session
    bool is_ws_client(int fd) const;

    static esp_err_t ws_handler(httpd_req_t* req);

    // Session close callback — called by httpd when session ctx is freed.
    // Signature matches httpd_free_ctx_fn_t: void(*)(void*)
    static void on_session_close(void* ctx);

    // ── Thread-safe send via httpd_queue_work ───────────────
    // httpd_ws_send_frame_async() sends directly from the calling
    // thread which races with httpd's recv(). We queue work to the
    // httpd thread instead.
    struct AsyncSendCtx {
        httpd_handle_t  server;
        WsService*      self;     // back-pointer for immediate fd removal on failure
        int             fd;
        httpd_ws_type_t type;
        size_t          len;
        char            data[];   // flexible array member
    };
    static void async_send_cb(void* arg);
};

} // namespace modesp
