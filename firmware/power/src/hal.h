// Hardware abstraction for the switchboard monitor. Two implementations:
// hal_esp32.cpp reads the real front ends, hal_sim.cpp closes the loop against a
// model of the ranch's supply and loads, so the board's volts, amps and
// frequency come from a grid that sags when the pump starts rather than from the
// numbers the firmware expected to see.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);
void consoleWrite(const char* data, size_t len);

// Rectified RMS front ends: the ADC reading is a DC quantity proportional to the
// channel's RMS value, which is why the frequency has to arrive on a digital
// input from the comparator instead.
uint16_t analogMillivolts(uint8_t pin);

// Transitions on phase A's comparator, as a time span rather than a count.
// Drains whatever accumulated since the last call into the interval between the
// first and last transition and the number of intervals that span covers.
//
// Period measurement rather than counting, because counting over a fixed window is
// wrong by up to half a transition: at 47.2 Hz a one second window reports 47.00
// or 47.50 on alternate samples, which straddles a 47.5 Hz threshold and resets
// the under-frequency debounce every other tick. That never trips.
void zeroCrossSpan(uint32_t& microseconds, uint16_t& intervals);

// External ADC (ADS1115) for the two channels that do not fit in ADC1. Returns
// false when the bus did not answer, which the meter turns into a sensor fault
// instead of a zero.
bool extAdcMV(uint8_t channel, float& millivolts);

// Contactors.
void setPump(bool on);
void setLights(bool on);
bool pumpFeedbackClosed();        // the contactor's own auxiliary
bool breakerClosed();             // main breaker auxiliary
bool pumpPermitClosed();          // the local hand/off/auto switch
bool lightsPermitClosed();

// Wall clock: today's energy resets on a local midnight, and only an RTC knows
// what that was after a power cut.
bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day);
void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second);
bool clockWasSet();

bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

void feedWatchdog();
const char* resetReason();
void setStatusLed(bool on);

#if defined(RANCH_SIM)
void halSimPump();
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
void halSimClock(bool enabled);

// Faults the plant can be told about, so a protection rule can be shown to
// actually react rather than only being able to report "all well".
enum SimFault : uint8_t {
    SIM_FAULT_NONE = 0,
    SIM_FAULT_PHASE_B,       // one leg collapses while the others stay up
    SIM_FAULT_RCD,           // leakage current climbs past the trip level
    SIM_FAULT_OVERTEMP,      // cabinet fan failed
    SIM_FAULT_BREAKER_OPEN,  // main breaker opened by hand
    SIM_FAULT_FREQ_LOW,      // a generator-sized supply drooping
    SIM_FAULT_EXT_ADC,       // the I2C ADC stops answering
    SIM_FAULT_SENSE_A,       // phase A's voltage sense comes off, its load keeps drawing
};
void halSimFault(SimFault which, bool on);
void halSimDemandScale(float scale);      // multiply the site's load curve
void halSimPermit(bool pump_line, bool closed);   // the local hand/off/auto switches

// What the simulated switchboard actually did, so the test can assert on the
// difference between "the board believed it was fine" and "nothing was energised".
struct GridCounters {
    float kw;
    float kva;
    float hz;
    float volts[3];
    float amps[3];
    float rcd_ma;
    float temp_c;
    bool breaker_closed;
    bool pump_energised;
    bool lights_energised;
    uint32_t pump_starts;
    uint32_t brownouts;       // times the pump was asked to run into a dead leg
};
void halGrid(GridCounters& out);
#endif

}  // namespace ranch
