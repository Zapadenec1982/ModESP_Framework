/**
 * @brief DataLogger — multi-channel temperature & event logging to LittleFS.
 *
 * Samples up to 6 configurable channels every N seconds.
 * Logs equipment events on state change (edge-detect).
 * Append-only files with rotate. Streaming chunked JSON v3 via HTTP.
 *
 * JSON v3 format:
 *   {"channels":["air","evap","setpoint",...],
 *    "temp":[[ts,v0,v1,v2,...],...],
 *    "events":[[ts,type],...]}
 * where value = null when TEMP_NO_DATA.
 * channels array contains only enabled channel IDs.
 */

#include "datalogger_module.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <ctime>

static const char* TAG = "DataLogger";

DataLoggerModule::DataLoggerModule()
    : BaseModule("datalogger", modesp::ModulePriority::LOW)
{
}

// ── Час: UNIX epoch якщо SNTP синхронізовано, інакше uptime ──

uint32_t DataLoggerModule::current_timestamp() const {
    time_t now = time(nullptr);
    if (now > 1700000000) return static_cast<uint32_t>(now);  // SNTP OK
    // Fallback: uptime в секундах
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
}

// ── Допоміжна: int16 → JSON число або "null" ──

int DataLoggerModule::append_temp_val(char* buf, size_t sz, int16_t val) {
    if (val == TEMP_NO_DATA) {
        return snprintf(buf, sz, "null");
    }
    return snprintf(buf, sz, "%d", static_cast<int>(val));
}

// ── Міграція старого формату (8 або 12 байт → 16 байт) ──

void DataLoggerModule::migrate_old_format() {
    // Обрізаємо файли до кратного sizeof(TempRecord) розміру.
    // Partial write після power loss → останній неповний запис відкидається,
    // решта даних зберігається.
    const char* files[] = {TEMP_FILE, TEMP_OLD_FILE};
    for (int i = 0; i < 2; i++) {
        struct stat st;
        if (stat(files[i], &st) != 0 || st.st_size == 0) continue;
        size_t remainder = st.st_size % sizeof(TempRecord);
        if (remainder == 0) continue;

        size_t valid_size = st.st_size - remainder;
        ESP_LOGW(TAG, "Truncate %s: %lu → %lu bytes (відкинуто %lu partial)",
                 files[i], (unsigned long)st.st_size,
                 (unsigned long)valid_size, (unsigned long)remainder);

        if (valid_size == 0) {
            remove(files[i]);
        } else {
            // Читаємо валідну частину, перезаписуємо файл
            FILE* f = fopen(files[i], "rb");
            if (!f) continue;

            // Використовуємо temp_buf_ як тимчасовий буфер (16 записів × 16 байт = 256 байт)
            // Копіюємо блоками
            FILE* tmp = fopen("/data/log/_trunc.tmp", "wb");
            if (!tmp) { fclose(f); continue; }

            TempRecord rec;
            size_t copied = 0;
            while (copied < valid_size && fread(&rec, sizeof(rec), 1, f) == 1) {
                fwrite(&rec, sizeof(rec), 1, tmp);
                copied += sizeof(rec);
            }
            fclose(f);
            fclose(tmp);

            remove(files[i]);
            rename("/data/log/_trunc.tmp", files[i]);
        }
    }
}

// ── Init ──

