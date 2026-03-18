# Patterns: ESP32 Code Examples

## SharedState Usage

### Variant A: Read state (preferred)
```cpp
auto val = state_->get("equipment.air_temp");
if (val) {
    float temp = etl::get<float>(*val);
}
```

### Variant B: Read with default
```cpp
float temp = state_->get_or("equipment.air_temp", 0.0f);
bool ok = state_->get_or("equipment.sensor1_ok", false);
```

### Write state
```cpp
// Normal — triggers WS broadcast
state_->set("thermostat.temperature", temp);

// Silent — no WS broadcast (timers, diagnostics)
state_->set("system.uptime", uptime, /*track_change=*/false);
```

### Anti-pattern
```cpp
// NEVER: std::string in hot path
std::string key = "thermostat." + name;  // heap allocation!
state_->set(key.c_str(), value);

// CORRECT: etl::string
etl::string<32> key;
key = "thermostat.";
key += name;
state_->set(key, value);
```

## Module on_update Pattern

### Standard update cycle
```cpp
void ThermostatModule::on_update() {
    // 1. Read inputs from SharedState
    float air_temp = state_->get_or("equipment.air_temp", 0.0f);
    bool sensor_ok = state_->get_or("equipment.sensor1_ok", false);

    // 2. Business logic (state machine)
    update_state_machine(air_temp, sensor_ok);

    // 3. Write requests to SharedState
    state_->set("thermostat.req.compressor", compressor_request_);

    // 4. Write status to SharedState
    state_->set("thermostat.state", static_cast<int32_t>(state_));
}
```

### Anti-pattern
```cpp
// NEVER: Direct HAL access from business module
auto* relay = hal_->get_driver("compressor");
relay->set(true);  // Only Equipment does this!

// CORRECT: Write request, Equipment arbitrates
state_->set("thermostat.req.compressor", true);
```

## Manifest State Key Declaration

### Numeric with range
```json
{
    "key": "thermostat.setpoint",
    "type": "float",
    "default": -18.0,
    "persist": true,
    "access": "readwrite",
    "min": -50.0,
    "max": 50.0,
    "step": 0.1,
    "label": {"ua": "Уставка", "en": "Setpoint"},
    "unit": "C"
}
```

### Select (enum)
```json
{
    "key": "defrost.type",
    "type": "int",
    "default": 0,
    "persist": true,
    "access": "readwrite",
    "options": [
        {"value": 0, "label": {"ua": "Природна", "en": "Natural"}},
        {"value": 1, "label": {"ua": "Тен", "en": "Electric"}},
        {"value": 2, "label": {"ua": "Гарячий газ", "en": "Hot gas"}}
    ]
}
```

## ESP-IDF Logging

```cpp
static const char* TAG = "Thermostat";

ESP_LOGI(TAG, "Setpoint: %.1f°C", setpoint);       // Info
ESP_LOGW(TAG, "Sensor timeout, entering safety");    // Warning
ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err)); // Error
ESP_LOGD(TAG, "State machine: %d -> %d", old, new_state);    // Debug
```

## FreeRTOS Patterns

### Mutex for shared resources
```cpp
// SharedState already handles mutex internally
// For custom shared resources:
SemaphoreHandle_t mutex_ = xSemaphoreCreateMutex();
if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    // critical section
    xSemaphoreGive(mutex_);
}
```

### Timer-based checks (not vTaskDelay)
```cpp
// In on_update() — called from main loop, no blocking
uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
if (now - last_check_ms_ >= CHECK_INTERVAL_MS) {
    last_check_ms_ = now;
    // periodic work
}
```
