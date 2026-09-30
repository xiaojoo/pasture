// Host sandbox: the real application, with the simulated airframe, run on the
// host. Nothing here reimplements firmware behaviour -- it drives appSetup /
// appLoop and then judges what the firmware reported, so a change to the
// mission, MAVLink, safety or uplink code is caught by this and not by a
// transcription of it.
//
// Build and run:
//   g++ -std=c++17 -Wall -Werror -O1 -DRANCH_SIM -DRANCH_HOST
//       -Ifirmware/drone/src -Ifirmware/lib
//       firmware/drone/src/*.cpp firmware/drone/test/test_sandbox.cpp -o sandbox
//
// Time is virtual: the whole patrol finishes in milliseconds of CPU.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "mavlink.h"
#include "mission.h"
#include "mission_planner.h"
#include "safety.h"
#include "telemetry.h"

namespace ranch {
void appSetup();
void appLoop();
void simCommand(const char*);
}  // namespace ranch

using namespace ranch;

// What the shipped firmware says the patrol is, written down here separately so
// that editing the route table silently shows up as a failure instead of as a
// test that follows along.
constexpr uint8_t kWaypoints = 9;
constexpr uint32_t kShots = 8;
constexpr float HOME_ALT = 42.0f;      // metres AMSL at the pad

namespace {

int checks = 0, fails = 0;

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    fails++;
    std::printf("  FAIL tests.cpp:%d  %s\n", line, what);
}

#define CHECK(cond, what) check((cond), (what), __LINE__)

// One whole mission, sampled once per loop iteration.
struct History {
    Phase phase[4096];
    uint32_t n = 0;
    float hdg_min = 360.0f;
    float hdg_max = -1.0f;
    float roll = 0.0f;
    float roll_min = 360.0f;
    float roll_max = -360.0f;
    float pitch = 0.0f;
    float pitch_min = 360.0f;
    float pitch_max = -360.0f;
    float max_tilt = 0.0f;
};

History run(bool leave_safe, bool launch = true) {
    History h;
    appSetup();
    if (leave_safe) {
        safetyForceSafe(true);
    } else {
        // The same two commands the ground station sends: take the safety off,
        // then ask it up. Nothing about the launch is a timer any more.
        simCommand("arm=1");
        if (launch) simCommand("takeoff");
    }

    Phase last = Phase::Ground;
    for (uint32_t i = 0; i < 400000 && h.n < 4096; ++i) {
        appLoop();
        // Sample what was actually published, not what the airframe holds: a stub
        // that writes hdg=0 every time has to fail here, because the aircraft turns
        // twice on the way round the route.
        const char* at = std::strstr(telemetryLastFrame(), ",hdg=");
        if (at) {
            float v = 0.0f;
            std::sscanf(at + 5, "%f", &v);
            if (v < h.hdg_min) h.hdg_min = v;
            if (v > h.hdg_max) h.hdg_max = v;
        }
        const char* rr = std::strstr(telemetryLastFrame(), ",roll=");
        if (rr) {
            std::sscanf(rr + 6, "%f", &h.roll);
            if (h.roll < h.roll_min) h.roll_min = h.roll;
            if (h.roll > h.roll_max) h.roll_max = h.roll;
        }
        const char* pt = std::strstr(telemetryLastFrame(), ",pitch=");
        if (pt) {
            std::sscanf(pt + 7, "%f", &h.pitch);
            if (h.pitch < h.pitch_min) h.pitch_min = h.pitch;
            if (h.pitch > h.pitch_max) h.pitch_max = h.pitch;
        }
        const float tilt = std::fabs(h.roll) > std::fabs(h.pitch) ? std::fabs(h.roll) : std::fabs(h.pitch);
        if (tilt > h.max_tilt) h.max_tilt = tilt;
        MissionStatus ms{};
        missionStatus(ms);
        if (ms.phase != last) {
            last = ms.phase;
            h.phase[h.n++] = ms.phase;
            if (ms.phase == Phase::Landed) break;
        }
    }
    return h;
}

bool sawPhase(const History& h, Phase p) {
    for (uint32_t i = 0; i < h.n; ++i) {
        if (h.phase[i] == p) return true;
    }
    return false;
}

}  // namespace

