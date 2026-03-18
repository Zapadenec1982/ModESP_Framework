/**
 * @file host_prefix.h
 * @brief Master prefix header force-included into every translation unit.
 *
 * CMakeLists.txt passes -include<path>/host_prefix.h so this header
 * is automatically included before any other include in every .cpp file.
 * This ensures ESP-IDF stubs are defined before any real ESP-IDF header.
 */
#pragma once

// Always pull in our ESP-IDF stubs first
#include "freertos_mock.h"
#include "esp_log_mock.h"
#include "esp_timer_mock.h"
#include "ctime_compat.h"

// Windows mkdir() has only 1 arg, POSIX has 2 (path, mode)
#if defined(_WIN32) && !defined(HOST_MKDIR_COMPAT)
#define HOST_MKDIR_COMPAT
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif
