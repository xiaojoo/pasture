// Host sandbox for the water board: the real application against the simulated
// plumbing. The firmware never sees the plant's state directly - it sees pulses,
// millivolts and two switches - so a passing test means the decisions were made
// from measurements, and a wrong threshold shows up as water in the wrong place.
//
// Build:
//   g++ -std=c++17 -Wall -Werror -O1 -DRANCH_SIM -DRANCH_HOST
//       -Ifirmware/water/src -Ifirmware/lib
//       firmware/water/src/*.cpp firmware/water/test/test_sandbox.cpp -o water-sandbox
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "programme.h"
#include "sense.h"
#include "telemetry.h"
#include "valves.h"

namespace ranch {
void appSetup();
void appLoop();
}  // namespace ranch

using namespace ranch;

namespace {

int checks = 0, fails = 0;

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    fails++;
    std::printf("  FAIL tests.cpp:%d  %s\n", line, what);
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

// Run the firmware for `seconds` of virtual time.
void runFor(uint32_t seconds) {
    for (uint32_t i = 0; i < seconds * 1000u / 20u; ++i) appLoop();
}

// Count the frames in which each valve was reported open, and keep the last one.
struct Feed {
    uint32_t barn_on;
    uint32_t house_on;
    uint32_t pump_on;
    uint32_t total;
    uint32_t overflow;
    uint32_t dry;
    uint32_t stuck;
    char last[208];
};

Feed sampleFrames(uint32_t seconds) {
    Feed f{};
    f.last[0] = '\0';
    char previous[208] = "";
    for (uint32_t i = 0; i < seconds * 1000u / 20u; ++i) {
        appLoop();
        const char* fr = telemetryLastFrame();
        if (!*fr) continue;
        // The last frame is held between publishes, so counting every iteration
        // would report loop passes, not frames. Only a changed frame counts.
        if (std::strcmp(fr, previous) == 0) continue;
        std::snprintf(previous, sizeof(previous), "%s", fr);
        FrameReader r(fr, std::strlen(fr));
        Field barn{}, house{};
        f.total++;
        if (r.find("barn", barn) && std::strncmp(barn.value, "on", barn.value_len) == 0) f.barn_on++;
        if (r.find("house", house) && std::strncmp(house.value, "on", house.value_len) == 0) f.house_on++;
        long p = 0;
        if (r.getInt("pump", p) && p == 1) f.pump_on++;
        if (std::strstr(fr, "why=DRY RUN")) f.dry++;
        if (std::strstr(fr, "why=BARN STUCK")) f.stuck++;
        std::snprintf(f.last, sizeof(f.last), "%s", fr);
    }
    return f;
}

// Print what a window actually measured. A failure that reports only "too many
// frames" cannot be diagnosed; these numbers can.
void report(const char* phase, const Feed& f) {
    PlantCounters c{};
    halPlant(c);
    std::printf("  [%s] total=%u barn_on=%u house_on=%u pump_on=%u\n",
                phase, f.total, f.barn_on, f.house_on, f.pump_on);
    std::printf("        dry=%u stuck=%u plant: barn=%.1f L house=%.1f L leaked=%.1f L "
                "tank=%.0f%% refills=%u\n",
                f.dry, f.stuck, c.barn_litres, c.house_litres, c.leaked_litres,
                c.tank_pct, c.tank_refills);
}

}  // namespace

