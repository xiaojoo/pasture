// Water board decision table. Platform neutral and side-effect free: it turns
// pressures, flows and switch states into one action plus one reason, and the
// caller decides what that action means on its own plumbing.
//
// Priority is fixed, because a pump about to destroy itself must not be outranked
// by a damp floor sensor that has not debounced yet:
//   close-all > stop-pump > close-one-line > refuse-new-cycles > run
#pragma once

#include <cstdint>

namespace ranch {

enum class WaterAction : uint8_t { Run, Refill, Hold, CloseBarn, CloseHouse, CloseAll };

inline const char* waterActionName(WaterAction a) {
    switch (a) {
        case WaterAction::Run:        return "RUN";
        case WaterAction::Refill:     return "REFILL";
        case WaterAction::Hold:       return "HOLD";
        case WaterAction::CloseBarn:  return "CLOSE BARN";
        case WaterAction::CloseHouse: return "CLOSE HOUSE";
        case WaterAction::CloseAll:   return "CLOSE ALL";
    }
    return "?";
}

struct PlantLimits {
    float dry_run_bar;         // pump running below this is running dry
    float dry_run_s;           // for this long before it counts
    float over_pressure_bar;   // burst risk
    float leak_flow_lmin;      // any flow at all with every valve shut
    float leak_s;              // sustained for this long
    float tank_refill_start;   // per cent
    float tank_refill_stop;    // per cent, with the high switch as the hard stop
};

constexpr PlantLimits PLANT_DEFAULTS{
    /*dry_run_bar*/     0.30f,
    /*dry_run_s*/       2.0f,
    /*over_pressure*/   8.0f,
    /*leak_flow_lmin*/  0.35f,
    /*leak_s*/          3.0f,
    /*tank_refill_start*/ 22.0f,
    /*tank_refill_stop*/  96.0f,
};

struct PlantInput {
    float pressure_bar;
    float tank_pct;
    bool tank_high;             // the float switch made: hard stop, not a percentage
    bool leak;                  // rope sensor
    float barn_flow;            // this line's own meter, L/min
    float house_flow;
    bool barn_open;
    bool house_open;
    bool pump_on;
    bool valve_barn_fault;      // the sequencer already latched a stuck valve
    bool valve_house_fault;
    bool clock_valid;           // no scheduled dispense without a wall clock
};

struct PlantState {
    float dry_run_s;            // accumulators, so a momentary sag does not
    float leak_s;               // trip a shutdown the way a real operator would not
    float stall_s;
};

struct PlantDecision {
    WaterAction action;
    bool allow_dispense;        // the programme may start a new cycle
    bool want_pump;             // the pressuriser should be energised
    bool pump_interlocked;
    // A veto, not a request: when this is false the pump stays off even if a
    // valve is still open. Without it "stop the pump" is a suggestion the output
    // stage overrules, and a dry borehole or a full tank never gets the pump it
    // is supposed to get.
    char reason[20];
};

inline void plantReset(PlantState& s) {
    s.dry_run_s = 0.0f;
    s.leak_s = 0.0f;
    s.stall_s = 0.0f;
}

inline void copyText(char (&dst)[20], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 19; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline PlantDecision plantEvaluate(const PlantLimits& lim, PlantState& st,
                                   const PlantInput& in, float dt_s) {
    PlantDecision out{WaterAction::Run, true, in.pump_on, true, ""};

    // --- sensors that are not reporting yet must not read as "safe" -----------
    // A tank percentage of 0 with no pump running is either empty or disconnected;
    // the caller passes -1 when the channel has not produced a sample.
    const bool tank_known = in.tank_pct >= 0.0f;

    st.dry_run_s = (in.pump_on && in.pressure_bar < lim.dry_run_bar) ? st.dry_run_s + dt_s : 0.0f;
    const float closed_flow = (in.barn_open || in.house_open) ? 0.0f
                              : (in.barn_flow + in.house_flow);
    st.leak_s = (closed_flow > lim.leak_flow_lmin || in.leak) ? st.leak_s + dt_s : 0.0f;

    // --- hard stops ------------------------------------------------------------
    if (in.leak && (in.barn_open || in.house_open)) {
        out.action = WaterAction::CloseAll;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, "LEAK");
        return out;
    }
    if (st.leak_s >= lim.leak_s) {
        // Either the meter moves with nothing open, or the rope has been wet for
        // seconds. Both mean water is going somewhere it should not.
        out.action = WaterAction::CloseAll;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, in.leak ? "LEAK" : "LEAK METER");
        return out;
    }
    if (in.pressure_bar > lim.over_pressure_bar) {
        out.action = WaterAction::CloseAll;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, "OVERPRESSURE");
        return out;
    }
    if (st.dry_run_s >= lim.dry_run_s) {
        // Stop the pump, leave the lines alone: an empty tank is not a reason to
        // close a valve that the house is currently using.
        out.action = WaterAction::Hold;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, "DRY RUN");
        return out;
    }

    // --- one line at a time -----------------------------------------------------
    if (in.valve_barn_fault) {
        out.action = WaterAction::CloseBarn;
        out.allow_dispense = false;
        copyText(out.reason, "BARN STUCK");
        return out;
    }
    if (in.valve_house_fault) {
        out.action = WaterAction::CloseHouse;
        copyText(out.reason, "HOUSE STUCK");
        return out;
    }

    // --- refill -----------------------------------------------------------------
    if (in.tank_high) {
        // The float switch is the hard stop. Without the interlock a line that is
        // still open would keep asking for the pump and the tank would overflow.
        out.want_pump = false;
        out.pump_interlocked = false;         // the float switch outranks the percentage
        copyText(out.reason, "TANK FULL");
    } else if (tank_known && in.tank_pct <= lim.tank_refill_start) {
        out.action = WaterAction::Refill;
        out.want_pump = true;
    } else if (tank_known && in.tank_pct >= lim.tank_refill_stop) {
        out.want_pump = false;
    }

    // --- no scheduled water without a clock ------------------------------------
    if (!in.clock_valid) {
        out.allow_dispense = false;    // the house line stays available
        if (*out.reason == '\0') copyText(out.reason, "NO CLOCK");
    }

    return out;
}

}  // namespace ranch
