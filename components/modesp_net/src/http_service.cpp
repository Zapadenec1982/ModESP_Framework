/**
 * @file http_service.cpp
 * @brief HTTP server with REST API and static file serving
 */

#include "modesp/net/http_service.h"
#include "modesp/shared_state.h"
#include "modesp/module_manager.h"
#include "modesp/services/config_service.h"
#include "modesp/services/persist_service.h"
#include "modesp/net/wifi_service.h"
#include "modesp/hal/hal.h"
#include "modesp/types.h"
#include "ds18b20_driver.h"
#include "datalogger_module.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_sntp.h"
#include "modesp/services/nvs_helper.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "state_meta.h"
#include "jsmn.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <sys/stat.h>

static const char* TAG = "HTTP";

namespace modesp {

// ── Auth credentials (static, accessible from any handler) ──────

static char s_auth_user[32] = "admin";
static char s_auth_pass[64] = "modesp";
static bool s_auth_enabled = true;
static const char* AUTH_NVS_NS = "auth";

// ── Auth rate-limiting (brute force protection) ──────
static constexpr uint8_t  AUTH_MAX_FAILURES    = 5;
static constexpr uint32_t AUTH_LOCKOUT_MS      = 30000;  // 30 секунд блокування
static uint8_t  s_auth_fail_count = 0;
static uint32_t s_auth_lockout_until = 0;  // xTaskGetTickCount() значення

void HttpService::load_auth_from_nvs() {
    nvs_helper::read_str(AUTH_NVS_NS, "user", s_auth_user, sizeof(s_auth_user));
    nvs_helper::read_str(AUTH_NVS_NS, "pass", s_auth_pass, sizeof(s_auth_pass));
    bool enabled = true;
    nvs_helper::read_bool(AUTH_NVS_NS, "enabled", enabled);
    s_auth_enabled = enabled;
    ESP_LOGI(TAG, "Auth loaded (user='%s', enabled=%d)", s_auth_user, s_auth_enabled);
}

void HttpService::save_auth_to_nvs() {
    nvs_helper::write_str(AUTH_NVS_NS, "user", s_auth_user);
    nvs_helper::write_str(AUTH_NVS_NS, "pass", s_auth_pass);
    nvs_helper::write_bool(AUTH_NVS_NS, "enabled", s_auth_enabled);
    ESP_LOGI(TAG, "Auth saved (user='%s', enabled=%d)", s_auth_user, s_auth_enabled);
}

bool HttpService::check_auth(httpd_req_t* req) {
    // Якщо auth вимкнено — пропускаємо
    if (!s_auth_enabled) return true;

    // Rate-limiting: блокування після AUTH_MAX_FAILURES невдалих спроб
    if (s_auth_fail_count >= AUTH_MAX_FAILURES) {
        uint32_t now = xTaskGetTickCount();
        if ((now - s_auth_lockout_until) < pdMS_TO_TICKS(AUTH_LOCKOUT_MS)) {
            ESP_LOGW(TAG, "Auth locked out (%u failures, %lus remaining)",
                     s_auth_fail_count,
                     (unsigned long)((pdMS_TO_TICKS(AUTH_LOCKOUT_MS) - (now - s_auth_lockout_until))
                                     / configTICK_RATE_HZ));
            httpd_resp_set_status(req, "429 Too Many Requests");
            httpd_resp_set_type(req, "text/plain");
            httpd_resp_send(req, "Too many failed attempts. Try again later.", HTTPD_RESP_USE_STRLEN);
            return false;
        }
        // Lockout минув — скидаємо лічильник
        s_auth_fail_count = 0;
    }

    // Читаємо Authorization header
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0 || hdr_len > 200) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ModESP\"");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return false;
    }

    char auth_buf[202];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ModESP\"");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return false;
    }

    // Перевіряємо "Basic " prefix
    if (strncmp(auth_buf, "Basic ", 6) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ModESP\"");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return false;
    }

    // Base64 decode
    unsigned char decoded[128];
    size_t decoded_len = 0;
    int ret = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                                     (const unsigned char*)(auth_buf + 6),
                                     strlen(auth_buf + 6));
    if (ret != 0 || decoded_len == 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ModESP\"");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return false;
    }
    decoded[decoded_len] = '\0';

    // Розділяємо user:pass
    char* colon = strchr((char*)decoded, ':');
    if (!colon) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ModESP\"");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return false;
    }

    *colon = '\0';
    const char* user = (char*)decoded;
    const char* pass = colon + 1;

    if (strcmp(user, s_auth_user) == 0 && strcmp(pass, s_auth_pass) == 0) {
        s_auth_fail_count = 0;  // Успішний логін — скидаємо лічильник
        return true;
    }

    // Невдала спроба — інкремент лічильника
    s_auth_fail_count++;
    if (s_auth_fail_count >= AUTH_MAX_FAILURES) {
        s_auth_lockout_until = xTaskGetTickCount();
        ESP_LOGW(TAG, "Auth lockout triggered after %u failures", s_auth_fail_count);
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ModESP\"");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
    return false;
}

// ── Helpers ─────────────────────────────────────────────────────

// AUDIT-039: CORS * видалений — UI на тому ж origin, cross-origin запити не потрібні.
// Це захищає від CSRF атак з зовнішніх сайтів на локальний IP пристрою.
void HttpService::set_cors_headers(httpd_req_t* req) {
    (void)req;
}

esp_err_t HttpService::handle_options(httpd_req_t* req) {
    set_cors_headers(req);
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// Helper: read a file from LittleFS into buffer, return bytes read or -1
static int read_file_to_buf(const char* path, char* buf, size_t buf_size) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    int len = (int)fread(buf, 1, buf_size - 1, f);
    fclose(f);
    if (len >= 0) buf[len] = '\0';
    return len;
}

