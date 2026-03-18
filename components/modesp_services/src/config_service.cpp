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

#include <cstdio>
#include <cstdlib>
#include <cstring>

// jsmn as header-only with JSMN_STATIC
#define JSMN_STATIC
#include "jsmn.h"

static const char* TAG = "ConfigSvc";

// Спільний parse-буфер для board.json і bindings.json (boot-only, sequential calls)
// Один набір замість двох static local — економія ~8 KB BSS
namespace {
static char s_json_buf[4096];
static jsmntok_t s_json_tokens[512];
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

    if (!parse_board_json()) {
        ESP_LOGE(TAG, "Failed to parse board.json");
        return false;
    }

    if (!parse_bindings_json()) {
        ESP_LOGE(TAG, "Failed to parse bindings.json");
        return false;
    }

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
                    } else {
                        j += 2;
                    }
                }

                if (!board_config_.gpio_inputs.full()) {
                    board_config_.gpio_inputs.push_back(cfg);
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
        } else {
            // Пропустити невідомий ключ + значення (масиви/об'єкти коректно)
            i++;  // skip key
            i = skip_token(tokens, i, ntokens);  // skip value
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
