// Host sandbox for the lighting board: the real application against a simulated
// sky. The firmware cannot see the sun - it sees millivolts from an LDR - so
// these tests ask the question that matters: did the light come on when it got
// dark, and did it stay off when it did not?
//
// Build:
//   g++ -std=c++17 -Wall -Werror -O1 -DRANCH_SIM -DRANCH_HOST
//       -Ifirmware/lighting/src -Ifirmware/lib
//       firmware/lighting/src/*.cpp firmware/lighting/test/test_sandbox.cpp -o light-sandbox
#include <cmath>
#include <cstdio>
#include <cstring>

#include "astro.h"
#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "light_policy.h"
#include "lights.h"
#include "telemetry.h"

namespace ranch {
void appSetup();
void appLoop();
void simCommand(const char*);
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

// Each appLoop() advances the simulated clock by 50 ms.
uint32_t loopsForMinutes(uint32_t minutes) { return minutes * 60u * 20u; }

struct Night {
    int32_t street_on_min = -1;      // first minute of day the street came on
    int32_t street_on_clock = -1;    // the board's own minute, read from the frame
    int32_t street_off_min = -1;
    int32_t house_off_min = -1;
    float max_duty = 0.0f;
    float max_lux_while_lit = 0.0f;
    uint32_t midday_lit = 0;
    uint32_t loops = 0;
    char last[208] = "";
};

Night runMinutes(uint32_t minutes) {
    Night n;
    char previous[208] = "";
    for (uint32_t i = 0; i < loopsForMinutes(minutes); ++i) {
        appLoop();
        const LampState& l = lightsGet();
        const bool lit = l.street || l.house || l.duty_now > 0.05f;
        if (lit) {
            if (l.duty_now > n.max_duty) n.max_duty = l.duty_now;
            if (l.lux > n.max_lux_while_lit) n.max_lux_while_lit = l.lux;
        }
        SkyCounters c{};
        halSky(c);
        n.midday_lit = c.midday_lit_events;

        const uint32_t minute = (i / 1200u);            // 1200 loops per minute
        const char* fr = telemetryLastFrame();
        if (l.street && n.street_on_min < 0) {
            n.street_on_min = static_cast<int32_t>(minute);
            FrameReader rt(fr, std::strlen(fr));
            Field tv{};
            long tval = 0;
            if (rt.find("t", tv) && rt.getInt("t", tval)) {
                n.street_on_clock = static_cast<int32_t>(tval);
            }
        }
        if (!l.street && n.street_on_min >= 0 && n.street_off_min < 0) {
            n.street_off_min = static_cast<int32_t>(minute);
        }
        if (!l.house && n.street_on_min >= 0 && n.house_off_min < 0) {
            n.house_off_min = static_cast<int32_t>(minute);
        }

        if (*fr && std::strcmp(fr, previous) != 0) {
            std::snprintf(previous, sizeof(previous), "%s", fr);
            std::snprintf(n.last, sizeof(n.last), "%s", fr);
        }
        n.loops++;
    }
    return n;
}

}  // namespace

