/**
 * @file ble_service.cpp
 * @brief modesp_ble — NimBLE host owner. Phase 0 advertiser + Phase 1 GATT + Phase 2 provisioning.
 *
 * The single owner of the NimBLE stack:
 *   - Phase 0: brings the host up, advertises a connectable peripheral.
 *   - Phase 1 (CONFIG_MODESP_BLE_GATT_SERVER): telemetry/control GATT service that
 *     mirrors SharedState (telemetry char read+notify, command char write).
 *   - Phase 2 (CONFIG_MODESP_BLE_PROVISIONING): write-only provisioning service that
 *     sets Wi-Fi credentials into NVS via wifi_service + triggers reconnect.
 *
 * Init order (verified, docs/ble/ipixel_nimble_spec.md §2.5):
 *   NVS (main step 0) -> Wi-Fi (phase 2, registered before this) -> NimBLE (here).
 */
#include "modesp/ble/ble_service.h"

#if defined(CONFIG_MODESP_BLE_ENABLE)

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

// Any GATT feature (telemetry/control or provisioning) needs the GATT scaffolding.
#if defined(CONFIG_MODESP_BLE_GATT_SERVER) || defined(CONFIG_MODESP_BLE_PROVISIONING)
#  define MODESP_BLE_GATT 1
#endif

#if defined(MODESP_BLE_GATT)
#include <stdlib.h>
#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "services/gatt/ble_svc_gatt.h"
#include "modesp/types.h"
#define JSMN_STATIC            // file-local jsmn impl (avoids clash with http_service)
#include "jsmn.h"
#endif

#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
#include "modesp/json_escape.h"
#include "mqtt_topics.h"   // generated: gen::MQTT_PUBLISH / MQTT_SUBSCRIBE
#include "state_meta.h"    // generated: gen::find_state_meta
#endif

#if defined(CONFIG_MODESP_BLE_PROVISIONING)
#include "modesp/net/wifi_service.h"
#endif

#if defined(CONFIG_MODESP_BLE_CENTRAL)
#include "esp_timer.h"
#include "esp_rom_crc.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "modesp/ble/ble_central.h"
#include "modesp/ble/ble_panel.h"
#include "panel_font_data.h"          // generated: PANEL_FONT 8x16 LSB-first + panel_font_index()
#endif

static const char* TAG = "modesp_ble";

namespace modesp {

// NimBLE host callbacks are C-style; bridge to the single instance.
static BleService* s_instance = nullptr;

#if defined(MODESP_BLE_GATT)
// ── Vendor 128-bit UUIDs (canonical 4d4f4445-5350-1000-8000-0000000000XX = "MODESP") ──
// BLE_UUID128_INIT takes bytes little-endian (reverse of canonical string).
// Telemetry/control service = ...a0/a1/a2 ; Provisioning service = ...b0/b1.
#define MODESP_BLE_UUID128(last) BLE_UUID128_INIT( \
    (last),0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x10,0x50,0x53,0x45,0x44,0x4f,0x4d)
#endif

#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
static const ble_uuid128_t SVC_UUID           = MODESP_BLE_UUID128(0xa0);
static const ble_uuid128_t CHR_TELEMETRY_UUID = MODESP_BLE_UUID128(0xa1);
static const ble_uuid128_t CHR_COMMAND_UUID   = MODESP_BLE_UUID128(0xa2);
static uint16_t s_telemetry_val_handle = 0;

// Telemetry (READ) + command (WRITE) share one access cb; the op is unambiguous
// because telemetry is read/notify-only and command is write-only.
static int gatt_ctrl_cb(uint16_t /*conn*/, uint16_t /*attr*/,
                        struct ble_gatt_access_ctxt* ctxt, void* /*arg*/) {
    if (!s_instance) return BLE_ATT_ERR_UNLIKELY;
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        char buf[512];
        int len = s_instance->serialize_telemetry(buf, sizeof(buf));
        if (len <= 0) return BLE_ATT_ERR_UNLIKELY;
        return os_mbuf_append(ctxt->om, buf, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        char buf[256];
        uint16_t outlen = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &outlen) != 0)
            return BLE_ATT_ERR_UNLIKELY;
        buf[outlen] = '\0';
        s_instance->apply_command(buf, outlen);
        return 0;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// NimBLE GATT tables use partial designated initializers (remaining fields are
// intentionally zero) — silence -Wmissing-field-initializers for these definitions.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const struct ble_gatt_chr_def s_ctrl_chrs[] = {
    {
        .uuid       = &CHR_TELEMETRY_UUID.u,
        .access_cb  = gatt_ctrl_cb,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_telemetry_val_handle,
    },
    {
        .uuid       = &CHR_COMMAND_UUID.u,
        .access_cb  = gatt_ctrl_cb,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    { 0 }  // terminator
};

static const struct ble_gatt_svc_def s_ctrl_svcs[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &SVC_UUID.u, .characteristics = s_ctrl_chrs },
    { 0 }
};
#pragma GCC diagnostic pop
#endif // CONFIG_MODESP_BLE_GATT_SERVER

#if defined(CONFIG_MODESP_BLE_PROVISIONING)
static const ble_uuid128_t PROV_SVC_UUID  = MODESP_BLE_UUID128(0xb0);
static const ble_uuid128_t CHR_PROV_UUID  = MODESP_BLE_UUID128(0xb1);

// Find a top-level JSON string field `name` and copy its value to `out` (NUL-terminated).
static bool json_get_str(const char* buf, const jsmntok_t* toks, int r,
                         const char* name, char* out, size_t out_sz) {
    int nlen = (int)strlen(name);
    for (int i = 1; i + 1 < r; i += 2) {
        if (toks[i].type != JSMN_STRING) continue;
        int klen = toks[i].end - toks[i].start;
        if (klen == nlen && strncmp(buf + toks[i].start, name, klen) == 0) {
            const jsmntok_t& v = toks[i + 1];
            int vlen = v.end - v.start;
            if (vlen < 0) return false;
            size_t n = ((size_t)vlen < out_sz - 1) ? (size_t)vlen : out_sz - 1;
            memcpy(out, buf + v.start, n);
            out[n] = '\0';
            return true;
        }
    }
    return false;
}

// Provisioning is WRITE-only — credentials are never exposed for read over BLE.
static int gatt_prov_cb(uint16_t /*conn*/, uint16_t /*attr*/,
                        struct ble_gatt_access_ctxt* ctxt, void* /*arg*/) {
    if (!s_instance) return BLE_ATT_ERR_UNLIKELY;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[192];
        uint16_t outlen = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &outlen) != 0)
            return BLE_ATT_ERR_UNLIKELY;
        buf[outlen] = '\0';
        s_instance->apply_provisioning(buf, outlen);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const struct ble_gatt_chr_def s_prov_chrs[] = {
    {
        .uuid      = &CHR_PROV_UUID.u,
        .access_cb = gatt_prov_cb,
        .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    { 0 }
};

static const struct ble_gatt_svc_def s_prov_svcs[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &PROV_SVC_UUID.u, .characteristics = s_prov_chrs },
    { 0 }
};
#pragma GCC diagnostic pop
#endif // CONFIG_MODESP_BLE_PROVISIONING

BleService::BleService()
    : BaseModule("ble", ModulePriority::HIGH)
{}

// ── NimBLE host task: runs the event loop until nimble_port_stop() ──
static void ble_host_task(void* /*param*/) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();              // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

static void on_stack_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE stack reset; reason=%d", reason);
}

static void on_stack_sync(void) {
    if (s_instance) s_instance->handle_sync();
}

void BleService::handle_sync() {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed; rc=%d", rc);
        return;
    }
    synced_ = true;
    start_advertising();
#if defined(CONFIG_MODESP_BLE_CENTRAL)
    start_scan();   // simultaneous peripheral (advertise) + observer (scan)
#endif
}

void BleService::start_advertising() {
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char* name = ble_svc_gap_device_name();
    fields.name = (uint8_t*)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   // connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;   // general discoverable

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, &BleService::gap_event, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising as '%s'", name);
    state_set("ble.advertising", true);
}

int BleService::gap_event(struct ble_gap_event* event, void* arg) {
    auto* self = static_cast<BleService*>(arg);
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect; status=%d", event->connect.status);
        if (event->connect.status != 0) {
            if (self) self->start_advertising();      // failed — resume advertising
        }
#if defined(MODESP_BLE_GATT)
        else if (self) self->on_gatt_connect(event->connect.conn_handle);
#endif
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
#if defined(MODESP_BLE_GATT)
        if (self) self->on_gatt_disconnect();
#endif
        if (self) self->start_advertising();          // resume advertising
        break;
#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (self) self->on_gatt_subscribe(event->subscribe.conn_handle,
                                          event->subscribe.attr_handle,
                                          event->subscribe.cur_notify);
        break;
#endif
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (self) self->start_advertising();
        break;
    default:
        break;
    }
    return 0;
}

#if defined(MODESP_BLE_GATT)

void BleService::gatt_server_init() {
    ble_svc_gatt_init();
    int rc;
#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    rc = ble_gatts_count_cfg(s_ctrl_svcs);
    if (rc) ESP_LOGE(TAG, "count_cfg(ctrl) rc=%d", rc);
#endif
#if defined(CONFIG_MODESP_BLE_PROVISIONING)
    rc = ble_gatts_count_cfg(s_prov_svcs);
    if (rc) ESP_LOGE(TAG, "count_cfg(prov) rc=%d", rc);
#endif
#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    rc = ble_gatts_add_svcs(s_ctrl_svcs);
    if (rc) ESP_LOGE(TAG, "add_svcs(ctrl) rc=%d", rc);
    else    ESP_LOGI(TAG, "GATT telemetry/control server registered");
#endif
#if defined(CONFIG_MODESP_BLE_PROVISIONING)
    rc = ble_gatts_add_svcs(s_prov_svcs);
    if (rc) ESP_LOGE(TAG, "add_svcs(prov) rc=%d", rc);
    else    ESP_LOGI(TAG, "GATT provisioning service registered (write-only)");
#endif
}

void BleService::on_gatt_connect(uint16_t conn_handle) {
    conn_handle_ = conn_handle;
    ESP_LOGI(TAG, "central connected (handle=%u)", conn_handle);
}

void BleService::on_gatt_disconnect() {
    conn_handle_ = 0xFFFF;
#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    telemetry_subscribed_ = false;
    last_telemetry_[0] = '\0';
#endif
}

#endif // MODESP_BLE_GATT

#if defined(CONFIG_MODESP_BLE_GATT_SERVER)

