/**
 * @file aws_iot_service.cpp
 * @brief AWS IoT Core cloud service — mTLS via ESP-IDF MQTT client
 *
 * Phase 5: mTLS connection + delta-publish telemetry + subscribe commands.
 * Phase 6: Device Shadow (desired/reported) — TODO.
 * Phase 7: IoT Jobs (OTA) — TODO.
 * Phase 8: Fleet Provisioning by Claim — TODO.
 */

#include "modesp/net/aws_iot_service.h"
#include "modesp/shared_state.h"
#include "modesp/services/nvs_helper.h"
#include "aws_root_ca.h"

#include "mqtt_topics.h"
#include "state_meta.h"

#include "modesp/services/ota_handler.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#define JSMN_STATIC
#include "jsmn.h"

#include <cstring>
#include <cstdio>

static const char* TAG = "AwsIoT";

namespace modesp {

// Статичні буфери для сертифікатів (.bss, не heap)
char AwsIotService::s_device_cert_pem_[CERT_BUF_SIZE] = {};
char AwsIotService::s_device_key_pem_[CERT_BUF_SIZE] = {};

// ═══════════════════════════════════════════════════════════════
// BaseModule interface
// ═══════════════════════════════════════════════════════════════

bool AwsIotService::on_init() {
    // Генеруємо device_id з MAC (аналогічно MqttService)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(device_id_, sizeof(device_id_), "%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Device ID: %s", device_id_);

    // Завантажуємо конфігурацію з NVS
    load_config();

    // Завантажуємо сертифікати
    cert_loaded_ = load_certificates();

    // Формуємо topic prefix
    snprintf(topic_prefix_, sizeof(topic_prefix_), "modesp/%s", device_id_);

    if (enabled_ && endpoint_[0] != '\0' && cert_loaded_) {
        ESP_LOGI(TAG, "Endpoint: %s, Thing: %s", endpoint_, thing_name_);
        ESP_LOGI(TAG, "Will connect after WiFi is ready");
        // start_client() відкладений до on_update() — WiFi ще не готовий при on_init()
    } else {
        if (!enabled_) {
            ESP_LOGI(TAG, "AWS IoT Core disabled");
        } else if (endpoint_[0] == '\0') {
            ESP_LOGW(TAG, "No endpoint — set via /api/cloud");
        } else if (!cert_loaded_) {
            ESP_LOGW(TAG, "No certificates — upload via /api/cloud");
        }
    }

    return true;
}

void AwsIotService::on_update(uint32_t dt_ms) {
    if (!enabled_ || !cert_loaded_ || endpoint_[0] == '\0') return;

    // Deferred start: чекаємо WiFi перед першим підключенням
    if (!client_) {
        // Перевіряємо чи WiFi з'єднаний через SharedState
        if (state_) {
            auto wifi_ip = state_->get("wifi.ip");
            if (!wifi_ip.has_value()) return;  // WiFi ще не готовий
            // Є IP — можемо підключатись
            if (etl::holds_alternative<StringValue>(wifi_ip.value())) {
                const auto& ip = etl::get<StringValue>(wifi_ip.value());
                if (ip.empty() || ip == "0.0.0.0") return;
            }
        }
        ESP_LOGI(TAG, "WiFi ready — starting MQTT client");
        start_client();
        return;
    }

    // Reconnect logic (якщо ще не підключені)
    if (!connected_) {
        reconnect_timer_ms_ += dt_ms;
        if (reconnect_timer_ms_ >= reconnect_delay_ms_) {
            reconnect_timer_ms_ = 0;
            ESP_LOGI(TAG, "Reconnecting (backoff %lu ms)...",
                     (unsigned long)reconnect_delay_ms_);
            esp_mqtt_client_reconnect(client_);
            reconnect_delay_ms_ = (reconnect_delay_ms_ * 2 > MAX_RECONNECT_MS)
                                  ? MAX_RECONNECT_MS : reconnect_delay_ms_ * 2;
        }
        return;
    }

    if (!enabled_ || !connected_ || !state_) return;

    // Delta-publish telemetry
    publish_timer_ += dt_ms;
    if (publish_timer_ >= PUBLISH_INTERVAL_MS) {
        publish_timer_ = 0;
        if (state_->version() != last_version_) {
            publish_state();
        }
    }

    // Shadow reported (batch update кожні 5с, тільки при змінах)
    shadow_timer_ms_ += dt_ms;
    if (shadow_timer_ms_ >= SHADOW_INTERVAL_MS) {
        shadow_timer_ms_ = 0;
        if (shadow_dirty_ || state_->version() != shadow_version_) {
            publish_shadow_reported();
            shadow_version_ = state_->version();
            shadow_dirty_ = false;
        }
    }

    // Heartbeat
    heartbeat_timer_ms_ += dt_ms;
    if (heartbeat_timer_ms_ >= HEARTBEAT_INTERVAL_MS) {
        heartbeat_timer_ms_ = 0;
        publish_heartbeat();
    }
}

void AwsIotService::on_stop() {
    stop_client();
    ESP_LOGI(TAG, "AWS IoT Core service stopped");
}

// ═══════════════════════════════════════════════════════════════
// MQTT Client (ESP-IDF esp_mqtt з mTLS)
// ═══════════════════════════════════════════════════════════════

bool AwsIotService::start_client() {
    if (client_) {
        stop_client();
    }

    // URI: mqtts://{endpoint}:8883
    char uri[192];
    snprintf(uri, sizeof(uri), "mqtts://%s:8883", endpoint_);

    // LWT topic: modesp/{device_id}/status
    char lwt_topic[48];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", topic_prefix_);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri;

    // mTLS: Root CA + client cert + client key
    cfg.broker.verification.certificate = AWS_ROOT_CA_PEM;
    cfg.credentials.authentication.certificate = s_device_cert_pem_;
    cfg.credentials.authentication.key = s_device_key_pem_;

    // Client ID = thing name
    cfg.credentials.client_id = thing_name_;

    // LWT
    cfg.session.last_will.topic = lwt_topic;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.msg_len = 7;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;

    // Buffer sizes
    cfg.buffer.size = 2048;
    cfg.buffer.out_size = 2048;

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return false;
    }

    esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY,
                                    mqtt_event_handler, this);

    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "Connecting to %s...", uri);
    return true;
}

void AwsIotService::stop_client() {
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    connected_ = false;
}

// ═══════════════════════════════════════════════════════════════
// MQTT Event Handler
// ═══════════════════════════════════════════════════════════════

void AwsIotService::mqtt_event_handler(void* args, esp_event_base_t base,
                                        int32_t event_id, void* event_data) {
    auto* self = static_cast<AwsIotService*>(args);
    auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "Connected to AWS IoT Core");
            self->connected_ = true;
            self->reconnect_delay_ms_ = 5000;  // Reset backoff

            // Публікуємо online статус
            char topic[48];
            snprintf(topic, sizeof(topic), "%s/status", self->topic_prefix_);
            esp_mqtt_client_publish(self->client_, topic, "online", 6, 1, 1);

            // Підписуємось на команди: modesp/{device_id}/cmd/+
            char sub_topic[64];
            snprintf(sub_topic, sizeof(sub_topic), "%s/cmd/+", self->topic_prefix_);
            esp_mqtt_client_subscribe(self->client_, sub_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s", sub_topic);

            // Підписуємось на Shadow delta
            char shadow_topic[128];
            snprintf(shadow_topic, sizeof(shadow_topic),
                     "$aws/things/%s/shadow/update/delta", self->thing_name_);
            esp_mqtt_client_subscribe(self->client_, shadow_topic, 1);

            snprintf(shadow_topic, sizeof(shadow_topic),
                     "$aws/things/%s/shadow/update/accepted", self->thing_name_);
            esp_mqtt_client_subscribe(self->client_, shadow_topic, 1);

            snprintf(shadow_topic, sizeof(shadow_topic),
                     "$aws/things/%s/shadow/update/rejected", self->thing_name_);
            esp_mqtt_client_subscribe(self->client_, shadow_topic, 1);

            ESP_LOGI(TAG, "Subscribed to Shadow topics");

            // Підписуємось на IoT Jobs
            snprintf(shadow_topic, sizeof(shadow_topic),
                     "$aws/things/%s/jobs/notify-next", self->thing_name_);
            esp_mqtt_client_subscribe(self->client_, shadow_topic, 1);
            ESP_LOGI(TAG, "Subscribed to Jobs topics");

            // Clear delta-publish cache — force full publish
            memset(self->last_payloads_, 0, sizeof(self->last_payloads_));
            self->last_version_ = 0;
            self->shadow_dirty_ = true;  // Force initial shadow reported
            self->shadow_version_ = 0;
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from AWS IoT Core");
            self->connected_ = false;
            break;

        case MQTT_EVENT_DATA:
            if (event->topic && event->topic_len > 0) {
                self->handle_incoming(event->topic, event->topic_len,
                                      event->data, event->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Transport error: esp_tls=%d, tls_stack=%d",
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_tls_stack_err);
            }
            break;

        default:
            break;
    }
}

