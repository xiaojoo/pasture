// Host unit tests for the platform-neutral firmware logic.
//
//   g++ -std=c++17 -Wall -Werror -I firmware/lib firmware/lib/tests.cpp -o /tmp/ranch-tests && /tmp/ranch-tests
//
// These run on the build host, not on the target: the planner, the failsafe
// table, the scheduler and the telemetry codec have no hardware dependency, so
// the safety behaviour can be proven before anything is wired up.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "failsafe.h"
#include "mission_planner.h"
#include "programme.h"
#include "scheduler.h"
#include "astro.h"
#include "frame_codec.h"
#include "fire_logic.h"
#include "power_meter.h"
#include "power_quality.h"
#include "valve_logic.h"
#include "light_policy.h"
#include "water_safety.h"
#include "show_core.h"

using namespace ranch;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                  \
    } while (0)

static void testMissionCompletesAndLoops() {
    std::printf("mission: transit -> dwell -> rtl\n");
    Mission m{};
    missionReset(m);
    Waypoint a{/*n*/10, /*e*/0, /*alt*/18, 0, 0, /*dwell*/2, /*cam*/1};
    Waypoint b{20, 0, 18, 0, 0, 0, 0};
    CHECK(missionAdd(m, a));
    CHECK(missionAdd(m, b));

    PlanState s{};
    CHECK(missionStart(m, s, 9.0f, 2.0f));
    CHECK(s.phase == Phase::Climbing);

    PlanInput in{0, 0, 0, 9.0f, 2.0f};
    float n, e, alt;

    // Climb out: the aircraft must be held over the pad until it is at altitude.
    planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.phase == Phase::Climbing);
    CHECK(n == 0.0f && e == 0.0f);

    in.alt = 18.0f;
    planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.phase == Phase::Transit);
    CHECK(alt == 18.0f);

    // Arrive on wp1: it has a dwell and a camera trigger.
    in.north = 10.0f;
    planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.phase == Phase::Dwelling);
    CHECK(s.captures == 1);

    // Dwell must not advance early.
    for (int i = 0; i < 15; ++i) planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.phase == Phase::Dwelling);
    CHECK(s.target == 0);

    // Finish the dwell, then fly wp2 (fly-through, no camera).
    for (int i = 0; i < 10; ++i) planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.target == 1);
    in.north = 20.0f;
    planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.mission_complete);
    CHECK(s.phase == Phase::Rtl);
    CHECK(s.captures == 1);   // wp2 triggers nothing

    // RTL flies home then descends to the pad.
    in.north = 0.0f;
    in.east = 0.0f;
    in.alt = 18.0f;
    planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.phase == Phase::Descending);
    for (int i = 0; i < 200; ++i) planStep(m, s, in, 0, 0, 0, 0.1f, n, e, alt);
    CHECK(s.phase == Phase::Landed);
    CHECK(alt == 0.0f);
}

static void testMissionGuards() {
    std::printf("mission: upload guards\n");
    Mission m{};
    missionReset(m);
    PlanState s{};
    CHECK(!missionStart(m, s, 9.0f, 2.0f));      // empty list cannot be armed

    Waypoint w{1, 1, 10, 0, 0, 0, 0};
    for (int i = 0; i < MISSION_MAX_WAYPOINTS; ++i) CHECK(missionAdd(m, w));
    CHECK(!missionAdd(m, w));                    // buffer full is refused, not wrapped
    CHECK(m.count == MISSION_MAX_WAYPOINTS);

    Geofence g{};
    g.radius_m = 100;
    g.ceil_m = 60;
    g.floor_m = 1;
    CHECK(checkGeofence(g, 0, 0, 140, 0, 20).outside);
    CHECK(checkGeofence(g, 0, 0, 0, 0, 75).too_high);
    CHECK(checkGeofence(g, 0, 0, 0, 0, 0.2).too_low);
    CHECK(!checkGeofence(g, 0, 0, 50, 50, 30).outside);
}

static FailsafeInput flying() {
    FailsafeInput in{};
    in.in_flight = true;
    in.rc_age_ms = 20;
    in.gcs_age_ms = 100;
    in.fix_type = 4;
    in.satellites = 14;
    in.hdop = 0.8f;
    in.battery_pct = 80;
    in.cell_v_min = 3.9f;
    in.baro_ok = in.imu_ok = in.gps_ok = true;
    return in;
}

static void testFailsafeDebounce() {
    std::printf("failsafe: debounce and priority\n");
    const FailsafeConfig cfg = FAILSAFE_DEFAULTS;
    FailsafeState st{};
    failsafeReset(st);

    FailsafeInput in = flying();
    CHECK(failsafeEvaluate(cfg, st, in, 100).action == Action::Continue);

    // A 100 ms radio dropout must not trigger anything.
    in.rc_age_ms = 900;
    CHECK(failsafeEvaluate(cfg, st, in, 100).action == Action::Continue);
    in.rc_age_ms = 20;
    CHECK(failsafeEvaluate(cfg, st, in, 100).action == Action::Continue);
    CHECK(st.rc_bad_ms == 0);

    // A sustained loss must cross the debounce window and command RTL.
    in.rc_age_ms = 900;
    CHECK(failsafeEvaluate(cfg, st, in, 300).action == Action::Continue);
    const FailsafeDecision rtl = failsafeEvaluate(cfg, st, in, 300);
    CHECK(rtl.action == Action::Rtl);
    CHECK(std::strcmp(rtl.reason, "RC LOST") == 0);

    // Battery beats the radio: with both bad, the reason must be the battery.
    failsafeReset(st);
    in.battery_pct = 4;
    const FailsafeDecision kill = failsafeEvaluate(cfg, st, in, 100);
    CHECK(kill.action == Action::LandNow);
    CHECK(std::strcmp(kill.reason, "BATT CRITICAL") == 0);

    // An IMU fault kills the motors even on the pad, and blocks arming.
    in.battery_pct = 80;
    in.imu_ok = false;
    const FailsafeDecision imu = failsafeEvaluate(cfg, st, in, 100);
    CHECK(imu.action == Action::Terminate);
    CHECK(imu.armed_block);

    // On the ground a poor GNSS fix refuses to arm rather than RTL.
    in.imu_ok = true;
    in.in_flight = false;
    in.fix_type = 1;
    const FailsafeDecision ground = failsafeEvaluate(cfg, st, in, 100);
    CHECK(ground.armed_block);
    CHECK(ground.action == Action::Continue);
    CHECK(std::strcmp(ground.reason, "GNSS POOR") == 0);

    // GCS loss with a healthy RC holds position instead of auto-RTL.
    in.in_flight = true;
    in.fix_type = 4;
    in.rc_age_ms = 5;
    failsafeReset(st);
    in.gcs_age_ms = 9000;
    FailsafeDecision d = failsafeEvaluate(cfg, st, in, 100);
    while (d.action == Action::Continue) d = failsafeEvaluate(cfg, st, in, 500);
    CHECK(d.action == Action::Hover);
    CHECK(std::strcmp(d.reason, "GCS LOST") == 0);
}

static void testUnreportedSensors() {
    std::printf("failsafe: an unreported sensor is not an empty one\n");
    const FailsafeConfig cfg = FAILSAFE_DEFAULTS;
    FailsafeState st{};
    failsafeReset(st);

    // Before the first BATTERY_STATUS the pack is unknown, which the caller
    // passes as a percentage above 100. Read as a number it is "flat", and a
    // supervisor that acted on that would refuse every bench arm and RTL the
    // first flight after takeoff.
    FailsafeInput in = flying();
    in.battery_pct = 255;
    in.cell_v_min = 0.0f;                    // no cell report yet either
    CHECK(failsafeEvaluate(cfg, st, in, 100).action == Action::Continue);

    in.in_flight = false;
    CHECK(!failsafeEvaluate(cfg, st, in, 100).armed_block);

    // And the same inputs with a known-but-low pack must still command RTL, so
    // the guard above cannot be satisfied by simply ignoring batteries.
    in.in_flight = true;
    in.battery_pct = 20;
    const FailsafeDecision low = failsafeEvaluate(cfg, st, in, 100);
    CHECK(low.action == Action::Rtl);
    CHECK(std::strcmp(low.reason, "BATT LOW") == 0);

    in.battery_pct = 3;
    CHECK(failsafeEvaluate(cfg, st, in, 100).action == Action::LandNow);
}

static void testScheduler() {
    std::printf("scheduler: rates and overrun\n");
    Scheduler sch;
    const int fast = sch.add("50hz", 20);
    const int slow = sch.add("5hz", 200);
    CHECK(sch.count() == 2);
    CHECK(sch.add("overflow", 100) >= 0);

    int fast_hits = 0;
    int slow_hits = 0;
    for (int i = 0; i < 100; ++i) {           // 100 ticks of 10 ms = 1000 ms
        if (sch.due(fast, 10)) fast_hits++;
        if (sch.due(slow, 10)) slow_hits++;
    }
    CHECK(fast_hits == 50);
    CHECK(slow_hits == 5);

    // One 500 ms stall on a 200 ms job is a late run; on a 20 ms job it is an
    // overrun, and the counter must say so.
    CHECK(sch.due(fast, 20));
    CHECK(sch.due(fast, 500));
    CHECK(sch.job(fast).overruns == 1);
    CHECK(sch.job(fast).worst_late_ms == 480);

    // A job that runs a clock must advance it by the time it actually covered,
    // not by its nominal period: the 500 ms stall above is 500 ms of mission
    // time, and using 20 ms would make the aircraft's clocks run slow whenever
    // the loop is loaded.
    CHECK(sch.elapsed(fast) == 500);
    CHECK(sch.elapsed(slow) == 200);
    CHECK(sch.elapsed(-1) == 0);

    // An out-of-range id is inert, never a crash.
    CHECK(!sch.due(-1, 10));
    CHECK(!sch.due(SCHED_MAX_JOBS + 5, 10));
}

static void testTelemetryCodec() {
    std::printf("telemetry: round trip and truncation\n");
    char buf[96];
    FrameWriter w(buf, sizeof(buf));
    w.begin("DRONE");
    w.add("mode", "TRANSIT");
    w.add("alt", 18.4f);
    w.add("wp", 3);
    w.add("batt", 87);
    w.add("rssi", -62);
    CHECK(!w.overflow());
    CHECK(std::strcmp(w.c_str(), "DRONE,mode=TRANSIT,alt=18.4,wp=3,batt=87,rssi=-62") == 0);

    FrameReader r(w.c_str(), w.size());
    CHECK(std::strncmp(r.source(), "DRONE", r.sourceLength()) == 0);
    float alt = 0;
    long wp = 0;
    Field mode{};
    CHECK(r.getFloat("alt", alt) && alt > 18.39f && alt < 18.41f);
    CHECK(r.getInt("wp", wp) && wp == 3);
    CHECK(r.find("mode", mode) && mode.value_len == 7);
    CHECK(!r.find("rss", mode));      // a prefix must not match a longer key

    // A buffer that cannot hold the whole frame reports overflow instead of
    // emitting a truncated frame that would parse as valid.
    char small_[16];
    FrameWriter t(small_, sizeof(small_));
    t.begin("WATER");
    CHECK(t.add("barn", "on"));
    CHECK(!t.add("house", "on"));
    CHECK(t.overflow());

    // The frame the ground application actually sends.
    const char* line = "DRONE,mode=CLIMB,alt=17.6,wp=1,batt=99,rssi=-48,shots=0,link=up\n";
    FrameReader in(line, std::strlen(line));
    long batt = 0;
    CHECK(in.getInt("batt", batt) && batt == 99);
    Field link{};
    CHECK(in.find("link", link) && std::strncmp(link.value, "up", link.value_len) == 0);
}

static void testValveSequencer() {
    std::printf("valve: inrush, hold, stuck, and the trailing water\n");
    const ValveLimits lim = VALVE_DEFAULTS;
    Valve v{};
    valveInit(v);
    CHECK(v.state == ValveState::Closed);
    CHECK(valveDuty(v, lim) == 0.0f);

    CHECK(valveOpen(v));
    CHECK(v.state == ValveState::Inrush);
    CHECK(valveDuty(v, lim) == 1.0f);
    valveStep(v, lim, 60, 10.0f);
    CHECK(v.state == ValveState::Inrush);
    valveStep(v, lim, 100, 10.0f);
    CHECK(v.state == ValveState::Hold);
    CHECK(valveDuty(v, lim) == lim.hold_duty);

    // Water that keeps moving is not a fault, however long the cycle runs.
    for (int i = 0; i < 40; ++i) valveStep(v, lim, 100, 10.0f);
    CHECK(v.state == ValveState::Hold);
    CHECK(v.faults == 0);

    // The cycle accumulator is in litres: 10 L/min for 4 s.
    CHECK(v.cycle_litres > 0.6f && v.cycle_litres < 0.8f);

    // Nothing moving for the no-flow window is a stuck valve or an empty supply.
    for (int i = 0; i < 120; ++i) valveStep(v, lim, 100, 0.0f);
    CHECK(v.state == ValveState::Fault);
    CHECK(v.faults == 1);
    CHECK(valveDuty(v, lim) == 0.0f);
    CHECK(!valveOpen(v));                 // a fault is not re-driven by the next cycle
    valveStep(v, lim, 100, 5.0f);
    CHECK(v.state == ValveState::Fault);  // and seeing water does not silently clear it
    valveClearFault(v);
    CHECK(v.state == ValveState::Closed);
    CHECK(valveOpen(v));

    // Max open time catches a programme that will not let go.
    valveInit(v);
    valveOpen(v);
    for (int i = 0; i < 1200; ++i) valveStep(v, lim, 1000, 10.0f);
    CHECK(v.state == ValveState::Fault);

    // Closing keeps counting: the column of water already in motion finishes.
    valveInit(v);
    valveOpen(v);
    valveStep(v, lim, 200, 10.0f);
    valveClose(v);
    CHECK(v.state == ValveState::Closing);
    const float before = v.cycle_litres;
    valveStep(v, lim, 200, 10.0f);
    CHECK(v.cycle_litres > before);
    CHECK(v.state == ValveState::Closing);      // still flowing, so still counting
    for (int i = 0; i < 20; ++i) valveStep(v, lim, 200, 0.0f);
    CHECK(v.state == ValveState::Closed);
    CHECK(v.last_litres == v.cycle_litres);
}

