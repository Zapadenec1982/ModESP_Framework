/**
 * @file ctime_compat.h
 * @brief HOST BUILD: POSIX time stubs for MinGW compatibility.
 */
#pragma once

#if defined(_WIN32) && !defined(localtime_r)
#include <ctime>
inline struct tm* localtime_r(const time_t* timep, struct tm* result) {
    localtime_s(result, timep);
    return result;
}
#endif
