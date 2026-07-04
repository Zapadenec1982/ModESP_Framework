/**
 * @file config_service.cpp
 * @brief ConfigService implementation — LittleFS mount + jsmn JSON parsing
 *
 * Boot-time only: all JSON parsing happens on the stack.
 * After on_init() returns, only the parsed structs remain in memory.
 */

#include "modesp/services/config_service.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// jsmn as header-only with JSMN_STATIC
#define JSMN_STATIC
#include "jsmn.h"

static const char* TAG = "ConfigSvc";

// Phase 1.2 (з v4_max): парс-буфери на heap тільки на час on_init (~9.6 KB).
// Раніше були static у BSS — ноукошлоштовно жерли RAM назавжди. Тепер
// heap_caps_malloc(MALLOC_CAP_INTERNAL) для on_init, free після parse завершення.
// Подальші reads ідуть через board_config_ / binding_table_ ETL containers.
namespace {
static constexpr size_t LOCAL_MAX_JSON_SIZE = 4096;
static constexpr size_t LOCAL_MAX_TOKENS    = 512;

static char*      s_json_buf    = nullptr;
static jsmntok_t* s_json_tokens = nullptr;

static bool allocate_parse_buffers() {
    s_json_buf = static_cast<char*>(
        heap_caps_malloc(LOCAL_MAX_JSON_SIZE,
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    s_json_tokens = static_cast<jsmntok_t*>(
        heap_caps_malloc(LOCAL_MAX_TOKENS * sizeof(jsmntok_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!s_json_buf || !s_json_tokens) {
        ESP_LOGE("ConfigSvc", "Parse buffer alloc failed (need ~9.6 KB free heap)");
        if (s_json_buf)    { heap_caps_free(s_json_buf);    s_json_buf = nullptr; }
        if (s_json_tokens) { heap_caps_free(s_json_tokens); s_json_tokens = nullptr; }
        return false;
    }
    return true;
}

static void free_parse_buffers() {
    if (s_json_buf)    { heap_caps_free(s_json_buf);    s_json_buf = nullptr; }
    if (s_json_tokens) { heap_caps_free(s_json_tokens); s_json_tokens = nullptr; }
}
}  // namespace

namespace modesp {

// ═══════════════════════════════════════════════════════════════
// Helper: compare jsmn token string with a key
// ═══════════════════════════════════════════════════════════════

static bool jsoneq(const char* json, const jsmntok_t* tok, const char* key) {
    if (tok->type != JSMN_STRING) return false;
    int len = tok->end - tok->start;
    return (int)strlen(key) == len &&
           strncmp(json + tok->start, key, len) == 0;
}

// Helper: extract token as null-terminated string into buffer
static void tok_to_str(const char* json, const jsmntok_t* tok, char* buf, size_t buf_size) {
    int len = tok->end - tok->start;
    if ((size_t)len >= buf_size) len = (int)buf_size - 1;
    strncpy(buf, json + tok->start, len);
    buf[len] = '\0';
}

// Helper: extract token as integer
static int tok_to_int(const char* json, const jsmntok_t* tok) {
    char buf[12];
    tok_to_str(json, tok, buf, sizeof(buf));
    return atoi(buf);
}

// Helper: extract token as bool
static bool tok_to_bool(const char* json, const jsmntok_t* tok) {
    return strncmp(json + tok->start, "true", 4) == 0;
}

// Helper: підрахунок кількості токенів у значенні (для пропуску об'єктів/масивів)
static int skip_token(const jsmntok_t* tokens, int idx, int ntokens) {
    if (idx >= ntokens) return idx;
    if (tokens[idx].type == JSMN_PRIMITIVE || tokens[idx].type == JSMN_STRING) {
        return idx + 1;
    }
    // Об'єкт або масив: пропустити всі дочірні токени
    int children = tokens[idx].size;
    int j = idx + 1;
    for (int c = 0; c < children; c++) {
        if (tokens[idx].type == JSMN_OBJECT) {
            j = skip_token(tokens, j, ntokens);     // key
            j = skip_token(tokens, j, ntokens);     // value
        } else {
            j = skip_token(tokens, j, ntokens);     // array element
        }
    }
    return j;
}

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

bool ConfigService::on_init() {
    if (!mount_littlefs()) {
        ESP_LOGE(TAG, "LittleFS mount failed");
        return false;
    }

    // Phase 1.2: парс-буфери на heap тільки на час on_init (~9.6 KB).
    if (!allocate_parse_buffers()) {
        return false;
    }

    if (!parse_board_json()) {
        ESP_LOGE(TAG, "Failed to parse board.json");
        free_parse_buffers();
        return false;
    }

    // Runtime device registry (/data/devices.json) — user-subscribed remote devices
    // (BLE by MAC today). Merges into board_config_.ble_devices, runtime-wins-by-id,
    // so a subscribed device becomes indistinguishable from a factory board.json one.
    // Absent/empty file is non-fatal (returns true) — board.json remains the seed.
    if (!parse_devices_json()) {
        ESP_LOGW(TAG, "devices.json present but malformed — ignoring runtime devices");
    }

    if (!parse_bindings_json()) {
        ESP_LOGE(TAG, "Failed to parse bindings.json");
        free_parse_buffers();
        return false;
    }

    // Парс завершено — звільняємо ~9.6 KB heap. Подальше читання config
    // йде через board_config_ / binding_table_ (зкопійовані ETL-контейнери).
    free_parse_buffers();

    ESP_LOGI(TAG, "Board: %s v%s (%d gpio_out, %d ow, %d gpio_in, %d adc, %d i2c_bus, %d i2c_exp)",
             board_config_.board_name.c_str(),
             board_config_.board_version.c_str(),
             (int)board_config_.gpio_outputs.size(),
             (int)board_config_.onewire_buses.size(),
             (int)board_config_.gpio_inputs.size(),
             (int)board_config_.adc_channels.size(),
             (int)board_config_.i2c_buses.size(),
             (int)board_config_.i2c_expanders.size());
    ESP_LOGI(TAG, "Loaded %d bindings",
             (int)binding_table_.bindings.size());

    return true;
}

// ═══════════════════════════════════════════════════════════════
// LittleFS mount
// ═══════════════════════════════════════════════════════════════

bool ConfigService::mount_littlefs() {
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/data";
    conf.partition_label = "data";
    conf.format_if_mount_failed = false;
    conf.dont_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS register failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Mounted LittleFS (partition 'data')");
    return true;
}

// ═══════════════════════════════════════════════════════════════
// Parse board.json
// ═══════════════════════════════════════════════════════════════

bool ConfigService::parse_board_json() {
    char* buf = s_json_buf;
    jsmntok_t* tokens = s_json_tokens;

    // Read file
    FILE* f = fopen("/data/board.json", "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open /data/board.json");
        return false;
    }
    size_t len = fread(buf, 1, MAX_JSON_SIZE - 1, f);
    fclose(f);
    buf[len] = '\0';

    // Parse JSON
    jsmn_parser parser;
    jsmn_init(&parser);
    int ntokens = jsmn_parse(&parser, buf, len, tokens, MAX_TOKENS);
    if (ntokens < 0) {
        ESP_LOGE(TAG, "board.json parse error: %d", ntokens);
        return false;
    }

    // Root must be object
    if (ntokens < 1 || tokens[0].type != JSMN_OBJECT) {
        ESP_LOGE(TAG, "board.json: root is not object");
        return false;
    }

    char tmp[32];

    // Iterate top-level keys
    for (int i = 1; i < ntokens; ) {
        if (jsoneq(buf, &tokens[i], "board")) {
            tok_to_str(buf, &tokens[i + 1], tmp, sizeof(tmp));
            board_config_.board_name = tmp;
            i += 2;
        } else if (jsoneq(buf, &tokens[i], "version")) {
            tok_to_str(buf, &tokens[i + 1], tmp, sizeof(tmp));
            board_config_.board_version = tmp;
            i += 2;
        } else if (jsoneq(buf, &tokens[i], "gpio_outputs")) {
            // tokens[i+1] is the array
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'gpio_outputs' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;  // first element of array

            for (int elem = 0; elem < arr_size; elem++) {
                // Each element is an object
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: gpio_output entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;  // move into object

                GpioOutputConfig cfg = {};
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "gpio")) {
                        cfg.gpio = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "active_high")) {
                        cfg.active_high = tok_to_bool(buf, &tokens[j + 1]);
                        j += 2;
                    } else {
                        j += 2;  // skip unknown key
                    }
                }

                if (!board_config_.gpio_outputs.full()) {
                    board_config_.gpio_outputs.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "onewire_buses")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'onewire_buses' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: ow_bus entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                OneWireBusConfig cfg = {};
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "gpio")) {
                        cfg.gpio = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else {
                        j += 2;
                    }
                }

                if (!board_config_.onewire_buses.full()) {
                    board_config_.onewire_buses.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "gpio_inputs")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'gpio_inputs' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: gpio_input entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                GpioInputConfig cfg = {};
                cfg.pull_up = true;  // default
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "gpio")) {
                        cfg.gpio = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "pull")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.pull_up = (strcmp(tmp, "up") == 0);
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "invert")) {
                        cfg.invert = tok_to_bool(buf, &tokens[j + 1]);
                        j += 2;
                    } else {
                        j += 2;
                    }
                }

                if (!board_config_.gpio_inputs.full()) {
                    board_config_.gpio_inputs.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "encoders")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'encoders' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: encoder entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                EncoderConfig cfg = {};
                cfg.pull_up = true;  // default
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "clk")) {
                        cfg.clk = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "dt")) {
                        cfg.dt = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "pull")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.pull_up = (strcmp(tmp, "up") == 0);
                        j += 2;
                    } else {
                        j += 2;
                    }
                }

                if (!board_config_.encoders.full()) {
                    board_config_.encoders.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "adc_channels")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'adc_channels' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: adc_channel entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                AdcChannelConfig cfg = {};
                cfg.atten = 11;  // default: 0-3.3V
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "gpio")) {
                        cfg.gpio = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "atten")) {
                        cfg.atten = (uint8_t)tok_to_int(buf, &tokens[j + 1]);
                        j += 2;
                    } else {
                        j += 2;
                    }
                }

                if (!board_config_.adc_channels.full()) {
                    board_config_.adc_channels.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "i2c_buses")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'i2c_buses' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: i2c_bus entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                I2CBusConfig cfg = {};
                cfg.freq_hz = 100000;  // default 100kHz
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "sda")) {
                        cfg.sda = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "scl")) {
                        cfg.scl = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "freq_hz")) {
                        cfg.freq_hz = (uint32_t)tok_to_int(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.i2c_buses.full()) {
                    board_config_.i2c_buses.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "uart_buses")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'uart_buses' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: uart_bus entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                UartBusConfig cfg = {};
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "port")) {
                        cfg.port = tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "tx")) {
                        cfg.tx = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "rx")) {
                        cfg.rx = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "baud")) {
                        cfg.baud_rate = (uint32_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "rx_buffer")) {
                        cfg.rx_buffer = (uint16_t)tok_to_int(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.uart_buses.full()) {
                    board_config_.uart_buses.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "i2s_buses")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'i2s_buses' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: i2s_bus entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                I2SBusConfig cfg = {};   // defaults: sd=-1, sample_rate_hz=16000
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "bclk")) {
                        cfg.bclk = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "ws")) {
                        cfg.ws = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "dout")) {
                        cfg.dout = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "sd")) {
                        cfg.sd = (gpio_num_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "sample_rate_hz")) {
                        cfg.sample_rate_hz = (uint32_t)tok_to_int(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.i2s_buses.full()) {
                    board_config_.i2s_buses.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "i2c_expanders")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'i2c_expanders' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: i2c_expander entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                I2CExpanderConfig cfg = {};
                cfg.pin_count = 8;  // default PCF8574
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "bus")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.bus_id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "chip")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.chip = tmp;
                    } else if (jsoneq(buf, &tokens[j], "address")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.address = (uint8_t)strtol(tmp, nullptr, 16);
                    } else if (jsoneq(buf, &tokens[j], "pins")) {
                        cfg.pin_count = (uint8_t)tok_to_int(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.i2c_expanders.full()) {
                    board_config_.i2c_expanders.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "expander_outputs")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'expander_outputs' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: expander_output entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                I2CExpanderOutputConfig cfg = {};
                cfg.active_high = false;  // default: active-LOW (KC868-A6)
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "expander")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.expander_id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "pin")) {
                        cfg.pin = (uint8_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "active_high")) {
                        cfg.active_high = tok_to_bool(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.expander_outputs.full()) {
                    board_config_.expander_outputs.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "expander_inputs")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'expander_inputs' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: expander_input entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                I2CExpanderInputConfig cfg = {};
                cfg.invert = false;  // default
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "expander")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.expander_id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "pin")) {
                        cfg.pin = (uint8_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "invert")) {
                        cfg.invert = tok_to_bool(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.expander_inputs.full()) {
                    board_config_.expander_inputs.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "i2c_displays")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'i2c_displays' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: i2c_display entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                I2CDisplayConfig cfg = {};
                cfg.cols = 20;  // sane defaults — конкретику задає board.json
                cfg.rows = 10;
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "bus")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.bus_id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "chip")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.chip = tmp;
                    } else if (jsoneq(buf, &tokens[j], "cols")) {
                        cfg.cols = (uint8_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "rows")) {
                        cfg.rows = (uint8_t)tok_to_int(buf, &tokens[j + 1]);
                    } else if (jsoneq(buf, &tokens[j], "cal_x")) {
                        cfg.cal_x = (int8_t)tok_to_int(buf, &tokens[j + 1]);   // overscan-зсув OSD
                    } else if (jsoneq(buf, &tokens[j], "cal_y")) {
                        cfg.cal_y = (int8_t)tok_to_int(buf, &tokens[j + 1]);
                    }
                    j += 2;
                }
                if (!board_config_.i2c_displays.full()) {
                    board_config_.i2c_displays.push_back(cfg);
                }
            }
            i = j;
        } else if (jsoneq(buf, &tokens[i], "ble_devices")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "board.json: 'ble_devices' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "board.json: ble_device entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                BleDeviceConfig cfg = {};
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "mac")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.mac = tmp;
                    } else if (jsoneq(buf, &tokens[j], "name")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.name = tmp;
                    }
                    j += 2;
                }
                if (!board_config_.ble_devices.full()) {
                    board_config_.ble_devices.push_back(cfg);
                }
            }
            i = j;
        } else {
            // Пропустити невідомий ключ + значення (масиви/об'єкти коректно)
            i++;  // skip key
            i = skip_token(tokens, i, ntokens);  // skip value
        }
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════
// Parse devices.json (runtime device registry)
// ═══════════════════════════════════════════════════════════════

