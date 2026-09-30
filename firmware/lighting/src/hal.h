// Hardware abstraction for the lighting board. Two implementations:
// hal_esp32.cpp drives contactors and a 0-10 V driver; hal_sim.cpp closes the
// loop against a sky and a lamp, so the firmware's lux and current readings come
// from what a day and a driver would actually do.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);
void consoleWrite(const char* data, size_t len);

// Outputs. The street and house circuits are contactors (on or off); the barn is
// a dimmer channel that takes 0..1 and owns the ramp between ticks.
void setRelayStreet(bool on);
void setRelayHouse(bool on);
void setDimDuty(float duty);
bool relayStreetIsOn();
bool relayHouseIsOn();
float dimDuty();

// Sensors.
bool motionSeen();                   // PIR input as it stands this instant
uint32_t motionAgeMs();              // since the last trigger
uint16_t analogMillivolts(uint8_t pin);
bool driverFaultContact();           // open = healthy, closed = the driver is unhappy

// Wall clock, same contract as the other boards: false when nothing has answered.
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

#if defined(RANCH_SIM)
void halSimPump();
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
void halSimMotion(bool present);
// Take the RTC away without losing the simulated time, so "no clock" can be
// tested on a board that had one. A dead coin cell looks exactly like this.
void halSimClock(bool enabled);
void halSimDriverFault(bool fault);

// What the simulated sky and lamp did, so the host test can assert that a
// commanded lamp actually drew current, and that nothing lit at midday.
struct SkyCounters {
    float lux;                  // sky brightness the sensor saw
    float max_duty;             // brightest the barn driver was ever asked for
    float lamp_amp;             // current the bus drew at the last sample
    uint32_t street_on_frames;
    uint32_t house_on_frames;
    uint32_t barn_lit_frames;
    uint32_t lamp_blown_events;
    uint32_t midday_lit_events; // a lamp on while the sky said daylight
    bool driver_fault;
    bool motion;
};
void halSky(SkyCounters& out);
#endif

}  // namespace ranch
