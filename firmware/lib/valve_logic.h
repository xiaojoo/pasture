// Solenoid actuator sequencer. Platform neutral: it takes elapsed time and
// observations and returns what the outputs should be, so the whole of
// "inrush, then hold, then refuse to hold forever" is testable without a coil.
//
// A 24 V irrigation solenoid pulls several times its holding current for as long
// as it takes the armature to travel. Driving it at full duty afterwards does not
// keep it more open - it just heats the coil and the MOSFET, and on a solar site
// it is the reason the board's outputs brown out on the second valve.
#pragma once

#include <cstdint>

namespace ranch {

enum class ValveState : uint8_t {
    Closed,      // de-energised
    Inrush,      // full duty, waiting for the armature
    Hold,        // reduced duty, open
    Closing,     // commanded shut, still counted as open until the pulse settles
    Fault,       // stuck or no flow: refuses to re-open until cleared
};

struct ValveLimits {
    uint16_t inrush_ms;         // full-duty window
    float hold_duty;            // 0..1 after the window
    float max_open_s;           // a programme may not hold a valve open forever
    float no_flow_s;            // open with no flow for this long is a fault
    float flow_seen_lmin;       // flow above this counts as "water is moving"
};

constexpr ValveLimits VALVE_DEFAULTS{
    /*inrush_ms*/      120,
    /*hold_duty*/      0.55f,
    /*max_open_s*/     900.0f,
    /*no_flow_s*/      6.0f,
    /*flow_seen_lmin*/ 0.12f,
};

struct Valve {
    ValveState state;
    uint32_t since_ms;          // time in the current state
    float open_seconds;         // total time at full or hold duty this cycle
    float no_flow_seconds;      // time open with nothing moving
    uint32_t cycles;
    uint32_t faults;
    float last_litres;          // moved by the most recent cycle
    float cycle_litres;         // accumulating for the cycle in progress
};

inline void valveInit(Valve& v) {
    v.state = ValveState::Closed;
    v.since_ms = 0;
    v.open_seconds = 0.0f;
    v.no_flow_seconds = 0.0f;
    v.cycles = 0;
    v.faults = 0;
    v.last_litres = 0.0f;
    v.cycle_litres = 0.0f;
}

inline bool valveIsOpenCommand(const Valve& v) {
    return v.state == ValveState::Inrush || v.state == ValveState::Hold;
}

inline float valveDuty(const Valve& v, const ValveLimits& lim) {
    switch (v.state) {
        case ValveState::Inrush: return 1.0f;
        case ValveState::Hold:   return lim.hold_duty;
        default:                 return 0.0f;
    }
}

// Open if the line is allowed to run. Returns false when the valve is latched in
// Fault, which is the point: a stuck valve must not be re-driven every cycle in
// the hope that it frees off.
inline bool valveOpen(Valve& v) {
    if (v.state == ValveState::Fault) return false;
    if (v.state == ValveState::Closed) {
        v.state = ValveState::Inrush;
        v.since_ms = 0;
        v.open_seconds = 0.0f;
        v.no_flow_seconds = 0.0f;
        v.cycle_litres = 0.0f;
        v.cycles++;
    }
    return true;
}

inline void valveClose(Valve& v) {
    if (v.state == ValveState::Fault) return;
    if (v.state != ValveState::Closed) {
        v.state = ValveState::Closing;
        v.since_ms = 0;
        v.last_litres = v.cycle_litres;
    }
}

// Clearing a fault is explicit and only ever comes from an operator command or a
// verified sensor recovery - never from the sequencer deciding it feels better.
inline void valveClearFault(Valve& v) {
    if (v.state == ValveState::Fault) valveInit(v);
}

// One control tick. `flow_lmin` is this line's own meter, so two lines on one
// board cannot hide each other's fault: the house line running while the barn
// valve is open is exactly what this signature makes visible.
inline void valveStep(Valve& v, const ValveLimits& lim, uint32_t dt_ms, float flow_lmin) {
    v.since_ms += dt_ms;
    const float dt_s = dt_ms / 1000.0f;

    switch (v.state) {
        case ValveState::Inrush:
            v.open_seconds += dt_s;
            v.cycle_litres += flow_lmin * dt_s / 60.0f;
            if (v.since_ms >= lim.inrush_ms) {
                v.state = ValveState::Hold;
                v.since_ms = 0;
            }
            break;

        case ValveState::Hold:
            v.open_seconds += dt_s;
            v.cycle_litres += flow_lmin * dt_s / 60.0f;
            if (flow_lmin > lim.flow_seen_lmin) {
                v.no_flow_seconds = 0.0f;
            } else {
                v.no_flow_seconds += dt_s;
            }
            if (v.no_flow_seconds >= lim.no_flow_s || v.open_seconds >= lim.max_open_s) {
                v.state = ValveState::Fault;
                v.faults++;
                v.last_litres = v.cycle_litres;
            }
            break;

        case ValveState::Closing:
            // The meter keeps ticking after the coil drops: a 25 mm valve takes
            // a few hundred milliseconds to shut, and the water already in
            // motion finishes the cycle. Counting it keeps the totaliser honest.
            v.cycle_litres += flow_lmin * dt_s / 60.0f;
            if (flow_lmin <= lim.flow_seen_lmin || v.since_ms > 1500) {
                v.last_litres = v.cycle_litres;
                v.state = ValveState::Closed;
                v.since_ms = 0;
            }
            break;

        case ValveState::Closed:
        case ValveState::Fault:
        default:
            break;
    }
}

inline const char* valveStateName(ValveState s) {
    switch (s) {
        case ValveState::Closed:  return "closed";
        case ValveState::Inrush:  return "inrush";
        case ValveState::Hold:    return "hold";
        case ValveState::Closing: return "closing";
        case ValveState::Fault:   return "fault";
    }
    return "?";
}

}  // namespace ranch
