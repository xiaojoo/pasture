// The only module that moves a lamp. The policy in firmware/lib says what level
// each circuit should be at; this decides how fast it may get there, checks that
// the lamp drew the current it was supposed to, and keeps the burn hours that
// maintenance asks for.
//
// Two mechanisms that look like one and are not:
//  - the dusk ramp is a *schedule* decision (the policy), measured in minutes
//  - the slew limit here is a *hardware* limit, measured in percent per second,
//    and it is what stops a command from the app stepping a cold high bay from
//    dark to full in one tick
#pragma once

#include <cstdint>

namespace ranch {

enum LampCircuit : uint8_t { LAMP_STREET = 0, LAMP_HOUSE = 1, LAMP_BARN = 2, LAMP_COUNT = 3 };

struct LampFault {
    bool barn_dead;        // commanded bright, drawing nothing: lamp or driver gone
    bool barn_over;        // drawing more than the bus is rated for
    bool driver_contact;   // the driver's own fault relay
    bool latched;          // over-current cut the output and stays cut until cleared
};

struct LampState {
    bool street;
    bool house;
    float duty_target;
    float duty_now;
    float amps;
    float lux;
    bool lux_valid;
    LampFault fault;
    uint32_t seconds_to_full;   // ramp distance the slew limiter is working through
};

void lightsInit();

// Requested states from the policy. Level, not edge: calling this with the same
// values every tick is the normal case.
void lightsRequest(bool street, bool house, float barn_duty);

void lightsTick(uint32_t dt_ms);

const LampState& lightsGet();

// Cumulative energised time, in tenths of a second so a minute of bookkeeping
// does not need a float. Persisted by main.cpp on a minute boundary.
uint32_t lightsBurnTenths(LampCircuit which);
void lightsSetBurnTenths(LampCircuit which, uint32_t tenths);

// The over-current latch is the only thing that can be cleared; a dead lamp is a
// maintenance finding, not something the board should keep retrying into a fire.
void lightsClearLatch();

}  // namespace ranch