// ═══════════════════════════════════════════════════════════════
// Publish (delta-publish аналогічно MqttService)
// ═══════════════════════════════════════════════════════════════

int AwsIotService::format_value(const StateValue& val, char* buf, size_t buf_size) {
    if (etl::holds_alternative<float>(val)) {
        return snprintf(buf, buf_size, "%.2f",
                        static_cast<double>(etl::get<float>(val)));
    } else if (etl::holds_alternative<int32_t>(val)) {
        return snprintf(buf, buf_size, "%ld",
                        (long)etl::get<int32_t>(val));
    } else if (etl::holds_alternative<bool>(val)) {
        return snprintf(buf, buf_size, "%s",
                        etl::get<bool>(val) ? "true" : "false");
    } else if (etl::holds_alternative<StringValue>(val)) {
        return snprintf(buf, buf_size, "%s",
                        etl::get<StringValue>(val).c_str());
    }
    return 0;
}

void AwsIotService::publish_state() {
    if (!client_ || !state_) return;

    for (size_t i = 0; i < gen::MQTT_PUBLISH_COUNT && i < MAX_PUBLISH_KEYS; i++) {
        auto val = state_->get(gen::MQTT_PUBLISH[i]);
        if (!val.has_value()) continue;

        char payload[32];
        int len = format_value(val.value(), payload, sizeof(payload));
        if (len <= 0) continue;

        // Delta-publish: порівнюємо з кешем
        if (strncmp(payload, last_payloads_[i], sizeof(last_payloads_[i])) == 0) {
            continue;
        }

        char topic[128];
        snprintf(topic, sizeof(topic), "%s/state/%s",
                 topic_prefix_, gen::MQTT_PUBLISH[i]);

        esp_mqtt_client_publish(client_, topic, payload, len, 0, 0);

        strncpy(last_payloads_[i], payload, sizeof(last_payloads_[i]) - 1);
        last_payloads_[i][sizeof(last_payloads_[i]) - 1] = '\0';
    }

    last_version_ = state_->version();
}

void AwsIotService::publish_heartbeat() {
    if (!connected_ || !client_) return;

    char topic[48];
    snprintf(topic, sizeof(topic), "%s/heartbeat", topic_prefix_);

    int rssi = 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    const esp_app_desc_t* desc = esp_app_get_description();
    char payload[192];
    int len = snprintf(payload, sizeof(payload),
        "{\"proto\":1,\"fw\":\"%s\",\"up\":%lu,\"heap\":%lu,\"rssi\":%d,\"thing\":\"%s\"}",
        desc->version,
        (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000),
        (unsigned long)esp_get_free_heap_size(),
        rssi,
        thing_name_);

    esp_mqtt_client_publish(client_, topic, payload, len, 0, 0);
}

// ═══════════════════════════════════════════════════════════════
// IoT Jobs (OTA)
// ═══════════════════════════════════════════════════════════════

void AwsIotService::handle_job_notify(const char* data, int data_len) {
    // Job notification JSON:
    // {"execution":{"jobId":"xxx","status":"QUEUED",
    //   "jobDocument":{"url":"https://...","version":"1.0.2","checksum":"sha256:..."}}}

    if (data_len < 10) {
        ESP_LOGD(TAG, "Jobs: no pending jobs");
        return;
    }

    jsmn_parser parser;
    jsmntok_t tokens[48];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, data, data_len, tokens, 48);
    if (r < 2) return;

    ESP_LOGI(TAG, "Job notification received (%d bytes)", data_len);

    // Шукаємо jobId та jobDocument
    char job_id[64] = {};
    char url[256] = {};
    char version[32] = {};
    char checksum[80] = {};

    for (int i = 1; i < r - 1; i++) {
        if (tokens[i].type != JSMN_STRING) continue;

        int klen = tokens[i].end - tokens[i].start;
        const char* k = data + tokens[i].start;
        int vlen = tokens[i + 1].end - tokens[i + 1].start;
        const char* v = data + tokens[i + 1].start;

        if (klen == 5 && strncmp(k, "jobId", 5) == 0 && tokens[i + 1].type == JSMN_STRING) {
            int l = (vlen < 63) ? vlen : 63;
            strncpy(job_id, v, l); job_id[l] = '\0';
        } else if (klen == 3 && strncmp(k, "url", 3) == 0 && tokens[i + 1].type == JSMN_STRING) {
            int l = (vlen < 255) ? vlen : 255;
            strncpy(url, v, l); url[l] = '\0';
        } else if (klen == 7 && strncmp(k, "version", 7) == 0 && tokens[i + 1].type == JSMN_STRING) {
            int l = (vlen < 31) ? vlen : 31;
            strncpy(version, v, l); version[l] = '\0';
        } else if (klen == 8 && strncmp(k, "checksum", 8) == 0 && tokens[i + 1].type == JSMN_STRING) {
            int l = (vlen < 79) ? vlen : 79;
            strncpy(checksum, v, l); checksum[l] = '\0';
        }
    }

    if (job_id[0] == '\0' || url[0] == '\0') {
        ESP_LOGW(TAG, "Job missing jobId or url");
        return;
    }

    ESP_LOGI(TAG, "OTA Job: id=%s, version=%s", job_id, version);
    ESP_LOGI(TAG, "OTA URL: %s", url);

    // Зберігаємо job ID для статус-апдейтів
    strncpy(current_job_id_, job_id, sizeof(current_job_id_) - 1);

    // Повідомляємо AWS: IN_PROGRESS
    update_job_status(job_id, "IN_PROGRESS", "Starting download");

    // Делегуємо до існуючого ota_handler
    ota_handler::OtaParams params = {};
    strncpy(params.url, url, sizeof(params.url) - 1);
    strncpy(params.version, version, sizeof(params.version) - 1);
    strncpy(params.checksum, checksum, sizeof(params.checksum) - 1);

    if (ota_handler::start_ota(params, state_)) {
        ESP_LOGI(TAG, "OTA task started for job %s", job_id);
    } else {
        ESP_LOGW(TAG, "OTA already in progress — rejecting job");
        update_job_status(job_id, "FAILED", "OTA already in progress");
    }
}

void AwsIotService::update_job_status(const char* job_id, const char* status, const char* detail) {
    if (!connected_ || !client_) return;

    char topic[128];
    snprintf(topic, sizeof(topic),
             "$aws/things/%s/jobs/%s/update", thing_name_, job_id);

    char payload[256];
    int len;
    if (detail) {
        len = snprintf(payload, sizeof(payload),
            "{\"status\":\"%s\",\"statusDetails\":{\"message\":\"%s\"}}", status, detail);
    } else {
        len = snprintf(payload, sizeof(payload),
            "{\"status\":\"%s\"}", status);
    }

    esp_mqtt_client_publish(client_, topic, payload, len, 1, 0);
    ESP_LOGI(TAG, "Job %s status: %s", job_id, status);
}

// ═══════════════════════════════════════════════════════════════
// Device Shadow
// ═══════════════════════════════════════════════════════════════

