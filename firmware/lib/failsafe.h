// Failsafe decision table. Platform neutral and deliberately side-effect free:
// it turns ages and sensor quality into one action plus one reason, and the
// caller decides what that action means on its own airframe.
//
// Priority is fixed, because a battery that is nearly empty must not be
// outranked by a glitched radio timer:
//   terminate > rtl > hover > continue
#pragma once

#include <cstdint>

namespace ranch {

enum class Action : uint8_t { Continue, Hover, Rtl, LandNow, Terminate };

inline const char* actionName(Action a) {
    switch (a) {
        case Action::Continue:  return "CONTINUE";
        case Action::Hover:     return "HOVER";
        case Action::Rtl:       return "RTL";
        case Action::LandNow:   return "LAND";
        case Action::Terminate: return "KILL";
    }
    return "?";
}

struct FailsafeConfig {
    uint32_t rc_timeout_ms;         // stick input staleness that counts as loss
    uint32_t gcs_timeout_ms;        // MAVLink heartbeat loss from the ground
    uint32_t link_debounce_ms;      // how long a loss must persist to act
    uint32_t geofence_debounce_ms;  // breaches are debounced: GPS can jump
    uint8_t  min_fix_type;          // 3 = 3D fix required to keep flying
    uint8_t  min_satellites;
    float    max_hdop;
    uint8_t  battery_rtl_pct;
    uint8_t  battery_land_pct;
    uint8_t  battery_kill_pct;
    float    cell_v_min;            // sag below this is a hard land, not RTL
};

constexpr FailsafeConfig FAILSAFE_DEFAULTS{
    /*rc_timeout*/       250,
    /*gcs_timeout*/      1500,
    /*link_debounce*/    400,
    /*geofence_debounce*/3000,
    /*min_fix*/          3,
    /*min_sats*/         8,
    /*max_hdop*/         2.2f,
    /*batt_rtl*/         25,
    /*batt_land*/        12,
    /*batt_kill*/        5,
    /*cell_v_min*/       3.30f,
};

struct FailsafeInput {
    uint32_t rc_age_ms;
    uint32_t gcs_age_ms;
    bool     in_flight;
    bool     geofence_breach;
    uint8_t  fix_type;
    uint8_t  satellites;
    float    hdop;
    uint8_t  battery_pct;
    float    cell_v_min;
    bool     baro_ok;
    bool     imu_ok;
    bool     gps_ok;
};

struct FailsafeDecision {
    Action action;
    char   reason[24];
    bool   armed_block;    // pre-flight: refuse to arm at all
};

// Per-source debounce state. Held by the caller so the module stays pure and a
// single instance can serve several vehicles in a test harness.
struct FailsafeState {
    uint32_t rc_bad_ms;
    uint32_t gcs_bad_ms;
    uint32_t fence_bad_ms;
    uint32_t gnss_bad_ms;
};

inline void failsafeReset(FailsafeState& s) {
    s.rc_bad_ms = s.gcs_bad_ms = s.fence_bad_ms = s.gnss_bad_ms = 0;
}

inline void copyReason(char (&dst)[24], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 23; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline FailsafeDecision failsafeEvaluate(const FailsafeConfig& cfg, FailsafeState& st,
                                         const FailsafeInput& in, uint32_t dt_ms) {
    FailsafeDecision out{Action::Continue, "", false};

    // Anything the sensors have not reported yet arrives as a sentinel: a
    // percentage above 100, or a cell voltage of exactly zero. Neither may be
    // read as "empty", or a bench rig with no battery would never arm and a
    // cold-start aircraft would declare a critical fault on the first tick.
    const bool batt_known = in.battery_pct <= 100;

    st.rc_bad_ms   = in.rc_age_ms   > cfg.rc_timeout_ms   ? st.rc_bad_ms + dt_ms   : 0;
    st.gcs_bad_ms  = in.gcs_age_ms  > cfg.gcs_timeout_ms  ? st.gcs_bad_ms + dt_ms  : 0;
    st.fence_bad_ms = in.geofence_breach ? st.fence_bad_ms + dt_ms : 0;
    const bool gnss_bad = !in.gps_ok || in.fix_type < cfg.min_fix_type ||
                          in.satellites < cfg.min_satellites || in.hdop > cfg.max_hdop;
    st.gnss_bad_ms = gnss_bad ? st.gnss_bad_ms + dt_ms : 0;

    // --- hard stops, in flight and on the ground -----------------------------
    if (!in.imu_ok) {
        out.action = Action::Terminate;
        copyReason(out.reason, "IMU FAULT");
        out.armed_block = true;
        return out;
    }
    if (in.cell_v_min > 0.0f && in.cell_v_min < cfg.cell_v_min) {
        out.action = in.in_flight ? Action::LandNow : Action::Continue;
        copyReason(out.reason, "CELL SAG");
        out.armed_block = !in.in_flight;
        return out;
    }
    if (batt_known && in.battery_pct <= cfg.battery_kill_pct) {
        out.action = in.in_flight ? Action::LandNow : Action::Continue;
        copyReason(out.reason, "BATT CRITICAL");
        out.armed_block = !in.in_flight;
        return out;
    }

    // --- pre-flight gates ----------------------------------------------------
    if (!in.in_flight) {
        if (!in.baro_ok) { out.armed_block = true; copyReason(out.reason, "BARO FAULT"); }
        else if (gnss_bad) { out.armed_block = true; copyReason(out.reason, "GNSS POOR"); }
        else if (st.rc_bad_ms > cfg.link_debounce_ms) {
            out.armed_block = true;
            copyReason(out.reason, "RC LOST");
        }
        if (out.armed_block) out.action = Action::Continue;
        return out;
    }

    // --- in flight, least severe last ---------------------------------------
    if (st.fence_bad_ms > cfg.geofence_debounce_ms) {
        out.action = Action::Rtl;
        copyReason(out.reason, "GEOFENCE");
        return out;
    }
    if (st.gnss_bad_ms > 2000) {
        out.action = Action::Rtl;
        copyReason(out.reason, "GNSS LOST");
        return out;
    }
    if (batt_known && in.battery_pct <= cfg.battery_rtl_pct) {
        out.action = Action::Rtl;
        copyReason(out.reason, "BATT LOW");
        return out;
    }
    if (st.rc_bad_ms > cfg.link_debounce_ms) {
        out.action = Action::Rtl;
        copyReason(out.reason, "RC LOST");
        return out;
    }
    if (st.gcs_bad_ms > cfg.gcs_timeout_ms * 4) {
        // The ground station is gone but the RC still flies the aircraft, so
        // hold position and let the pilot decide rather than auto-RTL.
        out.action = Action::Hover;
        copyReason(out.reason, "GCS LOST");
        return out;
    }

    return out;
}

}  // namespace ranch
