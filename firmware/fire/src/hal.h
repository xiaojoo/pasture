// Hardware abstraction for the fire panel. Two implementations: hal_esp32.cpp
// reads the real loops and drives the real relays, hal_sim.cpp models the
// detectors, the bell and the wiring faults, so an alarm arrives through a relay
// that takes time to pull in and a loop that can be cut, shorted or left dirty.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);
void consoleWrite(const char* data, size_t len);

// A supervised loop presented as milliVolts across the 4k7 pull-up divider.
uint16_t loopMillivolts(uint8_t pin);

// Outputs.
void setSiren(bool on);
void setStrobe(bool on);
void setPumpPermit(bool on);
bool pumpPermitIsClosed();

// Feedback and the panel's own controls.
bool sirenFeedbackClosed();     // the armature relay's auxiliary: the bell moved
bool keyArmed();                // false = BYPASS
bool silencePressed();
bool testPressed();

// Wall clock: a fire log with no timestamps is not evidence, and the RTC is the
// only thing that knows what time it was after the supply went down.
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

// Which of the six loops a test is allowed to talk about. Index 0..3 are the
// detector zones, 4 the call point, 5 the flow switch.
enum SimLoop : uint8_t {
    SIM_LOOP_HOUSE = 0, SIM_LOOP_BARN, SIM_LOOP_STORE, SIM_LOOP_POWER,
    SIM_LOOP_MCP, SIM_LOOP_FLOW,
};
// What to present on that loop. The point is that the firmware sees milliVolts, so
// a "smoke" here is a relay that takes milliseconds to close, and a "cut wire" is a
// loop that floats rather than a boolean the test got to pick.
enum SimLoopFault : uint8_t {
    SIM_LOOP_CLEAR = 0,     // end of line in circuit
    SIM_LOOP_SMOKE,         // detector relay closes the 1k alarm path
    SIM_LOOP_CUT,           // the loop floats high
    SIM_LOOP_SHORT,         // 0 ohm: crushed cable, terminal screwed to the rail
    SIM_LOOP_DIRTY,         // corrosion mid-band: no honest reading exists
    SIM_LOOP_RELAY_CHATTER, // the relay buzzes at the threshold
};
void halSimLoop(uint8_t which, SimLoopFault fault);
void halSimBellDead(bool dead);        // coil intact, armature relay never pulls in
void halSimPumpRan(bool running);      // the pump's own contactor, for the frame
void halSimKey(bool armed);
void halSimButton(bool silence, bool test);
// Put the *building* back the way it was. Each test phase is a new board on the same
// ranch, and a board reboot does not un-cut a wire: without this, a fault injected by
// one phase is silently inherited by the next, which is how a dead bell ended up in
// a bypass test.
void halSimClear();

struct PanelCounters {
    uint32_t siren_on_ms;
    uint32_t strobe_on_ms;
    uint32_t pump_permit_ms;
    uint32_t bell_attempts;      // times the coil was energised
    uint32_t bell_failures;      // ... and the auxiliary did not follow
    uint32_t smoke_events;
    uint32_t false_alarm_starts; // a zone alarmed and cleared with no second opinion
    bool pump_was_permitted;
};
void halPanel(PanelCounters& out);
#endif

}  // namespace ranch