int BleService::serialize_telemetry(char* buf, size_t size) {
    if (size < 4) return 0;
    size_t pos = 0;
    bool first = true;
    buf[pos++] = '{';
    for (size_t i = 0; i < gen::MQTT_PUBLISH_COUNT; i++) {
        const char* key = gen::MQTT_PUBLISH[i];
        auto v = state_get(key);
        if (!v.has_value()) continue;
        if (size - pos < 96) break;                   // reserve margin for "}\0"
        const StateValue& val = v.value();
        const char* sep = first ? "" : ",";
        int n = 0;
        if (etl::holds_alternative<float>(val)) {
            n = snprintf(buf + pos, size - pos, "%s\"%s\":%.2f",
                         sep, key, static_cast<double>(etl::get<float>(val)));
        } else if (etl::holds_alternative<int32_t>(val)) {
            n = snprintf(buf + pos, size - pos, "%s\"%s\":%ld",
                         sep, key, static_cast<long>(etl::get<int32_t>(val)));
        } else if (etl::holds_alternative<bool>(val)) {
            n = snprintf(buf + pos, size - pos, "%s\"%s\":%s",
                         sep, key, etl::get<bool>(val) ? "true" : "false");
        } else if (etl::holds_alternative<StringValue>(val)) {
            char esc[48];
            modesp::json_escape(esc, sizeof(esc), etl::get<StringValue>(val).c_str());
            n = snprintf(buf + pos, size - pos, "%s\"%s\":\"%s\"", sep, key, esc);
        } else {
            continue;
        }
        if (n > 0) { pos += static_cast<size_t>(n); first = false; }
    }
    if (pos + 2 <= size) { buf[pos++] = '}'; buf[pos] = '\0'; }
    return static_cast<int>(pos);
}

void BleService::apply_command(const char* json, int len) {
    jsmn_parser p;
    jsmntok_t toks[24];
    jsmn_init(&p);
    int r = jsmn_parse(&p, json, len, toks, 24);
    if (r < 1 || toks[0].type != JSMN_OBJECT) {
        ESP_LOGW(TAG, "cmd: invalid JSON");
        return;
    }

    char tmp[256];
    int n = (len < static_cast<int>(sizeof(tmp)) - 1) ? len : static_cast<int>(sizeof(tmp)) - 1;
    memcpy(tmp, json, n);
    tmp[n] = '\0';

    int accepted = 0, rejected = 0;
    for (int i = 1; i + 1 < r; i += 2) {
        if (toks[i].type != JSMN_STRING) continue;
        const jsmntok_t& vt = toks[i + 1];
        if (vt.end >= n) break;
        const char* key = tmp + toks[i].start;
        tmp[toks[i].end] = '\0';
        tmp[vt.end] = '\0';
        const char* vstr = tmp + vt.start;

        const auto* meta = gen::find_state_meta(key);
        if (!meta || !meta->writable) {
            ESP_LOGW(TAG, "cmd: reject '%s' (unknown or read-only)", key);
            rejected++;
            continue;
        }

        if (vt.type == JSMN_PRIMITIVE) {
            if (vstr[0] == 't' || vstr[0] == 'f') {
                state_set(key, vstr[0] == 't');
                accepted++;
            } else if (strcmp(meta->type, "float") == 0) {
                float f = static_cast<float>(atof(vstr));
                if (f < meta->min_val) f = meta->min_val;
                if (f > meta->max_val) f = meta->max_val;
                state_set(key, f);
                accepted++;
            } else {
                int32_t iv = static_cast<int32_t>(atoi(vstr));
                if (static_cast<float>(iv) < meta->min_val) iv = static_cast<int32_t>(meta->min_val);
                if (static_cast<float>(iv) > meta->max_val) iv = static_cast<int32_t>(meta->max_val);
                state_set(key, iv);
                accepted++;
            }
        } else if (vt.type == JSMN_STRING) {
            state_set(key, vstr);
            accepted++;
        }
    }
    ESP_LOGI(TAG, "BLE cmd: %d accepted, %d rejected", accepted, rejected);
}

void BleService::on_gatt_subscribe(uint16_t conn_handle, uint16_t attr_handle, bool subscribed) {
    if (attr_handle == s_telemetry_val_handle) {
        telemetry_subscribed_ = subscribed;
        conn_handle_ = conn_handle;
        last_telemetry_[0] = '\0';                    // force first notify after (un)subscribe
        ESP_LOGI(TAG, "telemetry notifications %s", subscribed ? "ON" : "OFF");
    }
}

#endif // CONFIG_MODESP_BLE_GATT_SERVER

#if defined(CONFIG_MODESP_BLE_PROVISIONING)

