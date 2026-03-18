/**
 * @file ds18b20_driver.cpp
 * @brief DS18B20 driver implementation with OneWire bit-bang
 *
 * OneWire protocol implemented via GPIO bit-bang with critical
 * sections (portDISABLE_INTERRUPTS) for precise timing.
 *
 * Algorithm:
 *   1. Every read_interval_ms: send CONVERT_T command
 *   2. After 750ms: read scratchpad with CRC8
 *   3. Validate value (range + rate of change)
 *   4. Store latest valid reading for read() calls
 */

#include "ds18b20_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"

static const char* TAG = "DS18B20";

// М'ютекс OneWire шини — захист від конкурентного доступу між
// DS18B20Driver::update() (main task) і scan_bus() (HTTP handler task).
// portDISABLE_INTERRUPTS() захищає тільки поточне ядро від переривань,
// але НЕ зупиняє задачі на іншому ядрі (dual-core ESP32).
static SemaphoreHandle_t s_bus_mutex = nullptr;
// Mutex ініціалізується в DS18B20Driver::init() — викликається послідовно
// з DriverManager::init() до старту HTTP/WiFi tasks.

// ═══════════════════════════════════════════════════════════════
// DS18B20 ROM commands
// ═══════════════════════════════════════════════════════════════
static constexpr uint8_t CMD_MATCH_ROM     = 0x55;
static constexpr uint8_t CMD_CONVERT_T     = 0x44;
static constexpr uint8_t CMD_READ_SCRATCH  = 0xBE;

// ═══════════════════════════════════════════════════════════════
// Configure (called by DriverManager before init)
// ═══════════════════════════════════════════════════════════════

void DS18B20Driver::configure(const char* role, gpio_num_t gpio,
                              uint32_t read_interval_ms, const char* address) {
    role_ = role;
    gpio_ = gpio;
    read_interval_ms_ = read_interval_ms;
    configured_ = true;

    if (address && address[0] != '\0') {
        if (parse_address(address)) {
            has_address_ = true;
            ESP_LOGI(TAG, "[%s] MATCH_ROM address: %s", role, address);
        } else {
            ESP_LOGE(TAG, "[%s] Invalid ROM address: %s", role, address);
        }
    } else {
        ESP_LOGE(TAG, "[%s] No ROM address — sensor MUST have address in bindings!", role);
    }
}

// ═══════════════════════════════════════════════════════════════
// ISensorDriver lifecycle
// ═══════════════════════════════════════════════════════════════

bool DS18B20Driver::init() {
    if (!configured_) {
        ESP_LOGE(TAG, "Driver not configured — call configure() first");
        return false;
    }

    // Ініціалізуємо bus mutex один раз (перший instance).
    // Виклик відбувається з DriverManager::init() — до старту HTTP task,
    // тому race condition між instances неможливий.
    if (!s_bus_mutex) {
        s_bus_mutex = xSemaphoreCreateMutex();
        if (!s_bus_mutex) {
            ESP_LOGE(TAG, "Failed to create OneWire bus mutex");
            return false;
        }
    }

    // Configure GPIO as open-drain output (for OneWire)
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << gpio_);
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d config failed: %s", gpio_, esp_err_to_name(err));
        return false;
    }
    gpio_set_level(gpio_, 1);  // Idle HIGH

    // Без адреси — помилка ініціалізації. Жодного auto-assign!
    if (!has_address_) {
        ESP_LOGE(TAG, "[%s] No ROM address configured — init FAILED", role_.c_str());
        ESP_LOGE(TAG, "[%s] Assign address via WebUI → Bindings → OneWire Discovery", role_.c_str());
        return false;
    }

    // Адреса задана — перевіряємо наявність датчика на шині
    if (!onewire_reset()) {
        ESP_LOGW(TAG, "[%s] No response on GPIO %d — will retry in runtime",
                 role_.c_str(), gpio_);
    }

    ESP_LOGI(TAG, "[%s] Initialized (GPIO=%d, interval=%lu ms, MATCH_ROM)",
             role_.c_str(), gpio_, read_interval_ms_);
    return true;
}