// AUDIT-004: JSON escape для string значень (захист від SSID з лапками тощо)
// Ескейпить ", \, і control characters. Пише в dest, повертає кількість записаних байт.
static size_t json_escape_str(char* dest, size_t dest_size, const char* src) {
    size_t w = 0;
    for (const char* p = src; *p && w + 2 < dest_size; ++p) {
        switch (*p) {
            case '"':  dest[w++] = '\\'; dest[w++] = '"';  break;
            case '\\': dest[w++] = '\\'; dest[w++] = '\\'; break;
            case '\n': dest[w++] = '\\'; dest[w++] = 'n';  break;
            case '\r': dest[w++] = '\\'; dest[w++] = 'r';  break;
            case '\t': dest[w++] = '\\'; dest[w++] = 't';  break;
            default:
                if (static_cast<unsigned char>(*p) < 0x20) {
                    // Пропускаємо інші control characters
                } else {
                    dest[w++] = *p;
                }
                break;
        }
    }
    dest[w] = '\0';
    return w;
}

// Context for SharedState serialization
struct SerCtx {
    char* buf;
    size_t size;
    size_t pos;
    bool first;

    size_t remaining() const { return (pos < size) ? size - pos : 0; }
};

static void serialize_state_entry(const StateKey& key, const StateValue& value, void* ud) {
    auto* ctx = static_cast<SerCtx*>(ud);
    if (ctx->remaining() < 64) return; // safety margin

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
        // AUDIT-004: ескейпимо string значення для коректного JSON
        char escaped[64];
        json_escape_str(escaped, sizeof(escaped),
                        etl::get<StringValue>(value).c_str());
        ctx->pos += snprintf(ctx->buf + ctx->pos, ctx->remaining(),
                             "%s\"%s\":\"%s\"", sep, key.c_str(), escaped);
    }
}

// ── API Handlers ────────────────────────────────────────────────

esp_err_t HttpService::handle_get_state(httpd_req_t* req) {
    auto* self = static_cast<HttpService*>(req->user_ctx);

    char buf[4096];  // AUDIT-021: ~87 ключів × ~35 bytes/key, запас до ~115
    SerCtx ctx = {buf, sizeof(buf), 0, true};
    ctx.pos += snprintf(buf, sizeof(buf), "{");

    self->state_->for_each(serialize_state_entry, &ctx);

    ctx.pos += snprintf(buf + ctx.pos, ctx.remaining(), "}");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)ctx.pos);
    return ESP_OK;
}

esp_err_t HttpService::handle_get_board(httpd_req_t* req) {
    char buf[1024];
    int len = read_file_to_buf("/data/board.json", buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "board.json not found");
        return ESP_FAIL;
    }

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

esp_err_t HttpService::handle_get_ui(httpd_req_t* req) {
    FILE* f = fopen("/data/ui.json", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "ui.json not found");
        return ESP_FAIL;
    }

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    // Стрімінг файлу чанками — ui.json може бути >4KB
    char buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t HttpService::handle_get_bindings(httpd_req_t* req) {
    char buf[1024];
    int len = read_file_to_buf("/data/bindings.json", buf, sizeof(buf));
    if (len < 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "bindings.json not found");
        return ESP_FAIL;
    }

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

esp_err_t HttpService::handle_post_bindings(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    // Приймаємо JSON body — bindings.json зазвичай < 512 байт
    char buf[1024];
    int total = 0;
    int remaining = req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            remaining <= 0 ? "Empty body" : "Body too large");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf + total, remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Receive error");
            return ESP_FAIL;
        }
        total += recv_len;
        remaining -= recv_len;
    }
    buf[total] = '\0';

    // Мінімальна валідація JSON: manifest_version + bindings array
    jsmn_parser parser;
    jsmntok_t tokens[96];  // ~10 bindings × ~8 tokens/binding + overhead
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, total, tokens, 96);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Перевірка: є manifest_version і bindings array
    bool has_version = false;
    bool has_bindings = false;
    for (int i = 1; i + 1 < r; ) {
        if (tokens[i].type != JSMN_STRING) break;
        int klen = tokens[i].end - tokens[i].start;
        const char* kstr = buf + tokens[i].start;
        if (klen == 16 && strncmp(kstr, "manifest_version", 16) == 0) {
            has_version = true;
        } else if (klen == 8 && strncmp(kstr, "bindings", 8) == 0) {
            if (tokens[i + 1].type == JSMN_ARRAY) {
                has_bindings = true;
            }
        }
        // Пропуск ключ + значення
        i++;  // skip key
        // skip value (може бути об'єкт/масив з вкладеними токенами)
        if (i < r && (tokens[i].type == JSMN_PRIMITIVE ||
                      tokens[i].type == JSMN_STRING)) {
            i++;
        } else if (i < r) {
            // Об'єкт або масив — порахувати всі вкладені токени
            int count = 1;
            int j = i + 1;
            while (count > 0 && j < r) {
                if (tokens[j].type == JSMN_OBJECT || tokens[j].type == JSMN_ARRAY) {
                    count += tokens[j].size;
                }
                count--;
                j++;
            }
            i = j;
        }
    }

    if (!has_version || !has_bindings) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing manifest_version or bindings array");
        return ESP_FAIL;
    }

    // Записуємо raw JSON в файл
    FILE* f = fopen("/data/bindings.json", "w");
    if (!f) {
        ESP_LOGE(TAG, "POST /bindings: cannot open file for writing");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Cannot write bindings.json");
        return ESP_FAIL;
    }
    size_t written = fwrite(buf, 1, total, f);
    fclose(f);

    if ((int)written != total) {
        ESP_LOGE(TAG, "POST /bindings: write incomplete (%d/%d)", (int)written, total);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "POST /bindings: saved %d bytes, restart needed", total);

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"needs_restart\":true}");
    return ESP_OK;
}

// Callback context for module serialization
struct ModSerCtx {
    char* buf;
    size_t size;
    size_t pos;
    bool first;
    size_t remaining() const { return (pos < size) ? size - pos : 0; }
};

static const char* state_to_str(BaseModule::State s) {
    switch (s) {
        case BaseModule::State::CREATED:     return "created";
        case BaseModule::State::INITIALIZED: return "initialized";
        case BaseModule::State::RUNNING:     return "running";
        case BaseModule::State::STOPPED:     return "stopped";
        case BaseModule::State::ERROR:       return "error";
        default:                             return "unknown";
    }
}