void AwsIotService::publish_shadow_reported() {
    if (!connected_ || !client_ || !state_) return;

    // Формуємо JSON: {"state":{"reported":{"key":value,...}}}
    // Тільки writable params (MQTT_SUBSCRIBE keys) — не телеметрія
    static char shadow_buf[2048];
    int pos = 0;
    pos += snprintf(shadow_buf + pos, sizeof(shadow_buf) - pos,
                    "{\"state\":{\"reported\":{");

    bool first = true;
    for (size_t i = 0; i < gen::MQTT_SUBSCRIBE_COUNT; i++) {
        auto val = state_->get(gen::MQTT_SUBSCRIBE[i]);
        if (!val.has_value()) continue;

        char payload[32];
        int len = format_value(val.value(), payload, sizeof(payload));
        if (len <= 0) continue;

        if (!first) {
            pos += snprintf(shadow_buf + pos, sizeof(shadow_buf) - pos, ",");
        }
        first = false;

        // Визначаємо тип для правильного JSON
        if (etl::holds_alternative<float>(val.value()) ||
            etl::holds_alternative<int32_t>(val.value())) {
            pos += snprintf(shadow_buf + pos, sizeof(shadow_buf) - pos,
                            "\"%s\":%s", gen::MQTT_SUBSCRIBE[i], payload);
        } else if (etl::holds_alternative<bool>(val.value())) {
            pos += snprintf(shadow_buf + pos, sizeof(shadow_buf) - pos,
                            "\"%s\":%s", gen::MQTT_SUBSCRIBE[i], payload);
        } else {
            pos += snprintf(shadow_buf + pos, sizeof(shadow_buf) - pos,
                            "\"%s\":\"%s\"", gen::MQTT_SUBSCRIBE[i], payload);
        }

        if (pos >= (int)sizeof(shadow_buf) - 32) break;  // Захист від overflow
    }

    pos += snprintf(shadow_buf + pos, sizeof(shadow_buf) - pos, "}}}");

    char topic[96];
    snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update", thing_name_);
    esp_mqtt_client_publish(client_, topic, shadow_buf, pos, 1, 0);

    ESP_LOGI(TAG, "Shadow reported published (%d bytes)", pos);
}

void AwsIotService::handle_shadow_delta(const char* data, int data_len) {
    // Delta JSON: {"state":{"key":value,...},"version":N}
    // Парсимо з jsmn і застосовуємо через STATE_META валідацію

    jsmn_parser parser;
    jsmntok_t tokens[64];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, data, data_len, tokens, 64);
    if (r < 2 || tokens[0].type != JSMN_OBJECT) return;

    ESP_LOGI(TAG, "Shadow delta received (%d bytes)", data_len);

    // Шукаємо "state" об'єкт
    int state_idx = -1;
    for (int i = 1; i < r - 1; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;
        int klen = tokens[i].end - tokens[i].start;
        const char* k = data + tokens[i].start;
        if (klen == 5 && strncmp(k, "state", 5) == 0 &&
            tokens[i + 1].type == JSMN_OBJECT) {
            state_idx = i + 1;
            break;
        }
    }

    if (state_idx < 0) return;

    // Ітеруємо key/value всередині state
    int state_size = tokens[state_idx].size;
    int idx = state_idx + 1;
    int applied = 0;

    for (int s = 0; s < state_size && idx < r - 1; s++) {
        if (tokens[idx].type != JSMN_STRING) { idx += 2; continue; }

        int klen = tokens[idx].end - tokens[idx].start;
        const char* key_ptr = data + tokens[idx].start;
        int vlen = tokens[idx + 1].end - tokens[idx + 1].start;
        const char* val_ptr = data + tokens[idx + 1].start;

        char key[32], val_buf[32];
        int kl = (klen < 31) ? klen : 31;
        int vl = (vlen < 31) ? vlen : 31;
        strncpy(key, key_ptr, kl); key[kl] = '\0';
        strncpy(val_buf, val_ptr, vl); val_buf[vl] = '\0';

        // Валідація через STATE_META
        const gen::StateMeta* meta = nullptr;
        for (size_t m = 0; m < gen::STATE_META_COUNT; m++) {
            if (strcmp(gen::STATE_META[m].key, key) == 0) {
                meta = &gen::STATE_META[m];
                break;
            }
        }

        if (meta && meta->writable) {
            if (strcmp(meta->type, "float") == 0) {
                float fval = strtof(val_buf, nullptr);
                if (fval < meta->min_val) fval = meta->min_val;
                if (fval > meta->max_val) fval = meta->max_val;
                state_->set(key, fval);
                applied++;
            } else if (strcmp(meta->type, "int") == 0) {
                int32_t ival = strtol(val_buf, nullptr, 10);
                if (ival < (int32_t)meta->min_val) ival = (int32_t)meta->min_val;
                if (ival > (int32_t)meta->max_val) ival = (int32_t)meta->max_val;
                state_->set(key, ival);
                applied++;
            } else if (strcmp(meta->type, "bool") == 0) {
                bool bval = (val_buf[0] == 't' || val_buf[0] == '1');
                state_->set(key, bval);
                applied++;
            }
        }

        idx += 2;
    }

    if (applied > 0) {
        ESP_LOGI(TAG, "Shadow delta: applied %d settings", applied);
        shadow_dirty_ = true;  // Оновити reported щоб очистити delta
    }
}

