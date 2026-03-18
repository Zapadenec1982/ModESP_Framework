/**
 * @file ws_service.cpp
 * @brief WebSocket real-time state broadcast implementation
 */

#include "modesp/net/ws_service.h"
#include "modesp/shared_state.h"
#include "modesp/types.h"

#include "esp_log.h"

#include <cstdio>
#include <cstring>

static const char* TAG = "WS";

namespace modesp {

// ── Serialization (same format as HTTP /api/state) ──────────────

// AUDIT-004: JSON escape для string значень
static size_t ws_json_escape(char* dest, size_t dest_size, const char* src) {
    size_t w = 0;
    for (const char* p = src; *p && w + 2 < dest_size; ++p) {
        switch (*p) {
            case '"':  dest[w++] = '\\'; dest[w++] = '"';  break;
            case '\\': dest[w++] = '\\'; dest[w++] = '\\'; break;
            case '\n': dest[w++] = '\\'; dest[w++] = 'n';  break;
            case '\r': dest[w++] = '\\'; dest[w++] = 'r';  break;
            case '\t': dest[w++] = '\\'; dest[w++] = 't';  break;
            default:
                if (static_cast<unsigned char>(*p) >= 0x20) dest[w++] = *p;
                break;
        }
    }
    dest[w] = '\0';
    return w;
}

struct WsSerCtx {
    char* buf;
    size_t size;
    size_t pos;
    bool first;
    size_t remaining() const { return (pos < size) ? size - pos : 0; }
};

static void ws_serialize_entry(const StateKey& key, const StateValue& value, void* ud) {
    auto* ctx = static_cast<WsSerCtx*>(ud);
    if (ctx->remaining() < 64) return;

    const char* sep = ctx->first ? "" : ",";
    ctx->first = false;

    if (etl::holds_alternative<float>(value)) {
        ctx->pos += snprintf(ctx->buf + ctx->pos, ctx->remaining(),
                             "%s\"%s\":%.2f", sep, key.c_str(),
                             static_cast<double>(etl::get<float>(value)));
    } else if (etl::holds_alternative<int32_t>(value)) {
        ctx->pos += snprintf(ctx->buf + ctx->pos, ctx->remaining(),
                             "%s\"%s\":%ld", sep, key.c_str(),
                             (long)etl::get<int32_t>(value));
    } else if (etl::holds_alternative<bool>(value)) {
        ctx->pos += snprintf(ctx->buf + ctx->pos, ctx->remaining(),
                             "%s\"%s\":%s", sep, key.c_str(),
                             etl::get<bool>(value) ? "true" : "false");
    } else if (etl::holds_alternative<StringValue>(value)) {
        char escaped[64];
        ws_json_escape(escaped, sizeof(escaped),
                       etl::get<StringValue>(value).c_str());
        ctx->pos += snprintf(ctx->buf + ctx->pos, ctx->remaining(),
                             "%s\"%s\":\"%s\"", sep, key.c_str(), escaped);
    }
}

// ── Client management (BUG-021: all access under clients_mutex_) ──

void WsService::add_client(int fd) {
    if (!clients_mutex_) return;
    xSemaphoreTake(clients_mutex_, portMAX_DELAY);

    // If fd is already tracked — this is a reconnect on the same fd number.
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (client_fds_[i] == fd) {
            xSemaphoreGive(clients_mutex_);
            ESP_LOGI(TAG, "Client reconnected (fd=%d, slot=%d)", fd, (int)i);
            return;
        }
    }

    // Clean up stale fds (mutex already held)
    if (server_) {
        for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
            int f = client_fds_[i];
            if (f != -1 && !is_ws_client(f)) {
                ESP_LOGI(TAG, "Removing stale client fd=%d (slot=%d)", f, (int)i);
                client_fds_[i] = -1;
            }
        }
    }

    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (client_fds_[i] == -1) {
            client_fds_[i] = fd;
            xSemaphoreGive(clients_mutex_);
            ESP_LOGI(TAG, "Client connected (fd=%d, slot=%d)", fd, (int)i);
            return;
        }
    }
    xSemaphoreGive(clients_mutex_);
    ESP_LOGW(TAG, "Max WS clients reached, rejecting fd=%d", fd);
}

