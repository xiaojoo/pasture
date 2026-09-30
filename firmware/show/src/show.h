// The show, as one aircraft flies it.
//
// The programme arrives as a plan; this module turns it into the one lane that
// belongs to this station and then follows it: a position target per tick along
// the baked transition, a hold on the formation point, the act's colour on the
// pixels while it is there, and the next act when the hold is spent. Nothing here
// decides the choreography, and nothing here steers: the flight controller closes
// the loop, and this file tells it where the point is.
//
// The three ways it stops are the interesting part, and each one has its own
// reason and its own telemetry field:
//   - the RTK solution degrades to a single fix: carry on, and say so.
//   - the position goes away altogether: stop advancing, hold for three seconds
//     in case it was a shadow, then go to this station's own return slot.
//   - the ground station stops being heard: go to the slot immediately, and do not
//     enter the next act on the way.
// Nobody up here is flying the aircraft when that happens, so the aircraft has to
// know its own square.
#pragma once

#include <cstddef>
#include <cstdint>

#include "mavlink.h"
#include "plan.h"
#include "rtk.h"
#include "show_core.h"

namespace ranch {

enum ShowPhase : uint8_t {
    SHOW_IDLE = 0,     // no programme on board
    SHOW_READY,        // uploaded and checked; on the pad, waiting for permission
    SHOW_REFUSED,      // asked to launch and told no, with the reason
    SHOW_CLIMB,        // straight up off the pad, to the height act one starts at
    SHOW_MOVE,         // flying a baked transition, lane and all
    SHOW_HOLD,         // on the formation point, holding it
    SHOW_HOVER,        // no position at all: the three seconds before committing
    SHOW_RETURN,       // on the way to this station's own slot
    SHOW_AT_SLOT,      // in the slot, holding, programme over or failed
    SHOW_LANDED,       // the FC has the landing; the programme is done
};

const char* showPhaseName(ShowPhase p);

struct ShowStatus {
    ShowPhase phase = SHOW_IDLE;
    int station = 0;
    int act = 0;              // 1-based, what the frame publishes; 0 before act 1
    int act_count = 0;
    // A checksum of the programme record this airframe accepted, so the ground
    // station can tell "it has a three-act show" from "it has *this* three-act show".
    // Zero means nothing has been uploaded since boot.
    uint16_t plan_ck = 0;
    int acts_done = 0;        // acts flown to the end of their hold
    float u = 0.0f;           // 0..1 through the whole act: approach then hold
    uint32_t colour = 0;      // what the programme wants on the pixels
    ShowPoint target = {};    // the point being commanded, local NED
    ShowPoint rtk = {};       // where the receiver says this aircraft is
    ShowPoint fc = {};        // where the flight controller says it is
    float path_error_m = 0.0f;    // |rtk - target|, the number a show is judged on
    uint8_t fix_type = 0;
    uint8_t carr_soln = 0;
    uint8_t sats = 0;
    uint16_t hacc_cm = 0xFFFF;
    PositionSource source = POS_NONE;
    float batt_pct = 0.0f;
    float batt_v = 0.0f;
    uint32_t uptime_ms = 0;
    uint32_t setpoints = 0;   // position targets put on the wire
    uint32_t phase_ms = 0;
    bool armed = false;
    bool guided = false;
    bool fc_alive = false;
    bool link_up = false;
    bool safe_closed = false;
    bool has_plan = false;
    // True from the moment this airframe decides to come home by itself. It is not
    // the same as `why` being set: a programme that runs to its end also returns to
    // its slot, and that is not something a person on the ground should hear.
    bool failsafe = false;
    char why[20] = "";
};

void showInit();

// The upload. Parses the compact plan, refuses it outright if the aircraft cannot
// fly it, and keeps only this station's lane. `why` says which of the two happened.
bool showUpload(const char* text, size_t len, char* why, size_t cap);

// The station this airframe is. Set from NVS at boot or from the ground, and it
// re-derives the lane from the plan already on board.
void showSetStation(int station);
int showStation();

void showRequestLaunch();
void showLand();
void showNoteGcsContact();
bool showLinkIsUp();
const char* showReason();

// A MAVLink frame that came off the flight controller's UART. The heartbeats and
// the FC's own position estimate both arrive here; the show state machine is the
// only consumer, so the link is not filtered twice.
void showOnMavlink(const MavMessage& m);

// 0..1 through one act: the approach and the hold together, which is what a person
// watching the timeline on the dashboard means by "how far into act two are we".
float actProgress(uint32_t act_ms, float move_s, float hold_s);

// One tick of the programme, at SHOW_SETPOINT_HZ.
void showTick(uint32_t dt_ms);

const ShowStatus& showStatus();
bool showCanLaunch(char* why, size_t cap);

// The fuel gate, and the state of charge the board's own divider is reading.
float showBatteryPct();
float showBatteryVolts();
// True when the pack holds enough to fly the programme, get home, and leave the
// crew two minutes of reserve. Deliberately the same arithmetic as the ground
// station's SHOW_BATTERY check; test_sandbox.cpp walks every percentage and
// asserts the two verdicts never differ.
bool showFuelMayFly(const ShowPlan& plan, float pct);

// The leg from the pad to the first formation point, which show_core's own speed
// check does not cover because the ground station never has to leave the ground.
bool showLaunchLegFlyable(const ShowPlan& plan, const ShowLane& lane);

}  // namespace ranch