static void testProgrammeAnchor() {
    std::printf("programme: anchored to the clock, not to boot time\n");
    Programme p{};
    p.period_s = 3600;
    p.run_s = 240;
    p.phase_s = 0;
    p.enabled = true;
    ProgrammeState s{};
    programmeInit(s);

    programmeAnchor(p, s, 1, 8 * 3600);
    CHECK(s.next_due == programmeClock(1, 9 * 3600));
    // Exactly on a boundary must not fire twice: the anchor is strictly after.
    programmeAnchor(p, s, 1, 9 * 3600);
    CHECK(s.next_due == programmeClock(1, 10 * 3600));

    // Re-anchor at 08:00 for the run below: the boundary check above moved the
    // programme to 10:00 on purpose.
    programmeAnchor(p, s, 1, 8 * 3600);
    uint32_t left = 0;
    CHECK(programmeTick(p, s, 1, 8 * 3600 + 3540, true, left) == Dispense::None);
    CHECK(left == 60);
    CHECK(programmeCountdown(p, s, 1, 8 * 3600 + 3540) == 60);
    CHECK(programmeTick(p, s, 1, 9 * 3600 - 1, true, left) == Dispense::None);

    CHECK(programmeTick(p, s, 1, 9 * 3600, true, left) == Dispense::Start);
    CHECK(s.running && s.runs == 1);
    CHECK(programmeCountdown(p, s, 1, 9 * 3600) == 0);        // running: no countdown
    CHECK(programmeTick(p, s, 1, 9 * 3600 + 239, true, left) == Dispense::Running);
    CHECK(programmeTick(p, s, 1, 9 * 3600 + 240, true, left) == Dispense::Finish);
    CHECK(!s.running);

    // A cycle the plant refused is skipped once and moved on, not retried every
    // tick: a pump that cannot build pressure must not be asked 20 times a second.
    ProgrammeState r{};
    programmeInit(r);
    programmeAnchor(p, r, 1, 8 * 3600);
    uint32_t ignored = 0;
    for (int i = 0; i < 100; ++i) {
        CHECK(programmeTick(p, r, 1, 9 * 3600 + i, false, ignored) == Dispense::None);
    }
    CHECK(r.skipped == 1);
    CHECK(r.runs == 0);
    CHECK(r.next_due == programmeClock(1, 10 * 3600));

    Programme off = p;
    off.enabled = false;
    ProgrammeState q{};
    programmeInit(q);
    programmeAnchor(off, q, 1, 8 * 3600);
    CHECK(programmeTick(off, q, 1, 9 * 3600, true, ignored) == Dispense::None);
    CHECK(programmeCountdown(off, q, 1, 9 * 3600) == 0);

    CHECK(!programmeNeedsExercise(3, 7, 0, 600));
    CHECK(programmeNeedsExercise(7, 7, 0, 600));
    CHECK(!programmeNeedsExercise(8, 7, 3000, 600));    // outside the window
}

static void testPlantPriority() {
    std::printf("plant: which fault wins, and what actually stops\n");
    const PlantLimits lim = PLANT_DEFAULTS;
    PlantState st{};
    plantReset(st);

    PlantInput in{};
    in.pressure_bar = 2.4f;
    in.tank_pct = 60.0f;
    in.barn_flow = 12.0f;
    in.barn_open = true;
    in.house_open = true;
    in.pump_on = true;
    in.clock_valid = true;
    CHECK(plantEvaluate(lim, st, in, 0.05f).action == WaterAction::Run);

    // A wet floor outranks a pressure spike and a stuck valve, and it takes the
    // pump with it: an interlock, not a preference.
    in.leak = true;
    PlantDecision leak = plantEvaluate(lim, st, in, 0.05f);
    CHECK(leak.action == WaterAction::CloseAll);
    CHECK(std::strcmp(leak.reason, "LEAK") == 0);
    CHECK(!leak.pump_interlocked);
    CHECK(!leak.want_pump);

    // Water moving with nothing open is the same shutdown from the other sensor.
    in.leak = false;
    in.barn_open = false;
    in.house_open = false;
    for (int i = 0; i < 20; ++i) plantEvaluate(lim, st, in, 0.2f);
    const PlantDecision metered = plantEvaluate(lim, st, in, 0.2f);
    CHECK(metered.action == WaterAction::CloseAll);
    CHECK(std::strcmp(metered.reason, "LEAK METER") == 0);

    // The float switch stops the pump even though a line is still asking for it.
    plantReset(st);
    in.barn_open = true;
    in.barn_flow = 12.0f;
    in.tank_high = true;
    const PlantDecision full = plantEvaluate(lim, st, in, 0.05f);
    CHECK(!full.want_pump);
    CHECK(!full.pump_interlocked);
    CHECK(std::strcmp(full.reason, "TANK FULL") == 0);

    // A dry borehole: the pump keeps asking, the pressure never comes.
    in.tank_high = false;
    plantReset(st);
    in.pressure_bar = 0.05f;
    PlantDecision dry = plantEvaluate(lim, st, in, 0.5f);
    CHECK(dry.action == WaterAction::Run);              // 0.5 s is not yet a fault
    dry = plantEvaluate(lim, st, in, 5.0f);
    CHECK(dry.action == WaterAction::Hold);
    CHECK(std::strcmp(dry.reason, "DRY RUN") == 0);
    CHECK(!dry.pump_interlocked);
    CHECK(!dry.allow_dispense);

    // An unmeasured tank must not be read as empty, or a board with a broken
    // float would run the pump continuously.
    in.pressure_bar = 2.4f;
    in.tank_pct = -1.0f;
    const PlantDecision unknown = plantEvaluate(lim, st, in, 0.05f);
    CHECK(unknown.action != WaterAction::Refill);
    CHECK(unknown.want_pump);                            // the lines still have head
    in.tank_pct = 10.0f;
    CHECK(plantEvaluate(lim, st, in, 0.05f).action == WaterAction::Refill);

    // No clock stops the schedule, never the house.
    in.tank_pct = 60.0f;
    in.clock_valid = false;
    const PlantDecision blind = plantEvaluate(lim, st, in, 0.05f);
    CHECK(!blind.allow_dispense);
    CHECK(blind.action != WaterAction::CloseAll);
    CHECK(std::strcmp(blind.reason, "NO CLOCK") == 0);
}

