// Host sandbox for the switchboard board: the real firmware against a model of
// the ranch's supply.
//
// The board sees milliVolts, a crossing count and four dry contacts, so a passing
// test means the protection decisions were made from measurements rather than from
// a summary the plant handed over. Each phase below prints what the plant did next
// to what the board reported, because the interesting failure is the pair of them
// disagreeing.
//
// Build:
//   g++ -std=c++17 -Wall -Werror -O1 -DRANCH_SIM -DRANCH_HOST
//       -Ifirmware/power/src -Ifirmware/lib
//       firmware/power/src/*.cpp firmware/power/test/test_sandbox.cpp -o power-sandbox
#include <cmath>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "feeders.h"
#include "frame_codec.h"
#include "hal.h"
#include "meter.h"
#include "power_quality.h"
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
    std::printf("  FAIL test_sandbox.cpp:%d  %s\n", line, what);
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

void runFor(uint32_t seconds) {
    for (uint32_t i = 0; i < seconds * 1000u / 20u; ++i) appLoop();
}

struct Feed {
    uint32_t total;
    uint32_t pump_on;
    uint32_t lit_on;
    uint32_t normal;
    uint32_t alarm;
    uint32_t shed;
    uint32_t trip;
    uint32_t sensor;
    float kw;
    float hz;
    float va, vb, vc;
    float load;
    float rcd;
    float temp;
    char last[256];
    char why[24];
};

float fieldOf(const FrameReader& r, const char* key, float fallback) {
    float v = 0.0f;
    return r.getFloat(key, v) ? v : fallback;
}

bool isWord(const Field& f, const char* word) {
    return std::strlen(word) == f.value_len && std::strncmp(f.value, word, f.value_len) == 0;
}

// Run the firmware for `seconds` and keep the last frame it published, counting
// which decisions were in force rather than only what the final one says.
//
// Frames are counted by the uplink's own publish counter, not by "did the text
// change": a board that has decided nothing is holding one identical frame twice
// a second, and a windowful of those is exactly the case where the rate check
// matters.
Feed sampleFrames(uint32_t seconds) {
    Feed f{};
    f.last[0] = '\0';
    f.why[0] = '\0';
    // Start from the count the panel has already reached. Seeding it at zero would
    // take the frame published before this window opened as if it were the first one
    // inside it, so the window's first sample is a stale reading, not a fresh one.
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
        Field act{}, pump{}, lit{}, why{}, ok{};
        f.total++;
        if (r.find("pump", pump) && isWord(pump, "1")) f.pump_on++;
        if (r.find("lit", lit) && isWord(lit, "1")) f.lit_on++;
        if (r.find("ok", ok)) f.sensor++;
        if (r.find("why", why)) {
            const size_t n = why.value_len < sizeof(f.why) - 1 ? why.value_len : sizeof(f.why) - 1;
            std::memcpy(f.why, why.value, n);
            f.why[n] = '\0';
        } else {
            // A frame with no reason clears it, so the printed line cannot show the
            // reason from before the board went quiet and read as a contradiction.
            f.why[0] = '\0';
        }
        if (r.find("act", act)) {
            if (isWord(act, "NORMAL")) f.normal++;
            else if (isWord(act, "ALARM")) f.alarm++;
            else if (isWord(act, "SHED")) f.shed++;
            else if (isWord(act, "TRIP")) f.trip++;
        }
        f.kw = fieldOf(r, "kw", f.kw);
        f.hz = fieldOf(r, "hz", f.hz);
        f.va = fieldOf(r, "va", f.va);
        f.vb = fieldOf(r, "vb", f.vb);
        f.vc = fieldOf(r, "vc", f.vc);
        f.load = fieldOf(r, "load", f.load);
        f.rcd = fieldOf(r, "rcd", f.rcd);
        f.temp = fieldOf(r, "temp", f.temp);
        std::snprintf(f.last, sizeof(f.last), "%s", fr);
    }
    return f;
}

void report(const char* phase, const Feed& f) {
    GridCounters g{};
    halGrid(g);
    std::printf("  [%s] frames=%u pump=%u lit=%u | normal=%u alarm=%u shed=%u trip=%u sensor=%u\n",
                phase, f.total, f.pump_on, f.lit_on, f.normal, f.alarm, f.shed, f.trip, f.sensor);
    std::printf("        board: va=%.1f vb=%.1f vc=%.1f hz=%.2f kw=%.1f load=%.0f%% rcd=%.0f "
                "temp=%.0f why=%s\n",
                f.va, f.vb, f.vc, f.hz, f.kw, f.load, f.rcd, f.temp, f.why);
    std::printf("        grid : va=%.1f vb=%.1f vc=%.1f hz=%.2f kw=%.1f kva=%.1f rcd=%.0f "
                "temp=%.0f starts=%u brown=%u\n",
                g.volts[0], g.volts[1], g.volts[2], g.hz, g.kw, g.kva, g.rcd_ma, g.temp_c,
                g.pump_starts, g.brownouts);
}

}  // namespace