void BleService::apply_provisioning(const char* json, int len) {
    if (!wifi_) {
        ESP_LOGW(TAG, "prov: wifi service not wired");
        return;
    }
    // SECURITY: accept provisioning only while NOT connected to Wi-Fi (AP/unconfigured).
    // This limits credential writes to the legitimate setup window. Hardening path:
    // NimBLE LE Secure Connections pairing/bonding or a proof-of-possession token.
    if (!wifi_->is_ap_mode()) {
        ESP_LOGW(TAG, "prov: rejected — device already on Wi-Fi (re-provision only in AP mode)");
        return;
    }

    jsmn_parser p;
    jsmntok_t toks[16];
    jsmn_init(&p);
    int r = jsmn_parse(&p, json, len, toks, 16);
    if (r < 1 || toks[0].type != JSMN_OBJECT) {
        ESP_LOGW(TAG, "prov: invalid JSON");
        return;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    if (!json_get_str(json, toks, r, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        ESP_LOGW(TAG, "prov: missing/empty 'ssid'");
        return;
    }
    json_get_str(json, toks, r, "pass", pass, sizeof(pass));  // empty allowed (open network)

    if (!wifi_->save_credentials(ssid, pass)) {
        ESP_LOGE(TAG, "prov: save_credentials failed");
        return;
    }
    wifi_->request_reconnect();
    // Never log the password — only its length.
    ESP_LOGI(TAG, "prov: credentials set for SSID '%s' (pass %d chars), reconnecting",
             ssid, (int)strlen(pass));
}

#endif // CONFIG_MODESP_BLE_PROVISIONING

#if defined(CONFIG_MODESP_BLE_CENTRAL)

// ── BLE central: passive-scan observer for broadcast sensors ──────────────────
// Decodes advertisement service-data (pvvx/ATC Xiaomi 0x181A, BTHome 0xFCD2) and
// logs readings (per-device throttled); publishes a discovered-count to SharedState.
// Per-device routing into named SharedState keys via bindings.json = Phase 3b.
// Decode formulas: docs/ble/sensors_observer_spec.md.

// ── BleCentral registry (bridge to BLE sensor drivers, hardware_type "ble") ──
BleCentral& BleCentral::instance() { static BleCentral s; return s; }

const BleReading* BleCentral::register_mac(const uint8_t mac6_display[6]) {
    uint8_t le[6];
    for (int i = 0; i < 6; i++) le[i] = mac6_display[5 - i];   // display → LE (addr.val)
    for (size_t i = 0; i < MAX_DEVICES; i++)
        if (slots_[i].used && memcmp(slots_[i].mac_le, le, 6) == 0) return &slots_[i].r;
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (!slots_[i].used) {
            memcpy(slots_[i].mac_le, le, 6);
            slots_[i].used = true;
            slots_[i].r = BleReading{};
            count_++;
            return &slots_[i].r;
        }
    }
    return nullptr;
}

void BleCentral::dispatch(const uint8_t mac6_le[6], int8_t rssi,
                          bool ht, float t, bool hh, float h, int bp, int mv) {
    for (size_t i = 0; i < MAX_DEVICES; i++) {
        if (!slots_[i].used || memcmp(slots_[i].mac_le, mac6_le, 6) != 0) continue;
        BleReading& r = slots_[i].r;
        if (ht)      { r.temp_c  = t; r.has_temp = true; }
        if (hh)      { r.hum_pct = h; r.has_hum  = true; }
        if (bp >= 0) r.batt_pct = bp;
        if (mv >= 0) r.batt_mv  = mv;
        r.rssi    = rssi;
        r.last_us = esp_timer_get_time();
        return;
    }
}

static constexpr size_t  MAX_SEEN = 8;
static constexpr int64_t LOG_THROTTLE_US = 5000000;  // 5 s per device

// Per-device cache. pvvx/ATC carry all fields in one frame, but pvvx-BTHome
// ALTERNATES frames (temp/hum vs voltage/flags), so we merge fields across frames
// and log the unified current reading. (docs/ble/sensors_observer_spec.md §3c)
struct SeenSensor {
    uint8_t mac[6];
    bool    used;
    int64_t last_us;
    float   temp;  bool has_temp;
    float   hum;   bool has_hum;
    int     batt_pct;          // -1 = unknown
    int     batt_mv;           // -1 = unknown
};
static SeenSensor s_seen[MAX_SEEN] = {};
static int s_sensor_count = 0;   // discovered-sensor count (host task writes, main reads)

// Find device in table (or add, with cleared cache). Returns index, -1 if full.
static int seen_index(const uint8_t* mac) {
    int free_idx = -1;
    for (size_t i = 0; i < MAX_SEEN; i++) {
        if (s_seen[i].used && memcmp(s_seen[i].mac, mac, 6) == 0) return (int)i;
        if (!s_seen[i].used && free_idx < 0) free_idx = (int)i;
    }
    if (free_idx < 0) return -1;
    SeenSensor& s = s_seen[free_idx];
    memcpy(s.mac, mac, 6);
    s.used = true; s.last_us = 0;
    s.has_temp = s.has_hum = false;
    s.batt_pct = -1; s.batt_mv = -1;
    s_sensor_count++;
    return free_idx;
}

// mac points at ble addr.val (little-endian); printed in display order (reversed).
#define MODESP_MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MODESP_MAC_ARG(m) (m)[5],(m)[4],(m)[3],(m)[2],(m)[1],(m)[0]

// Merge this frame's fields into the device cache, then log the unified reading
// (throttled per device). ht/hh = temp/hum present this frame; bp/mv < 0 = absent.
static void update_sensor(const uint8_t* mac, int8_t rssi, const char* fmt,
                          bool ht, float t, bool hh, float h, int bp, int mv) {
    // Feed registered BLE-sensor drivers every frame (unthrottled) — hardware_type "ble".
    BleCentral::instance().dispatch(mac, rssi, ht, t, hh, h, bp, mv);

    int idx = seen_index(mac);
    if (idx < 0) return;
    SeenSensor& s = s_seen[idx];
    if (ht)      { s.temp = t; s.has_temp = true; }
    if (hh)      { s.hum  = h; s.has_hum  = true; }
    if (bp >= 0) s.batt_pct = bp;
    if (mv >= 0) s.batt_mv  = mv;

    int64_t now = esp_timer_get_time();
    bool is_new = (s.last_us == 0);
    if (!is_new && (now - s.last_us) < LOG_THROTTLE_US) return;
    s.last_us = now;
    ESP_LOGI(TAG, "%s %-6s " MODESP_MAC_FMT "  T=%.2fC RH=%.2f%% batt=%d%%/%dmV rssi=%d",
             is_new ? "[NEW]" : "     ", fmt, MODESP_MAC_ARG(mac),
             s.has_temp ? s.temp : 0.0f, s.has_hum ? s.hum : 0.0f,
             s.batt_pct, s.batt_mv, rssi);
}

// Diagnostic: recognized service UUID but no known measurement — dump raw hex to
// confirm the actual on-air format against docs/ble/sensors_observer_spec.md.
static void log_raw(const uint8_t* mac, int8_t rssi, uint16_t uuid,
                    const uint8_t* sd, uint16_t len) {
    int idx = seen_index(mac);
    if (idx < 0) return;
    int64_t now = esp_timer_get_time();
    bool is_new = (s_seen[idx].last_us == 0);
    if (!is_new && (now - s_seen[idx].last_us) < LOG_THROTTLE_US) return;
    s_seen[idx].last_us = now;
    char hex[48]; size_t p = 0;
    for (uint16_t i = 0; i < len && p + 3 < sizeof(hex); i++)
        p += snprintf(hex + p, sizeof(hex) - p, "%02x", sd[i]);
    ESP_LOGI(TAG, "%s [raw]  " MODESP_MAC_FMT " uuid=%04x len=%u data=%s rssi=%d",
             is_new ? "[NEW]" : "     ", MODESP_MAC_ARG(mac), uuid, len, hex, rssi);
}

// ── start_observer_scan: shared passive-scan starter (sensors + panel name-match).
// Used by BleService::start_scan() and resumed by BlePanel after (dis)connect. ──
static void start_observer_scan() {
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) return;
    struct ble_gap_disc_params dp;
    memset(&dp, 0, sizeof(dp));
    dp.passive = 1; dp.filter_duplicates = 0; dp.itvl = 160; dp.window = 48;
    dp.filter_policy = 0; dp.limited = 0;
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &dp, &BleService::gap_scan_event, nullptr);
    if (rc == 0 || rc == BLE_HS_EALREADY)   // EALREADY: scan already running (coexists w/ panel link) — fine
        ESP_LOGI(TAG, "BLE observer scan running");
    else
        ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
}

