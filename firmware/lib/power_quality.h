// Switchboard protection and load-shedding decisions. Platform neutral and side
// effect free, like the aircraft's failsafe table: measurements go in, one
// action plus one reason comes out, and the caller decides which contactor that
// opens.
//
// Order matters and is deliberate:
//   leakage > over-temperature > frequency > breaker > phase loss > overload > unbalance > voltage
// A live-earth leakage trips even when everything else is perfect, because the
// thing that rule protects is a person leaning on a water trough.
#pragma once

#include <cstdint>

#include "power_meter.h"

namespace ranch {

enum class GridAction : uint8_t { Normal, Alarm, ShedLoad, Trip };

inline const char* gridActionName(GridAction a) {
    switch (a) {
        case GridAction::Normal:   return "NORMAL";
        case GridAction::Alarm:    return "ALARM";
        case GridAction::ShedLoad: return "SHED";
        case GridAction::Trip:     return "TRIP";
    }
    return "?";
}

struct GridLimits {
    float under_v_pct;             // per cent of nominal phase-to-neutral
    float over_v_pct;
    float hz_lo;
    float hz_hi;
    float shed_load_pct;           // sustained load that sheds non-critical feeders
    float trip_load_pct;           // sustained load that trips the board
    float unbalance_max_pct;
    float rcd_alarm_ma;
    float rcd_trip_ma;
    float temp_alarm_c;
    float temp_trip_c;
    float debounce_s;              // every rule must persist this long before acting
    float reclose_lockout_s;       // how long a trip keeps the feeders open
};

constexpr GridLimits GRID_DEFAULTS{
    /*under_v_pct*/   0.86f,     // -14 %: the supply tolerance, not a brownout probe
    /*over_v_pct*/    1.10f,
    /*hz_lo*/         47.5f,     // GB/IEC supply tolerance
    /*hz_hi*/         52.5f,
    /*shed_load_pct*/ 90.0f,
    /*trip_load_pct*/ 115.0f,
    /*unbalance_max*/  15.0f,
    /*rcd_alarm_ma*/  30.0f,
    /*rcd_trip_ma*/   100.0f,
    /*temp_alarm_c*/  55.0f,
    /*temp_trip_c*/   75.0f,
    /*debounce_s*/     2.0f,
    /*lockout_s*/    300.0f,
};

struct GridInput {
    PowerRead read;
    float nominal_v;        // the phase-to-neutral this board was set for
    float rcd_ma;
    float temp_c;
    bool breaker_closed;    // auxiliary contact on the main breaker
    bool pump_running;      // the biggest non-critical load on this board
    bool sensor_fault;      // an analog channel outside its valid range
};

struct GridState {
    float under_s, over_s, freq_s, phase_s, shed_s, overload_s;
    float unb_s, rcd_s, temp_s;
    float lockout_s;
};

struct GridDecision {
    GridAction action;
    bool open_pump;         // shed the water pump first: no animal dies of a dry hour
    bool lockout;           // feeders stay open; nothing may reclose automatically
    char reason[20];
};

inline void gridReset(GridState& s) {
    s.under_s = s.over_s = s.freq_s = s.phase_s = 0.0f;
    s.shed_s = s.overload_s = s.unb_s = s.rcd_s = s.temp_s = 0.0f;
    s.lockout_s = 0.0f;
}

inline void gridReason(char (&dst)[20], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 19; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Compared per leg, because a board that averages the three phases calls a lost
// phase "slightly low".
inline bool anyLegBelow(const PowerRead& r, float lo) {
    for (int i = 0; i < 3; ++i) {
        if (r.volts[i] < lo) return true;
    }
    return false;
}

inline bool anyLegAbove(const PowerRead& r, float hi) {
    for (int i = 0; i < 3; ++i) {
        if (r.volts[i] > hi) return true;
    }
    return false;
}

inline GridDecision gridEvaluate(const GridLimits& lim, GridState& st,
                                 const GridInput& in, float dt_s) {
    GridDecision out{GridAction::Normal, false, false, ""};
    const float v_lo = lim.under_v_pct * in.nominal_v;
    const float v_hi = lim.over_v_pct * in.nominal_v;
    // Nothing is being measured: an unplug is not a brownout, and shedding the
    // pump because an ADC came loose would be the board's fault, not the grid's.
    const bool live = in.read.kva > 0.0f || in.read.hz > 10.0f;

    st.under_s = (live && anyLegBelow(in.read, v_lo)) ? st.under_s + dt_s : 0.0f;
    st.over_s = (live && anyLegAbove(in.read, v_hi)) ? st.over_s + dt_s : 0.0f;
    const bool freq = in.read.hz > 1.0f && (in.read.hz < lim.hz_lo || in.read.hz > lim.hz_hi);
    st.freq_s = freq ? st.freq_s + dt_s : 0.0f;
    st.phase_s = in.read.phase_loss ? st.phase_s + dt_s : 0.0f;
    st.shed_s = in.read.load_pct >= lim.shed_load_pct ? st.shed_s + dt_s : 0.0f;
    st.overload_s = in.read.load_pct >= lim.trip_load_pct ? st.overload_s + dt_s : 0.0f;
    st.unb_s = in.read.unbalance_pct > lim.unbalance_max_pct ? st.unb_s + dt_s : 0.0f;
    st.rcd_s = in.rcd_ma > lim.rcd_alarm_ma ? st.rcd_s + dt_s : 0.0f;
    st.temp_s = in.temp_c > lim.temp_alarm_c ? st.temp_s + dt_s : 0.0f;
    if (st.lockout_s > 0.0f) st.lockout_s -= dt_s;

    if (in.sensor_fault) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "SENSOR FAULT");
        return out;
    }

    // --- instantaneous, no debounce: these two are already fault currents -----
    if (in.rcd_ma >= lim.rcd_trip_ma) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "LEAKAGE");
        return out;
    }
    if (in.temp_c >= lim.temp_trip_c) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "OVERTEMP");
        return out;
    }
    if (st.freq_s >= lim.debounce_s) {
        out.action = GridAction::Trip;
        out.lockout = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "FREQUENCY");
        return out;
    }
    // The main breaker open while we still think we are serving loads is the one
    // condition that must never auto-reclose, lockout timer or not.
    if (!in.breaker_closed) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        gridReason(out.reason, "BREAKER OPEN");
        return out;
    }

    if (st.phase_s >= lim.debounce_s) {
        // A single-phase motor on a collapsed leg will not start and will overheat;
        // taking the pump off is cheaper than replacing it.
        out.action = GridAction::ShedLoad;
        out.open_pump = true;
        gridReason(out.reason, "PHASE LOSS");
        return out;
    }
    if (st.overload_s >= lim.debounce_s) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "OVERLOAD");
        return out;
    }
    if (st.shed_s >= lim.debounce_s) {
        out.action = GridAction::ShedLoad;
        out.open_pump = true;
        gridReason(out.reason, "OVERLOAD");
        return out;
    }
    if (st.under_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "UNDERVOLTAGE");
        return out;
    }
    if (st.over_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "OVERVOLTAGE");
        return out;
    }
    if (st.unb_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "UNBALANCE");
        return out;
    }
    if (st.rcd_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "LEAKAGE WARN");
        return out;
    }
    if (st.temp_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "OVERTEMP WARN");
        return out;
    }

    if (st.lockout_s > 0.0f) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        gridReason(out.reason, "LOCKED OUT");
    }
    return out;
}

}  // namespace ranch
