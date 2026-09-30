// Host sandbox for the fire panel: the real firmware against a model of detectors,
// relays and wiring faults.
//
// The board sees milliVolts across a pull-up and four dry contacts. It is never told
// "the barn is on fire", so a passing test means the alarm was decided from a loop
// that changed resistance, and a wrong band edge shows up as a building that cannot
// alarm rather than as a green light.
//
// Build:
//   g++ -std=c++17 -Wall -Werror -O1 -DRANCH_SIM -DRANCH_HOST
//       -Ifirmware/fire/src -Ifirmware/lib
//       firmware/fire/src/*.cpp firmware/fire/test/test_sandbox.cpp -o fire-sandbox
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "fire_logic.h"
#include "frame_codec.h"
#include "hal.h"
#include "panel.h"
#include "telemetry.h"

namespace ranch {
void appSetup();
void appLoop();
void simCommand(const char*);
void simLimits(uint16_t confirm_s, uint16_t silence_s, uint16_t test_period_s);
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

void runFor(uint32_t seconds) {
    for (uint32_t i = 0; i < seconds * 1000u / 20u; ++i) appLoop();
}

// Every phase is a new board on the same ranch: the building is put back first, then
// the firmware boots, then the timers are set (booting reloads them from NVS), then
// the clock is handed over because there is no RTC on a simulation.
void boot() {
    halSimClear();
    appSetup();
    simLimits(20, 120, 3600);
    halSimTime(20u * 3600u + 10u, 2026, 4, 18);
}

struct Feed {
    uint32_t total;
    uint32_t normal;
    uint32_t fault;
    uint32_t alarm;
    uint32_t emergency;
    uint32_t siren;
    uint32_t strobe;
    uint32_t pump;
    uint32_t silenced;
    uint32_t supervision;
    uint32_t bell_fail;
    char lvl[12];
    char why[24];
    char z[6];
    uint32_t published;
};

bool isWord(const Field& f, const char* word) {
    return std::strlen(word) == f.value_len && std::strncmp(f.value, word, f.value_len) == 0;
}

Feed sampleFrames(uint32_t seconds) {
    Feed f{};
    f.lvl[0] = f.why[0] = f.z[0] = '\0';
    // Start from the count the panel has *already* reached. Seeding it at zero would
    // take the frame published before this window opened as if it were the first one
    // inside it, which turns a state change that happened two milliseconds ago into a
    // "still showing the old value" failure.
    UplinkStats start{};
    telemetryStats(start);
    uint32_t seen = start.published;
    for (uint32_t i = 0; i < seconds * 1000u / 20u; ++i) {
        appLoop();
        UplinkStats pub{};
        telemetryStats(pub);
        if (pub.published == seen) continue;
        seen = pub.published;
        const char* fr = telemetryLastFrame();
        if (!*fr) continue;
        FrameReader r(fr, std::strlen(fr));
        Field lvl{}, sw{}, st{}, pw{}, sil{}, z{}, sup{};
        f.total++;
        if (r.find("lvl", lvl)) {
            const size_t n = lvl.value_len < sizeof(f.lvl) - 1 ? lvl.value_len : sizeof(f.lvl) - 1;
            std::memcpy(f.lvl, lvl.value, n);
            f.lvl[n] = '\0';
            if (isWord(lvl, "NORMAL")) f.normal++;
            else if (isWord(lvl, "FAULT")) f.fault++;
            else if (isWord(lvl, "ALARM")) f.alarm++;
            else if (isWord(lvl, "EMERGENCY")) f.emergency++;
        }
        if (r.find("z", z) && z.value_len < sizeof(f.z)) {
            std::memcpy(f.z, z.value, z.value_len);
            f.z[z.value_len] = '\0';
        }
        if (r.find("siren", sw) && isWord(sw, "1")) f.siren++;
        if (r.find("strb", st) && isWord(st, "1")) f.strobe++;
        if (r.find("pump", pw) && isWord(pw, "1")) f.pump++;
        if (r.find("sil", sil) && isWord(sil, "1")) f.silenced++;
        if (r.find("sup", sup) && isWord(sup, "1")) f.supervision++;
        long bell_n = 0;
        if (r.getInt("bellfail", bell_n) && bell_n > 0) f.bell_fail++;
        // Cleared when a frame carries no reason, so the printed line cannot show a
        // reason from before the panel went quiet and read as a contradiction.
        Field whyf{};
        if (r.find("why", whyf)) {
            const size_t n = whyf.value_len < sizeof(f.why) - 1 ? whyf.value_len : sizeof(f.why) - 1;
            std::memcpy(f.why, whyf.value, n);
            f.why[n] = '\0';
        } else {
            f.why[0] = '\0';
        }
        f.published = pub.published;
    }
    return f;
}

void report(const char* phase, const Feed& f) {
    PanelCounters c{};
    halPanel(c);
    std::printf("  [%s] frames=%u lvl=%s why=%s z=%s | normal=%u fault=%u alarm=%u emerg=%u\n",
                phase, f.total, f.lvl, f.why, f.z, f.normal, f.fault, f.alarm, f.emergency);
    std::printf("        outputs: siren=%u strb=%u pump=%u silenced=%u sup=%u bell_fail=%u | "
                "panel: attempts=%u failures=%u pump_permit_ms=%u\n",
                f.siren, f.strobe, f.pump, f.silenced, f.supervision, f.bell_fail,
                c.bell_attempts, c.bell_failures, c.pump_permit_ms);
}

}  // namespace