static void serialize_module(const BaseModule& module, void* ud) {
    auto* ctx = static_cast<ModSerCtx*>(ud);
    if (ctx->remaining() < 128) return;

    const char* sep = ctx->first ? "" : ",";
    ctx->first = false;

    ctx->pos += snprintf(ctx->buf + ctx->pos, ctx->remaining(),
                         "%s{\"name\":\"%s\",\"priority\":%d,\"state\":\"%s\"}",
                         sep,
                         module.name(),
                         static_cast<int>(module.priority()),
                         state_to_str(module.state()));
}

esp_err_t HttpService::handle_get_modules(httpd_req_t* req) {
    auto* self = static_cast<HttpService*>(req->user_ctx);

    char buf[1536];
    ModSerCtx ctx = {buf, sizeof(buf), 0, true};
    ctx.pos += snprintf(buf, sizeof(buf), "[");

    self->modules_->for_each_module(serialize_module, &ctx);

    ctx.pos += snprintf(buf + ctx.pos, ctx.remaining(), "]");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)ctx.pos);
    return ESP_OK;
}

esp_err_t HttpService::handle_post_settings(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    auto* self = static_cast<HttpService*>(req->user_ctx);

    // 512B: один ключ до 32 chars + float = ~40B → вміщує до ~12 пар за раз.
    // 32 tokens: 1 об'єкт + 2 tokens/пара × 15 пар = 31 tokens — з запасом.
    char buf[512];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    // Parse with jsmn
    jsmn_parser parser;
    jsmntok_t tokens[32];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, len, tokens, 32);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    int accepted = 0;
    int rejected = 0;

    for (int i = 1; i + 1 < r; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;

        const char* key_str = buf + tokens[i].start;
        const jsmntok_t& val_tok = tokens[i + 1];

        // Null-terminate key and value in-place
        buf[tokens[i].end] = '\0';
        buf[val_tok.end] = '\0';
        const char* val_str = buf + val_tok.start;

        // Валідація через state_meta — тільки відомі writable ключі
        const auto* meta = gen::find_state_meta(key_str);
        if (!meta || !meta->writable) {
            ESP_LOGW(TAG, "POST /settings: rejected key '%s' (unknown or readonly)", key_str);
            rejected++;
            continue;
        }

        if (val_tok.type == JSMN_PRIMITIVE) {
            // BUG-023 fix: використовуємо meta->type замість евристики з крапкою.
            // JavaScript може відправити "5" (без крапки) для float параметра,
            // що ламало persist (etl::get_if<float> на int32_t = nullptr).
            if (val_str[0] == 't' || val_str[0] == 'f') {
                self->state_->set(key_str, val_str[0] == 't');
                accepted++;
            } else if (strcmp(meta->type, "float") == 0) {
                float fval = static_cast<float>(atof(val_str));
                if (fval < meta->min_val) fval = meta->min_val;
                if (fval > meta->max_val) fval = meta->max_val;
                self->state_->set(key_str, fval);
                accepted++;
            } else {
                int32_t ival = static_cast<int32_t>(atoi(val_str));
                if (static_cast<float>(ival) < meta->min_val) ival = static_cast<int32_t>(meta->min_val);
                if (static_cast<float>(ival) > meta->max_val) ival = static_cast<int32_t>(meta->max_val);
                self->state_->set(key_str, ival);
                accepted++;
            }
        } else if (val_tok.type == JSMN_STRING) {
            self->state_->set(key_str, val_str);
            accepted++;
        }
    }

    ESP_LOGI(TAG, "POST /settings: %d accepted, %d rejected", accepted, rejected);

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    if (rejected > 0 && accepted == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "All keys rejected");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t HttpService::handle_post_wifi(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    auto* self = static_cast<HttpService*>(req->user_ctx);

    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    jsmn_parser parser;
    jsmntok_t tokens[8];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, len, tokens, 8);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char* ssid = nullptr;
    const char* pass = nullptr;

    for (int i = 1; i + 1 < r; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;
        buf[tokens[i].end] = '\0';
        buf[tokens[i + 1].end] = '\0';

        const char* key = buf + tokens[i].start;
        const char* val = buf + tokens[i + 1].start;

        if (strcmp(key, "ssid") == 0) ssid = val;
        else if (strcmp(key, "password") == 0) pass = val;
    }

    if (!ssid || !self->wifi_) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid");
        return ESP_FAIL;
    }

    bool ok = self->wifi_->save_credentials(ssid, pass ? pass : "");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    if (ok) {
        httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Credentials saved. Restart to connect.\"}");
        // Не робимо reconnect автоматично — UI каже користувачу перезавантажити.
        // Якщо робити reconnect тут — AP зникає і клієнт втрачає з'єднання
        // ще до отримання HTTP response.
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save");
    }
    return ESP_OK;
}

esp_err_t HttpService::handle_get_wifi_scan(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    auto* self = static_cast<HttpService*>(req->user_ctx);

    if (!self->wifi_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi service unavailable");
        return ESP_FAIL;
    }

    // Start blocking scan
    if (!self->wifi_->start_scan()) {
        set_cors_headers(req);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"networks\":[]}");
        return ESP_OK;
    }

    // Get results
    WiFiService::ScanResult results[WiFiService::MAX_SCAN_RESULTS];
    int count = self->wifi_->get_scan_results(results, WiFiService::MAX_SCAN_RESULTS);

    // Serialize to JSON
    char buf[2048];
    size_t pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"ok\":true,\"networks\":[");

    for (int i = 0; i < count; i++) {
        const char* sep = (i == 0) ? "" : ",";
        const char* auth_str;
        switch (results[i].authmode) {
            case 0: auth_str = "open"; break;
            case 1: auth_str = "WEP"; break;
            case 2: auth_str = "WPA"; break;
            case 3: auth_str = "WPA2"; break;
            case 4: auth_str = "WPA/WPA2"; break;
            case 5: auth_str = "WPA3"; break;
            case 6: auth_str = "WPA2/WPA3"; break;
            default: auth_str = "unknown"; break;
        }
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\"}",
                        sep, results[i].ssid, results[i].rssi, auth_str);
        if (pos >= sizeof(buf) - 64) break;
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)pos);
    return ESP_OK;
}

