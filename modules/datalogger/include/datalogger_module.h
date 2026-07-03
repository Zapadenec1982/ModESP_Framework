#pragma once
/**
 * @brief DataLogger module — multi-channel temperature & event logging to LittleFS.
 *
 * Samples up to 6 configurable channels every N seconds, logs equipment events
 * (compressor, defrost, alarms, door, power) on change.
 * Append-only files with rotate. Streaming chunked JSON via HTTP.
 */

#include "modesp/base_module.h"
#include "modesp/net/log_source.h"
#include <esp_http_server.h>
#include <etl/vector.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <climits>

/// Sentinel: канал не логується або датчик відсутній
static constexpr int16_t TEMP_NO_DATA = INT16_MIN;  // -32768

/// Channel + event definitions — auto-generated from module manifests
#include "datalogger_channels.h"
#include "datalogger_events.h"

/// Fixed for binary compatibility (TempRecord size must not change)
static constexpr int MAX_CHANNELS = static_cast<int>(modesp::gen::MAX_LOG_CHANNELS);

/// Запис температури (16 bytes, 6 каналів)
struct TempRecord {
    uint32_t timestamp;      ///< UNIX epoch (секунди) або uptime_sec
    int16_t  ch[MAX_CHANNELS]; ///< Канали ×10 (TEMP_NO_DATA = немає даних)
};
static_assert(sizeof(TempRecord) == 16, "TempRecord must be 16 bytes");

/// Event types — from generated/datalogger_events.h (manifest-driven)
/// System events (not edge-detect):
///   modesp::gen::EVENT_POWER_ON    = 10
///   modesp::gen::EVENT_ALARM_CLEAR = 7
/// Edge-detect events: modesp::gen::LOG_EVENTS[] table

/// Запис події (8 bytes, aligned)
struct EventRecord {
    uint32_t timestamp;    ///< UNIX epoch або uptime_sec
    uint8_t  event_type;   ///< EventType
    uint8_t  _pad[3];      ///< alignment
};
static_assert(sizeof(EventRecord) == 8, "EventRecord must be 8 bytes");

class DataLoggerModule : public modesp::BaseModule, public modesp::ILogSource {
public:
    DataLoggerModule();

    bool on_init() override;
    void on_update(uint32_t dt_ms) override;
    void on_stop() override;

    // ── modesp::ILogSource (споживає HttpService через log_source-слот;
    //     модуль самореєструється в on_init — main.cpp не бере участі) ──

    /// Стрімінг логу як chunked JSON в HTTP response
    esp_err_t serialize_log_chunked(httpd_req_t* req, int hours) const override;

    /// Стислі лічильники для /api/log/summary
    bool serialize_summary(char* buf, size_t buf_size) const override;

private:
    // ── RAM буфери ──
    etl::vector<TempRecord, 16>   temp_buf_;
    etl::vector<EventRecord, 32>  event_buf_;

    // ── Concurrency: on_update()/flush() run on the main loop, while
    //    serialize_*() run on the esp_http_server task. ──
    //  buf_mutex_ — short critical sections around RAM buffer push/snapshot.
    //  io_mutex_  — guards file append/rotate (flush) vs file reads (serialize).
    //               flush takes it non-blocking (0 timeout) so the main loop is
    //               NEVER stalled by a multi-second HTTP download; if the HTTP
    //               task holds it, that flush is simply deferred to next round.
    SemaphoreHandle_t buf_mutex_ = nullptr;
    SemaphoreHandle_t io_mutex_  = nullptr;

    // ── Попередній стан для edge-detect (generated events) ──
    static constexpr size_t MAX_EVENTS = 32;  // max edge-detect events
    bool prev_event_state_[MAX_EVENTS] = {};

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
    void log_event(uint8_t event_id);
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