// ═══════════════════════════════════════════════════════════════
// Subscribe — incoming commands
// ═══════════════════════════════════════════════════════════════

void AwsIotService::handle_incoming(const char* topic, int topic_len,
                                     const char* data, int data_len) {
    // IoT Jobs: $aws/things/{thing}/jobs/notify-next
    if (topic_len > 10 && strstr(topic, "/jobs/notify-next") != nullptr) {
        handle_job_notify(data, data_len);
        return;
    }

    // Shadow delta: $aws/things/{thing}/shadow/update/delta
    if (topic_len > 10 && strstr(topic, "/shadow/update/delta") != nullptr) {
        handle_shadow_delta(data, data_len);
        return;
    }

    // Shadow accepted/rejected — просто логуємо
    if (topic_len > 10 && strstr(topic, "/shadow/update/accepted") != nullptr) {
        ESP_LOGD(TAG, "Shadow update accepted");
        return;
    }
    if (topic_len > 10 && strstr(topic, "/shadow/update/rejected") != nullptr) {
        ESP_LOGW(TAG, "Shadow update REJECTED: %.*s", data_len, data);
        return;
    }

    // Topic format: modesp/{device_id}/cmd/{key}
    // Шукаємо "/cmd/" в топіку
    const char* cmd = nullptr;
    int prefix_len = strlen(topic_prefix_);

    if (topic_len > prefix_len + 5 &&
        strncmp(topic, topic_prefix_, prefix_len) == 0 &&
        strncmp(topic + prefix_len, "/cmd/", 5) == 0) {
        cmd = topic + prefix_len + 5;
    }

    if (!cmd) return;

    // Формуємо state key
    int key_len = topic_len - (cmd - topic);
    if (key_len <= 0 || key_len > 31) return;

    char key[32];
    strncpy(key, cmd, key_len);
    key[key_len] = '\0';

    // Формуємо значення
    char val_buf[32];
    int vlen = (data_len < (int)sizeof(val_buf) - 1) ? data_len : (int)sizeof(val_buf) - 1;
    strncpy(val_buf, data, vlen);
    val_buf[vlen] = '\0';

    ESP_LOGI(TAG, "CMD: %s = %s", key, val_buf);

    // Валідація через STATE_META (аналогічно MqttService)
    if (!state_) return;

    const gen::StateMeta* meta = nullptr;
    for (size_t i = 0; i < gen::STATE_META_COUNT; i++) {
        if (strcmp(gen::STATE_META[i].key, key) == 0) {
            meta = &gen::STATE_META[i];
            break;
        }
    }

    if (!meta || !meta->writable) {
        ESP_LOGW(TAG, "CMD rejected: key '%s' not writable", key);
        return;
    }

    // Парсимо і записуємо відповідний тип
    if (strcmp(meta->type, "float") == 0) {
        float fval = strtof(val_buf, nullptr);
        if (fval < meta->min_val) fval = meta->min_val;
        if (fval > meta->max_val) fval = meta->max_val;
        state_->set(key, fval);
    } else if (strcmp(meta->type, "int") == 0) {
        int32_t ival = strtol(val_buf, nullptr, 10);
        if (ival < (int32_t)meta->min_val) ival = (int32_t)meta->min_val;
        if (ival > (int32_t)meta->max_val) ival = (int32_t)meta->max_val;
        state_->set(key, ival);
    } else if (strcmp(meta->type, "bool") == 0) {
        bool bval = (val_buf[0] == 't' || val_buf[0] == '1');
        state_->set(key, bval);
    }
}

