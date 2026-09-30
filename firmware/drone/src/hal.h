// Hardware abstraction. Two implementations: hal_esp32.cpp for the aircraft and
// hal_sim.cpp for the browser build, which compiles the same mission,
// failsafe and telemetry logic against a synthetic airframe.
//
// Nothing in this header allocates, and every call is safe to make from any
// task.
#pragma once

#include <cstdint>
#include <cstddef>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);

// Console sink for telemetry and the boot banner: USB-CDC on the aircraft,
// stdout in the browser and host builds. Never blocks on a reader.
void consoleWrite(const char* data, size_t len);

// Flight controller serial link (MAVLink).
int  fcAvailable();
int  fcRead(uint8_t* buf, size_t cap);
void fcWrite(const uint8_t* buf, size_t len);

// Safety outputs. setSafe(true) drives the FC kill input so the motors cannot
// start; the external watchdog pin must keep toggling while armed.
void setSafe(bool safe);
bool safeRequested();
void armSwitchClosed();          // sample the physical arm switch now
bool armSwitchIsClosed();        // the most recent sample
void feedWatchdog();
void setStatusLed(uint8_t pattern);

// Analogy.
float batteryVolts();            // pack voltage, divider corrected
float batteryCellVolts();        // pack / cell count
float busCurrentAmps();

// Non-volatile parameters. Return false when the key is absent or the stored
// type does not match.
bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

// Network. The sim back-end reports "connected" with a fixed RSSI so the
// telemetry path can be exercised without a radio.
bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

// Mass storage for the flight log. Returns false when no card is present, in
// which case the logger keeps a ring buffer in RAM instead.
bool sdOpen(const char* path);
bool sdAppend(const char* data, size_t len);
void sdClose();

// Reset reason string, for the boot banner and the first telemetry frame.
const char* resetReason();

#if defined(RANCH_SIM)
// Advance the synthetic airframe by the real (browser) or virtual (host test)
// clock. The aircraft build has no equivalent: the FC is a separate box there.
void halSimPump();

// What the simulated flight controller actually did, so a host test can assert
// on "the FC acknowledged 8 items" instead of only on "no crash".
struct SimCounters {
    uint32_t rx_overflow;
    uint32_t frames_emitted;
    uint32_t mavlink_crc_errors;
    uint32_t mavlink_dropped;
    uint32_t fc_items;
    uint32_t fc_captures;
    uint32_t wdt_feeds;
    uint32_t card_bytes;
    uint32_t led_pattern;
    bool fc_acked_mission;
};
void halSimCounters(SimCounters& out);
#endif

}  // namespace ranch