int main() {
    std::printf("light sandbox: one midday, then the shortest day of the year\n");

    // Start at noon so the first thing the board has to get wrong is "on".
    appSetup();
    halSimTime(12 * 3600u, 2026, 4, 18);
    Night noon = runMinutes(120);
    SkyCounters c{};
    halSky(c);
    CHECK(noon.max_duty == 0.0f, "no lamp burned during two hours of midday");
    CHECK(c.midday_lit_events == 0, "the plant never saw light against daylight");
    CHECK(c.lux > 1000.0f, "the sky really was bright");
    CHECK(std::strstr(noon.last, "street=off") != nullptr, "and it said so");

    // Run on into the night: dusk is at 19:2x civil here, so give it until 01:00.
    Night night = runMinutes(780);
    halSky(c);
    CHECK(noon.street_on_min < 0 && night.street_on_min >= 0, "the perimeter came on at dusk");
    // The board's own clock, not a loop count: the two runs cover different parts
    // of the day and adding up iterations only works if nothing ever restarts.
    std::printf("        dusk at board-minute %d (civil sunset 19:16 = 1156), max_duty=%.2f, "
                "lit-while-bright=%.0f lux\n",
                night.street_on_clock, night.max_duty, night.max_lux_while_lit);
    const int32_t dusk_minute = night.street_on_clock;
    CHECK(dusk_minute >= 1130 && dusk_minute <= 1180, "just after civil sunset (19:16)");
    CHECK(night.max_duty > 0.95f, "the barn reached full during the ramp");
    CHECK(night.midday_lit == 0, "nothing lit while it was day, all evening");
    CHECK(night.max_lux_while_lit < LIGHT_DEFAULTS.daylight_lux_hold,
          "the brightest moment with a lamp on was still dark");

    // The lamp has to actually draw current, or "on" means nothing.
    const LampState& l = lightsGet();
    if (l.duty_now > 0.1f) {
        CHECK(std::fabs(l.amps - l.duty_now * LAMP_NOMINAL_A) < 0.35f,
              "bus current follows the commanded duty");
    } else {
        check(false, "the barn was not lit at the end of the run to sample", __LINE__);
    }

    // --- motion in the economy window ----------------------------------------
    std::printf("light sandbox: motion after the economy step\n");
    halSimMotion(false);
    Night before = runMinutes(5);
    const float econ_duty = lightsGet().duty_now;
    CHECK(econ_duty < 0.9f && econ_duty > 0.05f, "the barn dropped to its economy level");
    halSimMotion(true);
    runMinutes(2);
    const float boost = lightsGet().duty_now;
    CHECK(boost > econ_duty + 0.3f, "a body in the barn takes it back up");
    FrameReader r1(before.last, std::strlen(before.last));
    Field mode{};
    CHECK(r1.find("mode", mode), "the frame names the rule that set the level");
    halSimMotion(false);

    // --- counter-proof: a driver that is not there ---------------------------
    std::printf("light sandbox: lamp blown\n");
    halSimDriverFault(true);
    Night blown = runMinutes(60);
    CHECK(std::strstr(blown.last, "fault=") != nullptr, "the board reports the dead lamp");
    CHECK(std::strstr(blown.last, "amp=0.00") != nullptr, "and the bus really drew nothing");
    halSimDriverFault(false);

    // --- counter-proof: no clock ----------------------------------------------
    std::printf("light sandbox: RTC dead\n");
    appSetup();
    halSimTime(21 * 3600u, 2026, 4, 19);
    halSimClock(false);              // the coin cell is dead
    Night blind = runMinutes(600);
    CHECK(blind.street_on_min < 0, "with no clock nothing is scheduled");
    CHECK(std::strstr(blind.last, "clock=unset") != nullptr, "and the board says why");


    // --- the console answers: one verb it took, one it never heard of ---------
    std::printf("console answers: the lamps answer\n");
    appSetup();
    simCommand("street=1");
    for (int i = 0; i < 24; ++i) appLoop();   // 50 ms per loop
    CHECK(std::strstr(telemetryLastFrame(), ",ack=ok:street") != nullptr,
          "the frame names the command this board carried out");
    simCommand("nonesuch=1");
    for (int i = 0; i < 24; ++i) appLoop();   // 50 ms per loop
    CHECK(std::strstr(telemetryLastFrame(), ",ack=no:nonesuch") != nullptr,
          "and says so when it does not know the verb");
    simCommand(" street=1");      // the padding a terminal or a paste adds
    for (int i = 0; i < 24; ++i) appLoop();
    CHECK(std::strstr(telemetryLastFrame(), ",ack=ok:street") != nullptr &&
          std::strstr(telemetryLastFrame(), "ok: street") == nullptr,
          "a padded verb is the same command, not a refusal with a space in it");
    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
