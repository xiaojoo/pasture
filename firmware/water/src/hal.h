// Hardware abstraction for the water board. Two implementations:
// hal_esp32.cpp drives the real valves, and hal_sim.cpp closes the loop against
// a hydraulic model of the ranch plumbing, so the firmware's own flow and
// pressure readings come from the plant rather than from what the firmware
// hoped the plant would do.
//
// Nothing here allocates, and every call is safe from any task.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);

// Console sink for telemetry: UART0 on the aircraft, stdout in the host test.
void consoleWrite(const char* data, size_t len);

// Outputs. `duty` is 0..1: full for the inrush window, the holding duty after
// it. The HAL owns the PWM so no caller can set a coil current twice.
void setValvePin(uint8_t pin, float duty);
void setPump(bool on);

// Inputs. The pulse counters are free running and wrap; the caller differences
// them and must handle the wrap (meter.cpp does).
uint32_t flowPulsesBarn();
uint32_t flowPulsesHouse();
bool leakWet();                         // true = water where water should not be
bool tankHigh();                        // tank level switch made

// Analog. The HAL converts the ADC to millivolts and nothing more: the
// engineering units, the filter windows and the out-of-range tests are policy
// about the plumbing, and they live in sense.cpp.
uint16_t analogMillivolts(uint8_t pin);

// Wall clock. Returns false when no time source is valid, which keeps every
// scheduled dispense from firing on a 1970 default.
bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day);
void clockSet(uint16_t year, uint8_t month, uint8_t day,
              uint8_t hour, uint8_t minute, uint8_t second);
bool clockWasSet();                     // a battery-backed RTC that kept time

// Non-volatile parameters, and the totalisers that have to survive a reboot.
bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

// Network.
bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

void feedWatchdog();
void setStatusLed(bool on);
const char* resetReason();

#if defined(RANCH_SIM)
// Advance the simulated plant by the real (browser) or virtual (host test) clock.
void halSimPump();
// The plant's wall clock. There is no RTC on a simulation, and a board with no
// clock never dispenses on schedule, so the test harness has to be able to give
// it one.
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
// Take the clock away without losing the time, so "no schedule without a clock"
// can be tested on a board that had one. A dead coin cell looks like this.
void halSimClock(bool enabled);

// What the simulated plant actually did, so the host test can assert on water
// moved and valves stuck instead of only on "no crash".
struct PlantCounters {
    float barn_litres;
    float house_litres;
    float leaked_litres;
    uint32_t pump_starts;
    uint32_t dry_run_events;
    uint32_t stuck_valve_events;
    uint32_t tank_refills;
    bool leak_active;
    bool tank_high_active;
    float pressure_bar;
    float tank_pct;
    float barn_flow_lmin;      // what is actually moving, for the metering test
    float house_flow_lmin;
};
void halPlant(PlantCounters& out);
void halPlantLeak(bool on);            // fault injection for the counter-proof
void halPlantSupplyLost(bool on);      // mains outage: pump runs, nothing moves
#endif

}  // namespace ranch