void DS18B20Driver::update(uint32_t dt_ms) {
    uptime_ms_ += dt_ms;
    ms_since_read_ += dt_ms;

    // Phase 1: start conversion
    if (!conversion_started_ && ms_since_read_ >= read_interval_ms_) {

        bool converted = false;
        if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            converted = start_conversion();
            xSemaphoreGive(s_bus_mutex);
        }
        if (converted) {
            conversion_started_ = true;
        } else {
            // Reset failed — sensor absent або bus busy
            ms_since_read_ = 0;
            consecutive_errors_++;
            if (consecutive_errors_ >= MAX_CONSECUTIVE_ERRORS &&
                consecutive_errors_ % MAX_CONSECUTIVE_ERRORS == 0) {
                ESP_LOGE(TAG, "[%s] Sensor offline after %d errors",
                         role_.c_str(), consecutive_errors_);
            }
        }
        return;
    }

    // Phase 2: read scratchpad 750ms after conversion start
    if (conversion_started_ && ms_since_read_ >= read_interval_ms_ + 750) {
        conversion_started_ = false;
        ms_since_read_ = 0;

        float temp = 0.0f;
        bool read_ok = false;

        if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            read_ok = retry([&]() { return read_temperature(temp); });
            xSemaphoreGive(s_bus_mutex);
        }

        if (read_ok) {
            if (validate_reading(temp)) {
                current_temp_ = temp;
                last_valid_temp_ = temp;
                last_valid_reading_ms_ = uptime_ms_;
                has_valid_reading_ = true;
                consecutive_errors_ = 0;

                ESP_LOGD(TAG, "[%s] %.2f°C", role_.c_str(), temp);
            } else {
                ESP_LOGW(TAG, "[%s] Validation failed: %.2f°C", role_.c_str(), temp);
                consecutive_errors_++;
            }
        } else {
            consecutive_errors_++;
            ESP_LOGW(TAG, "[%s] Read failed (errors=%d)", role_.c_str(), consecutive_errors_);
        }

        if (consecutive_errors_ == MAX_CONSECUTIVE_ERRORS) {
            ESP_LOGE(TAG, "[%s] %d consecutive errors", role_.c_str(), consecutive_errors_);
        }
    }
}

bool DS18B20Driver::read(float& value) {
    if (!has_valid_reading_) return false;
    value = current_temp_;
    return true;
}

bool DS18B20Driver::is_healthy() const {
    return consecutive_errors_ < MAX_CONSECUTIVE_ERRORS;
}

// ═══════════════════════════════════════════════════════════════
// OneWire low-level (bit-bang with critical sections)
//
// Таймінги оптимізовані для слабкого pull-up (4.7-5kΩ)
// та довгого кабелю (до 2м). Семпл зсунуто на ~15µs від
// початку тайм-слота для надійного зчитування.
// ═══════════════════════════════════════════════════════════════

bool DS18B20Driver::onewire_reset() {
    // Reset потребує 480µs LOW + 480µs recovery
    // Розбиваємо на дві фази щоб не тримати interrupts disabled 960µs
    gpio_set_level(gpio_, 0);
    ets_delay_us(480);

    portDISABLE_INTERRUPTS();
    gpio_set_level(gpio_, 1);
    ets_delay_us(70);
    int presence = gpio_get_level(gpio_);
    portENABLE_INTERRUPTS();

    // Дочекатись кінця presence pulse (загалом 480µs після release)
    ets_delay_us(410);

    return (presence == 0);
}

void DS18B20Driver::onewire_write_bit(uint8_t bit) {
    // Без власного critical section — викликається з write_byte під спільним
    if (bit & 1) {
        gpio_set_level(gpio_, 0);
        ets_delay_us(6);
        gpio_set_level(gpio_, 1);
        ets_delay_us(64);
    } else {
        gpio_set_level(gpio_, 0);
        ets_delay_us(60);
        gpio_set_level(gpio_, 1);
        ets_delay_us(10);
    }
}

uint8_t DS18B20Driver::onewire_read_bit() {
    // Без власного critical section — викликається з read_byte під спільним
    gpio_set_level(gpio_, 0);
    ets_delay_us(3);
    gpio_set_level(gpio_, 1);
    ets_delay_us(9);
    uint8_t bit = gpio_get_level(gpio_) ? 1 : 0;
    ets_delay_us(56);
    return bit;
}