bool WsService::remove_client(int fd) {
    if (!clients_mutex_) return false;
    xSemaphoreTake(clients_mutex_, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (client_fds_[i] == fd) {
            client_fds_[i] = -1;
            xSemaphoreGive(clients_mutex_);
            ESP_LOGI(TAG, "Client disconnected (fd=%d, slot=%d)", fd, (int)i);
            return true;
        }
    }
    xSemaphoreGive(clients_mutex_);
    return false;
}

bool WsService::has_client(int fd) const {
    if (!clients_mutex_) return false;
    xSemaphoreTake(clients_mutex_, portMAX_DELAY);
    bool found = false;
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (client_fds_[i] == fd) { found = true; break; }
    }
    xSemaphoreGive(clients_mutex_);
    return found;
}

bool WsService::is_ws_client(int fd) const {
    if (!server_ || fd < 0) return false;
    return httpd_ws_get_fd_info(server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET;
}

void WsService::cleanup_dead_clients() {
    if (!server_ || !clients_mutex_) return;
    xSemaphoreTake(clients_mutex_, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        int fd = client_fds_[i];
        if (fd == -1) continue;
        if (!is_ws_client(fd)) {
            ESP_LOGI(TAG, "Removing stale client fd=%d (slot=%d)", fd, (int)i);
            client_fds_[i] = -1;
        }
    }
    xSemaphoreGive(clients_mutex_);
}

// Called by httpd when session ctx is freed (TCP RST, timeout, etc.)
// Signature: httpd_free_ctx_fn_t = void(*)(void*)
// We don't get the fd here, so we run a full cleanup sweep.
void WsService::on_session_close(void* ctx) {
    if (!ctx) return;
    auto* self = static_cast<WsService*>(ctx);
    ESP_LOGI(TAG, "Session closed (httpd callback), cleaning up");
    self->cleanup_dead_clients();
}

// ── WS handler (static) ────────────────────────────────────────

esp_err_t WsService::ws_handler(httpd_req_t* req) {
    auto* self = static_cast<WsService*>(req->user_ctx);

    if (req->method == HTTP_GET) {
        // New WebSocket connection handshake
        int fd = httpd_req_to_sockfd(req);
        self->add_client(fd);

        // Store self as session context so on_session_close can find us.
        // The free_fn is called by httpd when the session closes for any
        // reason (clean CLOSE, TCP RST, timeout, server shutdown).
        httpd_sess_set_ctx(req->handle, fd, self, on_session_close);

        // Відправити повний state новому клієнту одразу при підключенні.
        // Delta broadcasts не містять ключів, що не змінились з моменту
        // останнього broadcast — новий клієнт пропустив би весь стан.
        self->send_full_state_to(fd);

        return ESP_OK;
    }

    // Receive frame — leave type as 0 (HTTPD_WS_TYPE_CONTINUE) so httpd
    // fills in the actual opcode from the wire.
    httpd_ws_frame_t ws_pkt = {};
    // ws_pkt.type is already 0 == HTTPD_WS_TYPE_CONTINUE after zero-init

    // First call with len=0 to get frame info (type + length)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGW(TAG, "ws_recv_frame failed fd=%d: %s", fd, esp_err_to_name(ret));
        // Remove client immediately to stop further sends to this fd
        self->remove_client(fd);
        return ret;
    }

    // ── Handle control frames (we set handle_ws_control_frames=true) ──
    // We must consume the full frame for every type, otherwise leftover
    // bytes in the socket buffer corrupt the next frame parse.

    if (ws_pkt.type == HTTPD_WS_TYPE_PONG) {
        // PONG is the response to our server-initiated PING.
        // Consume any payload (usually empty) and discard.
        if (ws_pkt.len > 0) {
            uint8_t discard[128];
            ws_pkt.payload = discard;
            httpd_ws_recv_frame(req, &ws_pkt, sizeof(discard));
        }
        ESP_LOGD(TAG, "PONG from fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        // Read PING payload (if any) and reply with PONG
        uint8_t ping_buf[128] = {};
        if (ws_pkt.len > 0 && ws_pkt.len <= sizeof(ping_buf)) {
            ws_pkt.payload = ping_buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret != ESP_OK) return ret;
        }
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        return httpd_ws_send_frame(req, &ws_pkt);
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "CLOSE from fd=%d", fd);
        // Consume the close payload (status code + reason)
        uint8_t close_buf[128] = {};
        if (ws_pkt.len > 0 && ws_pkt.len <= sizeof(close_buf)) {
            ws_pkt.payload = close_buf;
            httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        }
        // Reply with CLOSE
        ws_pkt.len = 0;
        ws_pkt.payload = nullptr;
        ws_pkt.type = HTTPD_WS_TYPE_CLOSE;
        httpd_ws_send_frame(req, &ws_pkt);
        self->remove_client(fd);
        return ESP_OK;
    }

    // ── Handle data frames (TEXT / BINARY / CONTINUE) ──

    if (ws_pkt.len > 0 && ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        char payload[128];
        if (ws_pkt.len >= sizeof(payload)) {
            ESP_LOGW(TAG, "WS payload too large: %d bytes", (int)ws_pkt.len);
            return ESP_FAIL;
        }
        ws_pkt.payload = reinterpret_cast<uint8_t*>(payload);
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) return ret;
        payload[ws_pkt.len] = '\0';

        ESP_LOGD(TAG, "WS recv: %s", payload);
        // Future: parse commands from client
    } else if (ws_pkt.len > 0) {
        // Non-TEXT data frame — consume and discard to keep stream aligned
        uint8_t discard[128];
        ws_pkt.payload = discard;
        size_t to_read = ws_pkt.len < sizeof(discard) ? ws_pkt.len : sizeof(discard);
        httpd_ws_recv_frame(req, &ws_pkt, to_read);
        ESP_LOGD(TAG, "Discarded non-TEXT frame type=%d len=%d", (int)ws_pkt.type, (int)ws_pkt.len);
    }

    return ESP_OK;
}

