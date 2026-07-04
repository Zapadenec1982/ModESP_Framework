/**
 * @file adv_decoder.h
 * @brief BLE advertisement-decoder registry — transport ↔ sensor-driver seam.
 *
 * modesp_ble is a pure BLE TRANSPORT: it owns the radio and the passive scan,
 * but it does NOT know any device's advertisement format. A BLE sensor driver
 * (hardware_type "ble") registers one or more AdvDecoders at factory time; the
 * transport calls each registered decoder for every scan result carrying 16-bit
 * service data, until one returns true (recognized + reported). The decoder
 * parses the bytes for its known format(s) and reports values via
 * report_sensor(), which routes them to the MAC's cache (BleCentral) that the
 * bound driver reads each cycle.
 *
 * Layering: driver → modesp_ble (registers a decoder); modesp_ble never depends
 * on any driver — same direction as DriverRegistry.
 */
#pragma once

#include "sdkconfig.h"

#if defined(CONFIG_MODESP_BLE_ENABLE) && defined(CONFIG_MODESP_BLE_CENTRAL)

#include <stdint.h>
#include <stddef.h>

namespace modesp::ble {

/// Decoder for one advertisement's 16-bit service data. Returns true when it
/// recognized the format and reported a reading (or a valid empty frame);
/// false to let the transport try the next decoder / raw-log the frame.
///   mac_le     — NimBLE addr.val order (little-endian)
///   svc_uuid16 — the 16-bit service-data UUID (sd[0] | sd[1]<<8)
///   sd / len   — full service-data payload (includes the 2-byte UUID prefix)
using AdvDecoder = bool (*)(const uint8_t* mac_le, int8_t rssi,
                            uint16_t svc_uuid16, const uint8_t* sd, uint16_t len);

/// Decoder for one advertisement's MANUFACTURER-specific data (a custom beacon —
/// e.g. the nRF52 tilt sensor, company 0xFFFF). Same claim contract as AdvDecoder.
///   company16 — the 16-bit company id (md[0] | md[1]<<8)
///   md / len  — full manufacturer-data payload (includes the 2-byte company prefix)
using AdvMfgDecoder = bool (*)(const uint8_t* mac_le, int8_t rssi,
                              uint16_t company16, const uint8_t* md, uint16_t len);

/// Register a decoder (idempotent by function pointer, fixed pool). Call once
/// per driver — e.g. from its factory.
bool          register_adv_decoder(AdvDecoder fn);
size_t        adv_decoder_count();
AdvDecoder    adv_decoder_at(size_t i);

/// Register a manufacturer-data decoder (idempotent, fixed pool).
bool          register_adv_mfg_decoder(AdvMfgDecoder fn);
size_t        adv_mfg_decoder_count();
AdvMfgDecoder adv_mfg_decoder_at(size_t i);

/// Record that a device was SEEN and identified as `driver_type` (the driver whose
/// decoder just claimed the frame), with a short `summary` of its current readings for the
/// manual scan — the operator distinguishes same-type devices by this string ("45° 2900mV"
/// vs "12° 3000mV"). A driver that owns its OWN read cache (e.g. nRF tilt) calls this; a
/// driver that uses report_sensor gets type+summary set for it. The scan only ever lists
/// devices that reached this table, i.e. SUPPORTED ones — unclaimed advertisers never enter.
void report_seen(const uint8_t* mac_le, int8_t rssi, const char* driver_type,
                 const char* summary);

/// Report a decoded environmental reading — routed to the MAC's cache (for the bound driver
/// to read) plus the seen table (type + a merged temp/hum/batt summary) and throttled
/// diagnostics. `driver_type` tags the device for the scan (e.g. "ble_xiaomi_th"); `fmt` is
/// the wire-format label for the log line. ht/hh: temp/hum present this frame; bp/mv<0 absent.
void report_sensor(const uint8_t* mac_le, int8_t rssi, const char* driver_type,
                   const char* fmt, bool ht, float temp_c, bool hh, float hum_pct,
                   int batt_pct, int batt_mv);

/// Dump a frame as raw hex (throttled per device) — for a decoder that owns the
/// service UUID but does not recognize this frame's length/format, to reveal the
/// actual on-air layout during bring-up. The transport never calls this for
/// unclaimed frames, so unrelated advertisers do not spam the log.
void log_raw(const uint8_t* mac_le, int8_t rssi, uint16_t svc_uuid16,
             const uint8_t* sd, uint16_t len);

/// One recently-seen broadcast device, as the UI "scan for BLE devices" shows it.
/// mac is DISPLAY order (a4:c1:38:b4:dc:11). A driver's discovery function fills a
/// DiscoveredDevice from this so a user can subscribe a role to the picked MAC.
struct BleSeenDevice {
    uint8_t  mac[6];           // display order (reversed from NimBLE addr.val)
    int8_t   rssi;             // 127 = unavailable
    char     type[16];         // identified driver type ("ble_xiaomi_th"/"ble_nrf_tilt"/"ble_led_panel"); "" = unknown
    char     name[24];         // adv-name of a CONNECTABLE device (panel); "" for broadcast observers — the subscription identity for connect devices
    char     summary[24];      // short current-readings string ("24.6C 41%", "45° 2900mV") — distinguishes same-type devices
    bool     has_temp; float temp_c;    // legacy BTHome preview fields (existing xiaomi discovery)
    bool     has_hum;  float hum_pct;
    int      batt_pct;         // -1 = unknown
    int      batt_mv;          // -1 = unknown
    uint32_t age_ms;           // ms since last advertisement
};

/// Snapshot the recently-seen advertisers the decoders have populated (every device a
/// registered decoder recognized, bound or not). Fills up to `max`, returns the count.
/// Lock-free read of the host-task cache — a torn field is at worst cosmetic for a scan.
size_t list_seen(BleSeenDevice* out, size_t max);

} // namespace modesp::ble

#endif // CONFIG_MODESP_BLE_ENABLE && CONFIG_MODESP_BLE_CENTRAL