int main() {
    std::printf("sandbox: full patrol on the simulated airframe\n");

    History h = run(false);
    MissionStatus ms{};
    missionStatus(ms);
    const AirState& air = safetyAir();
    SimCounters c{};
    halSimCounters(c);

    // --- the upload actually reached the flight controller ------------------
    CHECK(c.mavlink_crc_errors == 0, "every frame we sent passed the FC's CRC check");
    CHECK(c.fc_items == kWaypoints, "the FC received the whole waypoint list");
    CHECK(c.fc_acked_mission, "the FC accepted the mission");
    CHECK(c.rx_overflow == 0, "no FC frames were dropped by a full input buffer");

    // --- the state machine ran through in order -----------------------------
    CHECK(sawPhase(h, Phase::Climbing), "climbed");
    CHECK(sawPhase(h, Phase::Transit), "transited");
    CHECK(sawPhase(h, Phase::Dwelling), "dwelled on a waypoint");
    CHECK(sawPhase(h, Phase::Rtl), "returned to launch");
    CHECK(sawPhase(h, Phase::Descending), "descended");
    CHECK(sawPhase(h, Phase::Landed), "landed");
    CHECK(ms.complete, "the mission reported complete");
    CHECK(ms.target + 1 == kWaypoints, "the cursor finished on the last waypoint");

    // --- the two implementations agree through telemetry only ---------------
    CHECK(ms.captures == kShots, "one capture per camera waypoint");
    CHECK(c.fc_captures == kShots, "the FC counted the same triggers");
    CHECK(air.batt_pct > 40 && air.batt_pct <= 100, "fuel use is plausible, not invented");
    CHECK(air.fix_type >= 3, "the fix we flew on was 3D");
    CHECK(std::sqrt(air.north * air.north + air.east * air.east) < 4.0f,
          "touchdown within 4 m of home");
    CHECK(air.alt_m > HOME_ALT - 1.0f && air.alt_m < HOME_ALT + 1.5f,
          "touchdown at pad altitude, not underground");

    // --- housekeeping the aircraft depends on -------------------------------
    CHECK(c.wdt_feeds > 100, "the external watchdog was fed throughout");
    CHECK(c.card_bytes > 0, "the flight log reached the card");
    UplinkStats u{};
    telemetryStats(u);
    CHECK(u.published > 200, "telemetry kept its rate");
    CHECK(u.last_len > 0 && u.mqtt_failed == 0, "no frame was dropped for overflowing");
    CHECK(u.log_dropped == 0, "no log line was lost while the card was present");

    // --- the heading the FPV horizon reads ---------------------------------
    // `hdg` is the FC's own yaw. Without it the ground station infers the nose from
    // two positions -- which is track over ground, a different quantity the moment
    // there is wind, and the drawn horizon would be quietly wrong.
    {
        const char* f = telemetryLastFrame();
        const char* at = std::strstr(f, ",hdg=");
        CHECK(at != nullptr, "the frame carries the aircraft's heading");
        float hdg = -1.0f;
        if (at) std::sscanf(at + 5, "%f", &hdg);
        CHECK(hdg >= 0.0f && hdg < 360.0f, "heading is a compass bearing in degrees");
        CHECK(std::fabs(hdg - air.heading_deg) < 0.2f,
              "the published heading is the one the FC reported, not a re-derivation");
        CHECK(h.hdg_max >= h.hdg_min && h.hdg_max - h.hdg_min > 1.0f,
              "the heading moves across the patrol, so it is not a constant being printed");
    }

    // --- the horizon the FPV view is drawn from -----------------------------
    // Roll and pitch travel the same road the hardware's do: the simulator encodes
    // MAVLink ATTITUDE in radians, the firmware decodes it to degrees, and the frame
    // carries degrees. Anything wrong on that chain -- a missed radian conversion, a
    // field dropped in the codec -- shows up here and not on someone's screen.
    {
        const char* f = telemetryLastFrame();
        const char* rr = std::strstr(f, ",roll=");
        const char* pp = std::strstr(f, ",pitch=");
        CHECK(rr != nullptr && pp != nullptr, "the frame carries both tilt angles");
        float roll = 0.0f, pitch = 0.0f;
        if (rr) std::sscanf(rr + 6, "%f", &roll);
        if (pp) std::sscanf(pp + 7, "%f", &pitch);
        CHECK(std::fabs(roll - air.roll_deg) < 0.2f,
              "the published roll is the angle the FC reported, in degrees");
        CHECK(std::fabs(pitch - air.pitch_deg) < 0.2f,
              "the published pitch is the angle the FC reported, in degrees");
        CHECK(h.max_tilt > 0.5f && h.max_tilt <= 45.1f,
              "the tilt stays inside a credible envelope: a radians-as-degrees slip "
              "or a stuck constant fails this either way");
        CHECK(h.pitch_max - h.pitch_min > 1.0f,
              "the pitch changes between climb, cruise and descent");
        CHECK(h.roll_max - h.roll_min > 1.0f,
              "the aircraft banks through its turns: a nose that snaps from waypoint "
              "to waypoint reports no roll at all and fails this");
    }

    // --- the counter-proof: the same build, safety asserted -----------------
    std::printf("sandbox: same firmware with the safety input held\n");
    History h2 = run(true);
    MissionStatus ms2{};
    missionStatus(ms2);
    const AirState& air2 = safetyAir();
    CHECK(!sawPhase(h2, Phase::Transit), "a safe aircraft never leaves the pad");
    CHECK(ms2.phase == Phase::Ground, "and the planner stays on the ground");
    CHECK(!air2.armed, "the FC refused to arm");
    char why[24] = "";
    CHECK(!safetyCanArm(why, sizeof(why)), "and pre-flight says why");
    CHECK(std::strcmp(why, "SAFE SWITCH") == 0, "the reason is the safety switch");

    // --- the counter-proof that replaced the timer: armed is not launched ----
    // A safety switch in the ARM position used to be enough: the firmware lifted
    // off 20 s after boot whatever anybody intended. Arming is permission to fly,
    // not an order to.
    std::printf("sandbox: armed, but nobody asked it to take off\n");
    History h3 = run(false, false);
    MissionStatus ms3{};
    missionStatus(ms3);
    CHECK(!sawPhase(h3, Phase::Transit), "an armed aircraft still waits for the launch");
    CHECK(ms3.phase == Phase::Ground, "on the ground");
    CHECK(safetyCanArm(why, sizeof(why)), "and pre-flight does say it may fly");
    std::printf("        (pre-flight cleared with %s, still on the pad)\n", why);

    // --- MAVLink field layouts, both frame lengths an FC may actually send ---
    std::printf("codec: BATTERY_STATUS with and without extension fields\n");
    {
        uint8_t pl[37];
        std::memset(pl, 0, sizeof(pl));
        for (int i = 0; i < 4; ++i) {           // four cells at 3884 mV
            pl[10 + i * 2] = 0x2C;
            pl[11 + i * 2] = 0x0F;
        }
        for (int i = 4; i < 10; ++i) {          // the rest reported as absent
            pl[10 + i * 2] = 0xFF;
            pl[11 + i * 2] = 0xFF;
        }
        pl[30] = 0x1C; pl[31] = 0x05;           // current 1308 cA = 13.08 A
        pl[32] = 0; pl[33] = 0; pl[34] = 2;
        pl[35] = 62;                             // remaining, extended layout
        pl[36] = 3;

        MavMessage m{};
        m.payload = pl;
        m.len = 37;
        const Battery ext = decodeBattery(m);
        CHECK(ext.remaining_pct == 62, "state of charge read from the extended offset");
        CHECK(ext.cells == 4, "four cells reported, six absent");
        CHECK(ext.current_centi == 1308, "current read from the extension");

        pl[33] = 62;                             // same pack, base frame only
        m.len = 34;
        const Battery base = decodeBattery(m);
        CHECK(base.remaining_pct == 62, "state of charge follows the truncation rule");
        CHECK(base.cells == 4, "cell count unaffected");

        m.len = 30;                              // too short for the 1-byte group
        CHECK(decodeBattery(m).remaining_pct == -1, "a short frame reports unknown, not zero");
    }

    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