void DS18B20Driver::onewire_write_byte(uint8_t data) {
    // Critical section на весь байт (~560µs)
    portDISABLE_INTERRUPTS();
    for (uint8_t i = 0; i < 8; i++) {
        onewire_write_bit(data & 0x01);
        data >>= 1;
    }
    portENABLE_INTERRUPTS();
}

uint8_t DS18B20Driver::onewire_read_byte() {
    // Critical section на весь байт (~560µs)
    uint8_t data = 0;
    portDISABLE_INTERRUPTS();
    for (uint8_t i = 0; i < 8; i++) {
        data |= (onewire_read_bit() << i);
    }
    portENABLE_INTERRUPTS();
    return data;
}

// ═══════════════════════════════════════════════════════════════
// DS18B20 operations
// ═══════════════════════════════════════════════════════════════

void DS18B20Driver::send_rom_command() {
    // Завжди MATCH_ROM — адреса обов'язкова
    onewire_write_byte(CMD_MATCH_ROM);
    for (uint8_t i = 0; i < 8; i++) {
        onewire_write_byte(rom_address_[i]);
    }
}

bool DS18B20Driver::start_conversion() {
    if (!onewire_reset()) {
        return false;
    }
    send_rom_command();
    onewire_write_byte(CMD_CONVERT_T);
    return true;
}

bool DS18B20Driver::read_scratchpad(uint8_t* buf, size_t len) {
    if (!onewire_reset()) {
        return false;
    }
    send_rom_command();
    onewire_write_byte(CMD_READ_SCRATCH);

    for (size_t i = 0; i < len; i++) {
        buf[i] = onewire_read_byte();
    }
    return true;
}

bool DS18B20Driver::parse_address(const char* addr_str) {
    // Парсимо "28:FF:AA:BB:CC:DD:EE:01" → 8 байт
    uint8_t addr[8] = {};
    int idx = 0;
    const char* p = addr_str;

    while (*p && idx < 8) {
        // Парсимо hex байт
        char hi = *p++;
        if (!*p) return false;
        char lo = *p++;

        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };

        int h = hex_val(hi);
        int l = hex_val(lo);
        if (h < 0 || l < 0) return false;

        addr[idx++] = (uint8_t)((h << 4) | l);

        // Пропускаємо роздільник (':' або '-')
        if (*p == ':' || *p == '-') p++;
    }

    if (idx != 8) return false;

    // CRC8 перевірка: байти 0-6, CRC в байті 7
    if (crc8(addr, 7) != addr[7]) {
        ESP_LOGW(TAG, "ROM address CRC mismatch");
        return false;
    }

    memcpy(rom_address_, addr, 8);
    return true;
}