esp_err_t HttpService::handle_get_wifi_ap(httpd_req_t* req) {
    // Повертаємо поточну конфігурацію AP з NVS
    char ap_ssid[33] = {};
    int32_t ap_channel = 1;
    bool has_password = false;

    nvs_helper::read_str("wifi", "ap_ssid", ap_ssid, sizeof(ap_ssid));
    nvs_helper::read_i32("wifi", "ap_chan", ap_channel);

    // Якщо кастомний SSID не збережений — показуємо дефолтний (ModESP-XXXX)
    if (ap_ssid[0] == '\0') {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf(ap_ssid, sizeof(ap_ssid), "ModESP-%02X%02X", mac[4], mac[5]);
    }

    // Перевірити чи є збережений пароль (не повертаємо сам пароль)
    char ap_pass[65] = {};
    if (nvs_helper::read_str("wifi", "ap_pass", ap_pass, sizeof(ap_pass))) {
        has_password = (strlen(ap_pass) >= 8);
    }

    char buf[256];
    char escaped_ssid[68];
    json_escape_str(escaped_ssid, sizeof(escaped_ssid), ap_ssid);

    snprintf(buf, sizeof(buf),
             "{\"ssid\":\"%s\",\"channel\":%ld,\"password_set\":%s}",
             escaped_ssid, (long)ap_channel,
             has_password ? "true" : "false");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t HttpService::handle_post_wifi_ap(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    jsmn_parser parser;
    jsmntok_t tokens[12];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, len, tokens, 12);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char* ssid = nullptr;
    const char* password = nullptr;
    int channel = -1;

    for (int i = 1; i + 1 < r; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;
        buf[tokens[i].end] = '\0';
        buf[tokens[i + 1].end] = '\0';

        const char* key = buf + tokens[i].start;
        const char* val = buf + tokens[i + 1].start;

        if (strcmp(key, "ssid") == 0) ssid = val;
        else if (strcmp(key, "password") == 0) password = val;
        else if (strcmp(key, "channel") == 0) channel = atoi(val);
    }

    if (!ssid || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or empty ssid");
        return ESP_FAIL;
    }

    // Пароль: або порожній (open), або >= 8 символів (WPA2)
    if (password && strlen(password) > 0 && strlen(password) < 8) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Password must be empty (open) or at least 8 chars");
        return ESP_FAIL;
    }

    if (channel < 1 || channel > 13) channel = 1;

    // Зберігаємо в NVS
    nvs_helper::write_str("wifi", "ap_ssid", ssid);
    nvs_helper::write_str("wifi", "ap_pass", password ? password : "");
    nvs_helper::write_i32("wifi", "ap_chan", (int32_t)channel);

    ESP_LOGI(TAG, "AP config saved: ssid='%s' channel=%d pass=%s",
             ssid, channel, (password && strlen(password) >= 8) ? "set" : "open");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"AP config saved. Restart to apply.\"}");
    return ESP_OK;
}

esp_err_t HttpService::handle_post_restart(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    auto* self = static_cast<HttpService*>(req->user_ctx);
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Restarting...\"}");

    // Graceful shutdown: flush DataLogger RAM → flash, зупинити модулі
    if (self->modules_) {
        self->modules_->stop_all();
    }

    // BUG-014: flush pending NVS writes before restart
    if (self->persist_) {
        self->persist_->flush_now();
    }

    // Delay to allow response to be sent
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; // never reached
}

esp_err_t HttpService::handle_post_factory_reset(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    ESP_LOGW("HTTP", "FACTORY RESET — erasing NVS partition...");
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE("HTTP", "NVS erase failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS erase failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "{\"ok\":true,\"msg\":\"Factory reset. Restarting...\"}");

    // Graceful shutdown: flush DataLogger RAM → flash
    auto* self = static_cast<HttpService*>(req->user_ctx);
    if (self->modules_) {
        self->modules_->stop_all();
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; // never reached
}

// ── Backup / Restore ────────────────────────────────────────────

esp_err_t HttpService::handle_get_backup(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    auto* self = static_cast<HttpService*>(req->user_ctx);

    // Серіалізуємо тільки persist:true ключі з STATE_META
    char buf[2048];
    size_t pos = 0;
    pos += snprintf(buf, sizeof(buf), "{\"modesp_backup\":1,\"keys\":{");

    bool first = true;
    for (size_t i = 0; i < gen::STATE_META_COUNT; i++) {
        const auto& meta = gen::STATE_META[i];
        if (!meta.persist) continue;

        auto val = self->state_->get(meta.key);
        if (!val.has_value()) continue;

        size_t rem = sizeof(buf) - pos;
        if (rem < 64) break;

        const char* sep = first ? "" : ",";
        first = false;

        if (strcmp(meta.type, "float") == 0) {
            const auto* fp = etl::get_if<float>(&val.value());
            if (fp) {
                pos += snprintf(buf + pos, rem, "%s\"%s\":%.2f",
                                sep, meta.key, static_cast<double>(*fp));
            }
        } else if (strcmp(meta.type, "int") == 0) {
            const auto* ip = etl::get_if<int32_t>(&val.value());
            if (ip) {
                pos += snprintf(buf + pos, rem, "%s\"%s\":%ld",
                                sep, meta.key, (long)*ip);
            }
        } else if (strcmp(meta.type, "bool") == 0) {
            const auto* bp = etl::get_if<bool>(&val.value());
            if (bp) {
                pos += snprintf(buf + pos, rem, "%s\"%s\":%s",
                                sep, meta.key, *bp ? "true" : "false");
            }
        }
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "}}");

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"modesp_backup.json\"");
    httpd_resp_send(req, buf, (ssize_t)pos);
    return ESP_OK;
}

esp_err_t HttpService::handle_post_restore(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    auto* self = static_cast<HttpService*>(req->user_ctx);

    char buf[2048];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    // Парсимо JSON — шукаємо об'єкт "keys"
    jsmn_parser parser;
    jsmntok_t tokens[128];  // ~33 persist ключів × 2 + envelope
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, len, tokens, 128);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Знаходимо "keys" об'єкт
    int keys_tok = -1;
    for (int i = 1; i + 1 < r; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;
        int klen = tokens[i].end - tokens[i].start;
        if (klen == 4 && strncmp(buf + tokens[i].start, "keys", 4) == 0) {
            keys_tok = i + 1;
            break;
        }
        // Пропускаємо значення (може бути вкладений об'єкт)
        if (tokens[i + 1].type == JSMN_OBJECT || tokens[i + 1].type == JSMN_ARRAY) {
            int skip = tokens[i + 1].size * 2;
            i += skip;
        }
    }

    if (keys_tok < 0 || tokens[keys_tok].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'keys' object");
        return ESP_FAIL;
    }

    int restored = 0;
    int skipped = 0;
    int key_count = tokens[keys_tok].size;

    // Ітеруємо пари ключ:значення всередині "keys"
    int j = keys_tok + 1;
    for (int k = 0; k < key_count && j + 1 < r; k++, j += 2) {
        if (tokens[j].type != JSMN_STRING) continue;

        buf[tokens[j].end] = '\0';
        buf[tokens[j + 1].end] = '\0';
        const char* key_str = buf + tokens[j].start;
        const char* val_str = buf + tokens[j + 1].start;

        // Валідація: тільки persist ключі
        const auto* meta = gen::find_state_meta(key_str);
        if (!meta || !meta->persist) {
            skipped++;
            continue;
        }

        if (tokens[j + 1].type == JSMN_PRIMITIVE) {
            if (val_str[0] == 't' || val_str[0] == 'f') {
                self->state_->set(key_str, val_str[0] == 't');
                restored++;
            } else if (strcmp(meta->type, "float") == 0) {
                float fval = static_cast<float>(atof(val_str));
                if (fval < meta->min_val) fval = meta->min_val;
                if (fval > meta->max_val) fval = meta->max_val;
                self->state_->set(key_str, fval);
                restored++;
            } else {
                int32_t ival = static_cast<int32_t>(atoi(val_str));
                if (static_cast<float>(ival) < meta->min_val)
                    ival = static_cast<int32_t>(meta->min_val);
                if (static_cast<float>(ival) > meta->max_val)
                    ival = static_cast<int32_t>(meta->max_val);
                self->state_->set(key_str, ival);
                restored++;
            }
        }
    }

    ESP_LOGI(TAG, "Restore: %d restored, %d skipped", restored, skipped);

    // Flush NVS перед рестартом
    if (self->persist_) {
        self->persist_->flush_now();
    }

    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    char resp[96];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"restored\":%d,\"skipped\":%d}", restored, skipped);
    httpd_resp_sendstr(req, resp);

    // Graceful shutdown: flush DataLogger RAM → flash
    if (self->modules_) {
        self->modules_->stop_all();
    }

    // Перезавантаження для застосування налаштувань
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ── OTA Handlers ────────────────────────────────────────────────

