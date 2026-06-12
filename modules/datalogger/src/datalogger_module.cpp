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
    // М'ютекси для захисту буферів/файлів від гонки HTTP-задача ↔ main loop
    if (!buf_mutex_) buf_mutex_ = xSemaphoreCreateMutex();
    if (!io_mutex_)  io_mutex_  = xSemaphoreCreateMutex();
    if (!buf_mutex_ || !io_mutex_) {
        ESP_LOGE(TAG, "Не вдалося створити м'ютекси");
        return false;
    }

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
    log_event(modesp::gen::EVENT_POWER_ON);

    // Ініціалізувати SharedState (після POWER_ON щоб events_count включав його)
    state_set("datalogger.records_count", static_cast<int32_t>(temp_count_));
    state_set("datalogger.events_count",
              static_cast<int32_t>(event_count_ + event_buf_.size()));
    state_set("datalogger.flash_used", static_cast<int32_t>(flash_used_kb_));

    // Прочитати початковий стан для edge-detect (generated events)
    for (size_t i = 0; i < modesp::gen::LOG_EVENTS_COUNT && i < MAX_EVENTS; i++) {
        prev_event_state_[i] = read_bool(modesp::gen::LOG_EVENTS[i].state_key, false);
    }

    // Логувати активні канали
    int active = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_enabled_[i] && i < static_cast<int>(modesp::gen::LOG_CHANNELS_COUNT)) active++;
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
        if (i >= static_cast<int>(modesp::gen::LOG_CHANNELS_COUNT)) {
            ch_enabled_[i] = false;
            continue;
        }
        const auto& def = modesp::gen::LOG_CHANNELS[i];
        // Канал без enable_key — завжди увімкнений (air)
        if (!def.enable_key) {
            ch_enabled_[i] = true;
            continue;
        }
        // Канал увімкнений якщо toggle ON + hardware присутній (якщо потрібен)
        bool toggled = read_bool(def.enable_key, false);
        bool has_hw = def.requires_key ? read_bool(def.requires_key, false) : true;
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
            if (ch_enabled_[i] && i < static_cast<int>(modesp::gen::LOG_CHANNELS_COUNT)) {
                const auto& def = modesp::gen::LOG_CHANNELS[i];
                float val = read_float(def.state_key, 0.0f);
                rec.ch[i] = static_cast<int16_t>(val * 10.0f);
            } else {
                rec.ch[i] = TEMP_NO_DATA;
            }
        }

        // H5: уникнути тихої втрати семплів. При sample_interval < ~37 с
        // 16-record буфер заповнюється швидше за 10-хв flush — якщо повний,
        // примусовий flush ПЕРЕД додаванням (не стоїть на main loop: try-lock).
        if (temp_buf_.full()) {
            flush_to_flash();
        }
        if (xSemaphoreTake(buf_mutex_, portMAX_DELAY) == pdTRUE) {
            if (!temp_buf_.full()) {
                temp_buf_.push_back(rec);
            } else {
                ESP_LOGW(TAG, "temp buffer full — sample dropped (HTTP read in progress?)");
            }
            xSemaphoreGive(buf_mutex_);
        }
        state_set("datalogger.records_count",
                  static_cast<int32_t>(temp_count_ + temp_buf_.size()));
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
    using namespace modesp::gen;

    for (size_t i = 0; i < LOG_EVENTS_COUNT && i < MAX_EVENTS; i++) {
        const auto& ev = LOG_EVENTS[i];
        bool current = read_bool(ev.state_key, false);
        bool prev = prev_event_state_[i];

        if (current == prev) continue;
        prev_event_state_[i] = current;

        switch (ev.edge) {
            case EdgeType::RISING:
                if (current) log_event(ev.id);
                if (!current) log_event(modesp::gen::EVENT_ALARM_CLEAR);  // auto-clear on falling
                break;
            case EdgeType::FALLING:
                if (!current) log_event(ev.id);
                break;
            case EdgeType::BOTH:
                // For BOTH: id = rising event, id+1 = falling event
                log_event(current ? ev.id : (ev.id + 1));
                break;
        }
    }
}

// ── Запис події в RAM буфер ──

void DataLoggerModule::log_event(uint8_t event_id) {
    EventRecord rec;
    rec.timestamp = current_timestamp();
    rec.event_type = event_id;
    rec._pad[0] = 0;
    rec._pad[1] = 0;
    rec._pad[2] = 0;

    if (xSemaphoreTake(buf_mutex_, portMAX_DELAY) == pdTRUE) {
        if (!event_buf_.full()) {
            event_buf_.push_back(rec);
        } else {
            ESP_LOGW(TAG, "Event buffer full, dropping event %d", event_id);
        }
        xSemaphoreGive(buf_mutex_);
    }
}

// ── Flush RAM → LittleFS ──