bool DS18B20Driver::read_temperature(float& temp_out) {
    uint8_t scratchpad[9];

    if (!read_scratchpad(scratchpad, 9)) {
        return false;
    }

    // CRC8 check (bytes 0-7, CRC in byte 8)
    uint8_t calc_crc = crc8(scratchpad, 8);
    if (calc_crc != scratchpad[8]) {
        // Китайські клони DS18B20 мають reserved bytes A5 A5 замість FF 0C
        // і не рахують CRC правильно — детектимо по сигнатурі
        bool is_clone = (scratchpad[5] == 0xA5 && scratchpad[6] == 0xA5);
        if (is_clone) {
            if (!clone_detected_) {
                clone_detected_ = true;
                ESP_LOGW(TAG, "[%s] Chinese DS18B20 clone detected (A5 A5 signature) — CRC skip enabled",
                         role_.c_str());
            }
            // Валідуємо config register (byte 4): 0x7F=12bit, 0x5F=11, 0x3F=10, 0x1F=9
            uint8_t cfg = scratchpad[4];
            if (cfg != 0x7F && cfg != 0x5F && cfg != 0x3F && cfg != 0x1F) {
                ESP_LOGW(TAG, "[%s] Clone: invalid config byte 0x%02X", role_.c_str(), cfg);
                return false;
            }
            // CRC пропускаємо — температура валідується нижче
        } else {
            ESP_LOGW(TAG, "[%s] CRC8 mismatch: got=0x%02X calc=0x%02X raw=[%02X %02X %02X %02X %02X %02X %02X %02X %02X]",
                     role_.c_str(), scratchpad[8], calc_crc,
                     scratchpad[0], scratchpad[1], scratchpad[2], scratchpad[3],
                     scratchpad[4], scratchpad[5], scratchpad[6], scratchpad[7], scratchpad[8]);
            return false;
        }
    }

    // Check for default 85C value (conversion not complete)
    int16_t raw = (static_cast<int16_t>(scratchpad[1]) << 8) | scratchpad[0];
    float temp = raw / 16.0f;

    if (raw == 0x0550) {
        ESP_LOGD(TAG, "[%s] Power-on reset value (85C), skipping", role_.c_str());
        return false;
    }

    temp_out = temp;
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Static OneWire helpers (для scan_bus — без instance)
// Ті ж таймінги що й instance-методи, але з gpio параметром
// ═══════════════════════════════════════════════════════════════

bool DS18B20Driver::ow_reset(gpio_num_t gpio) {
    gpio_set_level(gpio, 0);
    ets_delay_us(480);

    portDISABLE_INTERRUPTS();
    gpio_set_level(gpio, 1);
    ets_delay_us(70);
    int presence = gpio_get_level(gpio);
    portENABLE_INTERRUPTS();

    ets_delay_us(410);
    return (presence == 0);
}

void DS18B20Driver::ow_write_bit(gpio_num_t gpio, uint8_t bit) {
    if (bit & 1) {
        gpio_set_level(gpio, 0);
        ets_delay_us(6);
        gpio_set_level(gpio, 1);
        ets_delay_us(64);
    } else {
        gpio_set_level(gpio, 0);
        ets_delay_us(60);
        gpio_set_level(gpio, 1);
        ets_delay_us(10);
    }
}

uint8_t DS18B20Driver::ow_read_bit(gpio_num_t gpio) {
    gpio_set_level(gpio, 0);
    ets_delay_us(3);
    gpio_set_level(gpio, 1);
    ets_delay_us(9);
    uint8_t bit = gpio_get_level(gpio) ? 1 : 0;
    ets_delay_us(56);
    return bit;
}

void DS18B20Driver::ow_write_byte(gpio_num_t gpio, uint8_t data) {
    portDISABLE_INTERRUPTS();
    for (uint8_t i = 0; i < 8; i++) {
        ow_write_bit(gpio, data & 0x01);
        data >>= 1;
    }
    portENABLE_INTERRUPTS();
}

uint8_t DS18B20Driver::ow_read_byte(gpio_num_t gpio) {
    uint8_t data = 0;
    portDISABLE_INTERRUPTS();
    for (uint8_t i = 0; i < 8; i++) {
        data |= (ow_read_bit(gpio) << i);
    }
    portENABLE_INTERRUPTS();
    return data;
}

// ═══════════════════════════════════════════════════════════════
// SEARCH_ROM — Maxim AN187 binary search algorithm
// ═══════════════════════════════════════════════════════════════

size_t DS18B20Driver::scan_bus(gpio_num_t gpio, RomAddress* results, size_t max_results) {
    // Захист від конкурентного доступу з DS18B20Driver::update() (main task).
    // Якщо mutex == nullptr — жоден DS18B20 не ініціалізований → active update() немає
    // → конкурентного доступу немає, сканування безпечне без mutex.
    bool have_mutex = false;
    if (s_bus_mutex) {
        if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "scan_bus: cannot acquire bus mutex (bus busy?)");
            return 0;
        }
        have_mutex = true;
    }

    // Налаштувати GPIO для OneWire (open-drain + pull-up)
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << gpio);
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(gpio, 1);

    // Стабілізація шини після конфігурації
    vTaskDelay(pdMS_TO_TICKS(10));

    size_t found = 0;
    uint8_t last_discrepancy = 0;    // Bit position of last conflict (1-based)
    bool last_device = false;
    uint8_t prev_rom[8] = {};        // ROM з попереднього проходу

    // Захист від нескінченного циклу при шумній шині
    static constexpr int MAX_SEARCH_PASSES = 24;  // 3x max devices
    int passes = 0;

    while (!last_device && found < max_results && passes < MAX_SEARCH_PASSES) {
        passes++;

        // 1. Reset bus
        if (!ow_reset(gpio)) break;  // Ніхто не відповів

        // 2. Send SEARCH ROM command (0xF0)
        ow_write_byte(gpio, 0xF0);

        uint8_t rom[8] = {};
        uint8_t new_discrepancy = 0;  // Нова позиція конфлікту
        bool search_ok = true;

        // 3. 64-bit search loop
        // Кожен triplet (read + complement + direction write) в одному critical section
        // ~210µs на позицію (менше ніж ~560µs для write_byte)
        for (uint8_t bit_pos = 1; bit_pos <= 64; bit_pos++) {
            uint8_t byte_idx = (bit_pos - 1) / 8;
            uint8_t bit_mask = 1 << ((bit_pos - 1) % 8);

            uint8_t bit_val, bit_comp, direction;

            portDISABLE_INTERRUPTS();
            bit_val  = ow_read_bit(gpio);
            bit_comp = ow_read_bit(gpio);

            if (bit_val == 1 && bit_comp == 1) {
                // Ніхто не відповів
                portENABLE_INTERRUPTS();
                search_ok = false;
                break;
            }

            if (bit_val != bit_comp) {
                // Всі пристрої мають однаковий біт — немає конфлікту
                direction = bit_val;
            } else {
                // Конфлікт: bit=0, comp=0 → є пристрої і з 0, і з 1
                if (bit_pos == last_discrepancy) {
                    direction = 1;
                } else if (bit_pos > last_discrepancy) {
                    direction = 0;
                } else {
                    direction = (prev_rom[byte_idx] & bit_mask) ? 1 : 0;
                }

                if (direction == 0) {
                    new_discrepancy = bit_pos;
                }
            }

            // Записуємо напрямок в ROM
            if (direction) {
                rom[byte_idx] |= bit_mask;
            }

            // Повідомляємо шину обраний напрямок
            ow_write_bit(gpio, direction);
            portENABLE_INTERRUPTS();
        }

        if (!search_ok) break;

        // 4. Валідація знайденої адреси
        if (crc8(rom, 7) != rom[7]) {
            // CRC failed — НЕ оновлюємо prev_rom/last_discrepancy,
            // повторюємо з тими ж параметрами
            ESP_LOGW(TAG, "CRC mismatch during bus scan (pass %d/%d)",
                     passes, MAX_SEARCH_PASSES);
            vTaskDelay(pdMS_TO_TICKS(5));  // Пауза для стабілізації шини
            continue;
        }

        // Family code validation: DS18B20=0x28, DS18S20=0x10, DS1822=0x22
        if (rom[0] != 0x28 && rom[0] != 0x10 && rom[0] != 0x22) {
            ESP_LOGW(TAG, "Unknown family code 0x%02X — not a DS18x20, skipping", rom[0]);
            // Адреса з правильним CRC але невідомий тип — пропускаємо,
            // але оновлюємо search state щоб продовжити пошук
            memcpy(prev_rom, rom, 8);
            last_discrepancy = new_discrepancy;
            if (last_discrepancy == 0) last_device = true;
            continue;
        }

        // Дедуплікація: клони можуть створювати фантомні конфлікти
        // і один датчик знаходиться кілька разів
        bool duplicate = false;
        for (size_t d = 0; d < found; d++) {
            if (memcmp(results[d].bytes, rom, 8) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            ESP_LOGD(TAG, "Duplicate ROM skipped (clone artifact)");
            memcpy(prev_rom, rom, 8);
            last_discrepancy = new_discrepancy;
            if (last_discrepancy == 0) last_device = true;
            continue;
        }

        memcpy(results[found].bytes, rom, 8);
        found++;
        ESP_LOGI(TAG, "Found DS18x20: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                 rom[0], rom[1], rom[2], rom[3],
                 rom[4], rom[5], rom[6], rom[7]);

        // 5. Оновлюємо search state ТІЛЬКИ при успішній валідації
        memcpy(prev_rom, rom, 8);
        last_discrepancy = new_discrepancy;
        if (last_discrepancy == 0) {
            last_device = true;  // Всі гілки дерева пройдені
        }
    }

    if (passes >= MAX_SEARCH_PASSES) {
        ESP_LOGW(TAG, "Bus scan aborted after %d passes — noisy bus?", passes);
    }

    ESP_LOGI(TAG, "Bus scan complete: %d device(s) on GPIO %d", (int)found, gpio);
    if (have_mutex) xSemaphoreGive(s_bus_mutex);
    return found;
}

