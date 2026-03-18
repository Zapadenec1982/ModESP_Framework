#pragma once
/**
 * @brief DataLogger module — multi-channel temperature & event logging to LittleFS.
 *
 * Samples up to 6 configurable channels every N seconds, logs equipment events
 * (compressor, defrost, alarms, door, power) on change.
 * Append-only files with rotate. Streaming chunked JSON via HTTP.
 */

#include "modesp/base_module.h"
#include <esp_http_server.h>
#include <etl/vector.h>
#include <cstdint>
#include <climits>

/// Sentinel: канал не логується або датчик відсутній
static constexpr int16_t TEMP_NO_DATA = INT16_MIN;  // -32768

/// Максимум каналів у записі
static constexpr int MAX_CHANNELS = 6;

/// Визначення каналу (compile-time)
struct ChannelDef {
    const char* id;          ///< "air", "evap", "cond", "setpoint", "humidity"
    const char* state_key;   ///< "equipment.air_temp" — звідки читати значення
    const char* enable_key;  ///< "datalogger.log_evap" — toggle (nullptr = завжди)
    const char* has_key;     ///< "equipment.has_evap_temp" — потрібен hardware (nullptr = ні)
};

/// Таблиця каналів (порядок = порядок у бінарному записі)
static constexpr ChannelDef CHANNEL_DEFS[MAX_CHANNELS] = {
    {"air",      "equipment.air_temp",      nullptr,                    nullptr                     },
    {"evap",     "equipment.evap_temp",     "datalogger.log_evap",      "equipment.has_evap_temp"   },
    {"cond",     "equipment.cond_temp",     "datalogger.log_cond",      "equipment.has_cond_temp"   },
    {"setpoint", "thermostat.setpoint",     "datalogger.log_setpoint",  nullptr                     },
    {"humidity", "equipment.humidity",      "datalogger.log_humidity",  "equipment.has_humidity"    },
    {nullptr,    nullptr,                   nullptr,                    nullptr                     },
};

/// Запис температури (16 bytes, 6 каналів)
struct TempRecord {
    uint32_t timestamp;      ///< UNIX epoch (секунди) або uptime_sec
    int16_t  ch[MAX_CHANNELS]; ///< Канали ×10 (TEMP_NO_DATA = немає даних)
};
static_assert(sizeof(TempRecord) == 16, "TempRecord must be 16 bytes");

/// Тип події
enum EventType : uint8_t {
    EVENT_COMPRESSOR_ON   = 1,
    EVENT_COMPRESSOR_OFF  = 2,
    EVENT_DEFROST_START   = 3,
    EVENT_DEFROST_END     = 4,
    EVENT_ALARM_HIGH      = 5,
    EVENT_ALARM_LOW       = 6,
    EVENT_ALARM_CLEAR     = 7,
    EVENT_DOOR_OPEN       = 8,
    EVENT_DOOR_CLOSE      = 9,
    EVENT_POWER_ON        = 10,
    // Phase 17 — protection alarms
    EVENT_ALARM_SENSOR1   = 11,
    EVENT_ALARM_SENSOR2   = 12,
    EVENT_ALARM_CONT_RUN  = 13,
    EVENT_ALARM_PULLDOWN  = 14,
    EVENT_ALARM_SHORT_CYC = 15,
    EVENT_ALARM_RAPID_CYC = 16,
    EVENT_ALARM_RATE_RISE = 17,
    EVENT_ALARM_DOOR      = 18,
};

/// Запис події (8 bytes, aligned)
struct EventRecord {
    uint32_t timestamp;    ///< UNIX epoch або uptime_sec
    uint8_t  event_type;   ///< EventType
    uint8_t  _pad[3];      ///< alignment
};
static_assert(sizeof(EventRecord) == 8, "EventRecord must be 8 bytes");

class DataLoggerModule : public modesp::BaseModule {
public:
    DataLoggerModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    /// Стрімінг логу як chunked JSON в HTTP response
    esp_err_t serialize_log_chunked(httpd_req_t* req, int hours) const;

    /// Стислі лічильники для /api/log/summary
    bool serialize_summary(char* buf, size_t buf_size) const;

private:
    // ── RAM буфери ──
    etl::vector<TempRecord, 16>   temp_buf_;
    etl::vector<EventRecord, 32>  event_buf_;

    // ── Попередній стан для edge-detect ──
    bool prev_compressor_     = false;
    bool prev_defrost_active_ = false;
    bool prev_door_open_      = false;
    bool prev_alarm_high_     = false;
    bool prev_alarm_low_      = false;
    bool prev_sensor1_alarm_  = false;
    bool prev_sensor2_alarm_  = false;
    bool prev_cont_run_alarm_ = false;
    bool prev_pulldown_alarm_ = false;
    bool prev_short_cyc_alarm_= false;
    bool prev_rapid_cyc_alarm_= false;
    bool prev_rate_alarm_     = false;
    bool prev_door_alarm_     = false;

    // ── Таймери ──
    uint32_t sample_timer_ms_ = 0;
    uint32_t flush_timer_ms_  = 0;

    // ── Налаштування (cached) ──
    int32_t  sample_interval_ms_ = 60000;
    int32_t  retention_hours_    = 48;
    bool     ch_enabled_[MAX_CHANNELS] = {true, false, false, false, false, false};

    // ── Статистика ──
    uint32_t temp_count_  = 0;
    uint32_t event_count_ = 0;
    uint32_t flash_used_kb_ = 0;

    // ── Внутрішні методи ──
    void sync_settings();
    bool flush_to_flash();
    void rotate_if_needed(const char* path, size_t max_size);
    void log_event(EventType type);
    uint32_t current_timestamp() const;
    void update_flash_used();
    void poll_events();
    void migrate_old_format();

    /// Допоміжна: int16 → JSON "null" або число
    static int append_temp_val(char* buf, size_t sz, int16_t val);

    static constexpr const char* LOG_DIR        = "/data/log";
    static constexpr const char* TEMP_FILE      = "/data/log/temp.bin";
    static constexpr const char* TEMP_OLD_FILE  = "/data/log/temp.old";
    static constexpr const char* EVENT_FILE     = "/data/log/events.bin";
    static constexpr const char* EVENT_OLD_FILE = "/data/log/events.old";
    static constexpr size_t      EVENT_MAX_SIZE = 16384;  // 16 KB hard limit
    static constexpr uint32_t    FLUSH_INTERVAL_MS = 600000;  // 10 хвилин
};
