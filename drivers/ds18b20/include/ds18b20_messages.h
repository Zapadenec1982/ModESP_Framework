/**
 * @file ds18b20_messages.h
 * @brief Повідомлення драйвера DS18B20
 *
 * MsgSensorReading — нове валідне значення температури.
 * MsgSensorError   — помилка зчитування (CRC, timeout, validation).
 */

#pragma once

#include "modesp/types.h"
#include "etl/message.h"
#include "etl/string.h"

/// Сенсор надіслав нове валідне значення
struct MsgSensorReading : etl::message<modesp::msg_id::SENSOR_READING> {
    etl::string<16> sensor_id;    // "temp1", "temp2", ...
    float value;                  // Температура °C
    uint32_t timestamp_ms;        // Uptime ms
};

/// Сенсор повідомив про помилку
struct MsgSensorError : etl::message<modesp::msg_id::SENSOR_ERROR> {
    etl::string<16> sensor_id;
    int32_t error_code;           // Код помилки
};
