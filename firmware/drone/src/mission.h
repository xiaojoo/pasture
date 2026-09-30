// Mission service: accepts a waypoint list from the ground app, uploads it to
// the flight controller over MAVLink, supervises progress with the planner in
// firmware/lib, and enforces the geofence.
#pragma once

#include "failsafe.h"
#include "mission_planner.h"

namespace ranch {

enum class MissionSource : uint8_t { None, Mqtt, Rtc, Manual };

struct MissionStatus {
    Phase phase;
    uint8_t target;
    uint8_t count;
    float remaining_m;
    uint32_t captures;
    bool complete;
    bool uploading;
    uint8_t arm_stage;       // ArmStep: how far the hand-over to AUTO has got
    uint8_t arm_retries;     // commands re-issued because the FC did not confirm
    Action failsafe;
    char failsafe_reason[24];
    char mode[12];            // FC mode string, e.g. AUTO, RTL, LOITER
};

void missionInit();
// Queue a mission for upload. Returns false when the list is empty or the FC is
// not reachable, so the caller can refuse the command with a reason.
bool missionUpload(const Mission& m, MissionSource src);
bool missionStartAuto();
void missionRtl();
void missionLand();
void missionHold();
void missionTick(uint32_t dt_ms);
void missionStatus(MissionStatus& out);

// Ground-station input.
void missionOnMavlink(const struct MavMessage& m);
void missionOnCommand(const char* cmd, const char* payload);

// Home position, latched from the first good GNSS fix while disarmed.
bool missionHomeSet();
void missionSetHome(float north, float east, float alt);
void missionHomeEnu(float& n, float& e, float& alt);

// The step of the arm/AUTO hand-over the aircraft is waiting on: "WAIT FIX",
// "SET MODE", "ARM", "TAKEOFF" or "AUTO".
const char* missionArmName();

}  // namespace ranch