static void testSunTimes() {
    std::printf("astro: sunrise against an almanac, not against itself\n");
    // The ranch site: 30.5 N, 114.3 E, UTC+8. Solar noon there is about 12:23
    // local because the time zone meridian is 120 E.
    const SunTimes eq = sunTimes(2026, 3, 20, 30.5, 114.3, SUN_OFFICIAL_ZENITH, 8.0);
    CHECK(eq.valid);
    CHECK(eq.sunrise_min > 6 * 60 + 5 && eq.sunrise_min < 6 * 60 + 40);
    CHECK(eq.sunset_min > 18 * 60 + 5 && eq.sunset_min < 18 * 60 + 45);
    CHECK(eq.day_length_min > 700.0f && eq.day_length_min < 760.0f);

    const SunTimes jun = sunTimes(2026, 6, 21, 30.5, 114.3, SUN_OFFICIAL_ZENITH, 8.0);
    // 30 degrees north at the summer solstice is about 13 h 55 m of day.
    CHECK(jun.day_length_min > 13.4f * 60.0f && jun.day_length_min < 14.4f * 60.0f);
    const SunTimes dec = sunTimes(2026, 12, 21, 30.5, 114.3, SUN_OFFICIAL_ZENITH, 8.0);
    // The two solstices differ by just under four hours of day at this latitude,
    // which is the whole reason the ranch does not run a fixed 18:00 timer.
    CHECK(dec.day_length_min < jun.day_length_min - 220.0f);
    CHECK(dec.sunrise_min > jun.sunrise_min);

    const SunTimes civil = sunTimes(2026, 6, 21, 30.5, 114.3, SUN_CIVIL_ZENITH, 8.0);
    // Print the times themselves: a band that fails has to say what was computed.
    std::printf("  equinox rise=%02d:%02d set=%02d:%02d day=%.0f | solstice day=%.0f | "
                "winter day=%.0f | civil dawn=%02d:%02d\n",
                static_cast<int>(eq.sunrise_min) / 60, static_cast<int>(eq.sunrise_min) % 60,
                static_cast<int>(eq.sunset_min) / 60, static_cast<int>(eq.sunset_min) % 60,
                eq.day_length_min, jun.day_length_min, dec.day_length_min,
                static_cast<int>(civil.sunrise_min) / 60, static_cast<int>(civil.sunrise_min) % 60);
    CHECK(civil.valid);
    CHECK(civil.sunrise_min < jun.sunrise_min);            // twilight precedes sunrise
    CHECK(civil.day_length_min > jun.day_length_min);

    // 70 N: midnight sun in June, none in December. Reporting a time here would
    // put the lamps on a schedule that the sky contradicts.
    const SunTimes polar = sunTimes(2026, 6, 21, 70.0, 25.0, SUN_OFFICIAL_ZENITH, 2.0);
    CHECK(!polar.valid && polar.circumpolar);
    const SunTimes dark = sunTimes(2026, 12, 21, 70.0, 25.0, SUN_OFFICIAL_ZENITH, 2.0);
    CHECK(!dark.valid && dark.circumpolar);

    CHECK(dayOfYear(2026, 1, 1) == 1u);
    CHECK(dayOfYear(2026, 12, 31) == 365u);
    CHECK(dayOfYear(2024, 12, 31) == 366u);
    // Cross-check the two ways of naming a day: the day-of-year sequence must be
    // continuous across the new year, which is the only property the policy needs.
    CHECK(daysFromCivil(2027, 1, 1) - daysFromCivil(2026, 12, 31) == 1);
}

static void testLightPolicy() {
    std::printf("policy: dusk ramp, economy, motion, override, daylight\n");
    const LightConfig cfg = LIGHT_DEFAULTS;
    LightInput in{};
    in.clock_valid = true;
    in.civil_sunset_min = 1140.0f;        // 19:00
    in.civil_sunrise_min = 330.0f;        // 05:30
    in.lux = 0.2f;
    in.lux_valid = true;

    in.minute_of_day = 1200;              // 20:00, an hour into the night
    LightOutput o = lightDecide(cfg, in);
    CHECK(o.street && o.house);
    CHECK(o.barn_duty > 0.9f);            // the ramp finished long before now

    in.minute_of_day = 1145;
    o = lightDecide(cfg, in);
    CHECK(o.barn_duty > 0.1f && o.barn_duty < 0.35f);   // five minutes into a 30 min ramp

    in.minute_of_day = 1400;              // 23:20, past the economy step
    o = lightDecide(cfg, in);
    CHECK(o.mode == LightMode::Economy);
    CHECK(std::fabs(o.barn_duty - cfg.economy_duty) < 0.01f);

    in.motion = true;
    in.motion_age_s = 10;
    o = lightDecide(cfg, in);
    CHECK(o.mode == LightMode::Motion);
    CHECK(o.barn_duty == cfg.motion_duty);
    in.motion = false;
    in.motion_age_s = 100000u;

    in.lux = 800.0f;                      // a truck's headlights across the sensor
    o = lightDecide(cfg, in);
    CHECK(o.mode == LightMode::DaylightHold);
    CHECK(!o.street && !o.house);
    in.lux = 0.2f;

    in.override_left_s = 60;
    in.street_override = true;
    in.house_override = false;
    o = lightDecide(cfg, in);
    CHECK(o.mode == LightMode::Override && o.street && !o.house);
    in.override_left_s = 0;
    in.street_override = false;

    in.minute_of_day = 720;               // noon: inside neither ramp, and bright
    o = lightDecide(cfg, in);
    CHECK(!o.street && !o.house && o.barn_duty == 0.0f);

    in.clock_valid = false;
    o = lightDecide(cfg, in);
    CHECK(o.mode == LightMode::NoClock && !o.street);

    // A latitude with no twilight on the date falls back to clock times instead
    // of leaving the yard dark all winter.
    LightInput arctic = in;
    arctic.clock_valid = true;
    arctic.civil_sunset_min = -1.0f;
    arctic.civil_sunrise_min = -1.0f;
    arctic.minute_of_day = 1200;
    o = lightDecide(cfg, arctic);
    CHECK(o.mode == LightMode::Polar);
    CHECK(o.street);

    CHECK(minutesBetween(1140, 330) == 630);       // the night wraps midnight
    CHECK(minutesBetween(1200, 1140) == 1380);
}

static PhaseRms leg(float volts, float amps, float pf = 0.92f, float hz = 50.0f) {
    return PhaseRms{volts, amps, pf, hz};
}

// The board these tests assume, so the load-percent assertions say what they mean.
constexpr float RATED_FOR_TESTS = 45.0f;
constexpr float NOMINAL_TEST = 230.0f;
// The board's own supply rail, which is what an open loop floats to.
constexpr uint16_t LOOP_RAIL_FOR_TESTS = 3300u;

