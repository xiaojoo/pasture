// Hardware abstraction for one show aircraft. Two implementations:
// hal_esp32.cpp drives the real pins (NeoPixel over RMT, two UARTs, an ADC), and
// hal_sim.cpp stands in for the things this board is wired to -- a NEO-M9N that
// knows where it is, an ArduCopter that follows setpoints at a bounded speed, a
// WS2812B strip, a pack that runs down and an external watchdog that resets the
// airframe if the feed stops.
//
// Nothing in this header allocates, and every call is safe to make from any task.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);

// Console sink for telemetry and the boot banner: USB-CDC on the aircraft,
// stdout in the browser and host builds. Never blocks on a reader.
void consoleWrite(const char* data, size_t len);

// Console source: what a person at the USB-C port (or the simulator's serial
// monitor) has typed. Returns how many bytes it took, 0 when nothing is waiting,
// and never blocks. Line assembly lives in main.cpp, not here, so the aircraft's
// UART and the sandbox's queue are parsed by the same code.
int consoleRead(char* buf, size_t cap);

// RTK receiver (UBX, one UART of its own).
int  rtkAvailable();
int  rtkRead(uint8_t* buf, size_t cap);
void rtkWrite(const uint8_t* buf, size_t len);   // UBX configuration frames out
// True once per GNSS second, on the rising edge of the receiver's TIMEPULSE.
// A show that starts on this edge starts on the same absolute second as every
// other airframe in it, which is what makes a baked timeline flyable.
bool rtkPpsEdge();

// Flight controller link (MAVLink).
int  fcAvailable();
int  fcRead(uint8_t* buf, size_t cap);
void fcWrite(const uint8_t* buf, size_t len);

// The lights. Pixels are staged into the strip's buffer by ledSetPixel() and
// moved out on the wire by ledCommit(); committing is the only call that has
// interrupts-disabled work in it, which is why it is not per pixel.
void ledSetPixel(uint8_t index, uint32_t rgb);
void ledCommit();

// Safety inputs and outputs. The SAFE switch is read, never written: closing it
// is a person's decision. The buzzer is the crew's audible channel for "this
// airframe is not flying" and for a pack that is too low to start.
bool safeSwitchClosed();         // sampled now
bool safeSwitchIsClosed();       // the last sample
void setBuzzer(bool on);
bool buzzerIsOn();
void feedWatchdog();             // the external watchdog wants an edge, >=10 Hz
void setStatusLed(bool on);

// Analogy. Pack voltage with the divider already divided out.
float batteryVolts();

// Non-volatile parameters. Return false when the key is absent or the stored
// type does not match.
bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

// Network. The ground station reaches this airframe over MQTT on this radio and
// nowhere else; there is no second link.
bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

// Reset reason string, for the boot banner and the first telemetry frame.
const char* resetReason();

#if defined(RANCH_SIM)
// Advance the synthetic airframe, receiver and strip by the real (browser) or
// virtual (host test) clock.
void halSimPump();

// What the model actually saw, so a host test can assert on "the strip is
// physically showing this colour" and "the FC latched this many setpoints"
// instead of only on "no crash".
struct SimCounters {
    uint32_t rtk_frames;         // NAV-PVT messages the receiver put on the wire
    uint32_t cfg_frames;         // UBX configuration frames it accepted from us
    uint32_t fc_frames_in;       // MAVLink frames the FC accepted from the board
    uint32_t setpoints_latched;  // ... of which were position targets
    uint32_t commands_taken;     // arm / mode / land commands the FC obeyed
    uint32_t led_commits;        // times the strip was latched
    uint32_t wdt_feeds;
    uint32_t buzzer_ms;
    uint16_t last_type_mask;     // what the FC decoded out of the last target
    float last_n;                // the same, in metres: the FC's copy of the point
    float last_e;
    float last_d;
    float pack_pct;              // the charge the simulated pack really holds
    uint32_t led_pixel[2];       // what the two pixels are physically showing
};
void halSimCounters(SimCounters& out);

// The state of the sky and the radio, which is all a test is allowed to touch.
// These change what the *receiver* reports, not what the firmware believes: a
// firmware that invented a fix would pass the same test that catches this one.
enum SimFix : uint8_t {
    SIM_FIX_NONE = 0,     // nothing locked: no position, no TIMEPULSE
    SIM_FIX_SINGLE,       // 3D, no correction data
    SIM_FIX_RTK_FLOAT,    // correction present, ambiguity not resolved
    SIM_FIX_RTK_FIXED,    // the fix a show is flown on
};
void halSimFix(SimFix f);
void halSimBatteryPct(float pct);   // swap the pack on the bench, so to speak
// Take the shorting connector out of the aircraft. This is the input every launch
// check is asked about first, and the only way to test that a firmware cannot be
// talked past it is to have a person's hand on the other end of the wire.
void halSimSafeSwitch(bool closed);
void halSimClear();                 // put the aircraft and the sky back
#endif

}  // namespace ranch