esp_err_t HttpService::handle_get_ota(httpd_req_t* req) {
    set_cors_headers(req);

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t* desc = esp_app_get_description();

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"partition\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\","
        "\"date\":\"%s\",\"time\":\"%s\",\"board\":\"%s\"}",
        running ? running->label : "?",
        desc->version,
        desc->idf_ver,
        desc->date,
        desc->time,
        desc->project_name);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t HttpService::handle_post_ota(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    set_cors_headers(req);

    ESP_LOGI(TAG, "OTA: Starting firmware update (%d bytes)", req->content_len);

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    // Знайти OTA partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        ESP_LOGE(TAG, "OTA: No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    // Валідація розміру: firmware повинна поміститися в partition
    if (req->content_len > (int)update_partition->size) {
        ESP_LOGE(TAG, "OTA: Firmware too large (%d > %lu)",
                 req->content_len, (unsigned long)update_partition->size);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large");
        return ESP_FAIL;
    }
    if (req->content_len < 256) {
        ESP_LOGE(TAG, "OTA: File too small (%d bytes)", req->content_len);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too small");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA: Writing to partition '%s' at offset 0x%lx (%d bytes)",
             update_partition->label, (unsigned long)update_partition->address,
             req->content_len);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    // Приймаємо firmware частинами
    char buf[1024];
    int remaining = req->content_len;
    int received_total = 0;
    bool magic_checked = false;
    bool board_checked = false;

    while (remaining > 0) {
        int to_recv = (remaining < (int)sizeof(buf)) ? remaining : (int)sizeof(buf);
        int recv_len = httpd_req_recv(req, buf, to_recv);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; // Retry на timeout
            }
            ESP_LOGE(TAG, "OTA: Receive error at %d/%d bytes", received_total, req->content_len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        // Перевірка magic byte (ESP_IMAGE_HEADER_MAGIC = 0xE9) у першому chunk
        if (!magic_checked && recv_len > 0) {
            magic_checked = true;
            if (static_cast<uint8_t>(buf[0]) != 0xE9) {
                ESP_LOGE(TAG, "OTA: Invalid magic byte 0x%02X (expected 0xE9)",
                         static_cast<uint8_t>(buf[0]));
                esp_ota_abort(ota_handle);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not a valid ESP32 firmware");
                return ESP_FAIL;
            }
        }

        // Валідація плати: esp_app_desc_t на offset 0x20 у .bin
        if (!board_checked && received_total == 0 && recv_len >= 0x70) {
            board_checked = true;
            static constexpr size_t DESC_OFFSET = 0x20;
            static constexpr size_t NAME_OFFSET = DESC_OFFSET + 48; // project_name
            uint32_t desc_magic;
            memcpy(&desc_magic, buf + DESC_OFFSET, 4);
            if (desc_magic == 0xABCD5432) {
                char incoming_name[33] = {};
                memcpy(incoming_name, buf + NAME_OFFSET, 32);
                const char* running_name = esp_app_get_description()->project_name;
                if (strcmp(incoming_name, running_name) != 0) {
                    ESP_LOGE(TAG, "OTA: Board mismatch: running '%s', incoming '%s'",
                             running_name, incoming_name);
                    esp_ota_abort(ota_handle);
                    char err_msg[160];
                    snprintf(err_msg, sizeof(err_msg),
                        "{\"error\":\"board_mismatch\",\"running\":\"%s\",\"incoming\":\"%s\"}",
                        running_name, incoming_name);
                    httpd_resp_set_type(req, "application/json");
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_sendstr(req, err_msg);
                    return ESP_FAIL;
                }
                ESP_LOGI(TAG, "OTA: Board check OK (%s)", running_name);
            }
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA: Write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= recv_len;
        received_total += recv_len;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: Validation failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: Set boot partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA: Success! Wrote %d bytes to '%s'", received_total, update_partition->label);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Firmware updated. Restarting...\"}");

    // Graceful shutdown: flush DataLogger RAM → flash
    auto* self = static_cast<HttpService*>(req->user_ctx);
    if (self->modules_) {
        self->modules_->stop_all();
    }

    // Дати HTTP response дійти до клієнта перед restart
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK; // unreachable
}

// ── Time API ────────────────────────────────────────────────────

esp_err_t HttpService::handle_get_time(httpd_req_t* req) {
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    bool ntp_enabled = true;
    nvs_helper::read_bool("time", "ntp_enabled", ntp_enabled);

    char tz[48] = "EET-2EEST,M3.5.0/3,M10.5.0/4";
    nvs_helper::read_str("time", "timezone", tz, sizeof(tz));

    time_t now = time(nullptr);
    bool synced = (now > 1700000000);

    char time_str[9] = "--:--:--";
    char date_str[11] = "--.--.----";
    if (synced) {
        struct tm ti;
        localtime_r(&now, &ti);
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &ti);
        strftime(date_str, sizeof(date_str), "%d.%m.%Y", &ti);
    }

    char json[256];
    snprintf(json, sizeof(json),
             "{\"time\":\"%s\",\"date\":\"%s\",\"ntp_enabled\":%s,"
             "\"timezone\":\"%s\",\"synced\":%s,\"unix\":%ld}",
             time_str, date_str,
             ntp_enabled ? "true" : "false",
             tz,
             synced ? "true" : "false",
             (long)now);
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

esp_err_t HttpService::handle_post_time(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    set_cors_headers(req);

    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    jsmn_parser parser;
    jsmntok_t tokens[12];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, len, tokens, 12);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char* mode = nullptr;
    long unix_time = 0;
    char new_tz[48] = {};

    for (int i = 1; i + 1 < r; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;
        buf[tokens[i].end] = '\0';
        buf[tokens[i + 1].end] = '\0';
        const char* key = buf + tokens[i].start;
        const char* val = buf + tokens[i + 1].start;

        if (strcmp(key, "mode") == 0) {
            mode = val;
        } else if (strcmp(key, "time") == 0) {
            unix_time = atol(val);
        } else if (strcmp(key, "timezone") == 0) {
            strncpy(new_tz, val, sizeof(new_tz) - 1);
        }
    }

    // Зберегти timezone якщо передано
    if (new_tz[0]) {
        nvs_helper::write_str("time", "timezone", new_tz);
        setenv("TZ", new_tz, 1);
        tzset();
        ESP_LOGI(TAG, "Timezone set: %s", new_tz);
    }

    if (mode && strcmp(mode, "ntp") == 0) {
        nvs_helper::write_bool("time", "ntp_enabled", true);
        if (esp_sntp_enabled()) {
            esp_sntp_stop();
        }
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        ESP_LOGI(TAG, "NTP enabled, syncing...");
    } else if (mode && strcmp(mode, "manual") == 0) {
        nvs_helper::write_bool("time", "ntp_enabled", false);
        if (esp_sntp_enabled()) {
            esp_sntp_stop();
        }
        if (unix_time > 1700000000) {
            struct timeval tv = { .tv_sec = unix_time, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            ESP_LOGI(TAG, "Manual time set: %ld", unix_time);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// ── OneWire Scan Handler ────────────────────────────────────────

esp_err_t HttpService::handle_get_ow_scan(httpd_req_t* req) {
    auto* self = static_cast<HttpService*>(req->user_ctx);
    set_cors_headers(req);

    if (!self->hal_ || !self->config_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HAL not available");
        return ESP_FAIL;
    }

    // 1. Отримати bus_id з query string
    char query[64] = {};
    char bus_id[16] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query: ?bus=ow_1");
        return ESP_FAIL;
    }
    if (httpd_query_key_value(query, "bus", bus_id, sizeof(bus_id)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'bus' parameter");
        return ESP_FAIL;
    }

    // 2. Знайти GPIO для шини через HAL
    auto* ow_res = self->hal_->find_onewire_bus(
        etl::string_view(bus_id, strlen(bus_id)));
    if (!ow_res) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Bus not found");
        return ESP_FAIL;
    }

    // 3. Scan bus
    DS18B20Driver::RomAddress devices[DS18B20Driver::MAX_DEVICES_PER_BUS];
    size_t count = DS18B20Driver::scan_bus(ow_res->gpio, devices,
                                            DS18B20Driver::MAX_DEVICES_PER_BUS);

    // 4. Побудувати JSON — для кожного пристрою: адреса, температура, role
    const auto& bindings = self->config_->binding_table().bindings;

    // Збираємо SKIP_ROM bindings на цій шині (без адреси)
    // Вони вже "займають" пристрій на шині, навіть без конкретної адреси
    struct SkipRomBinding { const char* role; bool matched; };
    SkipRomBinding skip_rom[4] = {};
    size_t skip_rom_count = 0;
    for (const auto& b : bindings) {
        if (b.address.empty() &&
            b.driver_type == "ds18b20" &&
            b.hardware_id.size() == strlen(bus_id) &&
            strncmp(b.hardware_id.c_str(), bus_id, b.hardware_id.size()) == 0 &&
            skip_rom_count < 4) {
            skip_rom[skip_rom_count++] = { b.role.c_str(), false };
        }
    }

    char json[1536];
    int pos = snprintf(json, sizeof(json),
        "{\"bus\":\"%s\",\"gpio\":%d,\"devices\":[", bus_id, ow_res->gpio);

    for (size_t i = 0; i < count; i++) {
        char addr_str[24];
        DS18B20Driver::format_address(devices[i].bytes, addr_str, sizeof(addr_str));

        float temp = 0;
        bool has_temp = DS18B20Driver::read_temp_by_address(ow_res->gpio, devices[i], temp);

        // Пошук в bindings: спочатку по адресі
        const char* role = nullptr;
        for (const auto& b : bindings) {
            if (!b.address.empty() &&
                b.address.size() == strlen(addr_str) &&
                strncmp(b.address.c_str(), addr_str, b.address.size()) == 0) {
                role = b.role.c_str();
                break;
            }
        }

        // Якщо не знайдено по адресі — перевіряємо SKIP_ROM bindings на цій шині
        if (!role) {
            for (size_t s = 0; s < skip_rom_count; s++) {
                if (!skip_rom[s].matched) {
                    role = skip_rom[s].role;
                    skip_rom[s].matched = true;
                    break;
                }
            }
        }

        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"address\":\"%s\"", addr_str);
        if (has_temp) {
            pos += snprintf(json + pos, sizeof(json) - pos,
                ",\"temperature\":%.1f", static_cast<double>(temp));
        }
        if (role) {
            pos += snprintf(json + pos, sizeof(json) - pos,
                ",\"role\":\"%s\",\"status\":\"assigned\"", role);
        } else {
            pos += snprintf(json + pos, sizeof(json) - pos,
                ",\"role\":null,\"status\":\"new\"");
        }
        pos += snprintf(json + pos, sizeof(json) - pos, "}");

        if (pos >= (int)sizeof(json) - 128) break;  // Захист від overflow
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, pos);
}

// ── DataLogger API ──────────────────────────────────────────────

esp_err_t HttpService::handle_get_log(httpd_req_t* req) {
    auto* self = static_cast<HttpService*>(req->user_ctx);
    set_cors_headers(req);

    if (!self->datalogger_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "DataLogger not available");
        return ESP_FAIL;
    }

    // Парсимо ?hours=24
    int hours = 24;
    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "hours", val, sizeof(val)) == ESP_OK) {
            int h = atoi(val);
            if (h > 0 && h <= 168) hours = h;
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return self->datalogger_->serialize_log_chunked(req, hours);
}

