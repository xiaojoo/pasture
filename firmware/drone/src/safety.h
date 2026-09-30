// Safety supervisor: arming checks, the failsafe table from firmware/lib, and
// the external watchdog. This module is the only one allowed to drive the FC
// kill input, so "who stopped this aircraft" always has one answer.
#pragma once

#include "failsafe.h"
#include "mavlink.h"

namespace ranch {

struct AirState {
    bool armed;
    bool in_flight;
    bool fc_alive;            // MAVLink heartbeats are arriving
    uint32_t fc_age_ms;
    uint32_t rc_age_ms;
    uint32_t gcs_age_ms;
    uint32_t custom_mode;     // HEARTBEAT custom_mode: the FC's own mode number
    uint8_t system_status;
    uint8_t fix_type;
    uint8_t sats;
    float hdop;
    float lat, lon, alt_m;
    float north, east;        // local ENU from the latched home
    float heading_deg;
    float roll_deg, pitch_deg;    // the FC's own ATTITUDE, in degrees
    uint16_t batt_mv_cell;
    int16_t batt_pct;
    float batt_a;
    float rssi_dbm;
    uint32_t uptime_ms;
};

void safetyInit();
const AirState& safetyAir();
bool safetyAirHasFix();
void safetyOnMavlink(const MavMessage& m);
void safetyUpdate(uint32_t dt_ms);

// Home latch. The geodetic origin lives here and nowhere else, so the
// metres-per-degree conversion used by the mission planner and the one used by
// an uploader cannot drift apart.
bool safetyHomeGeodetic(int32_t& lat_e7, int32_t& lon_e7, float& alt_m);
void safetyGeodeticFromEnu(float north, float east, int32_t& lat_e7, int32_t& lon_e7);

// Ground-station liveness: the GCS timeout only counts down when something has
// actually arrived from the ground.
void safetyNoteGcsContact();

Action safetyAction();
const char* safetyReason();
bool safetyCanArm(char* why, size_t cap);

// Called by the main loop; toggles the watchdog pin and kills the aircraft when
// the supervisor itself stops running.
void safetyKick();

// Ground-station / RC override.
void safetyForceSafe(bool safe);
bool safetyIsSafe();

// The decision table is loadable so an unattended scheduled patrol can disable
// the dashboard-liveness rule without a different binary. Read it, change the
// field, write it back.
const FailsafeConfig& safetyFailsafeConfig();
void safetySetFailsafeConfig(const FailsafeConfig& c);

}  // namespace ranch