// Runtime-writable sibling of board.json ble_devices. Rows: {id, hw_type, mac, label}.
// Merged into board_config_.ble_devices with runtime-wins-by-id, so a subscribed device
// resolves via find_ble_device exactly like a factory one — the role layer never sees a
// MAC. Only hw_type "ble" is honoured today; hw_type is the generalization hook for
// future transports. Absent file → no runtime devices (returns true, non-fatal).
bool ConfigService::parse_devices_json() {
    char* buf = s_json_buf;
    jsmntok_t* tokens = s_json_tokens;

    FILE* f = fopen("/data/devices.json", "r");
    if (!f) {
        return true;   // no runtime registry — board.json ble_devices are the seed
    }
    size_t len = fread(buf, 1, MAX_JSON_SIZE - 1, f);
    fclose(f);
    buf[len] = '\0';

    jsmn_parser parser;
    jsmn_init(&parser);
    int ntokens = jsmn_parse(&parser, buf, len, tokens, MAX_TOKENS);
    if (ntokens < 0) {
        ESP_LOGE(TAG, "devices.json parse error: %d", ntokens);
        return false;
    }
    if (ntokens < 1 || tokens[0].type != JSMN_OBJECT) {
        ESP_LOGE(TAG, "devices.json: root is not object");
        return false;
    }

    char tmp[32];

    for (int i = 1; i < ntokens; ) {
        if (jsoneq(buf, &tokens[i], "devices")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "devices.json: 'devices' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                // devices.json is runtime-written — a malformed element must not abort the
                // whole registry (that would drop already-parsed valid rows while the caller
                // logs "ignoring"). Skip the bad element and continue.
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGW(TAG, "devices.json: device entry is not object — skipped");
                    j = skip_token(tokens, j, ntokens);
                    continue;
                }
                int obj_keys = tokens[j].size;
                j++;

                BleDeviceConfig cfg = {};
                char hw_type[8] = "ble";   // default; only "ble" honoured today
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "id")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.id = tmp;
                    } else if (jsoneq(buf, &tokens[j], "mac")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.mac = tmp;
                    } else if (jsoneq(buf, &tokens[j], "name")) {
                        // Connect device's adv-name (the connect target). Distinct from the
                        // human "label" (UI-only, round-trips via GET) — must NOT be conflated
                        // or a panel's name would be overwritten by its display label.
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        cfg.name = tmp;
                    } else if (jsoneq(buf, &tokens[j], "hw_type")) {
                        tok_to_str(buf, &tokens[j + 1], hw_type, sizeof(hw_type));
                    }
                    // Advance past the key, then past the value SUBTREE. A blind j += 2 would
                    // desync the walk if a hand-crafted row carried a nested object/array
                    // value (mirrors parse_bindings_json's nested-settings handling).
                    j++;
                    j = skip_token(tokens, j, ntokens);
                }

                // Valid when it has an identity: a MAC (broadcast observer) OR a name
                // (connect device). id is always required.
                if (strcmp(hw_type, "ble") != 0 || cfg.id.empty() ||
                    (cfg.mac.empty() && cfg.name.empty())) {
                    continue;   // unsupported transport, or missing id + identity — skip
                }

                // Merge: runtime row overrides a board.json device of the same id,
                // else appends. runtime-wins-by-id keeps one merged inventory.
                bool merged = false;
                for (auto& dev : board_config_.ble_devices) {
                    if (dev.id == cfg.id) {
                        dev.mac  = cfg.mac;
                        dev.name = cfg.name;
                        merged = true;
                        break;
                    }
                }
                if (!merged && !board_config_.ble_devices.full()) {
                    board_config_.ble_devices.push_back(cfg);
                }
            }
            i = j;
        } else {
            i++;
            i = skip_token(tokens, i, ntokens);
        }
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════
// Parse bindings.json
// ═══════════════════════════════════════════════════════════════