// ═══════════════════════════════════════════════════════════════
// read_temp_by_address — для preview під час сканування
// ═══════════════════════════════════════════════════════════════

bool DS18B20Driver::read_temp_by_address(gpio_num_t gpio,
                                          const RomAddress& addr, float& temp_out) {
    bool have_mutex = false;
    if (s_bus_mutex) {
        if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "read_temp_by_address: cannot acquire bus mutex");
            return false;
        }
        have_mutex = true;
    }

    bool result = false;

    // Start conversion з MATCH_ROM
    if (!ow_reset(gpio)) {
        if (have_mutex) xSemaphoreGive(s_bus_mutex);
        return false;
    }
    ow_write_byte(gpio, CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) ow_write_byte(gpio, addr.bytes[i]);
    ow_write_byte(gpio, CMD_CONVERT_T);

    // Чекаємо 750ms на конвертацію (mutex тримаємо — bus зайнятий)
    vTaskDelay(pdMS_TO_TICKS(800));

    // Read scratchpad з MATCH_ROM
    if (ow_reset(gpio)) {
        ow_write_byte(gpio, CMD_MATCH_ROM);
        for (int i = 0; i < 8; i++) ow_write_byte(gpio, addr.bytes[i]);
        ow_write_byte(gpio, CMD_READ_SCRATCH);

        uint8_t scratchpad[9];
        for (int i = 0; i < 9; i++) scratchpad[i] = ow_read_byte(gpio);

        // CRC8 scratchpad check (з толерантністю до клонів)
        bool crc_ok = (crc8(scratchpad, 8) == scratchpad[8]);
        bool is_clone = (scratchpad[5] == 0xA5 && scratchpad[6] == 0xA5);
        if (crc_ok || is_clone) {
            int16_t raw = (static_cast<int16_t>(scratchpad[1]) << 8) | scratchpad[0];
            if (raw != 0x0550) {  // Power-on reset (85°C)
                temp_out = raw / 16.0f;
                result = true;
            }
        }
    }

    if (have_mutex) xSemaphoreGive(s_bus_mutex);
    return result;
}

