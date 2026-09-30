// Waypoint mission planner and geofence. Platform neutral: no Arduino, no
// ESP-IDF, no floating-point library calls beyond sqrt/atan2.
//
// Coordinates are local planar metres in a north-east-up frame anchored at the
// home position, which is what a mission upload is reduced to after the GNSS
// fix is validated. Keeping the planner in local metres removes all the
// latitude/longitude rounding from the state machine.
#pragma once

#include <cmath>
#include <cstdint>

namespace ranch {

constexpr uint8_t MISSION_MAX_WAYPOINTS = 32;
constexpr float   MISSION_DEFAULT_ARRIVAL_M = 2.0f;
constexpr float   GEOFENCE_RADIUS_M = 180.0f;
constexpr float   GEOFENCE_ALT_CEIL_M = 120.0f;
constexpr float   GEOFENCE_ALT_FLOOR_M = 1.0f;

struct Waypoint {
    float north;
    float east;
    float alt;
    // Absolute WGS84 in 1e7 integer degrees, filled in by the uploader from the
    // latched home. The planner itself never reads these; keeping both in one
    // struct means a waypoint cannot be flown with a stale georeference.
    int32_t lat_e7;
    int32_t lon_e7;
    float dwell_s;      // time spent on arrival before advancing, 0 = fly-through
    uint8_t camera;     // 0 = none, 1 = still capture, 2 = start clip, 3 = stop clip
};

struct Mission {
    Waypoint items[MISSION_MAX_WAYPOINTS];
    uint8_t count;
    uint8_t cursor;
    float arrival_m;
    bool loop;
};

enum class Phase : uint8_t {
    Ground,     // motors off, on the pad
    Climbing,   // ascending to the cruise altitude
    Transit,    // flying the current leg
    Dwelling,   // on top of a waypoint, holding for the payload
    Rtl,        // returning to home
    Descending, // final approach
    Landed,     // touchdown, motors disarm pending
};

inline const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Ground:     return "GROUND";
        case Phase::Climbing:   return "CLIMB";
        case Phase::Transit:    return "TRANSIT";
        case Phase::Dwelling:   return "DWELL";
        case Phase::Rtl:        return "RTL";
        case Phase::Descending: return "DESCEND";
        case Phase::Landed:     return "LANDED";
    }
    return "UNKNOWN";
}

struct PlanState {
    Phase phase;
    uint8_t target;        // waypoint index being flown
    float remaining_m;     // distance left on the current leg
    float dwell_left_s;    // countdown inside a waypoint
    uint32_t captures;     // payload triggers issued
    float cmd_alt;         // commanded altitude, owned by the planner
    bool mission_complete;
};

// Vertical and horizontal speed are commanded elsewhere (the FC does the
// closed loop); the planner only decides where the next setpoint is.
struct PlanInput {
    float north;
    float east;
    float alt;
    float cruise_ms;
    float climb_ms;
    // From the flight controller's heartbeat. The descent profile is planner
    // owned, but "down" is not a thing the planner can decide alone: a profile
    // that runs out before the aircraft does would otherwise report Landed with
    // the aircraft still in the air.
    bool in_flight;
};

struct Geofence {
    float radius_m;
    float ceil_m;
    float floor_m;
};

struct Breach {
    bool outside;
    bool too_high;
    bool too_low;
};

inline Breach checkGeofence(const Geofence& g, float home_n, float home_e,
                            float n, float e, float alt) {
    const float dn = n - home_n;
    const float de = e - home_e;
    Breach b{};
    b.outside = std::sqrt(dn * dn + de * de) > g.radius_m;
    b.too_high = alt > g.ceil_m;
    b.too_low = alt < g.floor_m;
    return b;
}

inline void missionReset(Mission& m) {
    m.count = 0;
    m.cursor = 0;
    m.arrival_m = MISSION_DEFAULT_ARRIVAL_M;
    m.loop = false;
}

// Returns false when the buffer is full, so a bad upload cannot walk off the
// end of the array.
inline bool missionAdd(Mission& m, const Waypoint& w) {
    if (m.count >= MISSION_MAX_WAYPOINTS) return false;
    m.items[m.count++] = w;
    return true;
}

