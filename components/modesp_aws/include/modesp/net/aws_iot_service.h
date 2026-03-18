/**
 * @file aws_iot_service.h
 * @brief AWS IoT Core cloud service — mTLS via ESP-IDF MQTT client
 *
 * Compile-time alternative to MqttService (Kconfig MODESP_CLOUD_AWS).
 * Uses ESP-IDF esp_mqtt for mTLS connection to AWS IoT Core.
 *
 * Certificates stored in NVS namespace "awscert" (PEM blobs).
 * AWS Root CA (AmazonRootCA1) embedded in firmware.
 *
 * Default: disabled until endpoint + certificates configured via /api/cloud.
 */

#pragma once

#include "modesp/base_module.h"
#include "esp_http_server.h"
#include "mqtt_client.h"

namespace modesp {

class SharedState;

class AwsIotService : public BaseModule {
public:
    AwsIotService() : BaseModule("cloud", ModulePriority::HIGH) {}

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // Dependency injection (сумісний інтерфейс з MqttService)
    void set_state(SharedState* s) { state_ = s; }
    void set_http_server(httpd_handle_t server);

    // Public API
    bool is_connected() const { return connected_; }
    bool is_enabled() const { return enabled_; }

private:
    // ESP-MQTT client
    esp_mqtt_client_handle_t client_ = nullptr;
    SharedState* state_ = nullptr;
    httpd_handle_t server_ = nullptr;
    bool http_registered_ = false;

    // NVS config (namespace "awscert")
    char endpoint_[128] = {};     // xxxxx.iot.region.amazonaws.com
    char thing_name_[64] = {};    // AWS Thing name
    char device_id_[8] = {};      // MAC-based (e.g., "A4CF12")
    bool enabled_ = false;
    bool connected_ = false;
    bool cert_loaded_ = false;
    bool reconnect_requested_ = false;

    // Сертифікати (статичні буфери в .bss, не heap)
    static constexpr size_t CERT_BUF_SIZE = 2048;
    static char s_device_cert_pem_[CERT_BUF_SIZE];
    static char s_device_key_pem_[CERT_BUF_SIZE];

    // Timers
    uint32_t publish_timer_ = 0;
    static constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;

    uint32_t heartbeat_timer_ms_ = 0;
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;

    // Reconnect з exponential backoff
    uint32_t reconnect_timer_ms_ = 0;
    uint32_t reconnect_delay_ms_ = 5000;
    static constexpr uint32_t MAX_RECONNECT_MS = 300000; // 5 min

    // Delta-publish cache (аналогічно MqttService)
    static constexpr size_t MAX_PUBLISH_KEYS = 56;
    char last_payloads_[MAX_PUBLISH_KEYS][16] = {};
    uint32_t last_version_ = 0;

    // Internal methods
    void load_config();
    bool load_certificates();
    bool start_client();
    void stop_client();
    void publish_state();
    void publish_heartbeat();
    void publish_shadow_reported();
    void handle_shadow_delta(const char* data, int data_len);
    void handle_incoming(const char* topic, int topic_len,
                         const char* data, int data_len);
    void register_http_handlers();

    // Topic prefix: "modesp/{device_id}"
    char topic_prefix_[24] = {};

    // Shadow
    uint32_t shadow_timer_ms_ = 0;
    static constexpr uint32_t SHADOW_INTERVAL_MS = 5000;  // batch update кожні 5с
    bool shadow_dirty_ = false;       // є зміни для shadow reported
    uint32_t shadow_version_ = 0;     // останній відомий version SharedState для shadow

    // IoT Jobs (OTA)
    void handle_job_notify(const char* data, int data_len);
    void update_job_status(const char* job_id, const char* status, const char* detail = nullptr);
    char current_job_id_[64] = {};    // ID поточного Job

    // Static callbacks
    static void mqtt_event_handler(void* args, esp_event_base_t base,
                                    int32_t event_id, void* event_data);

    // Форматування значення для MQTT payload
    static int format_value(const StateValue& val, char* buf, size_t buf_size);

    // HTTP handlers
    static esp_err_t handle_get_cloud(httpd_req_t* req);
    static esp_err_t handle_post_cloud(httpd_req_t* req);
    static void set_cors_headers(httpd_req_t* req);
};

} // namespace modesp
