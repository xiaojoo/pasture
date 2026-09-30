// Host sandbox: the real application for one show aircraft, against a model of the
// things it is wired to -- a receiver that only answers when it has a solution, a
// flight controller that follows position targets at a bounded speed, a light strip
// that shows what was latched into it, and a pack that runs down.
//
// Nothing here reimplements firmware behaviour. It calls appSetup / appLoop, feeds
// the same command channel the ground station uses, and judges what the firmware
// reported, so a change to the programme, the RTK decode, the MAVLink layout or the
// failsafe table is caught by this and not by a transcription of it.
//
// Build and run:
//   g++ -std=c++17 -Wall -Werror -O1 -DRANCH_SIM -DRANCH_HOST
//       -Ifirmware/show/src -Ifirmware/lib
//       firmware/show/src/*.cpp firmware/show/test/test_sandbox.cpp -o show-sandbox
//
// Time is virtual: the whole programme, both acts and every failsafe, runs in
// milliseconds of CPU.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "mavlink.h"
#include "plan.h"
#include "rtk.h"
#include "show.h"
#include "show_core.h"
#include "telemetry.h"

namespace ranch {
void appSetup();
void appLoop();
void simCommand(const char*);
void simConsoleInput(const char* data, size_t len);
}  // namespace ranch

using namespace ranch;

