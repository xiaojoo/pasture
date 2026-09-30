// The only module that touches the valve and pump outputs. Everything else asks
// for water; this decides how the coil actually gets energised, in what order,
// and when it refuses.
//
// Sequencing that lives here and nowhere else:
//  - the pressuriser comes up first and stays up a moment after the valve drops,
//    because a solenoid opening against zero pressure hammers its seat
//  - two lines on one board never share a pump request: the demand is a count of
//    open lines plus the refill flag, so closing the barn cannot switch the pump
//    out from under the house line that is still drawing
//  - a latched valve fault is not re-driven by the next cycle
#pragma once

#include <cstdint>

#include "sense.h"
#include "valve_logic.h"

namespace ranch {

enum class WaterLine : uint8_t { Barn, House };

struct Outputs {
    bool barn_open;
    bool house_open;
    bool pump_on;
    ValveState barn_state;
    ValveState house_state;
    float barn_duty;
    float house_duty;
    uint32_t barn_cycles;
    uint32_t house_cycles;
    uint32_t barn_faults;
    uint32_t house_faults;
    float barn_open_s;
    float house_open_s;
    uint32_t pump_starts;
};

void valvesInit();

// The sequencer limits come from NVS at boot. Without this the tuned values are
// read, printed, and then thrown away.
void valvesSetLimits(const ValveLimits& lim);

// Level requests, not edges: the programme and the safety layer may each say "I
// want this line open" and the last tick wins, which is what makes a safety close
// stick even while the programme still thinks it is running.
void valvesDemand(WaterLine line, bool open);
void valvesPumpDemand(bool needed);

// Hard veto from the safety layer: false keeps the pump off even while a valve
// is still open. A line finishing its cycle is not a reason to run the pump dry.
void valvesPumpAllowed(bool allowed);

// Advances the sequencers and writes the outputs. Call once per control tick
// with the measurement for the same tick.
void valvesTick(uint32_t dt_ms, const SenseState& sense);

const Outputs& valvesGet();

// Explicit, and the only way a stuck valve comes back.
void valvesClearFault(WaterLine line);

// The house line is allowed to run on a command while the barn programme is
// suspended; this says whether any line is drawing right now.
bool valvesAnyOpen();

}  // namespace ranch