bool ConfigService::parse_bindings_json() {
    char* buf = s_json_buf;
    jsmntok_t* tokens = s_json_tokens;

    FILE* f = fopen("/data/bindings.json", "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open /data/bindings.json");
        return false;
    }
    size_t len = fread(buf, 1, MAX_JSON_SIZE - 1, f);
    fclose(f);
    buf[len] = '\0';

    jsmn_parser parser;
    jsmn_init(&parser);
    int ntokens = jsmn_parse(&parser, buf, len, tokens, MAX_TOKENS);
    if (ntokens < 0) {
        ESP_LOGE(TAG, "bindings.json parse error: %d", ntokens);
        return false;
    }

    if (ntokens < 1 || tokens[0].type != JSMN_OBJECT) {
        ESP_LOGE(TAG, "bindings.json: root is not object");
        return false;
    }

    char tmp[32];

    for (int i = 1; i < ntokens; ) {
        if (jsoneq(buf, &tokens[i], "bindings")) {
            if (tokens[i + 1].type != JSMN_ARRAY) {
                ESP_LOGE(TAG, "bindings.json: 'bindings' is not array");
                return false;
            }
            int arr_size = tokens[i + 1].size;
            int j = i + 2;

            for (int elem = 0; elem < arr_size; elem++) {
                if (tokens[j].type != JSMN_OBJECT) {
                    ESP_LOGE(TAG, "bindings.json: entry is not object");
                    return false;
                }
                int obj_keys = tokens[j].size;
                j++;

                Binding binding = {};
                for (int k = 0; k < obj_keys; k++) {
                    if (jsoneq(buf, &tokens[j], "hardware")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        binding.hardware_id = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "role")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        binding.role = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "driver")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        binding.driver_type = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "module")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        binding.module_name = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "address")) {
                        tok_to_str(buf, &tokens[j + 1], tmp, sizeof(tmp));
                        binding.address = tmp;
                        j += 2;
                    } else if (jsoneq(buf, &tokens[j], "settings")) {
                        // Per-binding driver settings: {"beta": 3900, "offset": 0.5}.
                        // Object value, so the scalar "j += 2" fallback below would
                        // mis-walk the tokens — must consume the nested object here.
                        if (tokens[j + 1].type == JSMN_OBJECT) {
                            int nset = tokens[j + 1].size;
                            int sj = j + 2;
                            for (int s = 0; s < nset; s++) {
                                char skey[24];   // longest key "read_interval_ms" = 16 + NUL
                                char sval[24];
                                tok_to_str(buf, &tokens[sj],     skey, sizeof(skey));
                                tok_to_str(buf, &tokens[sj + 1], sval, sizeof(sval));
                                if (!binding.settings.full()) {
                                    BindingSetting bs;
                                    bs.key   = skey;
                                    bs.value = static_cast<float>(atof(sval));
                                    binding.settings.push_back(bs);
                                }
                                sj += 2;
                            }
                            j = sj;
                        } else {
                            j += 2;
                        }
                    } else {
                        j += 2;
                    }
                }

                if (!binding_table_.bindings.full()) {
                    binding_table_.bindings.push_back(binding);
                }
            }
            i = j;
        } else {
            i++;
            i = skip_token(tokens, i, ntokens);
        }
    }

    return true;
}

} // namespace modesp