// ── BlePanel: central-CONNECT link to one iPixel/LED_BLE panel (docs/ble/panel_protocol.md) ──
// The panel exposes its service+chars as FULL 128-bit base-derived UUIDs
// (0000faXX-0000-1000-8000-00805f9b34fb) and its firmware does NOT expand a 16-bit
// discovery request to match them (HW-confirmed: 16-bit disc → "service 0xFA00 not
// found"). Query with the 128-bit form — exactly what the working OSS (bleak/go-ipxl) use.
// BLE_UUID128_INIT takes the 16 bytes little-endian (LSB first); byte[12] carries the
// 16-bit value's low byte (0x00/0x02/0x03), byte[13]=0xfa.
#define PANEL_UUID128(lo) BLE_UUID128_INIT( \
    0xfb,0x34,0x9b,0x5f, 0x80,0x00, 0x00,0x80, 0x00,0x10, 0x00,0x00, (lo),0xfa,0x00,0x00)
[[maybe_unused]] static const ble_uuid128_t PANEL_SVC_UUID = PANEL_UUID128(0x00);  // 0000fa00- (doc; disc is char-wide now)
static const ble_uuid128_t PANEL_WR_UUID  = PANEL_UUID128(0x02);   // 0000fa02-... (write)
static const ble_uuid128_t PANEL_NT_UUID  = PANEL_UUID128(0x03);   // 0000fa03-... (notify)

BlePanel& BlePanel::instance() { static BlePanel s; return s; }

void BlePanel::set_target(const char* prefix) {
    if (!prefix) { name_len_ = 0; return; }
    size_t n = strlen(prefix);
    if (n >= sizeof(name_)) n = sizeof(name_) - 1;
    memcpy(name_, prefix, n); name_[n] = '\0'; name_len_ = (uint8_t)n;
    ESP_LOGI(TAG, "panel target name='%s'", name_);
}

bool BlePanel::name_matches(const uint8_t* adv_name, uint8_t adv_name_len) const {
    if (name_len_ == 0 || !adv_name || adv_name_len < name_len_) return false;
    return memcmp(adv_name, name_, name_len_) == 0;
}

// Serialized write-WITH-response. pypixelcolor writes every 244-byte chunk with
// response=True and relies on the BLE write-response for flow control (no sleeps) —
// the panel drops back-to-back no-response writes. We mirror that by BLOCKING here
// until the GATT write completes: one outstanding write at a time → no GATTC-proc-pool
// exhaustion, no dropped frames. MUST be called from the driver task, never the NimBLE
// host task (the wait below would deadlock against the callback that releases it).
static SemaphoreHandle_t s_panel_write_sem = nullptr;
static int panel_on_write_done(uint16_t /*conn*/, const struct ble_gatt_error* /*err*/,
                               struct ble_gatt_attr* /*attr*/, void* /*arg*/) {
    if (s_panel_write_sem) xSemaphoreGive(s_panel_write_sem);
    return 0;
}