bool DataLoggerModule::flush_to_flash() {
    // Non-blocking: якщо HTTP-задача зараз читає файли (io_mutex_ зайнятий),
    // відкласти flush — main loop НЕ повинен чекати на багатосекундний download.
    if (!io_mutex_ || xSemaphoreTake(io_mutex_, 0) != pdTRUE) {
        return false;
    }

    // Снепшот + clear RAM-буферів під buf_mutex_ (швидко), потім файлові операції
    // вже з локальних копій — буфери одразу вільні для нових семплів/подій.
    etl::vector<TempRecord, 16>  temp_snap;
    etl::vector<EventRecord, 32> event_snap;
    if (buf_mutex_ && xSemaphoreTake(buf_mutex_, portMAX_DELAY) == pdTRUE) {
        temp_snap  = temp_buf_;
        event_snap = event_buf_;
        temp_buf_.clear();
        event_buf_.clear();
        xSemaphoreGive(buf_mutex_);
    }

    if (temp_snap.empty() && event_snap.empty()) {
        xSemaphoreGive(io_mutex_);
        return true;
    }

    // Flush температури
    if (!temp_snap.empty()) {
        FILE* f = fopen(TEMP_FILE, "ab");
        if (f) {
            size_t written = fwrite(temp_snap.data(), sizeof(TempRecord),
                                    temp_snap.size(), f);
            fclose(f);
            temp_count_ += written;
            state_set("datalogger.records_count", static_cast<int32_t>(temp_count_));
        } else {
            ESP_LOGE(TAG, "Не вдалося відкрити %s", TEMP_FILE);
        }

        // M4: розмір ротації від РЕАЛЬНОГО інтервалу семплювання, не фіксованих
        // 60 записів/год — інакше при sample_interval≠60с retention брехливий.
        uint32_t per_hour = (sample_interval_ms_ > 0)
                          ? (3600000u / static_cast<uint32_t>(sample_interval_ms_)) : 60;
        if (per_hour == 0) per_hour = 1;
        size_t max_size = static_cast<size_t>(retention_hours_) * per_hour * sizeof(TempRecord);
        rotate_if_needed(TEMP_FILE, max_size);
    }

    // Flush подій
    if (!event_snap.empty()) {
        FILE* f = fopen(EVENT_FILE, "ab");
        if (f) {
            size_t written = fwrite(event_snap.data(), sizeof(EventRecord),
                                    event_snap.size(), f);
            fclose(f);
            event_count_ += written;
            state_set("datalogger.events_count", static_cast<int32_t>(event_count_));
        } else {
            ESP_LOGE(TAG, "Не вдалося відкрити %s", EVENT_FILE);
        }

        rotate_if_needed(EVENT_FILE, EVENT_MAX_SIZE);
    }

    update_flash_used();
    state_set("datalogger.flash_used", static_cast<int32_t>(flash_used_kb_));

    xSemaphoreGive(io_mutex_);
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

    // Тримаємо io_mutex_ на весь час читання файлів — flush_to_flash() через
    // try-lock відкладеться, тож файли не ротуються/не дописуються під час читання.
    if (io_mutex_) xSemaphoreTake(io_mutex_, portMAX_DELAY);

    // Снепшот RAM-буферів під buf_mutex_ — далі ітеруємо локальні копії, не
    // живі буфери, у які main loop одночасно пише.
    etl::vector<TempRecord, 16>  temp_snap;
    etl::vector<EventRecord, 32> event_snap;
    if (buf_mutex_ && xSemaphoreTake(buf_mutex_, portMAX_DELAY) == pdTRUE) {
        temp_snap  = temp_buf_;
        event_snap = event_buf_;
        xSemaphoreGive(buf_mutex_);
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
    // Scan RAM буфер (снепшот)
    for (size_t bi = 0; bi < temp_snap.size(); bi++) {
        const auto& rec = temp_snap[bi];
        if (rec.timestamp < cutoff) continue;
        for (int i = 0; i < MAX_CHANNELS; i++) {
            if (rec.ch[i] != TEMP_NO_DATA) ch_has_data[i] = true;
        }
    }

    // Побудувати індекси активних каналів
    int active_idx[MAX_CHANNELS];
    int active_count = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_has_data[i] && i < static_cast<int>(modesp::gen::LOG_CHANNELS_COUNT)) {
            active_idx[active_count++] = i;
        }
    }

    // 1. Header: channels масив
    int pos = snprintf(buf, sizeof(buf), "{\"channels\":[");
    for (int a = 0; a < active_count; a++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s\"%s\"",
                       a > 0 ? "," : "", modesp::gen::LOG_CHANNELS[active_idx[a]].id);
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
                if (io_mutex_) xSemaphoreGive(io_mutex_);
                httpd_resp_send_chunk(req, nullptr, 0);
                return ESP_FAIL;
            }
            first = false;
        }
        fclose(f);
    }

    // 3. RAM буфер (снепшот)
    for (size_t i = 0; i < temp_snap.size(); i++) {
        const auto& rec = temp_snap[i];
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
                if (io_mutex_) xSemaphoreGive(io_mutex_);
                httpd_resp_send_chunk(req, nullptr, 0);
                return ESP_FAIL;
            }
            first = false;
        }
        fclose(f);
    }

    // RAM events (снепшот)
    for (size_t i = 0; i < event_snap.size(); i++) {
        const auto& rec = event_snap[i];
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
    if (io_mutex_) xSemaphoreGive(io_mutex_);
    return ESP_OK;
}

// ── Summary для /api/log/summary ──

bool DataLoggerModule::serialize_summary(char* buf, size_t buf_size) const {
    uint32_t total_temp = temp_count_ + static_cast<uint32_t>(temp_buf_.size());
    uint32_t total_events = event_count_ + static_cast<uint32_t>(event_buf_.size());

    // Порахувати активні канали
    int active = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (ch_enabled_[i] && i < static_cast<int>(modesp::gen::LOG_CHANNELS_COUNT)) active++;
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
