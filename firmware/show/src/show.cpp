#include "show.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "hal.h"
#include "mavlink.h"

namespace ranch {
namespace {

constexpr size_t SHOW_WHY_LEN = 20;

ShowStatus st;
ShowPlan plan;
ShowLane lane;
bool have_plan = false;

// The workspace an upload needs to find this station's point in a fleet-wide
// programme: one row per station per act, because the routing repair only knows which
// destination belongs to which airframe after it has seen all of them. It costs
// 24,576 B of .bss (`nm -S` on the host build: lane_table = 0x6000), 19,456 B more than
// the three rows plus order vector it replaces -- paid because the alternative is the
// aircraft flying a routing nobody certified. None of it is touched in flight: the lane
// the airframe actually follows is SHOW_MAX_ACTS points.
ShowPoint lane_table[SHOW_MAX_ACTS * SHOW_MAX_DRONES];

bool buildLane(const ShowPlan& p, int station, ShowLane& out) {
    return planBuildLane(p, station, out, lane_table);
}

// --- the ground station, as far as this aircraft knows ----------------------
uint32_t gcs_ms = 0;
bool gcs_seen = false;

// --- the flight controller's own reports ------------------------------------
uint32_t fc_hb_ms = 0;
bool fc_heartbeat = false;
bool fc_armed = false;
uint32_t fc_mode = 0xFFFFFFFFu;
LocalNed fc_pos{};

// --- what the programme is doing -------------------------------------------
bool launch_pending = false;      // cleared on the next GNSS second edge
uint32_t act_ms = 0;              // time inside the current act
ShowPhase hover_from = SHOW_IDLE; // where a hover was interrupted
ShowPhase hover_back = SHOW_IDLE; // and what a resume has to go back to
uint32_t cmd_ms = 0;              // retry timer for the arm/mode commands
uint32_t boot_ms = 0;

void clearReason() { st.why[0] = '\0'; }

void setReason(const char* r) { std::snprintf(st.why, sizeof(st.why), "%s", r); }

bool inFlight(ShowPhase p) {
    return p == SHOW_CLIMB || p == SHOW_MOVE || p == SHOW_HOLD || p == SHOW_HOVER
        || p == SHOW_RETURN || p == SHOW_AT_SLOT;
}

// The height every act in this programme starts at. The aircraft gets there
// straight up, on its own axis, before the baked timeline starts: a copter cannot
// climb at the speed it can fly sideways, and a launch leg that asks it to fly 44 m
// of diagonal in six seconds is a leg it will not make.
float standoffD(const ShowLane& l) {
    return l.act_count > 0 ? l.at[0].d : 0.0f;
}

// One shared headroom figure for every pixel this aircraft lights, so the strip
// budget in board.h and what the crew dial in are the same number and not two.
//
// Read from NVS on every paint rather than cached: an nvs_get_i32 on a namespace
// this small is tens of microseconds, ten times a second is nothing next to the
// 800 us the strip itself steals with interrupts off, and the alternative is a
// cache that has to be invalidated from the command handler -- two places that
// have to agree about when the number changed.
uint32_t ledBrightness() {
    int32_t v = SHOW_LED_FULL_PCT;
    if (!nvGetI32("led_pct", v) || v < 20 || v > 100) v = SHOW_LED_FULL_PCT;
    return static_cast<uint32_t>(v);
}

uint32_t dimColour(uint32_t rgb, unsigned n) {
    if (n == 0) return rgb;
    const uint32_t k = ledBrightness() / static_cast<uint32_t>(n);
    const uint32_t r = ((rgb >> 16) & 0xFFu) * k / 100u;
    const uint32_t g = ((rgb >> 8) & 0xFFu) * k / 100u;
    const uint32_t b = (rgb & 0xFFu) * k / 100u;
    return (r << 16) | (g << 8) | b;
}

// The coarse charge curve the on-board gauge reads the pack off. Deliberately
// coarser than the model in hal_sim.cpp: a firmware carrying a 1 % resolution table
// would only be pretending to know, and the two disagreeing by a few points is what
// the sandbox measures, because a reversed or transposed table has to show up as a
// fuel figure that does not follow the pack.
struct OcvPoint { float volts; float pct; };
const OcvPoint kOcv[] = {
    {3.30f, 0.0f}, {3.55f, 15.0f}, {3.68f, 35.0f}, {3.78f, 55.0f},
    {3.90f, 72.0f}, {4.03f, 87.0f}, {4.20f, 100.0f},
};

float packPct(float volts) {
    const float cell = volts / static_cast<float>(BATT_CELLS);
    const size_t n = sizeof(kOcv) / sizeof(kOcv[0]);
    if (cell <= kOcv[0].volts) return kOcv[0].pct;
    if (cell >= kOcv[n - 1].volts) return kOcv[n - 1].pct;
    for (size_t i = 1; i < n; ++i) {
        if (cell <= kOcv[i].volts) {
            const float span = kOcv[i].volts - kOcv[i - 1].volts;
            const float at = (cell - kOcv[i - 1].volts) / (span > 0.001f ? span : 0.001f);
            return kOcv[i - 1].pct + at * (kOcv[i].pct - kOcv[i - 1].pct);
        }
    }
    return kOcv[n - 1].pct;
}

float distanceTo(const ShowPoint& a, const ShowPoint& b) {
    const float dn = a.n - b.n, de = a.e - b.e, dd = a.d - b.d;
    return std::sqrt(dn * dn + de * de + dd * dd);
}

void sendTarget(const ShowPoint& p) {
    PositionTargetTx t{};
    t.x = p.n;
    t.y = p.e;
    t.z = p.d;
    t.yaw_rad = SHOW_HEADING_DEG * 0.0174532925f;
    t.type_mask = TYPEMASK_POSITION_YAW;
    t.coordinate_frame = FRAME_LOCAL_NED;

    uint8_t pl[64];
    MavWriter w(pl, sizeof(pl));
    t.pack(w, halMillis() - boot_ms);
    uint8_t frame[MAV_FRAME_MAX];
    MavFrameBuilder fb(1, 190);           // this board is a companion computer
    const size_t n = fb.build(MSG_SET_POSITION_TARGET_LOCAL_NED, pl, w.size(), frame, sizeof(frame));
    if (n) {
        fcWrite(frame, n);
        st.setpoints++;
    }
}

void sendCommand(uint16_t command, float p1, float p2) {
    CommandLongTx c{};
    c.p[0] = p1;
    c.p[1] = p2;
    for (int i = 2; i < 7; ++i) c.p[i] = 0.0f;
    c.command = command;
    c.target_sys = 1;
    c.target_comp = 1;                    // ArduPilot's autopilot component id
    uint8_t pl[64];
    MavWriter w(pl, sizeof(pl));
    c.pack(w);
    uint8_t frame[MAV_FRAME_MAX];
    MavFrameBuilder fb(1, 190);
    const size_t n = fb.build(MSG_COMMAND_LONG, pl, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void requestArmAndGuided() {
    sendCommand(CMD_DO_SET_MODE, 1.0f, static_cast<float>(FC_MODE_GUIDED));
    sendCommand(CMD_COMPONENT_ARM_DISARM, 1.0f, 0.0f);
}

void paint(uint32_t arm, uint32_t beacon) {
    ledSetPixel(LED_PIXEL_ARM, arm);
    ledSetPixel(LED_PIXEL_BEACON, beacon);
    ledCommit();
}

// The failsafe light: the arm pixel goes flat red and the beacon strobes. This is
// the one thing that tells a person on the ground which of the twenty-four came
// home early, so it is deliberately not the programme's colour.
void paintFailsafe(uint32_t now_ms) {
    const bool blink = ((now_ms / 250u) & 1u) != 0u;
    paint(dimColour(0x800000u, 1), blink ? dimColour(0xff0000u, 1) : 0x000000u);
}

void beginHover() {
    hover_from = st.phase;
    hover_back = st.phase;
    st.phase = SHOW_HOVER;
    st.phase_ms = 0;
    st.failsafe = true;
    setReason("RTK LOST");
}

void beginReturn(const char* why) {
    hover_from = SHOW_IDLE;
    st.phase = SHOW_RETURN;
    st.phase_ms = 0;
    if (why != nullptr && *why != '\0') setReason(why);
}

void enterAct(int one_based) {
    st.act = one_based;
    st.phase = SHOW_MOVE;
    st.phase_ms = 0;
    act_ms = 0;
    st.colour = lane.colour[one_based - 1];
}

// The point this act is built between. Acts are 1-based on the wire and 0-based in
// the lane, and the first act starts from the pad rather than from a predecessor.
void actEnds(int one_based, ShowPoint& from, ShowPoint& to, float& move_s, float& hold_s) {
    const int i = (one_based > 0 ? one_based : 1) - 1;
    // Act one starts where the climb ended: above the origin, at the height the
    // formation is drawn at, with no horizontal distance left to cover.
    from = (i <= 0) ? ShowPoint{0.0f, 0.0f, lane.at[0].d} : lane.at[i - 1];
    to = lane.at[i];
    move_s = plan.acts[i].move_s;
    hold_s = plan.acts[i].hold_s;
}

// Which position this aircraft believes in right now. The receiver is the
// authority; when it has gone quiet the flight controller's own estimate is used,
// because the only thing it is good for is noticing that we arrived somewhere.
ShowPoint whereWeAre() {
    if (st.source != POS_NONE) return st.rtk;
    if (fc_pos.valid) return st.fc;
    return st.rtk;
}

void tickGround() {
    // On the pad nothing moves until the permission is given, and then it does not
    // move *when* the permission is given either: it moves on a GNSS second, so
    // that every other airframe in the show starts its baked timeline on the same
    // one. A fleet that starts on its own boot time is a fleet of near-misses.
    st.phase_ms += SHOW_LOOP_MS;
    if (!launch_pending) return;
    if (!rtkPpsEdge()) return;
    launch_pending = false;
    st.armed = false;
    st.guided = false;
    cmd_ms = 0;
    st.failsafe = false;
    requestArmAndGuided();
    // NAV_TAKEOFF is what arms the climb on ArduCopter: it is the one command that
    // says "leave the ground" without also saying "and then fly somewhere", which is
    // what the baked path is for.
    sendCommand(CMD_NAV_TAKEOFF, -standoffD(lane), 0.0f);
    st.phase = SHOW_CLIMB;
    st.phase_ms = 0;
    act_ms = 0;
    st.acts_done = 0;
    st.target = ShowPoint{0.0f, 0.0f, standoffD(lane)};
    clearReason();
}

void tickFlight(uint32_t now_ms, uint32_t dt) {
    st.phase_ms += dt;
    const bool link_up = showLinkIsUp();
    const PositionSource src = rtkSource();

    // 1. The two ways the programme is abandoned, looked at before anything can
    //    advance it, so no ordering of the switch below can step into the next act
    //    after a loss has been seen in this same tick.
    if (src == POS_NONE && (st.phase == SHOW_CLIMB || st.phase == SHOW_MOVE || st.phase == SHOW_HOLD)) {
        beginHover();
    } else if (!link_up && (st.phase == SHOW_CLIMB || st.phase == SHOW_MOVE || st.phase == SHOW_HOLD
                            || st.phase == SHOW_HOVER)) {
        // No supervising station means nobody is watching this airframe, and the
        // next act is exactly the moment at which it would like to be watched.
        st.failsafe = true;
        setReason("LINK LOST");
        beginReturn("LINK LOST");
    }

    // 2. The phase machine.
    switch (st.phase) {
        case SHOW_HOVER: {
            if (src != POS_NONE) {
                // The shadow passed. Pick the same act back up at the same place in
                // it: the aircraft held position throughout, so the path it was on
                // is still the path it is on.
                st.phase = hover_back;
                hover_from = SHOW_IDLE;
                st.failsafe = false;
                clearReason();
            } else if (st.phase_ms >= SHOW_HOVER_MS) {
                beginReturn("RTK LOST");
            }
            break;
        }
        case SHOW_CLIMB: {
            st.target = ShowPoint{0.0f, 0.0f, standoffD(lane)};
            st.u = 0.0f;
            // "High enough to start the programme." The FC's own tolerance on a
            // takeoff, not the show's arrival radius: what matters here is that the
            // aircraft is at the height the first act is drawn at, not that it is
            // within 30 cm of a point it has not started flying to yet.
            if (std::fabs(whereWeAre().d - standoffD(lane)) < 1.0f) enterAct(1);
            break;
        }
        case SHOW_MOVE: {
            ShowPoint from{}, to{};
            float move_s = 0.0f, hold_s = 0.0f;
            actEnds(st.act, from, to, move_s, hold_s);
            act_ms += dt;
            const float u = move_s > 0.0f
                ? static_cast<float>(act_ms) / (move_s * 1000.0f) : 1.0f;
            st.target = showPathLane(from, to, u, planLaneOffset(lane, st.act - 1, plan.separation_m));
            if (st.phase == SHOW_MOVE) st.u = actProgress(act_ms, move_s, hold_s);
            if (u >= 1.0f) {
                st.phase = SHOW_HOLD;
                st.phase_ms = 0;
                st.target = to;
            }
            break;
        }
        case SHOW_HOLD: {
            ShowPoint from{}, to{};
            float move_s = 0.0f, hold_s = 0.0f;
            actEnds(st.act, from, to, move_s, hold_s);
            st.target = to;
            act_ms += dt;
            st.u = actProgress(act_ms, move_s, hold_s);
            if (st.phase_ms >= static_cast<uint32_t>(hold_s * 1000.0f)) {
                if (st.act < lane.act_count) {
                    st.acts_done = st.act;
                    enterAct(st.act + 1);
                } else {
                    // The programme is over. Everybody goes to their own square: a
                    // fleet that finishes by sitting 24 aircraft in one formation is
                    // a fleet waiting for the first gust.
                    st.acts_done = st.act;
                    // The programme ended; nothing went wrong. The reason is still
                    // published, because a ground app that sees an aircraft sitting
                    // in a return grid should not have to guess why.
                    beginReturn("SHOW OVER");
                }
            }
            break;
        }
        case SHOW_RETURN:
        case SHOW_AT_SLOT: {
            st.target = showReturnSlot(plan, lane.station);
            st.u = 1.0f;
            if (distanceTo(whereWeAre(), st.target) < SHOW_ARRIVAL_M) {
                if (st.phase != SHOW_AT_SLOT) {
                    st.phase = SHOW_AT_SLOT;
                    st.phase_ms = 0;
                }
            } else if (st.phase == SHOW_AT_SLOT) {
                // Wind took it off the slot. It is not "in" any more.
                st.phase = SHOW_RETURN;
            }
            break;
        }
        default:
            break;
    }

    // 3. Arm and mode are re-asked until the FC agrees. A command sent once at the
    //    wrong moment is a command that never happened, and the only evidence is
    //    that nothing moves.
    if (!fc_armed || fc_mode != static_cast<uint32_t>(FC_MODE_GUIDED)) {
        cmd_ms += dt;
        if (cmd_ms >= 500u) { cmd_ms = 0; requestArmAndGuided(); }
    }
    st.armed = fc_armed;
    st.guided = (fc_mode == static_cast<uint32_t>(FC_MODE_GUIDED));

    // 4. The lights, then the setpoint. The strip is written first because
    //    committing it is the one thing on this path that runs with interrupts
    //    off, and it must not delay a position target.
    const bool failed = st.why[0] != '\0' && st.phase != SHOW_MOVE && st.phase != SHOW_HOLD;
    if (failed) paintFailsafe(now_ms);
    else paint(dimColour(st.colour, 1), dimColour(st.colour, 1));

    sendTarget(st.target);
}

}  // namespace

const char* showPhaseName(ShowPhase p) {
    switch (p) {
        case SHOW_IDLE:     return "IDLE";
        case SHOW_READY:    return "READY";
        case SHOW_REFUSED:  return "REFUSED";
        case SHOW_CLIMB:    return "CLIMB";
        case SHOW_MOVE:     return "MOVE";
        case SHOW_HOLD:     return "HOLD";
        case SHOW_HOVER:    return "HOVER";
        case SHOW_RETURN:   return "RETURN";
        case SHOW_AT_SLOT:  return "SLOT";
        case SHOW_LANDED:   return "LANDED";
    }
    return "?";
}

float actProgress(uint32_t act_ms, float move_s, float hold_s) {
    const float total = (move_s + hold_s) * 1000.0f;
    if (total <= 0.0f) return 1.0f;
    const float u = static_cast<float>(act_ms) / total;
    return u > 1.0f ? 1.0f : u;
}

void showInit() {
    st = ShowStatus{};
    plan = ShowPlan{};
    lane = ShowLane{};
    have_plan = false;
    gcs_ms = 0;
    gcs_seen = false;
    fc_hb_ms = 0;
    fc_heartbeat = false;
    fc_armed = false;
    fc_mode = 0xFFFFFFFFu;
    fc_pos = LocalNed{};
    launch_pending = false;
    act_ms = 0;
    hover_from = SHOW_IDLE;
    hover_back = SHOW_IDLE;
    cmd_ms = 0;
    boot_ms = halMillis();
    st.station = 0;
    st.phase = SHOW_IDLE;
    st.act_count = 0;
    clearReason();
}

bool showFuelMayFly(const ShowPlan& p, float pct) {
    const float need_s = showDuration(p) + SHOW_RTL_RESERVE_S + SHOW_PAD_RESERVE_S;
    return (pct / 100.0f) * SHOW_PACK_HOVER_S >= need_s;
}

bool showLaunchLegFlyable(const ShowPlan& p, const ShowLane& l) {
    if (l.act_count <= 0) return false;
    const float dt = p.acts[0].move_s;
    if (dt <= 0.0f) return false;
    // The horizontal part only: the vertical part of getting to the first act is
    // the climb's business, and it is bounded by the FC's own climb rate rather
    // than by the programme's clock. The same 1.5x the smoothstep costs at its
    // steepest point that the ground station's SHOW_TOO_FAST check applies to every
    // leg between acts -- and the launch leg is missing there because the ground
    // station never has to leave the pad.
    const float dn = l.at[0].n - l.pad.n;
    const float de = l.at[0].e - l.pad.e;
    const float v = 1.5f * std::sqrt(dn * dn + de * de) / dt;
    return v <= p.max_speed_ms;
}

bool showCanLaunch(char* why, size_t cap) {
    const char* reason = nullptr;
    const uint32_t now = halMillis();
    if (!have_plan) reason = "NO PROGRAMME";
    else if (!safeSwitchIsClosed()) reason = "SAFE SWITCH";
    else if (!rtkOriginSet()) reason = "NO ORIGIN";
    else if (!showLaunchLegFlyable(plan, lane)) reason = "LEG TOO FAST";
    else if (rtkSource() == POS_NONE) reason = "NO FIX";
    // The fleet launches on a fixed solution. It can finish on a single one, which
    // is why the two rules differ: on the pad there is no reason at all to accept
    // decimetres, and in the air there is nothing better to be had.
    else if (rtkSource() != POS_RTK) reason = "NO RTK FIX";
    else if (!fc_heartbeat || now - fc_hb_ms > SHOW_FC_STALE_MS) reason = "NO FC LINK";
    else if (!showLinkIsUp()) reason = "NO GCS LINK";
    else if (!showFuelMayFly(plan, packPct(batteryVolts()))) reason = "LOW BATTERY";
    else if (st.phase != SHOW_READY) reason = "NOT READY";

    if (reason == nullptr) return true;
    if (why && cap) std::snprintf(why, cap, "%s", reason);
    return false;
}

bool showUpload(const char* text, size_t len, char* why, size_t cap) {
    if (why && cap) why[0] = '\0';
    if (text == nullptr || len == 0) {
        if (why && cap) std::snprintf(why, cap, "EMPTY PLAN");
        return false;
    }
    char buf[SHOW_PLAN_MAX_TEXT];
    if (len >= sizeof(buf)) {
        if (why && cap) std::snprintf(why, cap, "PLAN TOO LONG");
        return false;
    }
    std::memcpy(buf, text, len);
    buf[len] = '\0';

    ShowPlan parsed{};
    if (!planParse(buf, parsed)) {
        if (why && cap) std::snprintf(why, cap, "PLAN CORRUPT");
        return false;
    }
    // The go/no-go table the ground station ran is run again here. It is not trust
    // in the operator that is the problem: a plan that reached this UART through a
    // repeater is a plan somebody else may have typed.
    const uint32_t why_bits = showValidate(parsed, packPct(batteryVolts()), 0.0f);
    if (why_bits != SHOW_OK) {
        if (why && cap) std::snprintf(why, cap, "PLAN REJECTED %lu", static_cast<unsigned long>(why_bits));
        return false;
    }
    ShowLane built{};
    if (!buildLane(parsed, st.station, built)) {
        if (why && cap) std::snprintf(why, cap, "NO LANE FOR STATION");
        return false;
    }
    if (!showLaunchLegFlyable(parsed, built)) {
        if (why && cap) std::snprintf(why, cap, "LEG TOO FAST");
        return false;
    }

    // The ack the ground station reads back, over the exact bytes it sent. Computed
    // here and assigned only on success: a refused upload leaves the previous
    // programme on board, and reporting ck=0 then would say "nothing loaded", which
    // is not what happened.
    const uint16_t ck = planChecksum(buf, len);

    plan = parsed;
    lane = built;
    have_plan = true;
    st.has_plan = true;
    st.act_count = lane.act_count;
    st.plan_ck = ck;
    st.act = 0;
    st.acts_done = 0;
    st.u = 0.0f;
    st.colour = lane.colour[0];
    st.phase = SHOW_READY;
    st.phase_ms = 0;
    launch_pending = false;
    st.failsafe = false;
    clearReason();
    showNoteGcsContact();
    return true;
}

void showSetStation(int station) {
    if (station < 0) station = 0;
    if (station >= SHOW_MAX_DRONES) station = SHOW_MAX_DRONES - 1;
    st.station = station;
    lane.station = station;
    if (have_plan) {
        ShowLane rebuilt{};
        if (buildLane(plan, station, rebuilt)) {
            lane = rebuilt;
            st.act_count = lane.act_count;
            st.colour = lane.colour[0];
        }
    }
    nvSetI32("station", station);
}

int showStation() { return st.station; }

void showRequestLaunch() {
    char why[SHOW_WHY_LEN];
    if (!showCanLaunch(why, sizeof(why))) {
        if (have_plan) st.phase = SHOW_REFUSED;
        setReason(why);
        return;
    }
    // Throw away the second we are standing in. The receiver's TIMEPULSE is latched
    // by an interrupt until somebody reads it, and an edge from before the launch
    // was asked for is not the boundary the rest of the fleet is about to start on.
    while (rtkPpsEdge()) {}
    launch_pending = true;
    setReason("WAIT PPS");
}

void showLand() {
    if (!inFlight(st.phase)) return;
    sendCommand(CMD_NAV_LAND, 0.0f, 0.0f);
    st.phase = SHOW_LANDED;
    st.phase_ms = 0;
    st.u = 1.0f;
    setReason("LANDED ON REQUEST");
}

void showNoteGcsContact() {
    gcs_ms = halMillis();
    gcs_seen = true;
}

bool showLinkIsUp() {
    if (!gcs_seen || !wifiUp()) return false;
    return (halMillis() - gcs_ms) < SHOW_LINK_TIMEOUT_MS;
}

const char* showReason() { return st.why; }

void showOnMavlink(const MavMessage& m) {
    if (m.id == MSG_HEARTBEAT) {
        const Heartbeat h = decodeHeartbeat(m);
        if (!h.alive) return;
        fc_heartbeat = true;
        fc_hb_ms = halMillis();
        fc_armed = h.armed;
        fc_mode = h.custom_mode;
        return;
    }
    if (m.id == MSG_LOCAL_POSITION_NED) {
        const LocalNed l = decodeLocalPosition(m);
        if (l.valid) fc_pos = l;
    }
}

void showTick(uint32_t dt_ms) {
    const uint32_t now = halMillis();
    const RtkState& r = rtkState();

    st.source = rtkSource();
    st.fix_type = r.fix_type;
    st.carr_soln = r.carr_soln;
    st.sats = r.sats;
    st.hacc_cm = rtkHaccCm();
    st.rtk.n = r.n;
    st.rtk.e = r.e;
    st.rtk.d = r.d;
    if (fc_pos.valid) {
        st.fc.n = fc_pos.n;
        st.fc.e = fc_pos.e;
        st.fc.d = fc_pos.d;
    }
    st.batt_v = batteryVolts();
    st.batt_pct = packPct(st.batt_v);
    st.link_up = showLinkIsUp();
    st.fc_alive = fc_heartbeat && (now - fc_hb_ms) <= SHOW_FC_STALE_MS;
    st.safe_closed = safeSwitchIsClosed();
    st.uptime_ms = now - boot_ms;

    if (inFlight(st.phase)) {
        st.path_error_m = distanceTo(whereWeAre(), st.target);
        tickFlight(now, dt_ms);
    } else if (st.phase == SHOW_READY) {
        st.path_error_m = 0.0f;
        tickGround();
        // The strip shows the first act's colour at a fifth of its current while it
        // waits: a crew checking a line-up needs to see which aircraft is which
        // without two hundred pixels loading the BEC on the pad.
        paint(dimColour(st.colour, 5), dimColour(st.colour, 5));
    } else {
        st.path_error_m = 0.0f;
        paint(0x000000u, 0x000000u);
    }
}

const ShowStatus& showStatus() { return st; }

float showBatteryPct() { return st.batt_pct; }
float showBatteryVolts() { return st.batt_v; }

}  // namespace ranch