static void testPowerMeter() {
    std::printf("power: three-phase arithmetic an electrician would check\n");
    PhaseRms bal[3] = {leg(230.0f, 10.0f), leg(230.0f, 10.0f), leg(230.0f, 10.0f)};
    PowerRead r = measurePower(bal, 45.0f, NOMINAL_TEST);
    // 3 * 230 * 10 = 6.9 kVA, of which pf * VA is real.
    CHECK(std::fabs(r.kva - 6.9f) < 0.01f);
    CHECK(std::fabs(r.kw - 6.348f) < 0.01f);
    CHECK(r.kvar > 2.6f && r.kvar < 2.8f);
    CHECK(std::fabs(r.load_pct - 15.33f) < 0.1f);
    CHECK(r.unbalance_pct < 0.01f);
    CHECK(!r.phase_loss);

    // Power factor is a *ratio of the sums*, not the average of the legs: with one
    // resistive leg and one heavily inductive one of equal VA, the true figure is
    // 0.75 and averaging the two would report 0.75 only by coincidence of symmetry.
    PhaseRms mix[3] = {leg(230.0f, 10.0f, 1.0f), leg(230.0f, 10.0f, 0.5f),
                       leg(230.0f, 10.0f, 0.75f)};
    r = measurePower(mix, 45.0f, NOMINAL_TEST);
    CHECK(std::fabs(r.pf - 0.75f) < 0.001f);
    CHECK(std::fabs(r.kw - (6900.0f * 0.75f) / 1000.0f) < 0.01f);

    // A collapsed leg: the board has to say so, and the frequency it reports has to
    // come from the legs that are still up. Averaging over three would call a 50 Hz
    // supply 33 Hz, which is inside the under-frequency trip window.
    PhaseRms lost[3] = {leg(231.0f, 12.0f), leg(0.0f, 0.0f, 0.92f, 0.0f), leg(229.0f, 11.0f)};
    r = measurePower(lost, 45.0f, NOMINAL_TEST);
    CHECK(r.phase_loss);
    CHECK(std::fabs(r.hz - 50.0f) < 0.01f);
    CHECK(r.unbalance_pct > 45.0f);

    // A supply that has simply been switched off is not a lost phase: nothing is
    // live, so there is no healthy leg for a dead one to be compared against.
    PhaseRms off[3] = {leg(0.0f, 0.0f, 0.92f, 0.0f), leg(0.0f, 0.0f), leg(0.0f, 0.0f)};
    r = measurePower(off, 45.0f, NOMINAL_TEST);
    CHECK(!r.phase_loss);
    CHECK(r.hz == 0.0f);
    CHECK(r.pf == 0.0f);

    // A leg that has collapsed on a 415 V delta service sits at 150 V: above half
    // of 230, so a board with the nominal voltage baked in calls it healthy and
    // never sheds the pump that will burn on two phases. The same fixture against
    // its real supply names it.
    PhaseRms delta[3] = {leg(414.0f, 12.0f), leg(150.0f, 0.0f, 0.92f, 0.0f), leg(412.0f, 11.0f)};
    CHECK(!measurePower(delta, 45.0f, 230.0f).phase_loss);
    CHECK(measurePower(delta, 45.0f, 415.0f).phase_loss);

    // Period, not count: 100 transitions in one second is 99 intervals spanning
    // 990 ms, and a counter that divided 100 by 1.0 s would be wrong by one percent
    // every window.
    CHECK(std::fabs(frequencyFromSpan(990000u, 99) - 50.0f) < 0.001f);
    CHECK(frequencyFromSpan(0u, 10) == 0.0f);
    CHECK(frequencyFromSpan(1000000u, 0) == 0.0f);
    CHECK(std::fabs(frequencyFromSpan(10000u, 1) - 50.0f) < 0.001f);

    // The CT sits at half its supply with no current in the aperture, and has no
    // phase reference: either side of the virtual ground is positive amps.
    CHECK(ampsFromMillivolts(1650, 1650, 15.0f) == 0.0f);
    CHECK(std::fabs(ampsFromMillivolts(1800, 1650, 15.0f) - 10.0f) < 0.001f);
    CHECK(std::fabs(ampsFromMillivolts(1500, 1650, 15.0f) - 10.0f) < 0.001f);

    // Energy integrates real power, and "today" belongs to the day it was counted.
    Energy e{};
    energyInit(e);
    CHECK(e.kwh_total == 0.0f && e.day == 0);
    energyAdd(e, 10.0f, 3600.0f, 1001);          // one hour at 10 kW
    CHECK(std::fabs(e.kwh_today - 10.0f) < 0.001f);
    CHECK(std::fabs(e.kwh_total - 10.0f) < 0.001f);
    CHECK(e.resets == 0);
    energyAdd(e, 10.0f, 3600.0f, 1002);          // the next morning
    CHECK(std::fabs(e.kwh_today - 10.0f) < 0.001f);
    CHECK(std::fabs(e.kwh_total - 20.0f) < 0.001f);
    CHECK(e.resets == 1);
    // A pump at 0.6 power factor costs the ranch its kilowatt-hours, not its kVA.
    Energy q{};
    energyInit(q);
    energyAdd(q, 7.5f * 0.6f, 3600.0f, 5);
    CHECK(std::fabs(q.kwh_today - 4.5f) < 0.001f);

    CHECK(dayKey(2026, 4, 18) != dayKey(2026, 4, 19));
    CHECK(dayKey(2026, 4, 18) == dayKey(2026, 4, 18));
    CHECK(dayKey(2027, 1, 1) != dayKey(2026, 1, 1));
}

static GridInput healthyGrid() {
    GridInput in{};
    PhaseRms p[3] = {leg(230.0f, 20.0f), leg(230.0f, 18.0f), leg(230.0f, 16.0f)};
    in.read = measurePower(p, RATED_FOR_TESTS, NOMINAL_TEST);
    in.read.hz = 50.0f;
    in.nominal_v = 230.0f;
    in.rcd_ma = 8.0f;
    in.temp_c = 30.0f;
    in.breaker_closed = true;
    in.pump_running = true;
    in.sensor_fault = false;
    return in;
}