esp_err_t HttpService::handle_get_log_summary(httpd_req_t* req) {
    auto* self = static_cast<HttpService*>(req->user_ctx);
    set_cors_headers(req);

    if (!self->datalogger_) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "DataLogger not available");
        return ESP_FAIL;
    }

    char buf[128];
    if (self->datalogger_->serialize_summary(buf, sizeof(buf))) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, buf, strlen(buf));
    }
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Serialize failed");
    return ESP_FAIL;
}

// ── Static file handler ─────────────────────────────────────────

static const char* get_content_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

esp_err_t HttpService::handle_static(httpd_req_t* req) {
    // Copy URI to local fixed-size buffer (silences -Wformat-truncation)
    char uri[52];
    strlcpy(uri, req->uri, sizeof(uri));
    if (strlen(req->uri) >= sizeof(uri)) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    // AUDIT-040: захист від directory traversal (/../../../etc/passwd)
    if (strstr(uri, "..") != nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK;
    }

    // Build filesystem path
    char filepath[64];
    if (strcmp(uri, "/") == 0) {
        strlcpy(filepath, "/data/www/index.html", sizeof(filepath));
    } else {
        snprintf(filepath, sizeof(filepath), "/data/www%s", uri);
    }

    // Try .gz version first
    char gz_path[68];
    snprintf(gz_path, sizeof(gz_path), "%s.gz", filepath);

    struct stat st;
    bool use_gz = (stat(gz_path, &st) == 0);
    const char* actual_path = use_gz ? gz_path : filepath;

    FILE* f = fopen(actual_path, "r");
    if (!f) {
        // Fallback to index.html for SPA routing
        f = fopen("/data/www/index.html", "r");
        if (!f) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
            return ESP_FAIL;
        }
        snprintf(filepath, sizeof(filepath), "/data/www/index.html");
        use_gz = false;
    }

    httpd_resp_set_type(req, get_content_type(filepath));
    if (use_gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        // no-store: браузер завжди запитує свіжу версію (важливо при OTA/flash)
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    }

    // Stream file in chunks
    char buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