bool BlePanel::write_cmd(const uint8_t* data, uint16_t len, bool with_response) {
    if (state_ != State::READY || conn_handle_ == 0xFFFF || write_handle_ == 0) return false;
    // fa02 advertises both WRITE (0x08) and WRITE_NO_RSP (0x04). No-response is the fast
    // fire-and-forget path (caller opts out of flow control); with-response blocks below.
    if (!(with_response && (write_props_ & 0x08 /*WRITE*/)))
        return ble_gattc_write_no_rsp_flat(conn_handle_, write_handle_, data, len) == 0;

    if (!s_panel_write_sem) {
        s_panel_write_sem = xSemaphoreCreateBinary();
        if (!s_panel_write_sem) return false;
    }
    xSemaphoreTake(s_panel_write_sem, 0);   // drain any stale completion
    if (ble_gattc_write_flat(conn_handle_, write_handle_, data, len, &panel_on_write_done, NULL) != 0)
        return false;
    return xSemaphoreTake(s_panel_write_sem, pdMS_TO_TICKS(3000)) == pdTRUE;  // flow control: wait for completion
}

void BlePanel::reset_link() {
    state_ = State::IDLE;
    conn_handle_ = 0xFFFF;
    write_handle_ = 0; notify_handle_ = 0;
    svc_start_ = 0; svc_end_ = 0;
    start_observer_scan();   // resume scanning (re-find panel + keep sensors)
}

void BlePanel::on_scan_hit(const void* addr) {
    if (state_ != State::IDLE) return;
    if (cooldown_until_us_ != 0 && esp_timer_get_time() < cooldown_until_us_) return;  // back off after a failed discovery
    state_ = State::CONNECTING;
    ble_gap_disc_cancel();   // must stop the observer scan before connecting
    uint8_t own_addr_type;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) { reset_link(); return; }
    int rc = ble_gap_connect(own_addr_type, static_cast<const ble_addr_t*>(addr),
                             30000, NULL, &BlePanel::gap_event, NULL);
    if (rc != 0) { ESP_LOGE(TAG, "panel ble_gap_connect rc=%d", rc); reset_link(); }
    else         ESP_LOGI(TAG, "panel connecting to '%s'...", name_);
}

int BlePanel::gap_event(struct ble_gap_event* event, void* /*arg*/) {
    BlePanel& p = instance();
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            p.conn_handle_ = event->connect.conn_handle;
            p.state_ = State::DISCOVERING;
            ESP_LOGI(TAG, "panel connected (handle=%u), enumerating all characteristics...", p.conn_handle_);
            ble_gattc_exchange_mtu(p.conn_handle_, NULL, NULL);
            // Discover ALL characteristics (1..0xffff) and match fa02/fa03 wherever they
            // live — robust to the panel's actual service layout (16- or 128-bit UUIDs).
            p.write_handle_ = 0; p.notify_handle_ = 0;
            ble_gattc_disc_all_chrs(p.conn_handle_, 1, 0xffff, &BlePanel::on_chr, NULL);
        } else {
            ESP_LOGW(TAG, "panel connect failed (status=%d)", event->connect.status);
            p.reset_link();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "panel disconnected (reason=%d)", event->disconnect.reason);
        p.reset_link();
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        return 0;   // fa03 ACKs — consumed in Increment 2 (image/text)
    default:
        return 0;
    }
}

// Match a characteristic UUID to the panel's write(fa02)/notify(fa03) role, handling
// both 16-bit and 128-bit base-derived forms. Returns 0xFA02 / 0xFA03 / 0.
static uint16_t panel_chr_match(const ble_uuid_t* u) {
    if (u->type == BLE_UUID_TYPE_16) {
        uint16_t v = ble_uuid_u16(u);
        if (v == 0xFA02 || v == 0xFA03) return v;
    } else if (u->type == BLE_UUID_TYPE_128) {
        if (ble_uuid_cmp(u, &PANEL_WR_UUID.u) == 0) return 0xFA02;
        if (ble_uuid_cmp(u, &PANEL_NT_UUID.u) == 0) return 0xFA03;
    }
    return 0;
}

int BlePanel::on_chr(uint16_t conn, const struct ble_gatt_error* err,
                     const struct ble_gatt_chr* chr, void* /*arg*/) {
    BlePanel& p = instance();
    if (err->status == 0 && chr) {
        char ub[BLE_UUID_STR_LEN];
        ble_uuid_to_str(&chr->uuid.u, ub);
        ESP_LOGI(TAG, "panel chr def=%u val=%u props=0x%02x uuid=%s",
                 chr->def_handle, chr->val_handle, chr->properties, ub);
        uint16_t m = panel_chr_match(&chr->uuid.u);
        if      (m == 0xFA02) { p.write_handle_ = chr->val_handle; p.write_props_ = chr->properties; }
        else if (m == 0xFA03) p.notify_handle_ = chr->val_handle;
    } else if (err->status == BLE_HS_EDONE) {
        if (p.write_handle_ == 0) {
            ESP_LOGE(TAG, "panel: fa02 write char not found among chrs (see uuids above) — back off 15s");
            p.cooldown_until_us_ = esp_timer_get_time() + 15000000;   // avoid hammering reconnect
            if (ble_gap_terminate(conn, 0x13) != 0) p.reset_link();
            return 0;
        }
        if (p.notify_handle_ != 0) {
            ble_gattc_disc_all_dscs(conn, p.notify_handle_, 0xffff, &BlePanel::on_dsc, NULL);
        } else {
            p.state_ = State::READY;   // write-only is enough for control
            ESP_LOGI(TAG, "panel READY (fa02 write=%u; no fa03 notify)", p.write_handle_);
            start_observer_scan();
        }
    } else {
        ESP_LOGW(TAG, "panel on_chr err=%d — terminating link", err->status);
        if (ble_gap_terminate(conn, 0x13) != 0) p.reset_link();
    }
    return 0;
}