// ═══════════════════════════════════════════════════════════════
// format_address — "28:FF:AA:BB:CC:DD:EE:01"
// ═══════════════════════════════════════════════════════════════

void DS18B20Driver::format_address(const uint8_t* addr, char* out, size_t out_size) {
    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0], addr[1], addr[2], addr[3],
             addr[4], addr[5], addr[6], addr[7]);
}

// ═══════════════════════════════════════════════════════════════
// CRC8 (Dallas/Maxim polynomial: x^8 + x^5 + x^4 + 1)
// ═══════════════════════════════════════════════════════════════

uint8_t DS18B20Driver::crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

// ═══════════════════════════════════════════════════════════════
// Retry pattern
// ═══════════════════════════════════════════════════════════════

template<typename F>
bool DS18B20Driver::retry(F operation, uint8_t max_attempts, uint32_t delay_ms) {
    for (uint8_t i = 0; i < max_attempts; i++) {
        if (operation()) return true;
        if (i < max_attempts - 1) {
            // Пауза + bus recovery reset перед наступною спробою
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            onewire_reset();
            ets_delay_us(100);
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════════════════════════

bool DS18B20Driver::validate_reading(float value) {
    if (value < MIN_VALID_TEMP || value > MAX_VALID_TEMP) {
        ESP_LOGW(TAG, "[%s] Out of range: %.2f°C", role_.c_str(), value);
        return false;
    }

    if (has_valid_reading_) {
        uint32_t elapsed_ms = uptime_ms_ - last_valid_reading_ms_;
        if (elapsed_ms > 0) {
            float dt_sec = elapsed_ms / 1000.0f;
            float rate = fabsf(value - last_valid_temp_) / dt_sec;
            if (rate > MAX_RATE_PER_SEC) {
                ESP_LOGW(TAG, "[%s] Rate too high: %.2f°C/s (dt=%lu ms)",
                         role_.c_str(), rate, elapsed_ms);
                return false;
            }
        }
    }

    return true;
}