int main() {
    std::printf("power sandbox: a healthy morning on a 45 kVA board\n");

    // --- nominal --------------------------------------------------------------
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);   // 06:30, water window
    Feed f = sampleFrames(60);
    report("nominal", f);
    GridCounters g{};
    halGrid(g);
    UplinkStats u{};
    telemetryStats(u);

    CHECK(f.total > 110 && f.total < 130, "telemetry held 2 Hz for a minute");
    CHECK(u.last_len > 100, "the frame carries the whole board");
    CHECK(u.mqtt_failed == 0, "no frame overflowed the buffer");
    CHECK(f.trip == 0 && f.shed == 0, "a healthy supply sheds and trips nothing");
    CHECK(f.alarm == 0, "and does not alarm");
    CHECK(f.pump_on > 100, "the pump feeder came in on its own");
    CHECK(f.lit_on > 100, "and so did the yard");
    CHECK(meterTrusted(), "the analog chain is believed");

    // The measurements have to agree with the plant that produced them, and to
    // within the filters' own tolerance rather than a margin picked to pass.
    CHECK(std::fabs(f.va - g.volts[0]) < 2.0f, "phase A volts match the grid");
    CHECK(std::fabs(f.vb - g.volts[1]) < 2.0f, "phase B volts match the grid");
    CHECK(std::fabs(f.vc - g.volts[2]) < 2.0f, "phase C volts match the grid");
    CHECK(f.va > 215.0f && f.va < 232.0f, "the board reads a sagged but legal supply");
    CHECK(std::fabs(f.hz - 50.0f) < 0.25f, "frequency measured from crossings, not assumed");
    CHECK(std::fabs(f.kw - g.kw) < 0.6f, "real power matches the load model");
    CHECK(f.load > 25.0f && f.load < 60.0f, "load percent is a third of the board");
    CHECK(f.rcd > 5.0f && f.rcd < 30.0f, "leakage sits at its normal value");
    CHECK(f.temp > 15.0f && f.temp < 55.0f, "the cabinet is warm, not hot");

    // Energy is the number the ranch pays for: compare the integral, not the rate.
    {
        const float kwh0 = meterGet().energy.kwh_total;
        float plant_kwh = 0.0f;
        for (uint32_t i = 0; i < 600u; ++i) {
            GridCounters p{};
            halGrid(p);                       // the plant's next step happens in appLoop
            appLoop();
            GridCounters q{};
            halGrid(q);
            plant_kwh += q.kw * 0.02f / 3600.0f;
            (void)p;
        }
        const float board_kwh = meterGet().energy.kwh_total - kwh0;
        std::printf("        metering over 12 s: grid %.4f kWh, board %.4f kWh\n",
                    static_cast<double>(plant_kwh), static_cast<double>(board_kwh));
        CHECK(plant_kwh > 0.0f, "the window moved real energy");
        CHECK(std::fabs(board_kwh - plant_kwh) < plant_kwh * 0.05f + 0.0005f,
              "the kWh the board counted is the kWh the grid delivered");
    }

    // --- the fault a motor pays for: one leg collapses ------------------------
    std::printf("power sandbox: phase B collapses while the pump runs\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    runFor(10);
    GridCounters before{};
    halGrid(before);
    halSimFault(SIM_FAULT_PHASE_B, true);
    Feed f2 = sampleFrames(20);
    report("phase loss", f2);
    GridCounters g2{};
    halGrid(g2);
    CHECK(f2.trip == 0, "a lost phase sheds, it does not trip the whole board");
    CHECK(f2.shed > 0, "the shed is reported while it is in force");
    CHECK(std::strcmp(f2.why, "PHASE LOSS") == 0, "and names the phase loss as its reason");
    CHECK(f2.pump_on > 0 && f2.pump_on < 60, "the pump ran briefly, then was taken off");
    CHECK(g2.pump_energised == false, "the contactor is actually open");
    CHECK(g2.brownouts <= 1, "the motor was single-phased for at most one event");
    CHECK(feedersGet().held, "the water controller's request is still waiting");
    halSimFault(SIM_FAULT_PHASE_B, false);
    runFor(10);
    halGrid(g2);
    CHECK(g2.pump_energised, "the pump comes back on its own when the leg returns");
    CHECK(g2.pump_starts >= 2, "and that is a second, logged start rather than a forgotten one");

    // --- the fault that must not re-arm itself: leakage -----------------------
    std::printf("power sandbox: leakage past 100 mA\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    runFor(10);
    halSimFault(SIM_FAULT_RCD, true);
    Feed f3 = sampleFrames(15);
    report("leakage", f3);
    GridCounters g3{};
    halGrid(g3);
    CHECK(f3.trip > 0, "a 100 mA leakage trips");
    CHECK(std::strcmp(f3.why, "LEAKAGE") == 0, "and says leakage, not overload");
    CHECK(g3.rcd_ma > 100.0f, "the plant really is over the trip level");
    CHECK(g3.pump_energised == false, "the pump is off");
    halSimFault(SIM_FAULT_RCD, false);
    runFor(20);
    halGrid(g3);
    CHECK(g3.rcd_ma < 20.0f, "the leakage itself has gone");
    CHECK(g3.pump_energised == false, "and the board still refuses to re-energise it");
    FrameReader r3(telemetryLastFrame(), std::strlen(telemetryLastFrame()));
    Field a3{}, why3{};
    CHECK(r3.find("act", a3) && isWord(a3, "TRIP"), "while it reports the trip it is holding");
    CHECK(r3.find("why", why3), "with the lockout named");
    simCommand("reclose");
    runFor(5);
    halGrid(g3);
    CHECK(g3.pump_energised, "until an operator says otherwise");

    // --- the fault that must not be acted on: nothing is measuring ------------
    std::printf("power sandbox: the external ADC stops answering\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    runFor(10);
    halSimFault(SIM_FAULT_EXT_ADC, true);
    Feed f4 = sampleFrames(25);
    report("sensor fault", f4);
    GridCounters g4{};
    halGrid(g4);
    CHECK(f4.sensor > 0, "the frame says the board could not measure");
    CHECK(f4.alarm > 0, "and raises an alarm about it");
    CHECK(std::strcmp(f4.why, "SENSOR FAULT") == 0, "naming the fault rather than a fake cause");
    CHECK(f4.shed == 0 && f4.trip == 0, "an unproven measurement does not shed or trip");
    CHECK(g4.pump_energised, "the pump that was already running keeps running");
    CHECK(g4.pump_starts == 1, "exactly once, from before the fault");
    halSimFault(SIM_FAULT_EXT_ADC, false);
    runFor(20);
    CHECK(!meterGet().sensor_fault, "the fault clears when the bus answers again");
    FrameReader r4(telemetryLastFrame(), std::strlen(telemetryLastFrame()));
    Field a4{};
    CHECK(r4.find("act", a4) && isWord(a4, "NORMAL"), "and the board goes quiet");

    // --- the reading that is not a measurement: the sense wire came off --------
    // This is the pair to the phase-loss phase above. Both present a voltage
    // channel at its floor, and the difference between them is whether the same
    // leg is still carrying amps. Get it wrong one way and a loose wire stops a
    // working pump; get it wrong the other and a collapsed leg is called a loose
    // wire and the motor burns on two phases.
    std::printf("power sandbox: phase A's voltage sense comes off, its load keeps drawing\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    runFor(10);
    halSimFault(SIM_FAULT_SENSE_A, true);
    Feed f4b = sampleFrames(20);
    report("sense off", f4b);
    GridCounters g4b{};
    halGrid(g4b);
    CHECK(f4b.sensor > 0, "the board says it lost a measurement");
    CHECK(std::strcmp(f4b.why, "SENSOR FAULT") == 0, "and names that, not a phase it did not lose");
    CHECK(f4b.shed == 0 && f4b.trip == 0, "a floating sense wire does not shed or trip anything");
    CHECK(f4b.va > 150.0f, "the last valid voltage is held rather than a fake zero written in");
    CHECK(g4b.volts[0] > 200.0f, "the leg really is still up behind the dead sense");
    CHECK(g4b.pump_energised, "and the pump is still running");
    halSimFault(SIM_FAULT_SENSE_A, false);
    runFor(15);
    CHECK(!meterGet().sensor_fault, "the fault clears when the wire goes back");
    FrameReader r4b(telemetryLastFrame(), std::strlen(telemetryLastFrame()));
    Field a4b{};
    CHECK(r4b.find("act", a4b) && isWord(a4b, "NORMAL"), "and the board stops claiming it");

    // --- too much load: shed first, trip second ------------------------------
    std::printf("power sandbox: the whole ranch runs at once\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    halSimDemandScale(5.0f);
    Feed f5 = sampleFrames(30);
    report("overload shed", f5);
    GridCounters g5{};
    halGrid(g5);
    CHECK(g5.kva > RATED_KVA * 0.9f, "the model really is over 90% of the board");
    CHECK(f5.shed > 0, "so the non-critical feeder comes off");
    CHECK(f5.trip == 0, "and it stops there: shedding is not an outage");
    CHECK(g5.pump_energised == false, "the pump contactor is open");

    std::printf("power sandbox: past the trip curve\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    halSimDemandScale(8.0f);
    Feed f5b = sampleFrames(30);
    report("overload trip", f5b);
    GridCounters g5b{};
    halGrid(g5b);
    CHECK(g5b.kva > RATED_KVA * 1.15f, "the model really is past the trip level");
    CHECK(f5b.trip > 0 && std::strcmp(f5b.why, "OVERLOAD") == 0, "which trips, not sheds");
    halSimDemandScale(1.0f);
    runFor(10);
    halGrid(g5b);
    CHECK(g5b.pump_energised == false, "a sustained overload needs a person too");
    simCommand("resume");
    runFor(5);
    halGrid(g5b);
    CHECK(g5b.pump_energised, "and resumes on the operator's word");

    // --- the supply itself goes wrong: frequency -----------------------------
    std::printf("power sandbox: a generator-sized supply droops\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    runFor(10);
    halSimFault(SIM_FAULT_FREQ_LOW, true);
    Feed f6 = sampleFrames(10);
    report("frequency", f6);
    CHECK(f6.trip > 0, "47.2 Hz trips: a grid-code item, not an alarm");
    CHECK(std::strcmp(f6.why, "FREQUENCY") == 0, "and names the frequency");
    halSimFault(SIM_FAULT_FREQ_LOW, false);
    runFor(60);
    GridCounters g6{};
    halGrid(g6);
    CHECK(g6.hz > 49.5f, "the supply recovered");
    CHECK(g6.pump_energised == false, "while the lockout is still holding the feeders open");
    runFor(300);
    halGrid(g6);
    CHECK(g6.pump_energised, "and the board re-arms itself once the lockout expires");
    CHECK(!feedersGet().trip_latched, "without an operator, for this fault and no other");

    // --- the operator's own authority ----------------------------------------
    std::printf("power sandbox: hand off auto\n");
    appSetup();
    halSimTime(static_cast<uint32_t>(6.5f * 3600.0f), 2026, 4, 18);
    runFor(10);
    halGrid(g6);
    CHECK(g6.pump_energised, "running to begin with");
    halSimPermit(true, false);            // the AUTO contact opens
    runFor(5);
    halGrid(g6);
    CHECK(g6.pump_energised == false, "a local switch outranks the whole network");
    halSimPermit(true, true);
    runFor(5);
    halGrid(g6);
    CHECK(g6.pump_energised, "and closes it again");


    // --- the console answers: one verb it took, one it never heard of ---------
    std::printf("console answers: the switchboard answers\n");
    appSetup();
    simCommand("reclose");
    runFor(2);
    CHECK(std::strstr(telemetryLastFrame(), ",ack=ok:reclose") != nullptr,
          "the frame names the command this board carried out");
    simCommand("nonesuch=1");
    runFor(2);
    CHECK(std::strstr(telemetryLastFrame(), ",ack=no:nonesuch") != nullptr,
          "and says so when it does not know the verb");
    simCommand(" shed=70");        // the same verb with the padding a terminal adds
    runFor(2);
    CHECK(std::strstr(telemetryLastFrame(), ",ack=ok:shed=70") != nullptr,
          "a padded verb is the same command");

    // --- the thresholds are the board's, not the page's ----------------------
    CHECK(std::strstr(telemetryLastFrame(), ",lim=uv") != nullptr,
          "the frame carries what this cabinet will trip on");
    simCommand("uv=0.72");
    runFor(2);
    CHECK(std::strstr(telemetryLastFrame(), "uv0.72/") != nullptr,
          "a changed threshold reads back from the board");
    simCommand("uv=1.7");           // outside the range this board accepts
    runFor(2);
    CHECK(std::strstr(telemetryLastFrame(), "uv0.72/") != nullptr &&
          std::strstr(telemetryLastFrame(), "uv1.70/") == nullptr,
          "and a threshold it refused did not move");
    std::printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