int BlePanel::on_dsc(uint16_t conn, const struct ble_gatt_error* err,
                     uint16_t /*chr_val_handle*/, const struct ble_gatt_dsc* dsc, void* /*arg*/) {
    BlePanel& p = instance();
    if (err->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == 0x2902) {                  // CCCD
            static const uint8_t cccd_on[2] = {0x01, 0x00};         // enable notifications
            ble_gattc_write_flat(conn, dsc->handle, cccd_on, sizeof(cccd_on), NULL, NULL);
        }
    } else if (err->status == BLE_HS_EDONE) {
        p.state_ = State::READY;
        ESP_LOGI(TAG, "panel READY (fa02 write + fa03 notify)");
        start_observer_scan();   // resume sensor observation alongside the connection
    } else {
        ESP_LOGW(TAG, "panel on_dsc err=%d — terminating link", err->status);
        if (ble_gap_terminate(conn, 0x13) != 0) p.reset_link();
    }
    return 0;
}

void BlePanel::show_text(const char* s) {
    if (state_ != State::READY || s == nullptr) return;
    using namespace modesp::panel;

    int n = 0;
    while (s[n] && n < 16) n++;                     // up to 16 chars (8px each)
    const uint8_t R = 0xFF, G = 0xFF, B = 0xFF;     // white

    // payload: [num_chars][3 rsv][anim/speed/rainbow 3][fg RGB 3][bg_en][bg RGB 3] + glyph blocks
    uint8_t payload[400];
    size_t pl = 0;
    payload[pl++] = static_cast<uint8_t>(n);
    payload[pl++] = 0; payload[pl++] = 0; payload[pl++] = 0;       // reserved
    payload[pl++] = 0; payload[pl++] = 0x32; payload[pl++] = 0;    // anim=0(fixed) speed=0x32 rainbow=0
    payload[pl++] = R; payload[pl++] = G; payload[pl++] = B;       // fg RGB
    payload[pl++] = 0;                                             // bg_enable=0
    payload[pl++] = 0; payload[pl++] = 0; payload[pl++] = 0;       // bg RGB
    for (int i = 0; i < n; i++) {
        uint8_t idx = panel_font_index(static_cast<uint8_t>(s[i]));
        payload[pl++] = 0x00;                                     // glyph block type: char 16x8
        payload[pl++] = R; payload[pl++] = G; payload[pl++] = B;  // per-char color
        for (int row = 0; row < PANEL_FONT_H; row++)
            payload[pl++] = PANEL_FONT[idx * PANEL_FONT_H + row];
    }

    uint32_t crc = esp_rom_crc32_le(0, payload, pl);

    // frame: [total_len u16][00 01][has_next 00][payload_size u32][crc u32][00][slot 0x65] + payload
    uint8_t frame[420];
    size_t fl = 0;
    uint16_t total = static_cast<uint16_t>(pl + 15);
    frame[fl++] = total & 0xFF;          frame[fl++] = (total >> 8) & 0xFF;
    frame[fl++] = 0x00;                  frame[fl++] = 0x01;       // text type
    frame[fl++] = 0x00;                                           // has_next (single frame)
    frame[fl++] = pl & 0xFF;  frame[fl++] = (pl >> 8) & 0xFF;  frame[fl++] = (pl >> 16) & 0xFF;  frame[fl++] = (pl >> 24) & 0xFF;
    frame[fl++] = crc & 0xFF; frame[fl++] = (crc >> 8) & 0xFF; frame[fl++] = (crc >> 16) & 0xFF; frame[fl++] = (crc >> 24) & 0xFF;
    frame[fl++] = 0x00;                  frame[fl++] = 0x65;       // 0x00, slot
    memcpy(frame + fl, payload, pl);     fl += pl;

    // chunked (244 B) with-response, serialized (write_cmd blocks per chunk)
    for (size_t off = 0; off < fl; off += 244) {
        size_t clen = (fl - off < 244) ? (fl - off) : 244;
        write_cmd(frame + off, static_cast<uint16_t>(clen), /*with_response=*/true);
    }
    ESP_LOGI(TAG, "panel show_text '%s' (%d chars, %u-byte frame)", s, n, (unsigned)fl);
}