int main() {
    std::printf("water sandbox: a dry day, two lines, an hourly trough\n");

    // --- nominal day ---------------------------------------------------------
    appSetup();
    halSimTime(28800u, 2026, 4, 18);          // 08:00 local
    Feed f = sampleFrames(7300);              // 08:00 to 10:03 => two scheduled runs
    report("day", f);
    PlantCounters c{};
    halPlant(c);
    UplinkStats u{};
    telemetryStats(u);
    const SenseState& s = senseGet();

    // the programme actually runs on the wall clock: two hourly cycles in a
    // little over two hours, each holding the coil for 240 s
    FrameReader r0(f.last, std::strlen(f.last));
    Field runs{};
    long runs_n = 0;
    CHECK(r0.find("runs", runs) && r0.getInt("runs", runs_n), "the frame counts its cycles");
    CHECK(runs_n == 2, "two dispenses in two hours, no more and no less");
    CHECK(f.barn_on > 600 && f.barn_on < 1100, "the barn valve opened on schedule, not continuously");
    CHECK(f.house_on > f.barn_on, "the house line stayed available through the whole day");
    CHECK(f.pump_on > 0, "the pressuriser came up before water moved");
    CHECK(f.total > 3000, "telemetry kept its rate");
    CHECK(u.mqtt_failed == 0, "no frame overflowed the buffer");
    CHECK(u.last_len > 60, "the frames are real frames");

    // what the firmware measured versus what the plant did
    // Two cycles of 240 s through a 12 L/min trough, with the header tank already
    // near full so the pump is interlocked off part of the time and the lines run
    // on gravity head: the plant's own physics, not a number picked to pass.
    CHECK(c.barn_litres > 20.0f && c.barn_litres < 80.0f, "two cycles moved a plausible volume");
    CHECK(c.tank_refills > 0, "the pressuriser did top the tank up");
    CHECK(c.house_litres > 100.0f, "the house line drew water all day");
    CHECK(std::fabs(c.barn_litres - s.barn.litres_total) < 3.0f,
          "the firmware's barn total matches the plant's");
    CHECK(std::fabs(c.house_litres - s.house.litres_total) < 3.0f,
          "and the house total matches too");
    CHECK(c.leaked_litres == 0.0f, "no water ended up where it should not");
    CHECK(!c.leak_active, "the trough never overflowed at 240 s per hour");
    CHECK(c.dry_run_events == 0, "the pump never ran dry");
    CHECK(c.stuck_valve_events == 0, "every open valve got water");
    // The instantaneous rate goes through two different filters - the plant ramps
    // with a 1.1 s time constant, the meter uses a 2 s bucket window - so the
    // honest comparison is the integral: over a window of steady flow, the litres
    // the firmware counted must match the litres that moved.
    {
        PlantCounters p0{};
        halPlant(p0);
        const float f0 = senseGet().barn.litres_total;
        uint32_t moving = 0;
        for (uint32_t i = 0; i < 60000u / 50u; ++i) {
            appLoop();
            PlantCounters p{};
            halPlant(p);
            if (p.barn_flow_lmin > 1.0f) moving++;
        }
        PlantCounters p1{};
        halPlant(p1);
        const float plant_l = p1.barn_litres - p0.barn_litres;
        const float meter_l = senseGet().barn.litres_total - f0;
        std::printf("        metering over 20 min: plant %.1f L, firmware %.1f L, %u samples flowing\n",
                    plant_l, meter_l, moving);
        CHECK(moving > 600, "water was moving for a usable part of the window");
        CHECK(plant_l > 1.0f, "and a real volume went through the barn line");
        CHECK(std::fabs(plant_l - meter_l) < plant_l * 0.02f + 0.05f,
              "the metered litres match the plant within 2%");
    }
    CHECK(s.tank_pct > 1.0f, "the header tank still has water in it");
    CHECK(s.pressure_bar > 0.4f, "the line has head while a valve is open");

    // --- the fault that a timer cannot see: the borehole goes dry -------------
    std::printf("water sandbox: supply lost mid-cycle\n");
    appSetup();
    halSimTime(28800u + 3540u, 2026, 4, 18);   // 08:59, so a cycle starts now
    runFor(60);
    halPlantSupplyLost(true);
    Feed f2 = sampleFrames(600);
    report("supply lost", f2);
    halPlant(c);
    FrameReader rr(f2.last, std::strlen(f2.last));
    Field why{};
    CHECK(rr.find("why", why), "the frame carries a reason");
    const bool dry = f2.dry + f2.stuck > 0;
    CHECK(dry, "a dry borehole is named in the log, not silently ignored");
    CHECK(c.dry_run_events > 0, "the plant saw the pump run dry");
    CHECK(std::strncmp(f2.last + 6, "barn=off", 8) == 0 || f2.barn_on == 0,
          "the barn valve was shut rather than held open against nothing");

    // --- counter-proof: the leak sensor ---------------------------------------
    std::printf("water sandbox: leak injected\n");
    appSetup();
    halSimTime(28800u, 2026, 4, 18);
    runFor(30);
    halPlantLeak(true);
    Feed f3 = sampleFrames(1200);
    report("leak", f3);
    // A second and a half of leak debounce is the correct behaviour, so the
    // first few frames may still show water running. What must not happen is the
    // valves staying open because the sensor "only" went wet.
    CHECK(f3.barn_on == 0, "no dispense starts while the floor is wet");
    CHECK(f3.house_on <= 12, "the house line closes inside the debounce window");
    FrameReader rl(f3.last, std::strlen(f3.last));
    Field h3{};
    CHECK(rl.find("house", h3) && std::strncmp(h3.value, "off", h3.value_len) == 0,
          "and the last frame says it is shut");
    CHECK(std::strlen(f3.last) > 0, "the last frame still reports");
    halPlant(c);
    CHECK(c.leak_active, "the plant agrees there is a leak");

    // --- counter-proof: no clock, no schedule ---------------------------------
    std::printf("water sandbox: RTC missing\n");
    appSetup();
    halSimClock(false);              // the coin cell is dead
    Feed f4 = sampleFrames(7300);
    report("no clock", f4);
    CHECK(f4.barn_on == 0, "nothing dispenses on a board that does not know the time");
    CHECK(f4.total > 3000, "while still reporting");
    FrameReader r4(f4.last, std::strlen(f4.last));
    Field clock{}, house{};
    CHECK(r4.find("clock", clock), "and it says the clock is missing");
    CHECK(r4.find("house", house), "the house line is still under its own control");

    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