// ── Broadcast ───────────────────────────────────────────────────

void WsService::broadcast_state() {
    if (!server_ || !state_) return;

    // Delta broadcast: серіалізуємо тільки змінені ключі
    // for_each_changed_and_clear() атомарно ітерує + очищує changed list
    char buf[4096];
    WsSerCtx ctx = {buf, sizeof(buf), 0, true};
    ctx.pos += snprintf(buf, sizeof(buf), "{");

    bool had_changes = state_->for_each_changed_and_clear(ws_serialize_entry, &ctx);

    if (!had_changes) return;  // Нічого не змінилось — skip

    ctx.pos += snprintf(buf + ctx.pos, ctx.remaining(), "}");

    ESP_LOGD(TAG, "Delta broadcast: %d bytes", (int)ctx.pos);
    send_to_all(buf, ctx.pos);
}

void WsService::send_full_state_to(int fd) {
    if (!server_ || !state_) return;

    // Захист від OOM: ~6KB serialize buf + ~6KB AsyncSendCtx × 3 клієнти = ~18KB peak
    if (esp_get_free_heap_size() < 32000) {
        ESP_LOGW(TAG, "Heap < 32KB, skip initial state to fd=%d", fd);
        return;
    }

    // Серіалізуємо повний state для нового клієнта (heap, не stack!)
    // 154 entries × ~35 bytes avg = ~5.4KB потрібно
    static constexpr size_t BUF_SIZE = 6144;
    char* buf = static_cast<char*>(malloc(BUF_SIZE));
    if (!buf) {
        ESP_LOGW(TAG, "No memory for serialize buf to fd=%d", fd);
        return;
    }

    WsSerCtx ctx = {buf, BUF_SIZE, 0, true};
    ctx.pos += snprintf(buf, BUF_SIZE, "{");

    state_->for_each(ws_serialize_entry, &ctx);

    ctx.pos += snprintf(buf + ctx.pos, ctx.remaining(), "}");

    auto* send_ctx = static_cast<AsyncSendCtx*>(
        malloc(sizeof(AsyncSendCtx) + ctx.pos));
    if (!send_ctx) {
        ESP_LOGW(TAG, "No memory for initial state to fd=%d", fd);
        free(buf);
        return;
    }
    send_ctx->server = server_;
    send_ctx->self   = this;
    send_ctx->fd     = fd;
    send_ctx->type   = HTTPD_WS_TYPE_TEXT;
    send_ctx->len    = ctx.pos;
    memcpy(send_ctx->data, buf, ctx.pos);
    free(buf);

    if (httpd_queue_work(server_, async_send_cb, send_ctx) != ESP_OK) {
        ESP_LOGW(TAG, "Queue full for initial state fd=%d", fd);
        free(send_ctx);
    } else {
        ESP_LOGI(TAG, "Full state sent to new client fd=%d (%d bytes)", fd, (int)ctx.pos);
    }
}