int BleService::gap_scan_event(struct ble_gap_event* event, void* /*arg*/) {
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    const struct ble_gap_disc_desc* d = &event->disc;

    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, d->data, d->length_data) != 0) return 0;

    // Panel (CONNECT device) advertises a NAME and no service-data: match the name
    // BEFORE the sensor service-data early-return below.
    if (BlePanel::instance().target_set() && !BlePanel::instance().is_connected() &&
        fields.name != NULL &&
        BlePanel::instance().name_matches(fields.name, fields.name_len)) {
        BlePanel::instance().on_scan_hit(&d->addr);
        return 0;
    }

    if (fields.svc_data_uuid16 == NULL || fields.svc_data_uuid16_len < 3) return 0;

    const uint8_t* sd   = fields.svc_data_uuid16;
    uint16_t       len  = fields.svc_data_uuid16_len;
    uint16_t       uuid = sd[0] | (sd[1] << 8);
    const uint8_t* mac  = d->addr.val;     // little-endian source address
    int8_t         rssi = d->rssi;

    if (uuid == 0x181A) {                  // pvvx / ATC Xiaomi (custom_beacon.h)
        if (len == 17) {                   // pvvx-custom, LITTLE-ENDIAN, all fields
            int16_t  t  = (int16_t)(sd[8]  | (sd[9]  << 8));
            uint16_t h  = (uint16_t)(sd[10] | (sd[11] << 8));
            uint16_t mv = (uint16_t)(sd[12] | (sd[13] << 8));
            update_sensor(mac, rssi, "pvvx", true, t / 100.0f, true, h / 100.0f, sd[14], mv);
        } else if (len == 15) {            // ATC1441, BIG-ENDIAN numerics
            int16_t  t  = (int16_t)((sd[8] << 8) | sd[9]);
            uint16_t mv = (uint16_t)((sd[12] << 8) | sd[13]);
            update_sensor(mac, rssi, "atc", true, t / 10.0f, true, (float)sd[10], sd[11], mv);
        } else {
            log_raw(mac, rssi, uuid, sd, len);     // reveal actual format
        }
    } else if (uuid == 0xFCD2) {           // BTHome v2 (LE TLV from offset 3) — frames alternate
        bool ht = false, hh = false; float t = 0, h = 0; int bp = -1, mv = -1;
        bool ok = true, any = false;
        uint16_t i = 3;                    // skip uuid(2) + device-info(1)
        while (i < len) {
            uint8_t id = sd[i++];
            if      (id == 0x02 && i + 2 <= len) { t = (int16_t)(sd[i] | (sd[i+1] << 8)) / 100.0f; ht = true; any = true; i += 2; }
            else if (id == 0x03 && i + 2 <= len) { h = (uint16_t)(sd[i] | (sd[i+1] << 8)) / 100.0f; hh = true; any = true; i += 2; }
            else if (id == 0x01 && i + 1 <= len) { bp = sd[i]; any = true; i += 1; }
            else if (id == 0x0C && i + 2 <= len) { mv = (uint16_t)(sd[i] | (sd[i+1] << 8)); any = true; i += 2; }
            else if (id == 0x00 && i + 1 <= len) { i += 1; }                       // packet id (1B)
            else if ((id == 0x0F || id == 0x10 || id == 0x11) && i + 1 <= len) { i += 1; }  // binary sensors (1B) — skip
            else { ok = false; break; }                                           // unknown id — size unknown, stop
        }
        if (any)      update_sensor(mac, rssi, "bthome", ht, t, hh, h, bp, mv);
        else if (!ok) log_raw(mac, rssi, uuid, sd, len);
    }
    return 0;
}

void BleService::start_scan() {
    start_observer_scan();   // shared starter; BlePanel resumes it after (dis)connect
}

#endif // CONFIG_MODESP_BLE_CENTRAL

bool BleService::on_init() {
    s_instance = this;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return false;
    }

    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb  = on_stack_sync;

    ble_svc_gap_init();
#if defined(MODESP_BLE_GATT)
    gatt_server_init();
#endif

    int rc = ble_svc_gap_device_name_set("ModESP");
    if (rc != 0) ESP_LOGW(TAG, "ble_svc_gap_device_name_set rc=%d", rc);

    nimble_port_freertos_init(ble_host_task);

    state_set("ble.enabled", true);
    state_set("ble.advertising", false);
    ESP_LOGI(TAG, "modesp_ble initialized (NimBLE host starting, coexist with Wi-Fi)");
    return true;
}

void BleService::on_update(uint32_t dt_ms) {
#if !defined(CONFIG_MODESP_BLE_GATT_SERVER)
    (void)dt_ms;
#endif

#if defined(CONFIG_MODESP_BLE_CENTRAL)
    // Publish the discovered BLE-sensor count when it changes (cheap; no spam).
    static int last_count = -1;
    if (s_sensor_count != last_count) {
        last_count = s_sensor_count;
        state_set("ble.sensors_seen", static_cast<int32_t>(s_sensor_count));
    }
#endif

#if defined(CONFIG_MODESP_BLE_GATT_SERVER)
    // Throttled telemetry notify (~1 Hz, like the WS delta cadence).
    notify_accum_ms_ += dt_ms;
    if (notify_accum_ms_ < 1000) return;
    notify_accum_ms_ = 0;

    if (conn_handle_ == 0xFFFF || !telemetry_subscribed_) return;

    // Cap the payload to the negotiated ATT MTU so the notification is one PDU.
    uint16_t mtu = ble_att_mtu(conn_handle_);
    if (mtu < 23) mtu = 23;
    size_t cap = static_cast<size_t>(mtu) - 3;
    if (cap > sizeof(last_telemetry_) - 1) cap = sizeof(last_telemetry_) - 1;

    char buf[512];
    int len = serialize_telemetry(buf, cap + 1);
    if (len <= 0) return;

    if (static_cast<size_t>(len) == strlen(last_telemetry_) &&
        memcmp(buf, last_telemetry_, len) == 0) {
        return;                                       // unchanged since last notify
    }
    memcpy(last_telemetry_, buf, len);
    last_telemetry_[len] = '\0';

    struct os_mbuf* om = ble_hs_mbuf_from_flat(buf, len);
    if (!om) return;
    ble_gatts_notify_custom(conn_handle_, s_telemetry_val_handle, om);
#else
    (void)dt_ms;
#endif
}

void BleService::on_stop() {
    ESP_LOGI(TAG, "stopping NimBLE host");
    nimble_port_stop();
}

} // namespace modesp

#endif // CONFIG_MODESP_BLE_ENABLE