// Starts the mission from the ground. Returns false when there is nothing to
// fly, so the caller can refuse the arm instead of entering an empty state.
inline bool missionStart(Mission& m, PlanState& s, float cruise_ms, float climb_ms) {
    if (m.count == 0) return false;
    m.cursor = 0;
    s.phase = Phase::Climbing;
    s.target = 0;
    s.remaining_m = 0.0f;
    s.dwell_left_s = 0.0f;
    s.mission_complete = false;
    (void)cruise_ms;
    (void)climb_ms;
    return true;
}

// Moves to the next waypoint, or ends/loops the mission. Kept separate so the
// transit and dwell branches cannot disagree about what "finished" means.
inline void advance(Mission& m, PlanState& s) {
    if (static_cast<uint8_t>(s.target + 1) >= m.count) {
        if (m.loop) {
            s.target = 0;
        } else {
            s.mission_complete = true;
            s.phase = Phase::Rtl;
        }
    } else {
        s.target = static_cast<uint8_t>(s.target + 1);
    }
    m.cursor = s.target;
}

// One planner tick. dt_s is the elapsed wall time since the previous tick.
// The horizontal setpoint is exposed through out_n/out_e so the caller can
// forward it to the FC as a position or velocity command.
inline void planStep(Mission& m, PlanState& s, const PlanInput& in,
                     float home_n, float home_e, float home_alt, float dt_s,
                     float& out_n, float& out_e, float& out_alt) {
    const Waypoint& wp = m.items[s.target < m.count ? s.target : 0];
    out_n = wp.north;
    out_e = wp.east;
    out_alt = wp.alt;

    const float dn = wp.north - in.north;
    const float de = wp.east - in.east;
    const float dist = std::sqrt(dn * dn + de * de);
    s.remaining_m = dist;

    switch (s.phase) {
        case Phase::Ground:
            out_alt = home_alt;
            break;

        case Phase::Climbing: {
            // Climb to the first waypoint's altitude before committing to the
            // leg, which is what keeps the aircraft clear of the shed roofline.
            out_alt = wp.alt;
            const float climb_rate = in.climb_ms > 0.0f ? in.climb_ms : 2.0f;
            if (in.alt >= wp.alt - 0.5f) {
                s.phase = Phase::Transit;
            } else {
                out_n = in.north;
                out_e = in.east;
                (void)climb_rate;
            }
            break;
        }

        case Phase::Transit:
            if (dist <= m.arrival_m) {
                if (wp.dwell_s > 0.0f) {
                    // Hold here first; the trigger fires on arrival and the
                    // cursor only moves when the dwell expires.
                    s.phase = Phase::Dwelling;
                    s.dwell_left_s = wp.dwell_s;
                    if (wp.camera == 1 || wp.camera == 2) s.captures++;
                } else {
                    if (wp.camera == 1 || wp.camera == 2) s.captures++;
                    // Fly-through: advance once per tick so a zero-dwell
                    // mission cannot consume the whole list in one frame.
                    advance(m, s);
                }
            }
            break;

        case Phase::Dwelling: {
            out_n = in.north;
            out_e = in.east;
            s.dwell_left_s -= dt_s;
            if (s.dwell_left_s <= 0.0f) {
                s.phase = Phase::Transit;
                advance(m, s);
            }
            break;
        }

        case Phase::Rtl: {
            out_n = home_n;
            out_e = home_e;
            s.cmd_alt = home_alt + 15.0f;   // RTL corridor above the obstacles
            out_alt = s.cmd_alt;
            // Measured to home, not to the last waypoint: a mission that ends
            // far from the pad must still come back to it.
            const float dn2 = in.north - home_n;
            const float de2 = in.east - home_e;
            if (std::sqrt(dn2 * dn2 + de2 * de2) <= m.arrival_m * 2.0f) s.phase = Phase::Descending;
            break;
        }

        case Phase::Descending: {
            // The descent profile is planner-owned, not feedback-derived: a laggy
            // altitude report must never stall the final approach.
            out_n = home_n;
            out_e = home_e;
            s.cmd_alt -= 1.5f * dt_s;                 // 1.5 m/s
            if (s.cmd_alt <= home_alt) {
                s.cmd_alt = home_alt;
                // The profile has reached the pad. Whether the aircraft has is
                // the flight controller's to say.
                if (!in.in_flight) s.phase = Phase::Landed;
            }
            out_alt = s.cmd_alt;
            break;
        }

        case Phase::Landed:
            out_n = home_n;
            out_e = home_e;
            out_alt = home_alt;
            s.cmd_alt = home_alt;
            break;
    }
}

}  // namespace ranch
