// The only module that drives a contactor coil on this board.
//
// Everything else expresses a wish: the water controller asks for the pump, the
// lighting controller asks for the yard feeders, the protection logic says "shed"
// or "trip". This module turns those into coil states with the rules that only
// the output stage is allowed to enforce:
//
//  - a trip is a veto, not a request. While the lockout is latched no demand of
//    any kind can energise anything, and the shed timer cannot expire its way
//    back into a live fault.
//  - shed is sticky within a tick but not across one. The protection layer
//    re-evaluates every 100 ms; if it stops saying "shed" the pump may come back
//    without a reclose command, which is what a momentary overload deserves.
//  - the coil is confirmed against its own auxiliary. A contactor that hums and
//    never closes is a burnt coil or a jammed armature, and the ranch finds that
//    out from a counter rather than from dry troughs two hours later. Staying
//    closed when the coil is dropped is worse: it means a shed or a trip did
//    nothing, so both directions are checked and both latch.
//  - a phase that has collapsed is not a place to start a three-phase motor. The
//    demand is held, not dropped, so the pump starts by itself when the supply
//    returns.
#pragma once

#include <cstdint>

namespace ranch {

enum class Feeder : uint8_t { Pump, Lights };

struct FeederOutputs {
    bool pump;                   // coil state we commanded
    bool lights;
    bool pump_closed;            // auxiliary feedback, only meaningful when energised
    bool lights_closed;
    bool trip_latched;           // nothing may close until an explicit reclose
    bool shed;                   // the protection layer is currently refusing the pump
    bool held;                   // a demand is waiting on the trip/shed to clear
    bool contactor_fault;        // commanded, and the auxiliary never closed
    bool contactor_welded;       // commanded open, and the auxiliary never opened
    uint32_t pump_starts;
    uint32_t pump_failures;
    uint32_t light_starts;
    uint32_t shed_s;             // how long the pump has been shed, for the frame
    uint32_t lockout_s;          // remaining trip lockout
};

void feedersInit();

// Wishes. Level, not edge: the last tick wins, so a protection close sticks even
// while the programme is still asking for water.
void feedersDemand(Feeder which, bool on);

// The protection layer's two authorities.
void feedersShed(bool on);           // shed the non-critical feeder while held
void feedersTrip(bool on);           // latch: opens everything and stays open
void feedersSupplyOk(bool ok);       // a collapsed leg parks a pending start

// Explicit and the only way a latch clears. Also clears a confirmed contactor
// fault, because the coil is what gets replaced.
void feedersReclose();

// Writes the coils. Call once per control tick.
void feedersTick(uint32_t dt_ms);

const FeederOutputs& feedersGet();

// What the rest of the firmware should report as "the pump is running": commanded
// AND confirmed. A motor that is being fed a single phase is not running, and a
// dashboard that says otherwise is the reason nobody trusts dashboards.
bool feedersPumpRunning();
bool feedersLightsRunning();

}  // namespace ranch