// ═══════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════

void AwsIotService::load_config() {
    nvs_helper::read_str("awscert", "endpoint", endpoint_, sizeof(endpoint_));
    nvs_helper::read_str("awscert", "thing", thing_name_, sizeof(thing_name_));
    nvs_helper::read_bool("awscert", "enabled", enabled_);

    if (thing_name_[0] == '\0') {
        snprintf(thing_name_, sizeof(thing_name_), "modesp-%s", device_id_);
    }
}

bool AwsIotService::load_certificates() {
    size_t cert_len = 0, key_len = 0;

    bool cert_ok = nvs_helper::read_blob("awscert", "cert",
                                          s_device_cert_pem_, CERT_BUF_SIZE - 1, cert_len);
    bool key_ok = nvs_helper::read_blob("awscert", "key",
                                         s_device_key_pem_, CERT_BUF_SIZE - 1, key_len);

    if (cert_ok && key_ok && cert_len > 0 && key_len > 0) {
        s_device_cert_pem_[cert_len] = '\0';
        s_device_key_pem_[key_len] = '\0';
        ESP_LOGI(TAG, "Certificates loaded: cert=%u, key=%u bytes",
                 (unsigned)cert_len, (unsigned)key_len);
        return true;
    }

    ESP_LOGD(TAG, "No certificates in NVS");
    return false;
}

// ═══════════════════════════════════════════════════════════════
// HTTP handlers
// ═══════════════════════════════════════════════════════════════

void AwsIotService::set_http_server(httpd_handle_t server) {
    server_ = server;
    if (server_ && !http_registered_) {
        register_http_handlers();
        http_registered_ = true;
    }
}

void AwsIotService::set_cors_headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
}

esp_err_t AwsIotService::handle_get_cloud(httpd_req_t* req) {
    auto* self = static_cast<AwsIotService*>(req->user_ctx);
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    char buf[384];
    int len = snprintf(buf, sizeof(buf),
        "{\"provider\":\"aws\","
        "\"endpoint\":\"%s\","
        "\"thing_name\":\"%s\","
        "\"device_id\":\"%s\","
        "\"enabled\":%s,"
        "\"connected\":%s,"
        "\"cert_loaded\":%s}",
        self->endpoint_,
        self->thing_name_,
        self->device_id_,
        self->enabled_ ? "true" : "false",
        self->connected_ ? "true" : "false",
        self->cert_loaded_ ? "true" : "false");

    return httpd_resp_send(req, buf, len);
}

// Unescape JSON string in-place: \n → newline, \\ → backslash, \" → quote
static size_t json_unescape(char* dst, const char* src, size_t src_len) {
    size_t j = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '\\' && i + 1 < src_len) {
            char next = src[i + 1];
            if (next == 'n') { dst[j++] = '\n'; i++; }
            else if (next == '\\') { dst[j++] = '\\'; i++; }
            else if (next == '"') { dst[j++] = '"'; i++; }
            else if (next == 'r') { dst[j++] = '\r'; i++; }
            else { dst[j++] = src[i]; }
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
    return j;
}

