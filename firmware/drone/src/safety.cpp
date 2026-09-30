#include "safety.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "hal.h"
#include "mission_planner.h"

namespace ranch {
namespace {

AirState st{};
FailsafeState fs{};
FailsafeConfig cfg = FAILSAFE_DEFAULTS;
Action act = Action::Continue;
char reason[24] = "";

int32_t home_lat_e7 = 0, home_lon_e7 = 0;
float home_alt_m = 0.0f;
bool home_set = false;
uint32_t last_fc_ms = 0, last_rc_ms = 0, last_gcs_ms = 0;
bool force_safe = true;
uint32_t wdt_next = 0;

// WGS84 -> local ENU at metre scale. Accurate enough for a 180 m geofence and
// it keeps double-precision trig off the hot path: the longitude scale is
// derived from the latched home latitude once, then cached.
//
// The differencing is done on the 1e7 integers, never on the float degrees:
// at latitude 30 a float32 has a quantum of about 0.4 m, so (lat - home) in
// floats cannot resolve the last half metre at all, and an aircraft parked on a
// waypoint reads as permanently two-ish metres short of it.
double m_per_deg_lat = 111320.0;
double m_per_deg_lon = 111320.0 * 0.9848;

void enuFromFix(int32_t lat_e7, int32_t lon_e7, float& n, float& e) {
    n = static_cast<float>((lat_e7 - home_lat_e7) * 1e-7 * m_per_deg_lat);
    e = static_cast<float>((lon_e7 - home_lon_e7) * 1e-7 * m_per_deg_lon);
}

void latchHome(const GnssFix& fix) {
    home_lat_e7 = fix.lat_e7;
    home_lon_e7 = fix.lon_e7;
    home_alt_m = fix.alt_mm / 1000.0f;
    const double lat_rad = static_cast<double>(fix.lat_e7) / 1e7 * 0.0174532925;
    double c = std::cos(lat_rad);
    if (c < 0.01) c = 0.01;             // never let the longitude scale collapse
    m_per_deg_lat = 111320.0;
    m_per_deg_lon = 111320.0 * c;
    home_set = true;
}

void setReason(const char* r) {
    std::snprintf(reason, sizeof(reason), "%s", r ? r : "");
}

uint8_t cfg_min_sats() { return FAILSAFE_DEFAULTS.min_satellites; }

// >100 is the decision table's "not measured yet": a pack the FC has not
// reported must not read as empty, and a half-stale RSSI must not read as a
// dead receiver.
uint8_t battPctForTable() {
    if (st.batt_pct < 0 || st.batt_pct > 100) return 255;
    return static_cast<uint8_t>(st.batt_pct);
}

const Geofence kFence{RANCH_GEOFENCE_R_M, RANCH_GEOFENCE_CEIL_M, GEOFENCE_ALT_FLOOR_M};

}  // namespace

void safetyInit() {
    // Establish the whole state, not just the parts that are not already zero.
    // After a watchdog reboot the RAM image is whatever the last flight left
    // there, and a supervisor that inherits "armed" from it will report an
    // aircraft that is on the pad as one that is in the air.
    st = AirState{};
    st.batt_pct = -1;              // unknown, not empty
    fs = FailsafeState{};
    failsafeReset(fs);
    act = Action::Continue;
    force_safe = true;
    setSafe(true);
    setReason("DISARMED");
    home_set = false;
    home_lat_e7 = home_lon_e7 = 0;
    home_alt_m = 0.0f;
    m_per_deg_lon = 111320.0 * 0.9848;
    const uint32_t now = halMillis();
    last_fc_ms = last_rc_ms = last_gcs_ms = now;
    wdt_next = now;
}

const AirState& safetyAir() { return st; }

bool safetyAirHasFix() { return st.fix_type >= 3 && st.sats >= cfg_min_sats(); }

bool safetyHomeGeodetic(int32_t& lat_e7, int32_t& lon_e7, float& alt_m) {
    lat_e7 = home_lat_e7;
    lon_e7 = home_lon_e7;
    alt_m = home_alt_m;
    return home_set;
}

void safetyGeodeticFromEnu(float north, float east, int32_t& lat_e7, int32_t& lon_e7) {
    lat_e7 = home_lat_e7 + static_cast<int32_t>(north / m_per_deg_lat * 1e7);
    lon_e7 = home_lon_e7 + static_cast<int32_t>(east / m_per_deg_lon * 1e7);
}

void safetyNoteGcsContact() { last_gcs_ms = halMillis(); }

void safetyOnMavlink(const MavMessage& m) {
    const uint32_t now = halMillis();
    last_fc_ms = now;

    switch (m.id) {
        case MSG_GPS_RAW_INT: {
            const GnssFix fix = decodeGpsRaw(m);
            if (!fix.valid) break;
            st.fix_type = fix.fix_type;
            st.sats = fix.satellites;
            st.hdop = fix.hdop_cm / 100.0f;
            st.lat = fix.lat_e7 / 1e7f;
            st.lon = fix.lon_e7 / 1e7f;
            st.alt_m = fix.alt_mm / 1000.0f;
            if (!home_set && !st.armed && fix.fix_type >= 3) latchHome(fix);
            if (home_set) enuFromFix(fix.lat_e7, fix.lon_e7, st.north, st.east);
            break;
        }

        case MSG_BATTERY_STATUS: {
            const Battery b = decodeBattery(m);
            if (b.remaining_pct >= 0) st.batt_pct = b.remaining_pct;
            if (b.cells) {
                uint32_t sum = 0;
                for (uint8_t i = 0; i < b.cells; ++i) sum += b.mv_per_cell[i];
                st.batt_mv_cell = static_cast<uint16_t>(sum / b.cells);
            }
            st.batt_a = b.current_centi / 100.0f;
            break;
        }

        case MSG_ATTITUDE: {
            const Attitude a = decodeAttitude(m);
            st.heading_deg = a.yaw_deg;
            st.roll_deg = a.roll_deg;
            st.pitch_deg = a.pitch_deg;
            break;
        }

        // base_mode bit 0 says custom_mode is meaningful and bit 7 is the arm
        // flag; system_status MAV_STATE_ACTIVE (4) means airborne.
        case MSG_HEARTBEAT:
            st.custom_mode = static_cast<uint32_t>(m.i32At(0));
            st.armed = hbArmed(m.u8At(6));
            st.system_status = m.u8At(7);
            st.in_flight = st.system_status == 4;
            last_fc_ms = now;
            break;

        // RADIO_STATUS lays the 16-bit fields out first: txbuf, rssi, remrssi.
        // remrssi is the downlink (our transmitter as seen by the ground) and is
        // reported as dBm + 256 by every modem in the fleet.
        case MSG_RADIO_STATUS:
            if (m.len >= 6) st.rssi_dbm = static_cast<float>(m.u16At(4)) - 256.0f;
            last_rc_ms = now;
            break;

        default:
            break;
    }
}

void safetyUpdate(uint32_t dt_ms) {
    const uint32_t now = halMillis();
    st.uptime_ms += dt_ms;
    st.fc_alive = (now - last_fc_ms) < 3000;
    st.rc_age_ms = now - last_rc_ms;
    st.gcs_age_ms = now - last_gcs_ms;

    if (!st.fc_alive) {
        // Blind, not sick. The aircraft is still flying under the FC's own
        // failsafes and every sensor value we hold is stale, so commanding RTL
        // or kill here would act on the past. The supervisor reports it and
        // stops touching the mission; missionTick gates on the same flag.
        act = Action::Continue;
        setReason("NO FC LINK");
        return;
    }

    FailsafeInput in{};
    // RC loss belongs to the FC: it samples the receiver at hundreds of hertz
    // and has a throttle failsafe wired into the mixer. RADIO_STATUS arrives at
    // a fraction of that rate, so the companion would only ever be second, and a
    // modem that reports slowly would false an RTL. Reported, not acted on.
    in.rc_age_ms = 0;
    in.gcs_age_ms = st.gcs_age_ms;
    in.in_flight = st.in_flight;
    // Above the pad, not above sea level: the ceiling in board.h is an AGL
    // number and the GNSS altitude is AMSL.
    const Breach b = checkGeofence(kFence, 0.0f, 0.0f, st.north, st.east,
                                   st.alt_m - home_alt_m);
    in.geofence_breach = home_set && (b.outside || b.too_high);
    in.fix_type = st.fix_type;
    in.satellites = st.sats;
    in.hdop = st.hdop;
    in.battery_pct = battPctForTable();
    in.cell_v_min = st.batt_mv_cell / 1000.0f;
    in.baro_ok = true;
    // No IMU on this board, and the FC heartbeat is already covered by the
    // early-out above, so this table input is a pass.
    in.imu_ok = true;
    in.gps_ok = st.fix_type > 0;

    const FailsafeDecision d = failsafeEvaluate(cfg, fs, in, dt_ms);
    if (d.action != act || std::strcmp(d.reason, reason) != 0) {
        act = d.action;
        setReason(d.reason);
    }

    if (act == Action::Terminate && !force_safe) {
        force_safe = true;
        setSafe(true);
    }
}

Action safetyAction() { return act; }
const char* safetyReason() { return reason; }
bool safetyIsSafe() { return force_safe; }

const FailsafeConfig& safetyFailsafeConfig() { return cfg; }

void safetySetFailsafeConfig(const FailsafeConfig& c) {
    cfg = c;
    failsafeReset(fs);
}

void safetyForceSafe(bool safe) {
    force_safe = safe;
    setSafe(safe);
}

bool safetyCanArm(char* why, size_t cap) {
    if (force_safe) { std::snprintf(why, cap, "%s", "SAFE SWITCH"); return false; }
    if (!st.fc_alive) { std::snprintf(why, cap, "%s", "NO FC HEARTBEAT"); return false; }
    if (!home_set) { std::snprintf(why, cap, "%s", "HOME NOT SET"); return false; }

    FailsafeInput in{};
    in.rc_age_ms = 0;                 // see safetyUpdate: the FC owns RC loss
    in.gcs_age_ms = st.gcs_age_ms;
    in.fix_type = st.fix_type;
    in.satellites = st.sats;
    in.hdop = st.hdop;
    in.battery_pct = battPctForTable();
    in.cell_v_min = st.batt_mv_cell / 1000.0f;
    in.baro_ok = in.imu_ok = in.gps_ok = true;
    // A throwaway debounce instance: a pre-flight question must not disturb the
    // running supervisor's accumulated timers.
    FailsafeState scratch{};
    const FailsafeDecision d = failsafeEvaluate(cfg, scratch, in, 0);
    if (!d.armed_block) return true;
    std::snprintf(why, cap, "%s", d.reason);
    return false;
}

void safetyKick() {
    const uint32_t now = halMillis();
    if (static_cast<int32_t>(now - wdt_next) < 0) return;
    wdt_next = now + 40;              // 12.5 Hz, above the 10 Hz watchdog floor
    feedWatchdog();
}

}  // namespace ranch