// ── Thread-safe send via httpd_queue_work ────────────────────

// Executed in the httpd thread — safe to call send() on the socket.
void WsService::async_send_cb(void* arg) {
    auto* ctx = static_cast<AsyncSendCtx*>(arg);

    // Fast path: if a previous callback already removed this fd from our
    // client list, skip the send entirely — avoids hitting the socket and
    // producing httpd_txrx warnings for a known-dead connection.
    if (ctx->self && !ctx->self->has_client(ctx->fd)) {
        ESP_LOGD(TAG, "fd=%d already removed before send, skipping", ctx->fd);
        free(ctx);
        return;
    }

    httpd_ws_client_info_t info = httpd_ws_get_fd_info(ctx->server, ctx->fd);
    if (info != HTTPD_WS_CLIENT_WEBSOCKET) {
        ESP_LOGD(TAG, "fd=%d no longer WS at send time (info=%d), skipping",
                 ctx->fd, (int)info);
        if (ctx->self) ctx->self->remove_client(ctx->fd);
        free(ctx);
        return;
    }

    httpd_ws_frame_t frame = {};
    frame.type    = ctx->type;
    frame.payload = (ctx->len > 0)
                        ? reinterpret_cast<uint8_t*>(ctx->data)
                        : nullptr;
    frame.len     = ctx->len;

    esp_err_t ret = httpd_ws_send_frame_async(ctx->server, ctx->fd, &frame);
    if (ret != ESP_OK) {
        // Only act if this fd was still tracked — avoids duplicate
        // trigger_close calls when multiple queued sends fail in sequence
        bool was_tracked = ctx->self ? ctx->self->remove_client(ctx->fd) : false;
        if (was_tracked) {
            ESP_LOGW(TAG, "Send failed fd=%d: %s — closing", ctx->fd, esp_err_to_name(ret));
            httpd_sess_trigger_close(ctx->server, ctx->fd);
        } else {
            ESP_LOGD(TAG, "Send failed fd=%d (already removed), ignoring", ctx->fd);
        }
    }

    free(ctx);
}

void WsService::send_to_all(const char* data, size_t len) {
    // BUG-021: snapshot fds під mutex, queue work поза mutex
    int fds[MAX_WS_CLIENTS];
    if (clients_mutex_) {
        xSemaphoreTake(clients_mutex_, portMAX_DELAY);
        for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
            int fd = client_fds_[i];
            if (fd != -1 && !is_ws_client(fd)) {
                ESP_LOGD(TAG, "fd=%d no longer WS, removing (slot=%d)", fd, (int)i);
                client_fds_[i] = -1;
                fd = -1;
            }
            fds[i] = fd;
        }
        xSemaphoreGive(clients_mutex_);
    } else {
        memcpy(fds, client_fds_, sizeof(fds));
    }

    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (fds[i] == -1) continue;

        // Захист від OOM: delta payload ~200B + AsyncSendCtx
        if (esp_get_free_heap_size() < 8000) {
            ESP_LOGW(TAG, "Heap < 8KB, skip WS send fd=%d", fds[i]);
            continue;
        }

        auto* ctx = static_cast<AsyncSendCtx*>(
            malloc(sizeof(AsyncSendCtx) + len));
        if (!ctx) {
            ESP_LOGW(TAG, "No memory for async send to fd=%d — removing client", fds[i]);
            remove_client(fds[i]);
            continue;
        }
        ctx->server = server_;
        ctx->self   = this;
        ctx->fd     = fds[i];
        ctx->type   = HTTPD_WS_TYPE_TEXT;
        ctx->len    = len;
        memcpy(ctx->data, data, len);

        if (httpd_queue_work(server_, async_send_cb, ctx) != ESP_OK) {
            ESP_LOGW(TAG, "Queue full for fd=%d, removing", fds[i]);
            free(ctx);
            remove_client(fds[i]);
        }
    }
}