namespace {

int checks = 0, fails = 0;

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    fails++;
    std::printf("  FAIL test_sandbox.cpp:%d  %s\n", line, what);
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

// --- the programme this airframe is flown with ------------------------------
// The same show, written twice: once as the bytes an upload is expected to produce
// and once as the structure the firmware builds them from. If the formatter and the
// parser are ever changed together into something that no longer matches the text,
// the byte-for-byte check fails -- which is the point, because the ground station
// pastes this string at the aircraft, it does not call this code.
const char* kProgrammeText =
    "SHOWPLAN,24,2.00,6.00,120.00,35.00,2"
    ";A,0,18.00,40.00,12.00,14.00,00e0a0"
    ";A,1,18.00,40.00,12.00,14.00,ffd000";

ShowPlan programme() {
    ShowPlan p{};
    p.drones = 24;
    p.separation_m = 2.0f;
    p.max_speed_ms = 6.0f;
    p.geofence_m = 120.0f;
    p.rtl_alt_m = 35.0f;
    p.act_count = 2;
    p.acts[0] = {SHAPE_RING, 18.0f, 40.0f, 12.0f, 14.0f, 0x00e0a0};
    p.acts[1] = {SHAPE_GRID, 18.0f, 40.0f, 12.0f, 14.0f, 0xffd000};
    return p;
}

constexpr int kStation = 7;
constexpr uint32_t kActColour[3] = {0x00e0a0, 0xffd000, 0x000000};
constexpr uint32_t STEP_MS = SHOW_LOOP_MS;    // one appLoop, from main.cpp
constexpr uint32_t RUN_MS = 130000;           // long enough for both acts and a return
// The pad the model flies from. It has to be the same surveyed point, or the
// aircraft is 300 km from where the programme says it is and the fix check that
// follows is meaningless.
const char* kPad = "origin=30.5000000,114.3000000,42.0";

enum FaultKind { F_BATT_LOW, F_FIX_SINGLE, F_FIX_BACK, F_FIX_NONE, F_HB_STOP, F_SAFE_OPEN };
struct Fault { uint32_t at_ms; FaultKind kind; };

// --- one run, and what it left behind --------------------------------------
struct Result {
    int max_act = 0;
    int acts_done = 0;
    bool held[4] = {false, false, false, false};
    float hold_err[4] = {0, 0, 0, 0};         // measured, at the end of each hold
    uint32_t hold_led[4] = {0, 0, 0, 0};      // what the strip was physically showing
    uint32_t hold_beacon[4] = {0, 0, 0, 0};
    bool hover = false;
    uint32_t hover_ms = 0;
    bool in_slot = false;
    float slot_err = 0.0f;
    float agreement = 0.0f;                   // |RTK - FC|, worst seen on a hold
    float path_err_max = 0.0f;
    uint32_t launch_ms = 0;
    uint32_t end_led0 = 0, end_led1 = 0;   // the pixels, as the last tick left them
    uint32_t red_at_ms = 0;                // first tick the arm pixel was red
    int red_act = 0;
    ShowPhase red_phase = SHOW_IDLE;
    char red_why[24] = "";
    int red_link = 0, red_src = 0;
    bool flew = false;
    char refused[24] = "";
    char last_why[24] = "";
    bool saw_single = false;
    bool saw_fixed = false;
    bool link_down = false;
    bool fc_alive = false;
    bool armed = false;
    uint8_t degraded_fix = 0;                 // the reported fix while on a single
    uint8_t degraded_rtk = 9;
    uint32_t setpoints = 0;
    uint32_t wdt = 0, commits = 0, buzzer_ms = 0, rtk_frames = 0, cfg_frames = 0, fc_frames = 0;
    uint16_t mask = 0;
    float batt_start = 0.0f, batt_end = 0.0f;
    ShowPhase end_phase = SHOW_IDLE;
    int fix_at_end = 0;
    int rtk_at_end = 0;
    uint32_t frames_published = 0;
    uint32_t frame_bytes = 0;
};

void applyFault(FaultKind k) {
    switch (k) {
        case F_BATT_LOW:    halSimBatteryPct(5.0f); break;
        case F_FIX_SINGLE:  halSimFix(SIM_FIX_SINGLE); break;
        case F_FIX_BACK:    halSimFix(SIM_FIX_RTK_FIXED); break;
        case F_FIX_NONE:    halSimFix(SIM_FIX_NONE); break;
        case F_SAFE_OPEN:   halSimSafeSwitch(false); break;
        case F_HB_STOP:     break;               // the loop's own heartbeat flag
    }
}

Result run(const Fault* faults, int n) {
    Result r;
    halSimClear();
    appSetup();
    // The three things a ground station does before a show, in this order: point
    // the aircraft at the pad it is standing on, tell it which station it is, and
    // hand it the programme. A lane cannot be built without the number, and the
    // number is meaningless without the shape it applies to.
    simCommand(kPad);
    char station[16];
    std::snprintf(station, sizeof(station), "station=%d", kStation);
    simCommand(station);
    char cmd[1200];
    std::snprintf(cmd, sizeof(cmd), "show=%s", kProgrammeText);
    simCommand(cmd);

    bool hb_alive = true;
    bool asked = false;
    ShowPhase prev = SHOW_IDLE;
    SimCounters c{};

    for (uint32_t t = 0; t < RUN_MS; t += STEP_MS) {
        for (int i = 0; i < n; ++i) {
            if (faults[i].at_ms != t) continue;
            if (faults[i].kind == F_HB_STOP) hb_alive = false;
            else applyFault(faults[i].kind);
        }
        // The ground station's own heartbeat, twice a second: the only thing that
        // keeps the link timeout from firing, so taking it away is the same as
        // walking out of range with the telemetry radio.
        if (hb_alive && (t % 500u) == 0u) simCommand("hb");
        if (!asked && t >= 2500u) { asked = true; simCommand("start"); }

        appLoop();

        const ShowStatus& s = showStatus();
        if (s.fc_alive) r.fc_alive = true;
        if (s.armed) r.armed = true;
        if (!s.link_up && s.phase != SHOW_IDLE && s.phase != SHOW_READY) r.link_down = true;
        if (s.source == POS_SINGLE && s.act > 0) {
            r.saw_single = true;
            r.degraded_fix = s.fix_type;
            r.degraded_rtk = s.carr_soln;
        }
        if (s.source == POS_RTK && s.act > 0) r.saw_fixed = true;
        if (s.phase == SHOW_REFUSED && !r.refused[0] && s.why[0]) {
            std::snprintf(r.refused, sizeof(r.refused), "%s", s.why);
        }
        // Sampled every tick, cleared when the firmware clears it: a reason that
        // outlived the state that set it would be reported as if the aircraft were
        // still in it.
        std::snprintf(r.last_why, sizeof(r.last_why), "%s", s.why);
        if (s.phase != prev) {
            if ((s.phase == SHOW_CLIMB || s.phase == SHOW_MOVE) && prev < SHOW_CLIMB) {
                r.launch_ms = t;
                r.flew = true;
            }
            prev = s.phase;
        }
        if (s.act > r.max_act) r.max_act = s.act;
        r.acts_done = s.acts_done;
        if (s.phase == SHOW_HOLD && s.act >= 1 && s.act <= 3) {
            r.held[s.act] = true;
            const float dn = s.rtk.n - s.target.n;
            const float de = s.rtk.e - s.target.e;
            const float dd = s.rtk.d - s.target.d;
            r.hold_err[s.act] = std::sqrt(dn * dn + de * de + dd * dd);
            const float an = s.rtk.n - s.fc.n, ae = s.rtk.e - s.fc.e, ad = s.rtk.d - s.fc.d;
            const float ag = std::sqrt(an * an + ae * ae + ad * ad);
            if (ag > r.agreement) r.agreement = ag;
            halSimCounters(c);
            r.hold_led[s.act] = c.led_pixel[0];
            r.hold_beacon[s.act] = c.led_pixel[1];
        }
        if (s.phase == SHOW_HOVER) { r.hover = true; r.hover_ms += STEP_MS; }
        if (s.phase == SHOW_AT_SLOT) { r.in_slot = true; r.slot_err = s.path_error_m; }
        if (s.path_error_m > r.path_err_max) r.path_err_max = s.path_error_m;
        if (t < 4000u) r.batt_start = s.batt_pct;
        r.batt_end = s.batt_pct;
        r.end_phase = s.phase;
        r.fix_at_end = s.fix_type;
        r.rtk_at_end = s.carr_soln;
        halSimCounters(c);
        r.end_led0 = c.led_pixel[0];
        r.end_led1 = c.led_pixel[1];
        if (!r.red_at_ms && c.led_pixel[0] == 0x4c0000u) {
            r.red_at_ms = t;
            r.red_act = s.act;
            r.red_phase = s.phase;
            std::snprintf(r.red_why, sizeof(r.red_why), "%s", s.why);
            r.red_link = s.link_up ? 1 : 0;
            r.red_src = static_cast<int>(s.source);
        }
    }
    halSimCounters(c);
    r.setpoints = c.setpoints_latched;
    r.wdt = c.wdt_feeds;
    r.commits = c.led_commits;
    r.buzzer_ms = c.buzzer_ms;
    r.rtk_frames = c.rtk_frames;
    r.cfg_frames = c.cfg_frames;
    r.fc_frames = c.fc_frames_in;
    r.mask = c.last_type_mask;
    UplinkStats u{};
    telemetryStats(u);
    r.frames_published = u.published;
    r.frame_bytes = u.last_len;
    return r;
}

void printRun(const char* label, const Result& r) {
    std::printf("  [%-11s] flew=%s act=%d done=%d end=%s why=%s | hover=%ums slot=%s(%.2fm)"
                " hold_err=%.2f/%.2f\n",
                label, r.flew ? "yes" : "no", r.max_act, r.acts_done,
                showPhaseName(r.end_phase), r.last_why[0] ? r.last_why : "-",
                r.hover_ms, r.in_slot ? "yes" : "no", static_cast<double>(r.slot_err),
                static_cast<double>(r.hold_err[1]), static_cast<double>(r.hold_err[2]));
    std::printf("               setpoints=%u fc_in=%u rtk=%u cfg=%u wdt=%u commits=%u buzzer=%ums"
                " batt=%.0f->%.0f%% agree=%.2fm\n",
                r.setpoints, r.fc_frames, r.rtk_frames, r.cfg_frames, r.wdt, r.commits,
                r.buzzer_ms, static_cast<double>(r.batt_start), static_cast<double>(r.batt_end),
                static_cast<double>(r.agreement));
}

// The pixels are compared through the one brightness knob rather than against a
// number this file invents: the hue has to be the act's and the level has to be
// where board.h says the strip budget sits.
bool hueMatches(uint32_t pixel, uint32_t wanted, unsigned pct_low, unsigned pct_high) {
    unsigned hits = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const unsigned w = (wanted >> shift) & 0xFFu;
        const unsigned p = (pixel >> shift) & 0xFFu;
        if (w == 0) { if (p == 0) ++hits; continue; }
        const unsigned pct = p * 100u / w;
        if (pct >= pct_low && pct <= pct_high) ++hits;
    }
    return hits == 3;
}

}  // namespace

