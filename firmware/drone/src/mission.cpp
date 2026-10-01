#include "mission.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "hal.h"
#include "mavlink.h"
#include "safety.h"

namespace ranch {
namespace {

constexpr uint8_t SYSID = 1;        // ArduPilot FC is usually 1
constexpr uint8_t COMPID = 0;       // autopilot
constexpr uint8_t OUR_SYSID = 23;   // companion computer
constexpr uint8_t OUR_COMPID = 190; // MAV_COMP_ID_USER10

Mission mission{};
MissionSource source = MissionSource::None;
PlanState plan{};
MissionStatus status{};
bool uploading = false;
uint8_t upload_next = 0;
uint32_t upload_retry_ms = 0;
bool hold_requested = false;
// "Land now" and "coming home" both end with the aircraft descending, but only
// one of them should take the FC out of RTL: an explicit land goes down where it
// stands, a return flies over the pad first. Without the distinction a normal
// return touched down several metres short of home.
bool land_now = false;
uint32_t last_captures = 0;
uint32_t last_upload_ms = 0;
// The stick channel: axes as sent by the ground station, and the two timers that
// decide when the board stops re-issuing the override.
constexpr uint32_t RC_SEND_MS = 100;     // 10 Hz; the FC times an override out
constexpr uint32_t RC_STALE_MS = 500;    // the ground sends at 8 Hz; silence = released
constexpr int32_t RC_MID_US = 1500;      // stick centre
constexpr int32_t RC_FULL_US = 500;      // full deflection either side of it
bool rc_live = false;
float rc_ax[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // fwd, side, climb, yaw, each -1..1
char rc_why[16] = "";
uint32_t rc_seen = 0;
uint32_t rc_next = 0;
float g_home_n = 0, g_home_e = 0, g_home_alt = 0;
bool g_home_locked = false;

MavFrameBuilder tx(OUR_SYSID, OUR_COMPID);

void sendCommand(uint16_t cmd, float p1, float p2, uint8_t force) {
    uint8_t payload[40];
    MavWriter w(payload, sizeof(payload));
    CommandLongTx c{};
    c.p[0] = p1;
    c.p[1] = p2;
    c.command = cmd;
    c.target_sys = SYSID;
    c.target_comp = COMPID;
    c.confirmation = force ? 1 : 0;
    c.pack(w);
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_COMMAND_LONG, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void setMode(uint32_t mode) {
    // param1 = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, param2 = the FC's own number.
    sendCommand(CMD_DO_SET_MODE, 1, static_cast<float>(mode), 0);
}

// Hand-over sequence. Every step waits for the flight controller to confirm the
// previous one in its own heartbeat before issuing the next: a companion
// computer that only fires commands into a UART cannot tell "accepted" apart
// from "transmit wire broken", and on an aircraft that difference is the whole
// point of the redundancy.
enum class ArmStep : uint8_t { WaitFix, SetMode, Arm, Takeoff, Running, Count };
ArmStep arm = ArmStep::WaitFix;
uint32_t arm_deadline = 0;
uint8_t arm_retries = 0;

const char* armStepName(ArmStep s) {
    switch (s) {
        case ArmStep::WaitFix:   return "WAIT FIX";
        case ArmStep::SetMode:   return "SET MODE";
        case ArmStep::Arm:       return "ARM";
        case ArmStep::Takeoff:   return "TAKEOFF";
        case ArmStep::Running:   return "AUTO";
        case ArmStep::Count:     return "?";
    }
    return "?";
}

void armAdvance(const AirState& air, uint32_t now) {
    switch (arm) {
        case ArmStep::WaitFix:
            if (!safetyAirHasFix()) return;
            arm = ArmStep::SetMode;
            arm_deadline = now + 2000;
            setMode(FC_MODE_AUTO);
            return;

        case ArmStep::SetMode:
            if (air.custom_mode != FC_MODE_AUTO) break;
            arm = ArmStep::Arm;
            arm_deadline = now + 2000;
            sendCommand(CMD_COMPONENT_ARM_DISARM, 1, 0, 0);
            return;

        case ArmStep::Arm:
            if (!air.armed) break;
            arm = ArmStep::Takeoff;
            arm_deadline = now + 3000;
            sendCommand(CMD_NAV_TAKEOFF, RANCH_MISSION_ALT_M, 0, 0);
            return;

        case ArmStep::Takeoff:
            if (!air.in_flight) break;
            // Only now does the planner claim to be climbing: every phase above
            // Ground is a phase the flight controller has confirmed, so a safe
            // or unconnected aircraft cannot make the dashboard say "CLIMB".
            plan.phase = Phase::Climbing;
            arm = ArmStep::Running;
            sendCommand(CMD_MISSION_START, 0, 0, 0);
            return;

        case ArmStep::Running:
        case ArmStep::Count:
            return;
    }

    // Nothing to step on yet: re-issue the pending command rather than
    // restarting the sequence. The FC may already be in the right mode and have
    // simply missed the frame.
    if (static_cast<int32_t>(now - arm_deadline) < 0) return;
    arm_deadline = now + 2000;
    if (arm_retries < 255) arm_retries++;
    if (arm == ArmStep::SetMode) setMode(FC_MODE_AUTO);
    else if (arm == ArmStep::Arm) sendCommand(CMD_COMPONENT_ARM_DISARM, 1, 0, 0);
    else if (arm == ArmStep::Takeoff) sendCommand(CMD_NAV_TAKEOFF, RANCH_MISSION_ALT_M, 0, 0);
}

void sendMissionItem(uint8_t index) {
    const Waypoint& wp = mission.items[index];
    uint8_t payload[60];
    MavWriter w(payload, sizeof(payload));
    MissionItemIntTx it{};
    it.seq = index;
    it.command = CMD_NAV_WAYPOINT;
    it.frame = FRAME_GLOBAL_INT;
    it.current = index == 0 ? 1 : 0;
    it.autocontinue = 1;
    it.mission_type = 0;
    it.target_sys = SYSID;
    it.target_comp = COMPID;
    // Absolute WGS84 in 1e7 degrees, stamped by missionUpload from the latched
    // home. A zero here would send the aircraft to the Gulf of Guinea.
    it.x = wp.lat_e7;
    it.y = wp.lon_e7;
    it.z = wp.alt;
    it.param1 = 0;
    it.param2 = wp.dwell_s;      // ArduPilot uses param 2 as the waypoint delay
    it.param3 = 0;
    it.param4 = wp.camera == 1 ? 1.0f : 0.0f;
    it.pack(w);

    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_MISSION_ITEM_INT, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void sendMissionCount() {
    uint8_t payload[8];
    MavWriter w(payload, sizeof(payload));
    w.u16(mission.count);
    w.u8(SYSID);
    w.u8(COMPID);
    w.u8(0);   // mission_type MAV_MISSION_TYPE_MISSION
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_MISSION_COUNT, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void sendAck(uint8_t type) {
    uint8_t payload[8];
    MavWriter w(payload, sizeof(payload));
    w.u8(SYSID);
    w.u8(COMPID);
    w.u8(type);   // MAV_MISSION_ACCEPTED = 0
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_MISSION_ACK, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

// --- the stick channel ----------------------------------------------------
//
// ArduCopter only honours an RC override in a mode that takes manual input, so
// taking the sticks also asks the FC for LOITER. Sending a setpoint into AUTO
// would count as "sent" and be ignored in the air, which is the worst kind of yes.
void sendRcOverride() {
    uint8_t payload[40];
    MavWriter w(payload, sizeof(payload));
    RcOverrideTx o{};
    for (int i = 0; i < 18; ++i) o.chan[i] = 0xFFFF;   // ignore: the radio keeps them
    // Channels 1..4 are roll, pitch, throttle, yaw, so the board's stick order
    // (fwd, side, climb, yaw) crosses over: forward belongs on the pitch channel and
    // side on the roll channel. Sending fwd down channel 1 would slide the aircraft
    // sideways when the operator pushed the stick forward.
    // A released stick writes 0 = "hand this channel back to the radio".
    const uint8_t CH_AXIS[4] = {1, 0, 2, 3};
    for (int i = 0; i < 4; ++i) {
        o.chan[i] = rc_live
            ? static_cast<uint16_t>(RC_MID_US + rc_ax[CH_AXIS[i]] * RC_FULL_US)
            : 0;
    }
    o.target_sys = SYSID;
    o.target_comp = COMPID;
    o.pack(w);
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_RC_CHANNELS_OVERRIDE, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

// "0.6,-0.25,0,0" -> four floats, clamped to -1..1. Written by hand because the
// board must not read a comma-decimal "0,6" as sixty.
bool parseAxes(const char* s, float out[4]) {
    if (!s) return false;
    for (int k = 0; k < 4; ++k) {
        float sign = 1.0f, val = 0.0f, frac = 0.1f;
        int digits = 0;
        while (*s == ' ' || *s == '	') ++s;   // a typed line arrives padded
        if (*s == '-') { sign = -1.0f; ++s; }
        else if (*s == '+') ++s;
        while (*s >= '0' && *s <= '9') { val = val * 10.0f + (*s - '0'); ++s; ++digits; }
        if (*s == '.') {
            ++s;
            while (*s >= '0' && *s <= '9') { val += (*s - '0') * frac; frac *= 0.1f; ++s; ++digits; }
        }
        if (!digits) return false;
        float v = sign * val;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[k] = v;
        if (k != 3) {
            if (*s != ',') return false;
            ++s;
        }
    }
    return *s == '\0';
}

void releaseRc() {
    if (!rc_live) return;
    rc_live = false;
    for (int i = 0; i < 4; ++i) rc_ax[i] = 0.0f;
    sendRcOverride();
}

}  // namespace

void missionInit() {
    missionReset(mission);
    plan = PlanState{};
    status = MissionStatus{};
    source = MissionSource::None;
    uploading = false;
    hold_requested = false;
    arm = ArmStep::WaitFix;
    arm_retries = 0;
    land_now = false;
    last_captures = 0;
    // A board restart must not come back with somebody's hand still on the sticks.
    rc_live = false;
    rc_seen = 0;
    rc_next = 0;
    rc_why[0] = '\0';
    for (int i = 0; i < 4; ++i) rc_ax[i] = 0.0f;
}

bool missionUpload(const Mission& m, MissionSource src) {
    if (m.count == 0) return false;
    int32_t lat_e7 = 0, lon_e7 = 0;
    float home_alt = 0.0f;
    // Refuse rather than fly: without a latched home there is no georeference,
    // and an unconverted waypoint list reads as a cluster of points on the
    // equator and Greenwich meridian.
    if (!safetyHomeGeodetic(lat_e7, lon_e7, home_alt)) return false;

    mission = m;
    for (uint8_t i = 0; i < mission.count; ++i) {
        const float n = mission.items[i].north;
        const float e = mission.items[i].east;
        safetyGeodeticFromEnu(n, e, mission.items[i].lat_e7, mission.items[i].lon_e7);
    }
    missionSetHome(0.0f, 0.0f, home_alt);

    source = src;
    uploading = true;
    upload_next = 0;
    // A new list starts a new flight: the supervisor's cursor, dwell timer and
    // capture count all belong to the list that is being replaced.
    plan = PlanState{};
    last_captures = 0;
    upload_retry_ms = halMillis() + 1500;
    last_upload_ms = halMillis();
    arm = ArmStep::WaitFix;
    arm_retries = 0;
    land_now = false;
    sendMissionCount();
    return true;
}

void missionOnMavlink(const MavMessage& m) {
    // The flight controller owns the cursor: it advances on arrival and it
    // skips a zero-dwell waypoint in the same tick it received it. Watching
    // MISSION_CURRENT is the only way the supervisor can know which leg is
    // really being flown, so it is handled even when nothing is uploading.
    if (m.id == MSG_MISSION_CURRENT) {
        const uint16_t seq = m.u16At(0);
        if (mission.count == 0 || seq >= mission.count) return;
        if (plan.phase == Phase::Rtl || plan.phase == Phase::Descending) return;
        const bool moved = seq != plan.target;
        plan.target = static_cast<uint8_t>(seq);
        mission.cursor = static_cast<uint8_t>(seq);
        // If the FC left a waypoint we were still dwelling on, its timer and
        // ours disagree; the aircraft is the one that is really flying.
        if (moved && plan.phase == Phase::Dwelling) {
            plan.phase = Phase::Transit;
            plan.dwell_left_s = 0.0f;
        }
        return;
    }

    if (!uploading) return;
    if (m.id == MSG_MISSION_REQUEST_INT) {
        const uint16_t seq = m.u16At(0);
        if (seq < mission.count) {
            sendMissionItem(static_cast<uint8_t>(seq));
            upload_next = static_cast<uint8_t>(seq + 1);
        } else {
            uploading = false;
            sendAck(0);
        }
    } else if (m.id == MSG_MISSION_ACK) {
        uploading = false;
    }
}

void missionTick(uint32_t dt_ms) {
    const uint32_t now = halMillis();
    // Re-send MISSION_COUNT if the FC never answered: a missed frame must not
    // leave the aircraft sitting on the pad with a queued mission. Signed
    // compare, because a wrap near millis()==0 would otherwise skip the retry.
    if (uploading && static_cast<int32_t>(now - upload_retry_ms) >= 0) {
        sendMissionCount();
        upload_retry_ms = now + 1500;
    }

    const AirState& air = safetyAir();

    PlanInput in{};
    in.north = air.north;
    in.east = air.east;
    in.alt = air.alt_m;
    in.cruise_ms = RANCH_CRUISE_MS;
    in.climb_ms = RANCH_CLIMB_MS;
    in.in_flight = air.in_flight;

    float hn, he, ha;
    missionHomeEnu(hn, he, ha);
    float n, e, alt;
    // The planner is a clock, not a mirror: it burns the dwell countdown, triggers the
    // camera and walks the waypoint list by elapsed time. None of that may run while a
    // person has cancelled the sortie or is holding the sticks, or the dashboard would
    // finish a mission the aircraft never flew -- it would hover over the pad and report
    // every shutter as taken. `remaining_m` still follows the aircraft, because the
    // resume is to whatever the next waypoint is from wherever it now is.
    if (hold_requested || rc_live) {
        const Waypoint& wp = mission.items[plan.target < mission.count ? plan.target : 0];
        const float dn = wp.north - air.north, de = wp.east - air.east;
        plan.remaining_m = std::sqrt(dn * dn + de * de);
        n = wp.north;
        e = wp.east;
        alt = wp.alt;
    } else {
        planStep(mission, plan, in, hn, he, ha, dt_ms / 1000.0f, n, e, alt);
    }

    // One camera trigger per planned capture. The planner counts the capture
    // when it enters the dwell, so comparing the counter is what makes this a
    // single frame: testing the countdown window instead re-sent the trigger on
    // every tick for the last third of every dwell.
    if (plan.captures != last_captures) {
        last_captures = plan.captures;
        sendCommand(CMD_DO_DIGICAM_CONTROL, 1, 0, 0);
    }

    status.phase = plan.phase;
    status.target = plan.target;
    status.count = mission.count;
    status.remaining_m = plan.remaining_m;
    status.captures = plan.captures;
    status.complete = plan.mission_complete;
    status.uploading = uploading;
    status.arm_stage = static_cast<uint8_t>(arm);
    status.arm_retries = arm_retries;
    status.rc_live = rc_live;
    std::snprintf(status.rc_why, sizeof(status.rc_why), "%s", rc_why);

    const Action a = safetyAction();
    status.failsafe = a;
    std::snprintf(status.failsafe_reason, sizeof(status.failsafe_reason), "%s", safetyReason());

    // The failsafe outranks the mission: once it says RTL, stop nudging the FC
    // with mission setpoints. And with no FC link there is nobody to nudge --
    // emitting arm or takeoff into a dead UART would still count as "sent".
    if (!air.fc_alive) return;

    if (a == Action::Terminate) { missionLand(); return; }
    if (a == Action::Rtl && plan.phase != Phase::Rtl && plan.phase != Phase::Descending) {
        missionRtl();
        return;
    }
    if (a == Action::Hover) { missionHold(); return; }

    // A hand on the sticks is re-issued at the FC's own refresh rate -- an override
    // that is not repeated times out and the aircraft stops under nobody. Silence
    // from the ground means released, and the failsafe above already outranks a hand.
    if (rc_live) {
        if (static_cast<int32_t>(now - rc_seen) >= static_cast<int32_t>(RC_STALE_MS)) {
            releaseRc();
        } else if (static_cast<int32_t>(now - rc_next) >= 0) {
            sendRcOverride();
            rc_next = now + RC_SEND_MS;
        }
    }

    // Mode watchdog: the planner's phase and the FC's mode have to agree. A lost
    // DO_SET_MODE would otherwise leave the aircraft flying the old plan while
    // the dashboard says RTL, which is the worst possible disagreement.
    // A cancelled sortie is in this list *before* the phase, not after it: the phase
    // still says DWELL or TRANSIT, because cancelling stopped the planner rather than
    // rewinding it, and reading the phase here is how 悬停 used to last exactly as long
    // as the stick was released -- the next tick demanded AUTO and took the aircraft out
    // from under the person who had just cancelled it.
    uint32_t want = 0;
    if (rc_live || hold_requested) want = FC_MODE_LOITER;
    else if (plan.phase == Phase::Rtl) want = FC_MODE_RTL;
    else if (plan.phase == Phase::Descending) want = land_now ? FC_MODE_LAND : FC_MODE_RTL;
    else if (arm == ArmStep::Running) want = FC_MODE_AUTO;
    if (want != 0 && air.custom_mode != want) setMode(want);

    // The hand-over runs whenever a mission is loaded and the aircraft is not
    // yet flying it, whatever the planner's phase says: a resume from a loiter
    // has to be able to re-hand-over without pretending to be on the pad. Not
    // while a hand holds the sticks -- that would yank the aircraft out from
    // under them back into AUTO the next tick.
    if (!rc_live && !uploading && mission.count && arm != ArmStep::Running) armAdvance(air, now);
}

bool missionStartAuto() {
    if (!mission.count) return false;
    plan.phase = Phase::Climbing;
    plan.target = 0;
    arm = ArmStep::WaitFix;
    arm_retries = 0;
    return true;
}

// Setting the planner's phase is only half of a mode change: the aircraft is
// flying the FC's state machine, so the FC has to be told too. The mode
// watchdog in missionTick re-sends if the frame is lost.
void missionRtl() {
    plan.phase = Phase::Rtl;
    hold_requested = false;
    land_now = false;
    setMode(FC_MODE_RTL);
}

void missionLand() {
    plan.phase = Phase::Descending;
    // A landing outranks a cancellation: without this the watchdog below would keep
    // demanding LOITER and the aircraft would descend under protest.
    hold_requested = false;
    land_now = true;
    setMode(FC_MODE_LAND);
}

// Cancelling is a state the board has to *keep*, not a nudge: it stops the planner, and
// the mode watchdog holds the FC in LOITER until the operator says otherwise -- resume,
// return, or land. On the pad there is nothing to cancel, and answering yes to that
// would be the page claiming a control worked when no control exists.
bool missionHold() {
    if (plan.phase == Phase::Ground || plan.phase == Phase::Landed) return false;
    if (hold_requested) return true;
    hold_requested = true;
    sendCommand(CMD_NAV_LOITER_UNLIM, 0, 0, 0);
    return true;
}

// The other half of the pair: without it a cancelled sortie can only be flown by hand
// until the battery ends, and `resume` was a verb with no button.
bool missionResume() {
    if (!hold_requested || !mission.count) return false;
    hold_requested = false;
    return missionStartAuto();
}

void missionOnRc(const char* axes) {
    float a[4];
    if (!parseAxes(axes, a)) {
        std::snprintf(rc_why, sizeof(rc_why), "%s", "BAD AXES");
        return;
    }
    const bool neutral = a[0] == 0.0f && a[1] == 0.0f && a[2] == 0.0f && a[3] == 0.0f;
    // A released stick is not a request, so it cannot be refused: it only hands the
    // channels back.
    if (neutral) { releaseRc(); return; }
    const AirState& air = safetyAir();
    if (!air.armed || !air.in_flight) {
        // "The button did nothing" is not something a person can read off an
        // aircraft on the pad, so the refusal is named and travels in the frame.
        std::snprintf(rc_why, sizeof(rc_why), "%s", air.armed ? "ON GROUND" : "NOT ARMED");
        return;
    }
    rc_why[0] = '\0';
    const bool first = !rc_live;
    for (int i = 0; i < 4; ++i) rc_ax[i] = a[i];
    rc_live = true;
    rc_seen = halMillis();
    if (first && air.custom_mode != FC_MODE_LOITER) setMode(FC_MODE_LOITER);
    sendRcOverride();
    rc_next = rc_seen + RC_SEND_MS;
}

void missionStatus(MissionStatus& out) {
    out = status;
    // The frame names who is flying it, and there are now two answers that are not the
    // planner's phase: a hand on the sticks (HOLD) and a sortie somebody cancelled and
    // is not currently stick-flying (LOITER). Without the second one, 取消自动 looked in
    // the dashboard exactly like 巡航中 again the moment the stick came back to centre.
    std::snprintf(out.mode, sizeof(out.mode), "%s",
                  rc_live ? "HOLD" : hold_requested ? "LOITER" : phaseName(plan.phase));
}

bool missionHomeSet() { return g_home_locked; }

// Which step of the hand-over we are stuck on. A mission that never leaves the
// pad looks identical to a dead upload unless the stage is reported.
const char* missionArmName() { return armStepName(arm); }

void missionSetHome(float north, float east, float alt) {
    g_home_n = north;
    g_home_e = east;
    g_home_alt = alt;
    g_home_locked = true;
}

void missionHomeEnu(float& n, float& e, float& alt) {
    n = g_home_n;
    e = g_home_e;
    alt = g_home_alt;
}

// False only for a verb this file owns *and* refused, which is the difference between
// the board saying 「已执行 hold」 and having hovered over the pad because there was no
// sortie to cancel. Verbs this file does not own answer true; main.cpp's own table
// decides whether they were recognised at all.
bool missionOnCommand(const char* cmd, const char* payload) {
    if (!cmd) return true;
    if (std::strcmp(cmd, "rtl") == 0) { missionRtl(); return true; }
    else if (std::strcmp(cmd, "land") == 0) { missionLand(); return true; }
    else if (std::strcmp(cmd, "hold") == 0) return missionHold();
    // The drawer has one button for both ends of a sortie, so the press that puts it back
    // under the plan arrives as `patrol` even in the air. In the air the only thing that
    // can mean is *un-cancel it*: the latch below keeps demanding LOITER from the mode
    // watchdog, so without clearing it the board answered `ok:patrol` and sat still --
    // a yes from the board that moved nothing.
    else if (std::strcmp(cmd, "patrol") == 0) {
        if (plan.phase == Phase::Ground || plan.phase == Phase::Landed) return true;
        hold_requested = false;
        return missionStartAuto();
    }
    else if (std::strcmp(cmd, "rc") == 0) { missionOnRc(payload); return true; }
    else if (std::strcmp(cmd, "resume") == 0) return missionResume();
    else if (std::strcmp(cmd, "camera") == 0 && payload && payload[0] == '1') {
        sendCommand(CMD_DO_DIGICAM_CONTROL, 1, 0, 0);
        return true;
    }
    return true;
}

}  // namespace ranch