void WsService::send_ping_to_all() {
    // BUG-021: snapshot fds під mutex
    int fds[MAX_WS_CLIENTS];
    if (clients_mutex_) {
        xSemaphoreTake(clients_mutex_, portMAX_DELAY);
        for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
            int fd = client_fds_[i];
            if (fd != -1 && !is_ws_client(fd)) {
                client_fds_[i] = -1;
                fd = -1;
            }
            fds[i] = fd;
        }
        xSemaphoreGive(clients_mutex_);
    } else {
        memcpy(fds, client_fds_, sizeof(fds));
    }

    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (fds[i] == -1) continue;

        // Ping frame: AsyncSendCtx без payload (~48B)
        if (esp_get_free_heap_size() < 8000) {
            ESP_LOGW(TAG, "Heap < 8KB, skip WS ping fd=%d", fds[i]);
            continue;
        }

        auto* ctx = static_cast<AsyncSendCtx*>(
            malloc(sizeof(AsyncSendCtx)));
        if (!ctx) {
            remove_client(fds[i]);
            continue;
        }
        ctx->server = server_;
        ctx->self   = this;
        ctx->fd     = fds[i];
        ctx->type   = HTTPD_WS_TYPE_PING;
        ctx->len    = 0;

        if (httpd_queue_work(server_, async_send_cb, ctx) != ESP_OK) {
            ESP_LOGW(TAG, "Ping queue full for fd=%d", fds[i]);
            free(ctx);
            remove_client(fds[i]);
        }
    }
}

// ── WS handler registration ────────────────────────────────────

void WsService::register_ws_handler() {
    if (!server_) {
        ESP_LOGE(TAG, "Cannot register WS handler: no HTTP server");
        return;
    }
    if (ws_registered_) {
        return;  // Already registered — skip silently
    }

    httpd_uri_t ws_uri = {};
    ws_uri.uri          = "/ws";
    ws_uri.method       = HTTP_GET;
    ws_uri.handler      = ws_handler;
    ws_uri.user_ctx     = this;
    ws_uri.is_websocket = true;
    // We MUST handle control frames ourselves.  When this is false (default),
    // ESP-IDF auto-handles PING (replies PONG) and CLOSE, but does NOT
    // consume PONG frames at all — their second-byte + mask-key + payload
    // remain in the socket buffer, causing the next read to misinterpret
    // leftover bytes as a new frame header ("WS frame is not properly masked").
    ws_uri.handle_ws_control_frames = true;

    esp_err_t err = httpd_register_uri_handler(server_, &ws_uri);
    if (err == ESP_OK) {
        ws_registered_ = true;
        ESP_LOGI(TAG, "WebSocket handler registered at /ws");
    } else {
        ESP_LOGE(TAG, "Failed to register /ws handler: %s", esp_err_to_name(err));
        return;
    }

    // Install session close callback so we get notified when any
    // client disconnects (TCP RST, timeout, clean close).
    // NOTE: httpd_sess_set_close_fn is per-session; we install it in
    // ws_handler on each new WS connection instead.
}

void WsService::set_http_server(httpd_handle_t server) {
    if (!server) return;                 // Немає серверу — нічого робити
    if (server_ == server && ws_registered_) return;  // Вже налаштований
    server_ = server;
    register_ws_handler();
}

// ── Lifecycle ───────────────────────────────────────────────────

bool WsService::on_init() {
    // BUG-021: створити mutex для client_fds_
    clients_mutex_ = xSemaphoreCreateMutexStatic(&clients_mutex_buf_);

    ESP_LOGI(TAG, "WS service init (waiting for HTTP server handle)");
    // WS handler буде зареєстрований коли set_http_server() буде
    // викликаний з main.cpp після старту HttpService.
    // Якщо server_ вже встановлений І handler ще не зареєстрований — реєструємо.
    if (server_ && !ws_registered_) {
        register_ws_handler();
    }
    return true;
}

void WsService::on_update(uint32_t dt_ms) {
    if (!server_ || !state_) return;

    // Delta broadcast: перевіряємо changed_keys_ замість version counter
    broadcast_timer_ += dt_ms;
    if (broadcast_timer_ >= BROADCAST_INTERVAL_MS) {
        broadcast_timer_ = 0;
        if (state_->has_changes()) {
            broadcast_state();
        }
    }

    // Heartbeat ping + cleanup stale fds
    heartbeat_timer_ += dt_ms;
    if (heartbeat_timer_ >= HEARTBEAT_INTERVAL_MS) {
        heartbeat_timer_ = 0;
        cleanup_dead_clients();
        send_ping_to_all();
    }
}

} // namespace modesp