// ── Auth settings (GET + POST) ──────────────────────────────────

esp_err_t HttpService::handle_get_auth(httpd_req_t* req) {
    // GET /api/auth — публічний (без auth), ніколи не повертає пароль
    set_cors_headers(req);
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"enabled\":%s,\"username\":\"%s\"}",
             s_auth_enabled ? "true" : "false",
             s_auth_user);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t HttpService::handle_post_auth(httpd_req_t* req) {
    if (!check_auth(req)) return ESP_OK;
    set_cors_headers(req);

    char buf[384];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[len] = '\0';

    // Parse JSON: enabled, username, current_pass, new_pass, reset
    jsmn_parser parser;
    jsmntok_t tokens[16];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, buf, len, tokens, 16);
    if (r < 1 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char* current_pass = nullptr;
    const char* new_pass = nullptr;
    const char* username = nullptr;
    int enabled_val = -1;  // -1 = не вказано
    bool reset = false;

    for (int i = 1; i + 1 < r; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;
        buf[tokens[i].end] = '\0';
        buf[tokens[i + 1].end] = '\0';
        const char* key = buf + tokens[i].start;
        const char* val = buf + tokens[i + 1].start;

        if (strcmp(key, "current_pass") == 0) current_pass = val;
        else if (strcmp(key, "new_pass") == 0) new_pass = val;
        else if (strcmp(key, "username") == 0) username = val;
        else if (strcmp(key, "enabled") == 0) {
            enabled_val = (val[0] == 't') ? 1 : 0;
        }
        else if (strcmp(key, "reset") == 0) {
            reset = (val[0] == 't');
        }
    }

    // Скидання до заводських
    if (reset) {
        if (!current_pass || strcmp(current_pass, s_auth_pass) != 0) {
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"wrong_password\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        strncpy(s_auth_user, "admin", sizeof(s_auth_user) - 1);
        strncpy(s_auth_pass, "modesp", sizeof(s_auth_pass) - 1);
        s_auth_enabled = true;
        save_auth_to_nvs();
        ESP_LOGI(TAG, "Auth reset to factory defaults");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":true,\"reset\":true}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Зміна пароля — потрібен current_pass
    if (new_pass) {
        if (!current_pass || strcmp(current_pass, s_auth_pass) != 0) {
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"wrong_password\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        if (strlen(new_pass) < 4) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too short (min 4)");
            return ESP_FAIL;
        }
        strncpy(s_auth_pass, new_pass, sizeof(s_auth_pass) - 1);
        s_auth_pass[sizeof(s_auth_pass) - 1] = '\0';
    }

    // Зміна username
    if (username && strlen(username) > 0 && strlen(username) < sizeof(s_auth_user)) {
        strncpy(s_auth_user, username, sizeof(s_auth_user) - 1);
        s_auth_user[sizeof(s_auth_user) - 1] = '\0';
    }

    // Toggle enabled
    if (enabled_val >= 0) {
        s_auth_enabled = (enabled_val == 1);
    }

    save_auth_to_nvs();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── Server setup ────────────────────────────────────────────────

void HttpService::register_api_handlers() {
    // GET endpoints
    struct {
        const char* uri;
        httpd_method_t method;
        esp_err_t (*handler)(httpd_req_t*);
    } endpoints[] = {
        {"/api/state",    HTTP_GET,  handle_get_state},
        {"/api/board",    HTTP_GET,  handle_get_board},
        {"/api/ui",       HTTP_GET,  handle_get_ui},
        {"/api/bindings", HTTP_GET,  handle_get_bindings},
        {"/api/bindings", HTTP_POST, handle_post_bindings},
        {"/api/modules",  HTTP_GET,  handle_get_modules},
        {"/api/settings",   HTTP_POST, handle_post_settings},
        {"/api/wifi",       HTTP_POST, handle_post_wifi},
        {"/api/wifi/scan",  HTTP_GET,  handle_get_wifi_scan},
        {"/api/wifi/ap",    HTTP_GET,  handle_get_wifi_ap},
        {"/api/wifi/ap",    HTTP_POST, handle_post_wifi_ap},
        {"/api/restart",    HTTP_POST, handle_post_restart},
        {"/api/factory-reset", HTTP_POST, handle_post_factory_reset},
        {"/api/backup",  HTTP_GET,  handle_get_backup},
        {"/api/restore", HTTP_POST, handle_post_restore},
        {"/api/ota",        HTTP_GET,  handle_get_ota},
        {"/api/time",       HTTP_GET,  handle_get_time},
        {"/api/time",       HTTP_POST, handle_post_time},
        {"/api/onewire/scan", HTTP_GET, handle_get_ow_scan},
        {"/api/log",         HTTP_GET, handle_get_log},
        {"/api/log/summary", HTTP_GET, handle_get_log_summary},
        {"/api/auth", HTTP_GET,  handle_get_auth},
        {"/api/auth", HTTP_POST, handle_post_auth},
    };

    // Реєструємо handlers + OPTIONS для CORS preflight
    // Використовуємо масив для відстеження URI, де OPTIONS вже зареєстрований
    const char* options_registered[24] = {};
    int options_count = 0;

    for (auto& ep : endpoints) {
        httpd_uri_t uri_handler = {};
        uri_handler.uri      = ep.uri;
        uri_handler.method   = ep.method;
        uri_handler.handler  = ep.handler;
        uri_handler.user_ctx = this;
        httpd_register_uri_handler(server_, &uri_handler);

        // OPTIONS для CORS — лише один раз per URI
        bool already = false;
        for (int j = 0; j < options_count; j++) {
            if (strcmp(options_registered[j], ep.uri) == 0) {
                already = true;
                break;
            }
        }
        if (!already && options_count < 16) {
            httpd_uri_t options = {};
            options.uri      = ep.uri;
            options.method   = HTTP_OPTIONS;
            options.handler  = handle_options;
            options.user_ctx = this;
            httpd_register_uri_handler(server_, &options);
            options_registered[options_count++] = ep.uri;
        }
    }

    // OTA POST registered separately (OPTIONS вже від GET /api/ota)
    httpd_uri_t ota_post = {};
    ota_post.uri      = "/api/ota";
    ota_post.method   = HTTP_POST;
    ota_post.handler  = handle_post_ota;
    ota_post.user_ctx = this;
    httpd_register_uri_handler(server_, &ota_post);

    ESP_LOGI(TAG, "API handlers registered (%d endpoints + OTA upload)",
             (int)(sizeof(endpoints) / sizeof(endpoints[0])));
}

void HttpService::register_static_handler() {
    httpd_uri_t static_handler = {};
    static_handler.uri            = "/*";
    static_handler.method         = HTTP_GET;
    static_handler.handler        = handle_static;
    static_handler.user_ctx       = this;
    static_handler.is_websocket   = false;
    httpd_register_uri_handler(server_, &static_handler);

    ESP_LOGI(TAG, "Static file handler registered (/* -> /data/www/)");
}

bool HttpService::start_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 48;
    config.max_open_sockets = 4;   // 3 WS + 1 HTTP; lwIP 10 total (1 listener + 4 sess + 1 MQTT = 6)
    config.stack_size = 8192;

    // WebSocket connections are long-lived. The default recv_wait_timeout
    // of 5s causes httpd to close idle WS clients (the recv() times out
    // and httpd interprets the next read as a malformed frame).
    // 30s matches the WS heartbeat interval so pings keep the session alive.
    config.recv_wait_timeout  = 30;
    config.send_wait_timeout  = 10;

    // When max_open_sockets is reached, close the oldest idle connection
    // instead of rejecting new ones. Essential when WS clients hold slots.
    config.lru_purge_enable   = true;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return false;
    }

    register_api_handlers();
    // NOTE: register_static_handler() must be called from main.cpp
    // AFTER all other handlers (WS, etc.) to avoid wildcard shadowing.

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return true;
}

// ── Lifecycle ───────────────────────────────────────────────────

bool HttpService::on_init() {
    ESP_LOGI(TAG, "Starting HTTP service...");
    load_auth_from_nvs();
    return start_server();
}

void HttpService::on_update(uint32_t dt_ms) {
    // Nothing to do — httpd runs in its own task
    (void)dt_ms;
}

} // namespace modesp