static void testGridProtection() {
    std::printf("grid: the order the rules fire in is the order of what hurts\n");
    const GridLimits lim = GRID_DEFAULTS;
    GridState st{};
    gridReset(st);

    GridInput in = healthyGrid();
    GridDecision d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Normal);
    // 12.4 kVA of a 45 kVA board: 28% load, below the shed level. The healthy
    // fixture has to sit below it or every rule below is being tested on a board
    // that was already shedding.
    CHECK(in.read.load_pct < lim.shed_load_pct);
    for (int i = 0; i < 100; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Normal);
    CHECK(d.reason[0] == '\0');
    CHECK(!d.lockout && !d.open_pump);

    // Undervoltage on one leg only. The mean of the three is 216 V, comfortably
    // inside the -14% limit, so a board that looked at the average would never see
    // this; the one that looks at each leg sees it after the debounce.
    in.read.volts[2] = 190.0f;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Normal);           // nothing on the first sample
    int ticks = 1;
    while (d.action == GridAction::Normal && ticks < 200) {
        d = gridEvaluate(lim, st, in, 0.1f);
        ticks++;
    }
    CHECK(ticks >= 20 && ticks <= 24);               // 2 s of persistence
    CHECK(d.action == GridAction::Alarm);
    CHECK(std::strcmp(d.reason, "UNDERVOLTAGE") == 0);
    in.read.volts[2] = 230.0f;
    for (int i = 0; i < 40; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Normal);

    // Leakage is already a fault current: it trips on the sample, not after two
    // seconds of agreeing with itself.
    in.rcd_ma = 120.0f;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Trip);
    CHECK(d.lockout && d.open_pump);
    CHECK(std::strcmp(d.reason, "LEAKAGE") == 0);

    // It outranks everything else too: a perfect board with a live earth still
    // trips, because the thing that rule protects is a person at the water trough.
    in.rcd_ma = 8.0f;
    in.temp_c = 80.0f;
    in.read.hz = 45.0f;
    in.breaker_closed = false;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(std::strcmp(d.reason, "OVERTEMP") == 0);
    in.temp_c = 30.0f;
    // Frequency has its own debounce - a trip that rode on one bad 20 ms of
    // counting would take the ranch off for five minutes on a glitch - so this has
    // to persist past it before the rule names itself.
    for (int i = 0; i < 25; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(std::strcmp(d.reason, "FREQUENCY") == 0);
    in.read.hz = 50.0f;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(std::strcmp(d.reason, "BREAKER OPEN") == 0);
    in.breaker_closed = true;

    // Once tripped for leakage the feeders stay open through the lockout, and the
    // reason changes from the fault to the state the board is now in.
    gridReset(st);
    in = healthyGrid();
    in.rcd_ma = 130.0f;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Trip);
    in.rcd_ma = 5.0f;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Trip);
    CHECK(std::strcmp(d.reason, "LOCKED OUT") == 0);
    CHECK(d.lockout && d.open_pump);
    for (int i = 0; i < static_cast<int>(lim.reclose_lockout_s / 0.1f) + 5; ++i) {
        d = gridEvaluate(lim, st, in, 0.1f);
    }
    CHECK(d.action == GridAction::Normal);           // frequency recloses by itself

    // A lost phase sheds rather than trips, and comes back without an operator:
    // the pump waits, the ranch does not go dark.
    gridReset(st);
    in = healthyGrid();
    PhaseRms down[3] = {leg(231.0f, 12.0f), leg(0.0f, 0.0f, 0.92f, 0.0f),
                        leg(229.0f, 11.0f)};
    in.read = measurePower(down, RATED_FOR_TESTS, NOMINAL_TEST);
    in.read.hz = 50.0f;
    d = gridEvaluate(lim, st, in, 0.1f);
    for (int i = 0; i < 30; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::ShedLoad);
    CHECK(d.open_pump && !d.lockout);
    CHECK(std::strcmp(d.reason, "PHASE LOSS") == 0);

    // Nothing measured at all is the case a naive table gets worst: an unplugged
    // board reads zero volts, and zero volts is a brownout to a rule that never
    // asked whether anything was being measured.
    gridReset(st);
    in = healthyGrid();
    std::memset(in.read.volts, 0, sizeof(in.read.volts));
    in.read.kva = 0.0f;
    in.read.kw = 0.0f;
    in.read.hz = 0.0f;
    in.read.load_pct = 0.0f;
    in.read.unbalance_pct = 0.0f;
    in.read.phase_loss = false;
    for (int i = 0; i < 100; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Normal);

    // ...and a board that knows it is not measuring says so, instead of acting on
    // the numbers it does not have.
    in.sensor_fault = true;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Alarm);
    CHECK(std::strcmp(d.reason, "SENSOR FAULT") == 0);
    CHECK(!d.open_pump && !d.lockout);

    // Overload: shed at 90% sustained, trip at 115%, and the reason is the same
    // word because it is the same cause at two sizes. 45 kVA at 228 V is 65.8 A per
    // leg, so 80 A is 122% and 63 A is 96%.
    gridReset(st);
    in = healthyGrid();
    PhaseRms hot[3] = {leg(228.0f, 80.0f), leg(228.0f, 80.0f), leg(228.0f, 80.0f)};
    in.read = measurePower(hot, 45.0f, NOMINAL_TEST);
    in.read.hz = 50.0f;
    CHECK(in.read.load_pct > lim.trip_load_pct);
    d = gridEvaluate(lim, st, in, 0.1f);
    for (int i = 0; i < 30; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Trip);
    CHECK(std::strcmp(d.reason, "OVERLOAD") == 0);
    CHECK(d.lockout);

    gridReset(st);
    PhaseRms warm[3] = {leg(229.0f, 63.0f), leg(229.0f, 63.0f), leg(229.0f, 63.0f)};
    in.read = measurePower(warm, 45.0f, NOMINAL_TEST);
    in.read.hz = 50.0f;
    CHECK(in.read.load_pct > lim.shed_load_pct && in.read.load_pct < lim.trip_load_pct);
    d = gridEvaluate(lim, st, in, 0.1f);
    for (int i = 0; i < 30; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::ShedLoad);
    CHECK(!d.lockout);

    // Unbalance with every leg inside the voltage window: the alarm a meter man
    // sees first, and the one an averaging implementation cannot see at all.
    gridReset(st);
    in = healthyGrid();
    in.read.volts[0] = 245.0f;
    in.read.volts[1] = 230.0f;
    in.read.volts[2] = 215.0f;
    in.read.unbalance_pct = 6.5f;
    d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Normal);
    in.read.unbalance_pct = 22.0f;
    for (int i = 0; i < 30; ++i) d = gridEvaluate(lim, st, in, 0.1f);
    CHECK(d.action == GridAction::Alarm);
    CHECK(std::strcmp(d.reason, "UNBALANCE") == 0);
}

