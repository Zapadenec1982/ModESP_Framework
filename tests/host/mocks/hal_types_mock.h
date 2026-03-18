/**
 * @file hal_types_mock.h
 * @brief HOST BUILD: HAL type stubs for gpio_num_t, adc_atten_t etc.
 */
#pragma once

#ifndef HAL_TYPES_MOCK_H
#define HAL_TYPES_MOCK_H

#include <cstdint>

// GPIO
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)

// ADC
typedef enum {
    ADC_ATTEN_DB_0   = 0,
    ADC_ATTEN_DB_2_5 = 1,
    ADC_ATTEN_DB_6   = 2,
    ADC_ATTEN_DB_11  = 3,
    ADC_ATTEN_DB_12  = 3,  // alias
} adc_atten_t;

typedef int adc_channel_t;
typedef int adc_unit_t;

// I2C — pointer types для сумісності з nullptr в hal_types.h
typedef void* i2c_master_bus_handle_t;
typedef void* i2c_master_dev_handle_t;

// esp_err_t
typedef int esp_err_t;
#define ESP_OK    0
#define ESP_FAIL  (-1)

#endif // HAL_TYPES_MOCK_H