bool DataLoggerModule::on_init() {
    // Створити директорію логів
    mkdir(LOG_DIR, 0775);

    // Міграція старого формату (8/12 bytes → 16 bytes)
    migrate_old_format();

    sync_settings();

    // Порахувати існуючі записи після ребуту
    struct stat st;
    temp_count_ = 0;
    if (stat(TEMP_FILE, &st) == 0)
        temp_count_ += st.st_size / sizeof(TempRecord);
    if (stat(TEMP_OLD_FILE, &st) == 0)
        temp_count_ += st.st_size / sizeof(TempRecord);

    event_count_ = 0;
    if (stat(EVENT_FILE, &st) == 0)
        event_count_ += st.st_size / sizeof(EventRecord);
    if (stat(EVENT_OLD_FILE, &st) == 0)
        event_count_ += st.st_size / sizeof(EventRecord);

    update_flash_used();

    // POWER_ON маркер
    log_event(EVENT_POWER_ON);

    // Ініціалізувати SharedState (після POWER_ON щоб events_count включав його)
    state_set("datalogger.records_count", static_cast<int32_t>(temp_count_));
    state_set("datalogger.events_count",
              static_cast<int32_t>(event_count_ + event_buf_.size()));
    state_set("datalogger.flash_used", static_cast<int32_t>(flash_used_kb_));

    // Прочитати початковий стан для edge-detect
    prev_compressor_     = read_bool("equipment.compressor", false);
    prev_defrost_active_ = read_bool("defrost.active", false);
    prev_door_open_      = read_bool("equipment.door_open", false);
    prev_alarm_high_     = read_bool("protection.high_temp_alarm", false);
    prev_alarm_low_      = read_bool("protection.low_temp_alarm", false);
    prev_sensor1_alarm_  = read_bool("protection.sensor1_alarm", false);
    prev_sensor2_alarm_  = read_bool("protection.sensor2_alarm", false);
    prev_cont_run_alarm_ = read_bool("protection.continuous_run_alarm", false);
    prev_pulldown_alarm_ = read_bool("protection.pulldown_alarm", false);
    prev_short_cyc_alarm_= read_bool("protection.short_cycle_alarm", false);
    prev_rapid_cyc_alarm_= read_bool("protection.rapid_cycle_alarm", false);
    prev_rate_alarm_     = read_bool("protection.rate_alarm", false);
    prev_door_alarm_     = read_bool("protection.door_alarm", false);

    // Логувати активні канали
    int active = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_enabled_[i] && CHANNEL_DEFS[i].id) active++;
    }
    ESP_LOGI(TAG, "Ініціалізовано: %lu temp, %lu events, %lu KB flash, %d каналів",
             (unsigned long)temp_count_, (unsigned long)event_count_,
             (unsigned long)flash_used_kb_, active);
    return true;
}

// ── Sync settings ──

void DataLoggerModule::sync_settings() {
    int32_t interval = read_int("datalogger.sample_interval", 60);
    sample_interval_ms_ = interval * 1000;
    retention_hours_ = read_int("datalogger.retention_hours", 48);

    // Оновити enabled стан кожного каналу
    for (int i = 0; i < MAX_CHANNELS; i++) {
        const auto& def = CHANNEL_DEFS[i];
        if (!def.id) {
            ch_enabled_[i] = false;
            continue;
        }
        // Канал без enable_key — завжди увімкнений (air)
        if (!def.enable_key) {
            ch_enabled_[i] = true;
            continue;
        }
        // Канал увімкнений якщо toggle ON + hardware присутній (якщо потрібен)
        bool toggled = read_bool(def.enable_key, false);
        bool has_hw = def.has_key ? read_bool(def.has_key, false) : true;
        ch_enabled_[i] = toggled && has_hw;
    }
}

// ── Main loop ──

void DataLoggerModule::on_update(uint32_t dt_ms) {
    if (!read_bool("datalogger.enabled", true)) return;

    sync_settings();

    // 1. Семплювання температури (до 6 каналів)
    sample_timer_ms_ += dt_ms;
    if (sample_timer_ms_ >= static_cast<uint32_t>(sample_interval_ms_)) {
        sample_timer_ms_ = 0;

        uint32_t now = current_timestamp();

        TempRecord rec;
        rec.timestamp = now;

        for (int i = 0; i < MAX_CHANNELS; i++) {
            const auto& def = CHANNEL_DEFS[i];
            if (ch_enabled_[i] && def.state_key) {
                float val = read_float(def.state_key, 0.0f);
                rec.ch[i] = static_cast<int16_t>(val * 10.0f);
            } else {
                rec.ch[i] = TEMP_NO_DATA;
            }
        }

        if (!temp_buf_.full()) {
            temp_buf_.push_back(rec);
            state_set("datalogger.records_count",
                      static_cast<int32_t>(temp_count_ + temp_buf_.size()));
        }
    }

    // 2. Polling подій (edge-detect)
    size_t events_before = event_buf_.size();
    poll_events();
    if (event_buf_.size() != events_before) {
        state_set("datalogger.events_count",
                  static_cast<int32_t>(event_count_ + event_buf_.size()));
    }

    // 3. Flush на LittleFS кожні 10 хвилин
    flush_timer_ms_ += dt_ms;
    if (flush_timer_ms_ >= FLUSH_INTERVAL_MS) {
        flush_timer_ms_ = 0;
        flush_to_flash();
    }
}

// ── Edge-detect подій ──