static void testFireLoops() {
    std::printf("fire: the four bands a supervised loop can sit in\n");
    const LoopBands b = LOOP_DEFAULT_BANDS;
    // The nominal resistance of each state, converted the way the divider does it.
    const auto mv = [](uint32_t ohm) {
        return static_cast<uint16_t>(3300.0f * static_cast<float>(ohm) /
                                     static_cast<float>(ohm + 4700u) + 0.5f);
    };
    CHECK(loopClassify(mv(1000u), b) == LoopState::Alarm);
    CHECK(loopClassify(mv(4700u), b) == LoopState::Normal);
    CHECK(loopClassify(LOOP_RAIL_FOR_TESTS, b) == LoopState::Open);
    CHECK(loopClassify(0u, b) == LoopState::Shorted);
    // The gap between the alarm and normal bands is not rounded to the nearer side:
    // a corroded terminal reads a middling voltage and a panel that picked a side for
    // it would be reporting something it did not measure.
    CHECK(loopClassify(1050u, b) == LoopState::Unknown);
    CHECK(loopClassify(850u, b) == LoopState::Unknown);
    CHECK(mv(1000u) > b.short_hi_mv && mv(1000u) < b.normal_lo_mv);

    // Three samples of agreement, per tracker. A relay that bounces at the threshold
    // never accumulates three of the same reading in a row.
    LoopTracker t{};
    loopTrackerReset(t);
    CHECK(loopTrack(t, LoopState::Alarm) == LoopState::Unknown);
    CHECK(loopTrack(t, LoopState::Alarm) == LoopState::Unknown);
    CHECK(loopTrack(t, LoopState::Alarm) == LoopState::Alarm);
    loopTrackerReset(t);
    for (int i = 0; i < 40; ++i) {
        loopTrack(t, (i % 2) ? LoopState::Alarm : LoopState::Unknown);
    }
    CHECK(t.state == LoopState::Unknown);        // never settled, so never reported
    loopTrackerReset(t);
    loopTrack(t, LoopState::Alarm);
    loopTrack(t, LoopState::Alarm);
    loopTrack(t, LoopState::Unknown);            // breaks the run
    loopTrack(t, LoopState::Alarm);
    CHECK(t.state == LoopState::Unknown);        // and does not alarm on the fourth

    // The decision rules the sandbox cannot reach cheaply.
    FireLimits lim = FIRE_DEFAULTS;
    FireState st{};
    fireReset(st);
    FireInput in{};
    in.key_armed = true;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) in.zone[i] = LoopState::Normal;
    FireDecision d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Normal && !d.siren && !d.pump_permit);

    // One zone: the building is warned, the riser stays dry.
    in.zone[1] = LoopState::Alarm;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Alarm);
    CHECK(d.siren && d.strobe && d.dial);
    CHECK(!d.pump_permit);
    CHECK(d.zone_bits == 2u);
    // The latch is what a reset has to clear, so reset must be gated on the cause and
    // not on the latch -- gating on the latch would make reset unable to do its job.
    st.reset_edge = true;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(std::strcmp(d.reason, "RESET BLOCKED") == 0);
    CHECK(st.latched);
    in.zone[1] = LoopState::Normal;
    st.reset_edge = true;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Normal && !st.latched);

    // Twenty seconds of the same detector is the second opinion.
    in.zone[1] = LoopState::Alarm;
    for (int i = 0; i < 15; ++i) d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Alarm && !d.pump_permit);
    for (int i = 0; i < 410; ++i) d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Emergency && d.pump_permit);

    // A second zone confirms immediately, and a flow switch confirms without a second
    // detector because the water is already moving.
    fireReset(st);
    in.zone[0] = LoopState::Alarm;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Emergency && d.pump_permit);
    fireReset(st);
    in.zone[0] = LoopState::Normal;
    in.zone[1] = LoopState::Alarm;
    in.flow = true;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Emergency && d.pump_permit);
    in.flow = false;

    // Bypass: the outputs die, the indication does not. With a zone alight a bypassed
    // panel still reports the alarm rather than downgrading it to a fault -- that
    // precedence is the same one the sandbox asserts on.
    fireReset(st);
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) in.zone[i] = LoopState::Normal;
    in.key_armed = false;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Fault && d.bypassed);
    CHECK(!d.siren && !d.strobe && !d.pump_permit);
    in.key_armed = true;

    // Supervision: an open loop is a fault, and is not a fire.
    fireReset(st);
    in.zone[2] = LoopState::Open;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.level == FireLevel::Fault && d.supervision && d.dial);
    CHECK(!d.siren && !d.pump_permit);
    in.zone[2] = LoopState::Normal;

    // A bell commanded with nothing moving behind it: the failure is reported above
    // the fault count, and the siren proof is measured against the previous tick's
    // command so a relay that chatters open again is caught.
    fireReset(st);
    in.zone[3] = LoopState::Alarm;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.siren);
    in.siren_feedback = false;
    for (int i = 0; i < 5; ++i) d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(st.siren_failed);
    CHECK(d.supervision);
    in.siren_feedback = true;
    in.zone[3] = LoopState::Normal;
    fireReset(st);

    // Silence: allowed on a detector alarm, refused on a manual call point, and capped.
    fireReset(st);
    in.zone[0] = LoopState::Alarm;
    in.silence_edge = true;
    d = fireEvaluate(lim, st, in, 0.05f);
    CHECK(d.silenced && !d.siren && d.strobe);
    in.manual_call = true;
    CHECK(!fireSilenceAllowed(lim, in, st));      // a broken glass cannot be hung up on
    in.manual_call = false;
    st.silences = lim.silence_max;
    CHECK(!fireSilenceAllowed(lim, in, st));      // and the button runs out
    in.silence_edge = false;
}