esp_err_t AwsIotService::handle_post_cloud(httpd_req_t* req) {
    auto* self = static_cast<AwsIotService*>(req->user_ctx);
    set_cors_headers(req);

    static char body[4096];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    ESP_LOGI(TAG, "POST /api/cloud: %d bytes", received);

    jsmn_parser parser;
    jsmntok_t tokens[32];
    jsmn_init(&parser);
    int r = jsmn_parse(&parser, body, received, tokens, 32);
    if (r < 2 || tokens[0].type != JSMN_OBJECT) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    bool config_changed = false;
    bool cert_uploaded = false;

    for (int i = 1; i < r - 1; i += 2) {
        if (tokens[i].type != JSMN_STRING) continue;

        int klen = tokens[i].end - tokens[i].start;
        int vlen = tokens[i + 1].end - tokens[i + 1].start;
        const char* k = body + tokens[i].start;
        const char* v = body + tokens[i + 1].start;

        char saved = body[tokens[i + 1].end];
        body[tokens[i + 1].end] = '\0';

        if (klen == 8 && strncmp(k, "endpoint", 8) == 0) {
            strncpy(self->endpoint_, v, sizeof(self->endpoint_) - 1);
            self->endpoint_[sizeof(self->endpoint_) - 1] = '\0';
            nvs_helper::write_str("awscert", "endpoint", self->endpoint_);
            config_changed = true;
        } else if (klen == 10 && strncmp(k, "thing_name", 10) == 0) {
            strncpy(self->thing_name_, v, sizeof(self->thing_name_) - 1);
            self->thing_name_[sizeof(self->thing_name_) - 1] = '\0';
            nvs_helper::write_str("awscert", "thing", self->thing_name_);
            config_changed = true;
        } else if (klen == 7 && strncmp(k, "enabled", 7) == 0) {
            self->enabled_ = (v[0] == 't' || v[0] == '1');
            nvs_helper::write_bool("awscert", "enabled", self->enabled_);
            config_changed = true;
        } else if (klen == 4 && strncmp(k, "cert", 4) == 0 && vlen > 10) {
            // Unescape JSON \n → real newlines для PEM
            size_t real_len = json_unescape(s_device_cert_pem_, v, vlen);
            nvs_helper::write_blob("awscert", "cert", s_device_cert_pem_, real_len);
            cert_uploaded = true;
            ESP_LOGI(TAG, "Device certificate saved (%u bytes, was %d in JSON)",
                     (unsigned)real_len, vlen);
        } else if (klen == 3 && strncmp(k, "key", 3) == 0 && vlen > 10) {
            size_t real_len = json_unescape(s_device_key_pem_, v, vlen);
            nvs_helper::write_blob("awscert", "key", s_device_key_pem_, real_len);
            cert_uploaded = true;
            ESP_LOGI(TAG, "Private key saved (%u bytes, was %d in JSON)",
                     (unsigned)real_len, vlen);
        }

        body[tokens[i + 1].end] = saved;
    }

    if (cert_uploaded) {
        self->cert_loaded_ = (s_device_cert_pem_[0] != '\0' && s_device_key_pem_[0] != '\0');
    }

    if (config_changed) {
        ESP_LOGI(TAG, "Config saved: endpoint=%s, thing=%s, enabled=%d",
                 self->endpoint_, self->thing_name_, self->enabled_);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

void AwsIotService::register_http_handlers() {
    if (!server_) return;

    httpd_uri_t get_cloud = {};
    get_cloud.uri      = "/api/cloud";
    get_cloud.method   = HTTP_GET;
    get_cloud.handler  = handle_get_cloud;
    get_cloud.user_ctx = this;
    httpd_register_uri_handler(server_, &get_cloud);

    httpd_uri_t post_cloud = {};
    post_cloud.uri      = "/api/cloud";
    post_cloud.method   = HTTP_POST;
    post_cloud.handler  = handle_post_cloud;
    post_cloud.user_ctx = this;
    httpd_register_uri_handler(server_, &post_cloud);

    httpd_uri_t options = {};
    options.uri      = "/api/cloud";
    options.method   = HTTP_OPTIONS;
    options.handler  = [](httpd_req_t* req) -> esp_err_t {
        set_cors_headers(req);
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    };
    httpd_register_uri_handler(server_, &options);

    ESP_LOGI(TAG, "HTTP handlers registered (GET/POST /api/cloud)");
}

} // namespace modesp