void DataLoggerModule::poll_events() {
    bool comp = read_bool("equipment.compressor", false);
    if (comp != prev_compressor_) {
        log_event(comp ? EVENT_COMPRESSOR_ON : EVENT_COMPRESSOR_OFF);
        prev_compressor_ = comp;
    }

    bool defrost = read_bool("defrost.active", false);
    if (defrost != prev_defrost_active_) {
        log_event(defrost ? EVENT_DEFROST_START : EVENT_DEFROST_END);
        prev_defrost_active_ = defrost;
    }

    bool door = read_bool("equipment.door_open", false);
    if (door != prev_door_open_) {
        log_event(door ? EVENT_DOOR_OPEN : EVENT_DOOR_CLOSE);
        prev_door_open_ = door;
    }

    // === Аварії: rising edge → event, falling edge → ALARM_CLEAR ===
    // ВАЖЛИВО: зберігаємо prev_ ПІСЛЯ clear check

    bool alarm_high = read_bool("protection.high_temp_alarm", false);
    if (alarm_high && !prev_alarm_high_) log_event(EVENT_ALARM_HIGH);
    if (!alarm_high && prev_alarm_high_) log_event(EVENT_ALARM_CLEAR);
    prev_alarm_high_ = alarm_high;

    bool alarm_low = read_bool("protection.low_temp_alarm", false);
    if (alarm_low && !prev_alarm_low_) log_event(EVENT_ALARM_LOW);
    if (!alarm_low && prev_alarm_low_) log_event(EVENT_ALARM_CLEAR);
    prev_alarm_low_ = alarm_low;

    // Sensor alarms
    bool s1 = read_bool("protection.sensor1_alarm", false);
    if (s1 && !prev_sensor1_alarm_) log_event(EVENT_ALARM_SENSOR1);
    if (!s1 && prev_sensor1_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_sensor1_alarm_ = s1;

    bool s2 = read_bool("protection.sensor2_alarm", false);
    if (s2 && !prev_sensor2_alarm_) log_event(EVENT_ALARM_SENSOR2);
    if (!s2 && prev_sensor2_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_sensor2_alarm_ = s2;

    // Compressor protection alarms
    bool cont = read_bool("protection.continuous_run_alarm", false);
    if (cont && !prev_cont_run_alarm_) log_event(EVENT_ALARM_CONT_RUN);
    if (!cont && prev_cont_run_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_cont_run_alarm_ = cont;

    bool pull = read_bool("protection.pulldown_alarm", false);
    if (pull && !prev_pulldown_alarm_) log_event(EVENT_ALARM_PULLDOWN);
    if (!pull && prev_pulldown_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_pulldown_alarm_ = pull;

    bool sc = read_bool("protection.short_cycle_alarm", false);
    if (sc && !prev_short_cyc_alarm_) log_event(EVENT_ALARM_SHORT_CYC);
    if (!sc && prev_short_cyc_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_short_cyc_alarm_ = sc;

    bool rc = read_bool("protection.rapid_cycle_alarm", false);
    if (rc && !prev_rapid_cyc_alarm_) log_event(EVENT_ALARM_RAPID_CYC);
    if (!rc && prev_rapid_cyc_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_rapid_cyc_alarm_ = rc;

    bool rate = read_bool("protection.rate_alarm", false);
    if (rate && !prev_rate_alarm_) log_event(EVENT_ALARM_RATE_RISE);
    if (!rate && prev_rate_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_rate_alarm_ = rate;

    bool da = read_bool("protection.door_alarm", false);
    if (da && !prev_door_alarm_) log_event(EVENT_ALARM_DOOR);
    if (!da && prev_door_alarm_) log_event(EVENT_ALARM_CLEAR);
    prev_door_alarm_ = da;
}

// ── Запис події в RAM буфер ──

void DataLoggerModule::log_event(EventType type) {
    EventRecord rec;
    rec.timestamp = current_timestamp();
    rec.event_type = static_cast<uint8_t>(type);
    rec._pad[0] = 0;
    rec._pad[1] = 0;
    rec._pad[2] = 0;

    if (!event_buf_.full()) {
        event_buf_.push_back(rec);
    } else {
        ESP_LOGW(TAG, "Event buffer full, dropping event %d", type);
    }
}

// ── Flush RAM → LittleFS ──

bool DataLoggerModule::flush_to_flash() {
    if (temp_buf_.empty() && event_buf_.empty()) return true;

    // Flush температури
    if (!temp_buf_.empty()) {
        FILE* f = fopen(TEMP_FILE, "ab");
        if (f) {
            size_t written = fwrite(temp_buf_.data(), sizeof(TempRecord),
                                    temp_buf_.size(), f);
            fclose(f);
            temp_count_ += written;
            temp_buf_.clear();
            state_set("datalogger.records_count", static_cast<int32_t>(temp_count_));
        } else {
            ESP_LOGE(TAG, "Не вдалося відкрити %s", TEMP_FILE);
        }

        size_t max_size = static_cast<size_t>(retention_hours_) * 60 * sizeof(TempRecord);
        rotate_if_needed(TEMP_FILE, max_size);
    }

    // Flush подій
    if (!event_buf_.empty()) {
        FILE* f = fopen(EVENT_FILE, "ab");
        if (f) {
            size_t written = fwrite(event_buf_.data(), sizeof(EventRecord),
                                    event_buf_.size(), f);
            fclose(f);
            event_count_ += written;
            event_buf_.clear();
            state_set("datalogger.events_count", static_cast<int32_t>(event_count_));
        } else {
            ESP_LOGE(TAG, "Не вдалося відкрити %s", EVENT_FILE);
        }

        rotate_if_needed(EVENT_FILE, EVENT_MAX_SIZE);
    }

    update_flash_used();
    state_set("datalogger.flash_used", static_cast<int32_t>(flash_used_kb_));

    ESP_LOGD(TAG, "Flush: %lu temp, %lu events",
             (unsigned long)temp_count_, (unsigned long)event_count_);
    return true;
}

// ── Ротація файлу ──

void DataLoggerModule::rotate_if_needed(const char* path, size_t max_size) {
    struct stat st;
    if (stat(path, &st) != 0) return;
    if (static_cast<size_t>(st.st_size) <= max_size) return;

    const char* old_path = nullptr;
    if (strcmp(path, TEMP_FILE) == 0) {
        old_path = TEMP_OLD_FILE;
    } else if (strcmp(path, EVENT_FILE) == 0) {
        old_path = EVENT_OLD_FILE;
    }
    if (!old_path) return;

    remove(old_path);
    rename(path, old_path);
    ESP_LOGI(TAG, "Ротація %s (%lu bytes)", path, (unsigned long)st.st_size);
}

// ── Підрахунок flash ──

void DataLoggerModule::update_flash_used() {
    struct stat st;
    size_t total = 0;
    if (stat(TEMP_FILE, &st) == 0) total += st.st_size;
    if (stat(TEMP_OLD_FILE, &st) == 0) total += st.st_size;
    if (stat(EVENT_FILE, &st) == 0) total += st.st_size;
    if (stat(EVENT_OLD_FILE, &st) == 0) total += st.st_size;
    flash_used_kb_ = static_cast<uint32_t>((total + 512) / 1024);
}

// ── Streaming chunked JSON v3 для GET /api/log ──
//
// Формат: {"channels":["air","evap","setpoint"],
//          "temp":[[ts,v0,v1,v2],...],
//          "events":[[ts,type],...]}
// Всі 6 слотів записані у бінарному файлі; JSON містить тільки
// ті канали що мають хоча б 1 != TEMP_NO_DATA значення.

esp_err_t DataLoggerModule::serialize_log_chunked(httpd_req_t* req, int hours) const {
    char buf[256];
    int len;

    uint32_t cutoff = 0;
    if (hours > 0) {
        uint32_t now = current_timestamp();
        if (now > static_cast<uint32_t>(hours * 3600)) {
            cutoff = now - static_cast<uint32_t>(hours * 3600);
        }
    }

    // Визначити які канали мають дані (scan файлів + RAM)
    bool ch_has_data[MAX_CHANNELS] = {};
    // Scan файлів
    const char* temp_files_scan[] = {TEMP_OLD_FILE, TEMP_FILE};
    for (int fi = 0; fi < 2; fi++) {
        FILE* f = fopen(temp_files_scan[fi], "rb");
        if (!f) continue;
        TempRecord rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            if (rec.timestamp < cutoff) continue;
            for (int i = 0; i < MAX_CHANNELS; i++) {
                if (rec.ch[i] != TEMP_NO_DATA) ch_has_data[i] = true;
            }
        }
        fclose(f);
    }
    // Scan RAM буфер
    for (size_t bi = 0; bi < temp_buf_.size(); bi++) {
        const auto& rec = temp_buf_[bi];
        if (rec.timestamp < cutoff) continue;
        for (int i = 0; i < MAX_CHANNELS; i++) {
            if (rec.ch[i] != TEMP_NO_DATA) ch_has_data[i] = true;
        }
    }

    // Побудувати індекси активних каналів
    int active_idx[MAX_CHANNELS];
    int active_count = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_has_data[i] && CHANNEL_DEFS[i].id) {
            active_idx[active_count++] = i;
        }
    }

    // 1. Header: channels масив
    int pos = snprintf(buf, sizeof(buf), "{\"channels\":[");
    for (int a = 0; a < active_count; a++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s\"%s\"",
                       a > 0 ? "," : "", CHANNEL_DEFS[active_idx[a]].id);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "],\"temp\":[");
    httpd_resp_send_chunk(req, buf, pos);

    // 2. Temp records (файли + RAM)
    const char* temp_files[] = {TEMP_OLD_FILE, TEMP_FILE};
    bool first = true;

    for (int fi = 0; fi < 2; fi++) {
        FILE* f = fopen(temp_files[fi], "rb");
        if (!f) continue;

        TempRecord rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            if (rec.timestamp < cutoff) continue;

            char tmp[128];
            int p = snprintf(tmp, sizeof(tmp), "%s[%lu",
                            first ? "" : ",",
                            (unsigned long)rec.timestamp);
            for (int a = 0; a < active_count; a++) {
                p += snprintf(tmp + p, sizeof(tmp) - p, ",");
                p += append_temp_val(tmp + p, sizeof(tmp) - p, rec.ch[active_idx[a]]);
            }
            p += snprintf(tmp + p, sizeof(tmp) - p, "]");

            if (httpd_resp_send_chunk(req, tmp, p) != ESP_OK) {
                fclose(f);
                httpd_resp_send_chunk(req, nullptr, 0);
                return ESP_FAIL;
            }
            first = false;
        }
        fclose(f);
    }

    // 3. RAM буфер
    for (size_t i = 0; i < temp_buf_.size(); i++) {
        const auto& rec = temp_buf_[i];
        if (rec.timestamp < cutoff) continue;

        char tmp[128];
        int p = snprintf(tmp, sizeof(tmp), "%s[%lu",
                        first ? "" : ",",
                        (unsigned long)rec.timestamp);
        for (int a = 0; a < active_count; a++) {
            p += snprintf(tmp + p, sizeof(tmp) - p, ",");
            p += append_temp_val(tmp + p, sizeof(tmp) - p, rec.ch[active_idx[a]]);
        }
        p += snprintf(tmp + p, sizeof(tmp) - p, "]");

        httpd_resp_send_chunk(req, tmp, p);
        first = false;
    }

    // 4. Events section
    len = snprintf(buf, sizeof(buf), "],\"events\":[");
    httpd_resp_send_chunk(req, buf, len);

    const char* event_files[] = {EVENT_OLD_FILE, EVENT_FILE};
    first = true;

    for (int fi = 0; fi < 2; fi++) {
        FILE* f = fopen(event_files[fi], "rb");
        if (!f) continue;

        EventRecord rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            if (rec.timestamp < cutoff) continue;
            len = snprintf(buf, sizeof(buf), "%s[%lu,%d]",
                          first ? "" : ",",
                          (unsigned long)rec.timestamp,
                          (int)rec.event_type);
            if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) {
                fclose(f);
                httpd_resp_send_chunk(req, nullptr, 0);
                return ESP_FAIL;
            }
            first = false;
        }
        fclose(f);
    }

    // RAM events
    for (size_t i = 0; i < event_buf_.size(); i++) {
        const auto& rec = event_buf_[i];
        if (rec.timestamp < cutoff) continue;
        len = snprintf(buf, sizeof(buf), "%s[%lu,%d]",
                      first ? "" : ",",
                      (unsigned long)rec.timestamp,
                      (int)rec.event_type);
        httpd_resp_send_chunk(req, buf, len);
        first = false;
    }

    // 5. Footer
    httpd_resp_send_chunk(req, "]}", 2);
    httpd_resp_send_chunk(req, nullptr, 0);  // end chunked
    return ESP_OK;
}

// ── Summary для /api/log/summary ──

bool DataLoggerModule::serialize_summary(char* buf, size_t buf_size) const {
    uint32_t total_temp = temp_count_ + static_cast<uint32_t>(temp_buf_.size());
    uint32_t total_events = event_count_ + static_cast<uint32_t>(event_buf_.size());

    // Порахувати активні канали
    int active = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_enabled_[i] && CHANNEL_DEFS[i].id) active++;
    }

    int len = snprintf(buf, buf_size,
        "{\"hours\":%ld,\"temp_count\":%lu,\"event_count\":%lu,\"flash_kb\":%lu,\"channels\":%d}",
        (long)retention_hours_,
        (unsigned long)total_temp,
        (unsigned long)total_events,
        (unsigned long)flash_used_kb_,
        active);
    return len > 0 && static_cast<size_t>(len) < buf_size;
}

// ── Stop ──

void DataLoggerModule::on_stop() {
    flush_to_flash();
    ESP_LOGI(TAG, "Зупинено, фінальний flush виконано");
}