static void testShowChoreography() {
    std::printf("show: shape sampling, assignment, baked paths, the go/no-go checks\n");
    static const int counts[] = { 1, 7, 24, 100 };
    ShowPoint a[SHOW_MAX_DRONES], b[SHOW_MAX_DRONES];

    for (int s = 0; s < SHOW_SHAPE_COUNT; ++s) {
        for (int n : counts) {
            const int made = showSampleShape(static_cast<ShowShape>(s), n, 18.0f, 40.0f, a, SHOW_MAX_DRONES);
            CHECK(made == n);
            const int again = showSampleShape(static_cast<ShowShape>(s), n, 18.0f, 40.0f, b, SHOW_MAX_DRONES);
            CHECK(again == made);
            bool same = true, in_box = true, level = true;
            for (int i = 0; i < made; ++i) {
                if (a[i].n != b[i].n || a[i].e != b[i].e || a[i].d != b[i].d) same = false;
                // Per axis, not radially: a square's corner is legitimately further
                // from the origin than its half-width, and that is not a leak.
                if (std::fabs(a[i].n) > 18.01f || std::fabs(a[i].e) > 18.01f) in_box = false;
                // Every shape has to honour the requested height: one that drops the
                // altitude field leaves the formation on the ground, and a shape that
                // only sometimes writes it is exactly what a static buffer hides.
                // Half the shape's own size is the tolerance a vertical formation
                // needs -- the column spans +/- 8 m of height by design -- while a
                // dropped altitude field is 40 m out and still fails.
                if (std::fabs(-a[i].d - 40.0f) > 9.01f) level = false;
            }
            CHECK(same);          // the same cloud every time, or a rehearsal proves nothing
            CHECK(in_box);
            CHECK(level);
        }
    }

    // Assignment: shifting the whole formation by half a spacing must not make
    // stations cross the field, and every point must be taken exactly once.
    const int n = 24;
    showSampleShape(SHAPE_GRID, n, 18.0f, 40.0f, a, SHOW_MAX_DRONES);
    for (int i = 0; i < n; ++i) { b[i] = a[i]; b[i].e += 1.5f; }
    int order[SHOW_MAX_DRONES];
    showAssign(a, b, n, order);
    bool unique = true;
    float moved = 0.0f, naive = 0.0f;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) if (order[i] == order[j]) unique = false;
        const ShowPoint& t = b[order[i]];
        moved += std::sqrt((t.n - a[i].n) * (t.n - a[i].n) + (t.e - a[i].e) * (t.e - a[i].e));
        naive += std::sqrt((b[i].n - a[i].n) * (b[i].n - a[i].n) + (b[i].e - a[i].e) * (b[i].e - a[i].e));
    }
    CHECK(unique);
    CHECK(moved <= naive + 0.01f);

    const ShowPoint p0 = { 0.0f, 0.0f, -40.0f }, p1 = { 20.0f, 0.0f, -50.0f };
    CHECK(showPath(p0, p1, 0.0f).n == 0.0f);
    CHECK(showPath(p0, p1, 1.0f).n == 20.0f);
    CHECK(showPath(p0, p1, 1.0f).d == -50.0f);
    CHECK(showPath(p0, p1, 0.1f).n < 1.0f);      // it leaves the ground slowly
    CHECK(showPath(p0, p1, -1.0f).n == 0.0f);    // and does not run backwards

    // A plan that should pass, and then one deliberate break per refusal bit: a
    // check that has never been seen red is not a check.
    ShowPlan ok{};
    ok.drones = 24;
    ok.act_count = 3;
    ok.acts[0] = { SHAPE_RING, 18.0f, 40.0f, 12.0f, 12.0f, 0x00e0a0 };
    ok.acts[1] = { SHAPE_GRID, 18.0f, 40.0f, 12.0f, 14.0f, 0xffd000 };
    CHECK(showValidate(ok, 100.0f, 0.0f) == SHOW_OK);
    CHECK(showDuration(ok) > 50.0f && showDuration(ok) < 90.0f);
    // The arc at 18 m holds 24 aircraft 1.7 m apart, which is not a formation anyone
    // should upload: the density check has to catch a shape that is too small for the
    // count even when every transition is slow and every point is inside the fence.
    ShowPlan dense = ok;
    dense.drones = 24;
    dense.acts[1].shape = SHAPE_ARC;
    dense.acts[1].scale_m = 18.0f;
    CHECK((showValidate(dense, 100.0f, 0.0f) & SHOW_SEPARATION) != 0u);
    // 24 aircraft through ring -> grid -> arc: the repair pass exists now, so this
    // plan has to be certified rather than refused -- and certified *because* of the
    // swaps, not because it was never tight. Greedy assignment plus altitude lanes
    // alone left stations 16 and 18 passing 0.03 m apart on the second transition.
    ShowPlan three = ok;
    three.act_count = 3;
    three.acts[2] = { SHAPE_ARC, 30.0f, 40.0f, 10.0f, 14.0f, 0xff4040 };
    CHECK(showValidate(three, 100.0f, 0.0f) == SHOW_OK);
    ShowPoint pos3[3 * SHOW_MAX_DRONES];
    int swaps = -1;
    CHECK(showCompilePlan(three, pos3, 3 * SHOW_MAX_DRONES, &swaps) == 3);
    CHECK(swaps > 0);
    int wi = 0, wa = 0, wb = 0;
    CHECK(showWorstPair(pos3, 3, three.drones, three.separation_m, wi, wa, wb) >= three.separation_m);
    // A formation that is too dense for its own shape cannot be repaired by handing
    // the same points to different aircraft: that one still has to be refused -- and
    // the repair pass has to notice and stand down instead of running its whole budget
    // on a show that was never going to fly (400 passes on a 100-aircraft plan before
    // this guard existed).
    ShowPlan wall = three;
    wall.acts[2].shape = SHAPE_ARC;
    wall.acts[2].scale_m = 18.0f;
    CHECK((showValidate(wall, 100.0f, 0.0f) & SHOW_SEPARATION) != 0u);
    ShowPoint posW[3 * SHOW_MAX_DRONES];
    int wswaps = -1;
    CHECK(showCompilePlan(wall, posW, 3 * SHOW_MAX_DRONES, &wswaps) == 3);
    CHECK(wswaps == 0);
    ShowPlan few = three;
    few.drones = 12;
    CHECK(showValidate(few, 100.0f, 0.0f) == SHOW_OK);

    ShowPlan bad = ok;
    bad.drones = 0;
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_BAD_COUNT) != 0u);
    bad = ok; bad.act_count = 0;
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_BAD_ACTS) != 0u);
    bad = ok; bad.acts[1].move_s = 0.0f;
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_BAD_ACTS) != 0u);
    bad = ok; bad.acts[1].alt_m = 5.0f;
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_ALTITUDE) != 0u);
    bad = ok; bad.acts[1].scale_m = 300.0f;
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_OUTSIDE) != 0u);
    bad = ok; bad.acts[1].move_s = 0.4f;
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_TOO_FAST) != 0u);
    bad = ok;
    bad.drones = 100;
    bad.acts[0] = { SHAPE_GRID, 2.0f, 40.0f, 12.0f, 12.0f, 0x00e0a0 };
    bad.acts[1] = { SHAPE_GRID, 2.0f, 45.0f, 12.0f, 12.0f, 0xffd000 };
    bad.acts[2] = { SHAPE_GRID, 2.0f, 40.0f, 10.0f, 12.0f, 0xff4040 };
    CHECK((showValidate(bad, 100.0f, 0.0f) & SHOW_SEPARATION) != 0u);
    CHECK((showValidate(ok, 5.0f, 0.0f) & SHOW_BATTERY) != 0u);
    CHECK((showValidate(ok, 100.0f, 5.5f) & SHOW_TOO_FAST) != 0u);   // wind eats the margin

    // Return slots: distinct, and further apart than the separation they must keep.
    ShowPlan big = ok;
    big.drones = 64;
    bool distinct = true, spread = true;
    for (int i = 0; i < big.drones; ++i) {
        const ShowPoint si = showReturnSlot(big, i);
        if (std::sqrt(si.n * si.n + si.e * si.e) > big.geofence_m) spread = false;
        for (int j = i + 1; j < big.drones; ++j) {
            const ShowPoint sj = showReturnSlot(big, j);
            const float dn = si.n - sj.n, de = si.e - sj.e;
            if (dn == 0.0f && de == 0.0f) distinct = false;
            if (std::sqrt(dn * dn + de * de) < big.separation_m) spread = false;
        }
    }
    CHECK(distinct);
    CHECK(spread);
}


int main() {
    testMissionCompletesAndLoops();
    testMissionGuards();
    testFailsafeDebounce();
    testUnreportedSensors();
    testValveSequencer();
    testProgrammeAnchor();
    testPlantPriority();
    testSunTimes();
    testLightPolicy();
    testPowerMeter();
    testGridProtection();
    testFireLoops();
    testScheduler();
    testTelemetryCodec();
    testShowChoreography();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