int main() {
    std::printf("show sandbox: one aircraft of a 24-ship, two-act programme, station %d\n", kStation);

    // --- the upload format, both ways round ---------------------------------
    std::printf("plan: the wire format, round tripped and pinned to bytes\n");
    {
        const ShowPlan p = programme();
        char text[SHOW_PLAN_MAX_TEXT];
        const size_t n = planFormat(p, text, sizeof(text));
        CHECK(n == std::strlen(kProgrammeText), "the upload is exactly as long as the text below");
        CHECK(std::strcmp(text, kProgrammeText) == 0,
              "the ground station's bytes and this build's bytes are the same bytes");
        ShowPlan back{};
        CHECK(planParse(text, back), "the text parses");
        CHECK(back.drones == p.drones && back.act_count == p.act_count, "count and acts survive");
        CHECK(std::fabs(back.separation_m - p.separation_m) < 0.005f
              && std::fabs(back.max_speed_ms - p.max_speed_ms) < 0.005f
              && std::fabs(back.geofence_m - p.geofence_m) < 0.005f
              && std::fabs(back.rtl_alt_m - p.rtl_alt_m) < 0.005f, "every limit comes back");
        bool same = true;
        for (int i = 0; i < p.act_count; ++i) {
            same = same && back.acts[i].shape == p.acts[i].shape
                && std::fabs(back.acts[i].scale_m - p.acts[i].scale_m) < 0.005f
                && std::fabs(back.acts[i].alt_m - p.acts[i].alt_m) < 0.005f
                && std::fabs(back.acts[i].hold_s - p.acts[i].hold_s) < 0.005f
                && std::fabs(back.acts[i].move_s - p.acts[i].move_s) < 0.005f
                && back.acts[i].colour == p.acts[i].colour;
        }
        CHECK(same, "and so does each act, including the colour, which is not a number");

        ShowPlan junk{};
        CHECK(!planParse("SHOWPLAN,24,2.00,6.00,120.00,35.00,2;A,0,18.00", junk),
              "an act cut off in the middle is refused, not flown");
        CHECK(!planParse("SHOWPLAN,24,2.00,6.00,120.00,35.00,2;B,0,18.00,40.00,12.00,14.00,00e0a0", junk),
              "a record type this build does not know is refused");
        CHECK(!planParse("SHOWPLAN,24,2.00,6.00,120.00,35.00,1;A,7,18.00,40.00,12.00,14.00,00e0a0", junk),
              "a shape number outside the sampled set is refused");
        CHECK(!planParse("SHOWPLN,24,2.00,6.00,120.00,35.00,1;A,0,18.00,40.00,12.00,14.00,00e0a0", junk),
              "and so is a header that is not ours");

        // The lane: this station's points, out of the whole fleet's geometry.
        static ShowPoint tbl[SHOW_MAX_ACTS * SHOW_MAX_DRONES];
        ShowLane lane{};
        CHECK(planBuildLane(p, kStation, lane, tbl), "station 7 has a lane");
        CHECK(lane.act_count == 2, "with both acts in it");
        CHECK(std::fabs(lane.at[0].n - 17.39f) < 0.02f && std::fabs(lane.at[0].e + 4.66f) < 0.02f,
              "the ring point the fleet's assignment gives it, not an index into the shape");
        CHECK(std::fabs(lane.at[1].n - 9.0f) < 0.02f && std::fabs(lane.at[1].e + 9.0f) < 0.02f,
              "and its grid point in act two");
        CHECK(planLaneOffset(lane, 1, p.separation_m) > 0.0f, "and a height layer to move through");
        CHECK(!planBuildLane(p, 99, lane, tbl),
              "a station that is not in the fleet gets no lane at all");

        // The repair has to reach the airframe. Two acts is the wrong programme to
        // prove it with -- nothing gets swapped there -- so take the one the ground
        // station only clears after re-routing: 24 ships, ring -> grid -> arc.
        ShowPlan three = p;
        three.act_count = 3;
        three.acts[2] = { SHAPE_ARC, 30.0f, 40.0f, 10.0f, 14.0f, 0x4040ff };
        static ShowPoint ground[SHOW_MAX_ACTS * SHOW_MAX_DRONES];
        int swaps = -1;
        CHECK(showCompilePlan(three, ground, SHOW_MAX_ACTS * SHOW_MAX_DRONES, &swaps) == 3,
              "the ground station compiles the three-act programme");
        CHECK(swaps > 0, "and it is a programme that needed the routing repaired");

        // What sample+assign alone -- the lane builder before the repair was wired in --
        // would have handed each airframe.
        static ShowPoint bare[SHOW_MAX_ACTS * SHOW_MAX_DRONES];
        {
            static ShowPoint cloud[SHOW_MAX_DRONES];
            for (int i = 0; i < three.act_count; ++i) {
                showSampleShape(static_cast<ShowShape>(three.acts[i].shape), three.drones,
                                three.acts[i].scale_m, three.acts[i].alt_m, cloud, SHOW_MAX_DRONES);
                for (int k = 0; k < three.drones; ++k) bare[i * three.drones + k] = cloud[k];
                if (i > 0) {
                    int ord[SHOW_MAX_DRONES];
                    showAssign(bare + (i - 1) * three.drones, bare + i * three.drones, three.drones, ord);
                    static ShowPoint held[SHOW_MAX_DRONES];
                    for (int k = 0; k < three.drones; ++k) held[k] = bare[i * three.drones + ord[k]];
                    for (int k = 0; k < three.drones; ++k) bare[i * three.drones + k] = held[k];
                }
            }
        }

        int mismatch = 0, moved = 0;
        for (int k = 0; k < three.drones; ++k) {
            const ShowPoint& a = bare[2 * three.drones + k];
            const ShowPoint& b = ground[2 * three.drones + k];
            if (std::fabs(a.n - b.n) > 0.001f || std::fabs(a.e - b.e) > 0.001f) ++moved;

            ShowLane l{};
            if (!planBuildLane(three, k, l, tbl)) { ++mismatch; continue; }
            for (int i = 0; i < three.act_count; ++i) {
                const ShowPoint& g = ground[i * three.drones + k];
                if (std::fabs(l.at[i].n - g.n) > 0.001f || std::fabs(l.at[i].e - g.e) > 0.001f
                    || std::fabs(l.at[i].d - g.d) > 0.001f) ++mismatch;
            }
        }
        std::printf("        lane vs the certified table: %d of %d airframes re-routed by the repair,"
                    " %d lanes disagree\n", moved, three.drones, mismatch);
        CHECK(moved > 0, "the repair really did move airframes this programme would otherwise fly");
        CHECK(mismatch == 0, "and every on-board lane is the point the ground station certified");
    }

    // --- the console a technician types into --------------------------------
    // The upload the ground station's button performs: bytes into UART0 RX, assembled
    // into a line by the firmware's own parser, then parsed, validated and acked. The
    // record below is the string ranch/js/show/plan.js emits for this programme --
    // .probe/cross.sh proves the two formatters agree on it byte for byte, and the
    // checksum asserted here is the number the page compares against the aircraft's
    // own frame. Going in through the console rather than through simCommand() is the
    // point: this exercises the line assembler, not just the handler at the end of it.
    std::printf("upload: the bytes the ground station's button sends, in through the console\n");
    {
        halSimClear();
        appSetup();
        simCommand(kPad);
        char station[16];
        std::snprintf(station, sizeof(station), "station=%d", kStation);
        simCommand(station);
        for (int i = 0; i < 40; ++i) appLoop();
        CHECK(showStatus().act_count == 0 && showStatus().plan_ck == 0,
              "nothing is on board before the console is used");

        char line[1200];
        std::snprintf(line, sizeof(line), "show=%s\n", kProgrammeText);
        const size_t len = std::strlen(line);
        simConsoleInput(line, 40);
        for (int i = 0; i < 40; ++i) appLoop();
        CHECK(showStatus().act_count == 0, "the first 40 bytes are not a programme yet");
        simConsoleInput(line + 40, 30);
        for (int i = 0; i < 40; ++i) appLoop();
        CHECK(showStatus().act_count == 0, "and a record with no terminator does not land");
        simConsoleInput(line + 70, len - 70);
        for (int i = 0; i < 40; ++i) appLoop();
        CHECK(showStatus().act_count == 2, "the terminator lands the programme");
        CHECK(showStatus().plan_ck == planChecksum(kProgrammeText, std::strlen(kProgrammeText)),
              "and the aircraft acks the exact bytes it was given");
        CHECK(showStatus().plan_ck == 4574u,
              "which is the same number the ground station's formatter computes");
        CHECK(showStatus().phase == SHOW_READY, "it waits on the pad for a start, not for the console");

        // A record that does not parse must leave the one already on board alone:
        // "refused" is not "erased".
        const uint16_t held = showStatus().plan_ck;
        simConsoleInput("show=SHOWPLAN,24,2.00,6.00,120.00,35.00,2;A,0,18.00\n", 52);
        for (int i = 0; i < 40; ++i) appLoop();
        CHECK(showStatus().plan_ck == held && showStatus().act_count == 2,
              "a corrupt upload is refused without wiping the programme that was loaded");
    }

    // --- the fuel gate, against the ground station's own arithmetic ---------
    std::printf("fuel: the on-board gate and show_core's SHOW_BATTERY, side by side\n");
    {
        const ShowPlan p = programme();
        int differ = 0, first_at = 0;
        for (int pct = 1; pct <= 100; ++pct) {
            const bool may = showFuelMayFly(p, static_cast<float>(pct));
            const bool core = (showValidate(p, static_cast<float>(pct), 0.0f) & SHOW_BATTERY) == 0u;
            if (may != core) { if (!differ) first_at = pct; ++differ; }
        }
        CHECK(differ == 0, "the two verdicts are identical at every state of charge");
        std::printf("        needs %.0fs of the %.0fs a full pack holds; first divergence at %d%%\n",
                    static_cast<double>(showDuration(p) + SHOW_RTL_RESERVE_S + SHOW_PAD_RESERVE_S),
                    static_cast<double>(SHOW_PACK_HOVER_S), first_at);
        CHECK(!showFuelMayFly(p, 5.0f), "a five percent pack cannot fly this programme");
        CHECK(showFuelMayFly(p, 100.0f), "and a full one can");
    }

    // --- the MAVLink layout the FC has to agree with ------------------------
    std::printf("codec: the position target and the heartbeat back from the FC\n");
    {
        PositionTargetTx t{};
        t.x = 17.5f; t.y = -4.5f; t.z = -40.0f; t.yaw_rad = 0.0f;
        t.type_mask = TYPEMASK_POSITION_YAW;
        t.coordinate_frame = FRAME_LOCAL_NED;
        uint8_t pl[64];
        MavWriter w(pl, sizeof(pl));
        t.pack(w, 1234u);
        CHECK(w.size() == 53, "SET_POSITION_TARGET_LOCAL_NED is 53 bytes on the wire");
        CHECK(TYPEMASK_POSITION_YAW == 2552,
              "xy, z and yaw locked; velocity, acceleration, force and yaw-rate ignored");
        uint8_t frame[MAV_FRAME_MAX];
        MavFrameBuilder fb(1, 190);
        const size_t len = fb.build(MSG_SET_POSITION_TARGET_LOCAL_NED, pl, w.size(), frame, sizeof(frame));
        CHECK(len == 53 + 12, "and the frame is header, payload and two CRC bytes");
        MavParser p1;
        MavMessage m{};
        bool got = false;
        for (size_t i = 0; i < len; ++i) got = p1.push(frame[i], m) || got;
        CHECK(got, "a peer parser reads the frame back");
        CHECK(m.id == MSG_SET_POSITION_TARGET_LOCAL_NED, "and it is the message it claims to be");
        CHECK(std::fabs(m.f32At(4) - 17.5f) < 0.001f && std::fabs(m.f32At(8) + 4.5f) < 0.001f
              && std::fabs(m.f32At(12) + 40.0f) < 0.001f, "x, y and z are where the layout says");
        CHECK(m.u16At(48) == TYPEMASK_POSITION_YAW, "and the type mask is at its own offset");
        CHECK(m.u8At(52) == FRAME_LOCAL_NED, "in the frame the paths are baked in");
        CHECK(p1.crcErrors() == 0, "with no CRC complaints");

        MavParser p2;
        MavMessage m2{};
        bool got2 = false;
        frame[len - 1] = static_cast<uint8_t>(frame[len - 1] ^ 0xA5);      // one bit of the CRC
        for (size_t i = 0; i < len; ++i) got2 = p2.push(frame[i], m2) || got2;
        CHECK(!got2 && p2.crcErrors() == 1,
              "and a frame whose CRC is wrong is counted and dropped, not flown");

        uint8_t hpl[9];
        MavWriter hw(hpl, sizeof(hpl));
        HeartbeatTx ht{};
        ht.custom_mode = FC_MODE_GUIDED;
        ht.type = 2; ht.autopilot = 3; ht.base_mode = 0x81; ht.system_status = 4;
        ht.pack(hw);
        MavParser p3;
        MavMessage m3{};
        uint8_t hframe[MAV_FRAME_MAX];
        const size_t hlen = fb.build(MSG_HEARTBEAT, hpl, hw.size(), hframe, sizeof(hframe));
        bool got3 = false;
        for (size_t i = 0; i < hlen; ++i) got3 = p3.push(hframe[i], m3) || got3;
        CHECK(got3 && m3.id == MSG_HEARTBEAT, "a heartbeat round trips through the same codec");
        const Heartbeat h = decodeHeartbeat(m3);
        CHECK(h.alive && h.armed, "decoded as armed");
        CHECK(h.custom_mode == static_cast<uint32_t>(FC_MODE_GUIDED), "and in GUIDED");
    }

    // --- the UBX decode, including the register that moved -----------------
    std::printf("rtk: a NAV-PVT from either protocol generation, and one with bad checksum\n");
    {
        uint8_t pl[92];
        std::memset(pl, 0, sizeof(pl));
        pl[20] = 3;                                  // fixType: 3D
        pl[21] = 0x01 | 0x02 | (2u << 6);            // fixOk, diffSoln, carrSoln fixed (<= 23.01)
        pl[23] = 12;
        const int32_t lon = 1143000000, lat = 305000000, h = 42000 + 40000;
        std::memcpy(pl + 24, &lon, 4);
        std::memcpy(pl + 28, &lat, 4);
        std::memcpy(pl + 32, &h, 4);
        std::memcpy(pl + 36, &h, 4);
        const uint32_t acc = 20;
        std::memcpy(pl + 40, &acc, 4);

        uint8_t frame[160];
        size_t n = 0;
        frame[n++] = 0xB5; frame[n++] = 0x62; frame[n++] = 0x01; frame[n++] = 0x07;
        frame[n++] = static_cast<uint8_t>(sizeof(pl) & 0xFF);
        frame[n++] = static_cast<uint8_t>(sizeof(pl) >> 8);
        std::memcpy(frame + n, pl, sizeof(pl)); n += sizeof(pl);
        uint8_t a = 0, b = 0;
        for (size_t i = 2; i < n; ++i) { a = static_cast<uint8_t>(a + frame[i]); b = static_cast<uint8_t>(b + a); }
        frame[n++] = a; frame[n++] = b;

        rtkInit();
        rtkSetOrigin(305000000, 1143000000, 42000);
        bool got = false;
        for (size_t i = 0; i < n; ++i) got = rtkFeed(frame[i]) || got;
        CHECK(got, "the legacy register placement still decodes as an RTK fix");
        CHECK(rtkSource() == POS_RTK, "and it is the position class the show wants");
        CHECK(rtkState().frames == 1 && rtkState().crc_errors == 0, "one frame, no checksum trouble");
        CHECK(rtkState().hacc_cm == 2, "with the receiver's own accuracy claim carried through");

        const uint8_t keep = frame[n - 1];
        frame[n - 1] = static_cast<uint8_t>(keep ^ 0xFF);       // break the Fletcher
        rtkInit();
        bool got2 = false;
        for (size_t i = 0; i < n; ++i) got2 = rtkFeed(frame[i]) || got2;
        CHECK(!got2, "a NAV-PVT whose checksum does not match is not a position");
        CHECK(rtkState().crc_errors == 1, "and it is counted, so a chewed UART is visible");
        CHECK(rtkSource() == POS_NONE, "with nothing else on hand the aircraft has no solution");
        frame[n - 1] = keep;

        // 27.00 and later -- every current M-series -- moved the carrier solution
        // out of flags and into flags2. Reading only the old one reports "no RTK"
        // forever on a module whose own LED says fixed.
        pl[21] = 0x01 | 0x02;
        pl[22] = 2u << 6;
        rtkInit();
        a = 0; b = 0;
        for (size_t i = 2; i < n - 2; ++i) { a = static_cast<uint8_t>(a + frame[i]); b = static_cast<uint8_t>(b + a); }
        frame[n - 2] = a; frame[n - 1] = b;
        bool got3 = false;
        for (size_t i = 0; i < n; ++i) got3 = rtkFeed(frame[i]) || got3;
        CHECK(got3 && rtkSource() == POS_RTK, "and so does the placement the M9N actually uses");
        rtkInit();
    }

    // --- the whole programme, flown ----------------------------------------
    Result nom = run(nullptr, 0);
    printRun("nominal", nom);
    SimCounters nc{};
    halSimCounters(nc);
    UplinkStats nu{};
    telemetryStats(nu);
    CHECK(nom.flew, "it left the pad");
    CHECK(nom.fc_alive, "the flight controller was talking before it did");
    CHECK(nom.armed, "and it armed, in GUIDED");
    CHECK(nom.max_act == 2, "both acts were flown");
    CHECK(nom.held[1] && nom.held[2], "each one held, not just passed through");
    CHECK(nom.acts_done == 2, "and the programme finished with two acts done");
    CHECK(nom.in_slot, "then went to its own return slot");
    CHECK(std::strcmp(nom.last_why, "SHOW OVER") == 0, "saying that is why, because it is");
    std::printf("        arrival, measured: act 1 %.3f m, act 2 %.3f m, slot %.3f m; radius %.2f m\n",
                static_cast<double>(nom.hold_err[1]), static_cast<double>(nom.hold_err[2]),
                static_cast<double>(nom.slot_err), static_cast<double>(SHOW_ARRIVAL_M));
    CHECK(nom.hold_err[1] < SHOW_ARRIVAL_M && nom.hold_err[2] < SHOW_ARRIVAL_M,
          "every act ended inside the arrival radius, on the point show_core assigned station 7");
    CHECK(nom.slot_err < SHOW_ARRIVAL_M, "and the slot is its own square, not somebody else's");
    CHECK(nom.agreement < 0.60f, "the receiver and the flight controller agree on where it is");
    std::printf("        two independent measurements of the same airframe, worst gap %.3f m\n",
                static_cast<double>(nom.agreement));
    CHECK(nom.saw_fixed, "the nominal show was flown on a fixed solution");
    CHECK(!nom.saw_single && !nom.hover, "and never had to fall back");

    // --- the lights ---------------------------------------------------------
    {
        CHECK(hueMatches(nom.hold_led[1], kActColour[0], SHOW_LED_FULL_PCT - 5, SHOW_LED_FULL_PCT + 5),
              "act one's colour is what the arm pixel is physically showing");
        CHECK(hueMatches(nom.hold_led[2], kActColour[1], SHOW_LED_FULL_PCT - 5, SHOW_LED_FULL_PCT + 5),
              "and act two's took over when the programme changed act");
        CHECK(nom.hold_led[1] != nom.hold_led[2], "the two acts are not the same colour by accident");
        CHECK(nom.hold_beacon[2] == nom.hold_led[2], "the bottom beacon follows the arm light");
        CHECK(nom.commits > 100, "the strip was latched on every tick, not once at boot");
        char rgb[10];
        std::snprintf(rgb, sizeof(rgb), "%06lx", static_cast<unsigned long>(kActColour[1]));
        const char* at = std::strstr(telemetryLastFrame(), ",led=");
        CHECK(at != nullptr, "the frame carries the colour the programme asked for");
        if (at) CHECK(std::strncmp(at + 5, rgb, 6) == 0, "byte for byte, as the pixels show it");
    }

    // --- the frame itself ---------------------------------------------------
    {
        static const char* kFields[] = {"id", "act", "acts", "ck", "u", "n", "e", "d", "led",
                                        "fix", "rtk", "batt", "link", "up", "why"};
        constexpr int kFieldsLen = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
        const char* f = telemetryLastFrame();
        FrameReader rd(f, std::strlen(f));
        CHECK(rd.sourceLength() == 4 && std::strncmp(rd.source(), "SHOW", 4) == 0,
              "the frame is a SHOW frame, on a source token no other board uses");
        Field got[kFieldsLen];
        int seen = 0;
        while (seen < kFieldsLen && rd.next(got[seen])) ++seen;
        bool ordered = seen >= kFieldsLen - 1;      // why= is the one optional field
        for (int i = 0; i < seen && ordered; ++i) {
            ordered = std::strlen(kFields[i]) == got[i].key_len
                   && std::memcmp(kFields[i], got[i].key, got[i].key_len) == 0;
        }
        CHECK(ordered, "every field, in the order the dashboard reads them, no extras");
        // The three fields a show is judged on have to come back out as numbers.
        long station_v = -1;
        CHECK(rd.getInt("id", station_v) && station_v == kStation, "the frame is this station's");
        long act_v = -1;
        CHECK(rd.getInt("act", act_v) && act_v == 2, "and it says which act it finished");
        // The two fields the upload button reads: an aircraft that answers acts=2
        // ck=<checksum> is one holding this exact record, which is a different claim
        // from "it is on act two of something".
        long acts_v = -1, ck_v = -1;
        CHECK(rd.getInt("acts", acts_v) && acts_v == 2, "and how many acts it is holding");
        CHECK(rd.getInt("ck", ck_v)
                  && ck_v == static_cast<long>(planChecksum(kProgrammeText, std::strlen(kProgrammeText))),
              "with the checksum of the record it was handed");
        long batt_v = -1;
        CHECK(rd.getInt("batt", batt_v) && batt_v > 80 && batt_v <= 100,
              "and the fuel in it is the fuel the pack holds, not a constant");
        Field uf{};
        double u_v = -1.0;
        if (rd.find("u", uf)) {
            char tmp[8];
            const size_t k = uf.value_len < 7 ? uf.value_len : 7;
            std::memcpy(tmp, uf.value, k);
            tmp[k] = '\0';
            u_v = std::strtod(tmp, nullptr);
        }
        CHECK(u_v > 0.99, "u reaches one at the end of an act rather than wandering");
        float n_v = 0.0f;
        CHECK(rd.getFloat("n", n_v) && std::fabs(n_v) < 60.0f, "and n is local metres, not degrees");
        CHECK(nu.published > 100 && nu.mqtt_failed == 0, "at five hertz, with nothing dropped for length");
        CHECK(nu.last_len > 60, "and the frames are real frames");
        std::printf("        frames=%u len=%u last=%s", nu.published, nu.last_len, f);
    }

    // --- housekeeping the airframe depends on -------------------------------
    CHECK(nom.setpoints > 200, "the FC latched a position target on every tick of the programme");
    CHECK(nc.last_type_mask == TYPEMASK_POSITION_YAW, "with the mask that locks xy, z and yaw");
    std::printf("        the FC's own copy of the last target: n=%.2f e=%.2f d=%.2f\n",
                static_cast<double>(nc.last_n), static_cast<double>(nc.last_e),
                static_cast<double>(nc.last_d));
    CHECK(std::fabs(nc.last_d + 35.0f) < 2.0f, "the last one it held is the return grid's height");
    CHECK(nom.wdt > 500, "the external watchdog was fed throughout");
    CHECK(nom.rtk_frames > 200, "the receiver kept talking");
    CHECK(nom.cfg_frames > 0, "and took the UBX configuration this firmware sends at boot");
    CHECK(nom.batt_end < nom.batt_start && nom.batt_end > 80.0f,
          "the pack drained over the show, and the gauge followed it");
    CHECK(nom.launch_ms > 0 && (nom.launch_ms % 1000u) < 200u,
          "it lifted off on the receiver's second boundary, so all 24 start together");
    std::printf("        launch at t=%u ms, %u ms into a GNSS second\n",
                nom.launch_ms, nom.launch_ms % 1000u);

    // --- the RTK degradation that is not a failure -------------------------
    std::printf("rtk: the fixed solution is lost and the single one is not\n");
    {
        const Fault f[] = {{35000u, F_FIX_SINGLE}, {41000u, F_FIX_BACK}};
        Result r = run(f, 2);
        printRun("degraded", r);
        CHECK(r.saw_single, "it flew on a single solution for a while");
        CHECK(r.held[2], "and still entered and held act two: a single is degraded, not gone");
        CHECK(!r.hover, "it never stopped to think about it");
        CHECK(r.degraded_fix >= 3 && r.degraded_rtk == 0,
              "and it reported the fix it had, not the one it lost");
        CHECK(r.saw_fixed, "and went back to a fixed solution for the rest of it");
        std::printf("        while degraded the frame said fix=%u rtk=%u and the programme advanced\n",
                    r.degraded_fix, r.degraded_rtk);
    }

    // --- counter-proofs: the same firmware, one input taken away ------------
    std::printf("counter-proof: the programme is running and the position goes away\n");
    {
        const Fault f[] = {{35000u, F_FIX_NONE}};
        Result r = run(f, 1);
        printRun("no fix", r);
        CHECK(!r.held[2], "it never enters act two");
        CHECK(r.max_act == 1, "the programme is stopped where it stood");
        CHECK(r.hover, "and it hovers first, rather than committing on one dropped fix");
        std::printf("        hover measured at %u ms against the %u ms rule\n",
                    r.hover_ms, static_cast<unsigned>(SHOW_HOVER_MS));
        CHECK(r.hover_ms >= SHOW_HOVER_MS && r.hover_ms <= SHOW_HOVER_MS + 240u,
              "the hover is the three seconds the firmware promises, to the tick");
        CHECK(r.in_slot, "then it goes to its own return slot");
        CHECK(std::strcmp(r.last_why, "RTK LOST") == 0, "and says that is why");
        CHECK(r.fix_at_end == 0 && r.rtk_at_end == 0, "the frame reports no fix, not the last one it had");
        CHECK(r.buzzer_ms > 0, "the buzzer is running: the crew can hear which one came home");
        // The strip itself, not the programme's intention: a returns-home aircraft
        // has to look different from the four still flying the shape.
        CHECK(((r.end_led0 >> 16) & 0xFF) > 0 && (r.end_led0 & 0xFFFF) == 0,
              "the arm pixel went red on the way home, not the act's colour");
        CHECK(r.end_led1 != r.end_led0, "and the beacon is strobing, not sitting on the same value");
        std::printf("        probe: end_led0=%08lx end_led1=%08lx hold_led2=%06lx red_at=%u/%s/act%d failsafe=%d\n",
                    static_cast<unsigned long>(nom.end_led0), static_cast<unsigned long>(nom.end_led1),
                    static_cast<unsigned long>(nom.hold_led[2]), nom.red_at_ms,
                    showPhaseName(nom.red_phase), nom.red_act, showStatus().failsafe ? 1 : 0);
        // A finished show does not end on the programme's colour: the airframe's last
        // phase is flying home, and home-bound is red for the same reason it is red when
        // the position drops -- asserted that way for the aborted run just above. The
        // colours *during* the acts are checked against the programme's own by the
        // hold_led checks; what is worth gating here is that the red never starts
        // inside the show.
        CHECK(nom.held[2] && nom.red_phase == SHOW_RETURN && nom.red_at_ms > 0,
              "red starts on the way home, after both acts were flown -- never during them");
        CHECK(nom.max_act == 2 && !nom.hover, "the same firmware, with RTK kept, flies both acts");
        std::printf("        [red  ] no solution -> stopped in act %d, hovered %u ms, home to the slot\n",
                    r.max_act, r.hover_ms);
        std::printf("        [green] solution kept -> flew act 2, never hovered, programme finished\n");
    }

    std::printf("counter-proof: the ground station stops being heard\n");
    {
        const Fault f[] = {{35000u, F_HB_STOP}};
        Result r = run(f, 1);
        printRun("no link", r);
        CHECK(r.link_down, "the link really did go down");
        CHECK(!r.held[2], "no next act while nobody is supervising");
        CHECK(r.max_act == 1, "it stays on act one");
        CHECK(r.in_slot, "and goes to its own slot");
        CHECK(std::strcmp(r.last_why, "LINK LOST") == 0, "with the reason on the frame");
        CHECK(!r.hover, "no three second wait: the position is fine, the supervisor is not");
        CHECK(r.buzzer_ms > 0, "and the crew hears it");
        CHECK(!nom.link_down && nom.held[2], "the same firmware, heartbeat kept, flies act two");
        std::printf("        [red  ] heartbeat stopped -> act %d, %s, home\n", r.max_act, r.last_why);
        std::printf("        [green] heartbeat kept   -> act 2 flown, programme finished\n");
    }

    std::printf("counter-proof: a five percent pack\n");
    {
        const Fault f[] = {{1000u, F_BATT_LOW}};
        Result r = run(f, 1);
        printRun("low batt", r);
        CHECK(std::strcmp(r.refused, "LOW BATTERY") == 0, "the launch is refused, with the reason");
        CHECK(!r.flew, "nothing leaves the pad on a pack that cannot finish the show and get home");
        CHECK(r.max_act == 0, "no act was entered");
        CHECK(r.buzzer_ms > 0, "the crew hears it, because a refused launch is silent otherwise");
        CHECK(nom.flew && nom.max_act == 2, "the same firmware, at a full charge, flies both acts");
        std::printf("        [red  ] 5%% pack -> %s, on the pad\n", r.refused);
        std::printf("        [green] full pack -> flew, act 2, programme finished\n");
    }

    std::printf("counter-proof: the SAFE switch is not inserted\n");
    {
        const Fault f[] = {{1000u, F_SAFE_OPEN}};
        Result r = run(f, 1);
        printRun("switch open", r);
        CHECK(std::strcmp(r.refused, "SAFE SWITCH") == 0, "a physical input outranks an upload");
        CHECK(!r.flew, "and the aircraft stays on the pad");
        CHECK(!r.armed, "the flight controller never armed either: the switch is in its kill path");
        CHECK(nom.armed && nom.flew, "the same firmware, switch in, arms and flies");
        std::printf("        [red  ] switch open -> %s, on the pad\n", r.refused);
        std::printf("        [green] switch in   -> armed, flew act 1 and act 2\n");
    }

    std::printf("counter-proof: the programme is cut off in the radio\n");
    {
        halSimClear();
        appSetup();
        simCommand(kPad);
        simCommand("station=7");
        simCommand("show=SHOWPLAN,24,2.00,6.00,120.00,35.00,2;A,0,18.00");
        CHECK(showStatus().phase == SHOW_IDLE, "half an upload leaves the aircraft idle");
        char why[24] = "";
        CHECK(!showCanLaunch(why, sizeof(why)), "and it cannot be launched");
        CHECK(std::strcmp(why, "NO PROGRAMME") == 0, "with the reason being that there is none");
        simCommand("show=SHOWPLAN,24,2.00,6.00,120.00,35.00,9;A,0,18.00,40.00,12.00,14.00,00e0a0");
        CHECK(showStatus().phase == SHOW_IDLE, "nine acts claimed and one sent is not a nine act show");
        std::printf("        [red  ] truncated upload -> phase %s, launch refused: %s\n",
                    showPhaseName(showStatus().phase), why);
    }

    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