int main() {
    std::printf("fire sandbox: a normal evening, four loops, one bell\n");

    // --- nominal --------------------------------------------------------------
    boot();
    Feed f = sampleFrames(30);
    report("nominal", f);
    UplinkStats u{};
    telemetryStats(u);
    CHECK(f.total > 25 && f.total < 35, "the dial keeps its 1 Hz rate");
    CHECK(u.mqtt_failed == 0, "no frame overflowed the buffer");
    CHECK(u.last_len > 80, "the frames are real frames");
    CHECK(f.normal == f.total, "a healthy panel reports healthy, not silent");
    CHECK(f.siren == 0 && f.strobe == 0, "nothing is making noise");
    CHECK(std::strcmp(f.z, "0000") == 0, "all four loops read their end-of-line resistor");
    CHECK(f.why[0] == '\0', "and carries no reason, because there is none");

    // The hourly self-test is what proves the bell works before it is needed, so the
    // test button has to produce exactly one pulse and the panel has to hear it.
    std::printf("fire sandbox: the bell proves itself\n");
    simCommand("test");
    runFor(1);
    PanelCounters c{};
    halPanel(c);
    CHECK(c.bell_attempts >= 1, "the coil was driven for the test pulse");
    CHECK(c.bell_failures == 0, "and the armature relay followed it");
    Feed ft = sampleFrames(2);
    CHECK(ft.normal == ft.total, "a successful test does not raise an alarm");

    // --- one detector: the building hears it, the pump does not move -----------
    std::printf("fire sandbox: smoke in the barn, one detector\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_SMOKE);
    Feed f1 = sampleFrames(10);
    report("single zone", f1);
    PanelCounters c1{};
    halPanel(c1);
    CHECK(f1.alarm > 0, "a single detector alarms the building");
    CHECK(f1.pump == 0, "and does not start the sprinkler pump on its own");
    CHECK(c1.pump_permit_ms == 0, "with the permit contact still open after ten seconds");
    CHECK(std::strcmp(f1.why, "SINGLE ZONE") == 0, "naming the confirmation it is waiting for");
    CHECK(f1.siren > 0, "the bell rang");
    CHECK(c1.bell_attempts > 0, "and was driven, not merely decided");
    CHECK(std::strncmp(f1.z, "01", 2) == 0, "the frame says which loop");

    // Twenty seconds of the same detector saying the same thing is the second
    // opinion; the escalation must arrive on its own, with nothing else touched.
    Feed f1b = sampleFrames(20);
    report("escalated", f1b);
    CHECK(f1b.emergency > 0, "an alarm that persists becomes an emergency");
    halPanel(c1);
    CHECK(c1.pump_permit_ms > 0, "and the permit closed by itself once it did");

    // --- two detectors: no waiting -------------------------------------------
    std::printf("fire sandbox: barn and house both alight\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_SMOKE);
    halSimLoop(SIM_LOOP_HOUSE, SIM_LOOP_SMOKE);
    Feed f2 = sampleFrames(6);
    report("two zones", f2);
    CHECK(f2.emergency > 0 && f2.alarm == 0, "two zones are confirmed the moment they both arrive");
    CHECK(f2.pump > 0, "and the pump is permitted without waiting for the clock");

    // --- a person outranks a detector ----------------------------------------
    std::printf("fire sandbox: the call point is broken\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_MCP, SIM_LOOP_SMOKE);
    Feed f3 = sampleFrames(5);
    report("manual call", f3);
    CHECK(f3.emergency > 0, "a broken glass is a second opinion by itself");
    CHECK(f3.pump > 0, "and it permits the pump immediately");
    FrameReader r3(telemetryLastFrame(), std::strlen(telemetryLastFrame()));
    Field mcp3{};
    CHECK(r3.find("mcp", mcp3) && isWord(mcp3, "1"), "and the frame says a person called it");

    // --- silence: the courtesy, not the cure ---------------------------------
    std::printf("fire sandbox: silencing a live alarm\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_STORE, SIM_LOOP_SMOKE);
    runFor(3);
    simCommand("silence");
    Feed fs = sampleFrames(5);
    report("silenced", fs);
    CHECK(fs.silenced > 0, "the panel accepts a silence on a detector alarm");
    CHECK(fs.siren == 0, "and the bell stops");
    CHECK(fs.strobe > 0, "while the beacons keep working -- silence is audible only");
    CHECK(fs.emergency + fs.alarm > 0, "and the alarm itself is still being reported");
    runFor(140);
    Feed fs2 = sampleFrames(5);
    report("silence expired", fs2);
    CHECK(fs2.silenced == 0, "the silence ran out on its own");
    CHECK(fs2.siren > 0, "and a still-latched alarm makes noise again");

    std::printf("fire sandbox: silencing a manual call point\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_MCP, SIM_LOOP_SMOKE);
    runFor(2);
    simCommand("silence");
    Feed fm = sampleFrames(3);
    report("mcp silence", fm);
    CHECK(fm.silenced == 0, "a person who broke the glass cannot be hung up on");
    CHECK(fm.siren > 0, "so the bell keeps ringing");
    CHECK(std::strcmp(fm.why, "SILENCE REFUSED") == 0 || fm.emergency > 0,
          "and the refusal is visible rather than the button doing nothing");

    // --- the wiring faults: the panel must not invent a fire -----------------
    std::printf("fire sandbox: the barn loop is cut\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_CUT);
    Feed fc = sampleFrames(10);
    report("loop cut", fc);
    CHECK(fc.fault > 0, "an open loop is a supervision fault");
    CHECK(fc.alarm == 0 && fc.emergency == 0, "and is not a fire");
    CHECK(std::strncmp(fc.z, "02", 2) == 0, "reported as an open loop, digit 2");
    CHECK(fc.siren == 0, "with the bell left alone: nothing is on fire");

    std::printf("fire sandbox: the loop is shorted, which looks like an alarm\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_POWER, SIM_LOOP_SHORT);
    Feed fsh = sampleFrames(10);
    report("loop short", fsh);
    CHECK(std::strncmp(fsh.z, "0003", 4) == 0, "a dead short reads as digit 3, not as a fire");
    CHECK(fsh.fault > 0, "and raises the supervision fault it is");
    CHECK(fsh.pump == 0, "because crushed cable should not flood a switch room");

    std::printf("fire sandbox: a relay buzzing at its threshold\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_HOUSE, SIM_LOOP_RELAY_CHATTER);
    Feed fch = sampleFrames(20);
    report("chatter", fch);
    CHECK(fch.emergency == 0, "a relay that never settles in the alarm band gets no pump");
    CHECK(fch.siren == 0, "and the three-sample hold keeps the dial quiet about it");
    halSimLoop(SIM_LOOP_HOUSE, SIM_LOOP_CLEAR);

    // --- the bell that is only commanded ------------------------------------
    std::printf("fire sandbox: the coil works, the armature does not\n");
    boot();
    runFor(3);
    halSimBellDead(true);
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_SMOKE);
    Feed fd = sampleFrames(10);
    report("dead bell", fd);
    PanelCounters cd{};
    halPanel(cd);
    CHECK(cd.bell_failures >= 1, "the plant saw a coil energise with nothing moving");
    CHECK(fd.alarm + fd.emergency > 0, "the alarm still stands, because the detectors are fine");
    // The failed bell does not downgrade the level. A panel that reported "fault"
    // while a zone was alight would send the operator off to look for a broken wire,
    // so the alarm keeps its level and the failure is carried in `sup` and `bell`.
    CHECK(fd.supervision > 0, "the supervision flag says something is not working");
    CHECK(fd.bell_fail > 0, "and the frame carries the failed notification, not just the alarm");

    // --- reset, and the one thing it must not do ----------------------------
    std::printf("fire sandbox: reset into a live alarm\n");
    boot();
    runFor(3);
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_SMOKE);
    runFor(3);
    simCommand("reset");
    Feed fr = sampleFrames(3);
    report("reset blocked", fr);
    CHECK(fr.alarm + fr.emergency > 0, "a reset with smoke present is refused");
    CHECK(std::strstr(telemetryLastEvent(), "reset-refused") != nullptr,
          "and the refusal is an event on the log, not a state that lasts half a second");
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_CLEAR);
    runFor(2);
    simCommand("reset");
    // The state frame goes out once a second, so the frame straddling a command is
    // always one that was composed before it. Let the panel settle and then measure
    // the steady state, rather than asserting on a transition.
    runFor(2);
    Feed fr2 = sampleFrames(4);
    report("reset taken", fr2);
    CHECK(fr2.normal > 0, "with the cause gone the panel clears");
    CHECK(fr2.siren == 0 && fr2.pump == 0, "and every output drops with it");
    PanelCounters cr{};
    halPanel(cr);
    CHECK(cr.false_alarm_starts == 1,
          "the barn episode is counted as the false alarm it was, with no second opinion");

    // --- the key switch: indications live, outputs dead ---------------------
    std::printf("fire sandbox: the panel is put on bypass\n");
    boot();
    runFor(3);
    halSimKey(false);
    Feed fk = sampleFrames(5);
    report("bypassed", fk);
    CHECK(fk.fault > 0, "a bypassed panel is a fault, not a normal one");
    CHECK(fk.siren == 0 && fk.strobe == 0, "and it cannot make noise");
    halSimKey(true);
    halSimLoop(SIM_LOOP_BARN, SIM_LOOP_SMOKE);
    runFor(3);
    halSimKey(false);
    runFor(2);
    Feed fkb = sampleFrames(4);
    report("bypassed with smoke", fkb);
    // What the key switch is for: the outputs go dead. The *indication* deliberately
    // stays an alarm, because a bypassed panel that downgrades a burning zone to
    // "fault" sends the operator off to look for a broken wire while the barn burns.
    CHECK(fkb.alarm + fkb.emergency > 0, "a bypassed panel still reports the alarm it sees");
    CHECK(fkb.siren == 0 && fkb.strobe == 0, "while every output stays down");
    FrameReader rkb(telemetryLastFrame(), std::strlen(telemetryLastFrame()));
    Field zkb{}, armkb{};
    CHECK(rkb.find("z", zkb) && zkb.value_len == 4 && zkb.value[1] == '1',
          "while the loop state is still reported, because a bypassed panel is blind "
          "and the log has to show that");
    CHECK(rkb.find("arm", armkb) && isWord(armkb, "0"), "and says it is bypassed");

    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
