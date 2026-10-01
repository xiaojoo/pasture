// Switchboard board entry point: protection thresholds, the control loop, and the
// commands the rest of the ranch sends it.
//
// One cooperative task measures, decides and drives in that fixed order every
// tick. The order is the design: the meters have to be read before the decision
// can be made, the decision before the contactors move, and the contactor
// feedback has to be read after it moved so the confirmation lands in the same
// frame the dashboard sees.
//
// What may open a coil is deliberately hard to reach. The protection layer can
// only ever refuse; the demand comes from the local hand-off-auto switch and the
// water controller's request, and a trip that came from leakage, temperature or
// an overload stays open until a person says otherwise.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "feeders.h"
#include "frame_codec.h"
#include "hal.h"
#include "meter.h"
#include "power_meter.h"
#include "power_quality.h"
#include "scheduler.h"
#include "telemetry.h"

#if !defined(RANCH_SIM)
#include <Arduino.h>
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#ifndef RANCH_FW_VERSION
#define RANCH_FW_VERSION "dev"
#endif

namespace ranch {
namespace {

// The water controller renews its pump request once a second. Five seconds of
// silence is four missed refreshes, and after that the pump stops on its own:
// a request that outlives the thing that made it would run a tank dry.
constexpr uint32_t PUMP_LEASE_MS = 5000;

Scheduler sched;
int job_control = -1, job_telem = -1, job_persist = -1, job_health = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

GridLimits limits = GRID_DEFAULTS;
GridState gstate{};
float nominal_v = NOMINAL_PHASE_VOLTS;
float rated_kva = RATED_KVA;
float site_pf = SITE_POWER_FACTOR;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "power-1";
uint16_t mqtt_port = MQTT_PORT;

uint32_t pump_lease_ms = 0;
bool lights_net_on = false;
bool networked = false;         // a request has arrived at least once, or the broker is up
bool pump_auto_local = true;    // what a standalone board drives its pump feeder with
uint16_t today_key = 0;
bool have_clock = false;
bool manual_latch = false;
GridAction act = GridAction::Normal;
GridAction prev_act = GridAction::Normal;
char reason[20] = "";
char prev_reason[20] = "";
uint32_t trips = 0, sheds = 0, alarms = 0;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.add("rated", rated_kva, 0);
    w.endLine();
    consoleWrite(line, w.size());
}

void paramsLoad() {
    int32_t v = 0;
    float f = 0.0f;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", tag, sizeof(tag)) || !tag[0]) {
        std::snprintf(tag, sizeof(tag), "%s", "power-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);
    if (nvGetI32("pauto", v)) pump_auto_local = v != 0;

    // Every threshold is range-checked on the way in. A limit that arrives as a
    // typo is worse than no limit at all, because it looks configured.
    if (nvGetF32("uv", f) && f > 0.5f && f < 1.0f) limits.under_v_pct = f;
    if (nvGetF32("ov", f) && f > 1.0f && f < 1.5f) limits.over_v_pct = f;
    if (nvGetF32("hzlo", f) && f > 40.0f && f < 50.0f) limits.hz_lo = f;
    if (nvGetF32("hzhi", f) && f > 50.0f && f < 65.0f) limits.hz_hi = f;
    if (nvGetF32("shed", f) && f > 20.0f && f < 100.0f) limits.shed_load_pct = f;
    if (nvGetF32("trip", f) && f > 100.0f && f < 400.0f) limits.trip_load_pct = f;
    if (nvGetF32("unb", f) && f > 1.0f && f < 50.0f) limits.unbalance_max_pct = f;
    if (nvGetF32("rcda", f) && f > 5.0f && f < 100.0f) limits.rcd_alarm_ma = f;
    if (nvGetF32("rcdt", f) && f > 30.0f && f < 500.0f) limits.rcd_trip_ma = f;
    if (nvGetF32("tmpa", f) && f > 30.0f && f < 70.0f) limits.temp_alarm_c = f;
    if (nvGetF32("tmpt", f) && f > 50.0f && f < 105.0f) limits.temp_trip_c = f;
    if (nvGetF32("deb", f) && f > 0.1f && f < 60.0f) limits.debounce_s = f;
    if (nvGetF32("lock", f) && f > 5.0f && f < 3600.0f) limits.reclose_lockout_s = f;
    if (nvGetF32("nomv", f) && f > 100.0f && f < 500.0f) nominal_v = f;
    if (nvGetF32("rated", f) && f > 1.0f && f < 2500.0f) rated_kva = f;
    if (nvGetF32("pf", f) && f > 0.2f && f <= 1.0f) site_pf = f;
    // The two leakage levels must not cross, whatever combination was restored.
    if (limits.rcd_trip_ma <= limits.rcd_alarm_ma) limits.rcd_alarm_ma = limits.rcd_trip_ma * 0.3f;
    if (limits.temp_trip_c <= limits.temp_alarm_c) limits.temp_alarm_c = limits.temp_trip_c * 0.75f;
    if (limits.trip_load_pct <= limits.shed_load_pct) limits.shed_load_pct = limits.trip_load_pct * 0.78f;
}

void paramsPersist() {
    nvSetF32("uv", limits.under_v_pct);
    nvSetF32("ov", limits.over_v_pct);
    nvSetF32("hzlo", limits.hz_lo);
    nvSetF32("hzhi", limits.hz_hi);
    nvSetF32("shed", limits.shed_load_pct);
    nvSetF32("trip", limits.trip_load_pct);
    nvSetF32("rcdt", limits.rcd_trip_ma);
    nvSetF32("tmpt", limits.temp_trip_c);
    nvSetF32("nomv", nominal_v);
    nvSetF32("rated", rated_kva);
    nvSetF32("pf", site_pf);
}

// What this cabinet will actually trip on, as one self-describing string. The names
// travel attached to their numbers on purpose: a packed list re-ordered by the next
// person to touch it would publish a plausible frame with the wrong thresholds, and
// nothing downstream could tell.
void publishLimits() {
    char s[128];
    std::snprintf(s, sizeof(s),
                  "uv%.2f/ov%.2f/rcdt%.0f/tmpt%.0f/deb%.1f/shed%.0f/nomv%.0f/rated%.0f/pf%.2f",
                  static_cast<double>(limits.under_v_pct), static_cast<double>(limits.over_v_pct),
                  static_cast<double>(limits.rcd_trip_ma), static_cast<double>(limits.temp_trip_c),
                  static_cast<double>(limits.debounce_s), static_cast<double>(limits.shed_load_pct),
                  static_cast<double>(nominal_v), static_cast<double>(rated_kva),
                  static_cast<double>(site_pf));
    telemetrySetLimits(s);
}

void clockUpdate() {
    uint16_t y = 0;
    uint8_t mo = 0, dm = 0;
    uint32_t sod = 0;
    have_clock = clockNow(sod, y, mo, dm);
    // Without a clock there is no "today": the daily counter keeps accumulating
    // against the last day key it knew and the frame says clock=unset, which is a
    // claim the dashboard can act on. A silent 0 would not be.
    if (have_clock) today_key = dayKey(y, mo, dm);
}

// Leakage, cabinet temperature and a sustained overload are the three that must
// not re-energise themselves. Frequency and an open breaker are different: the
// grid recovers, and someone re-closes the breaker by hand, which the auxiliary
// contact reports.
bool needsOperator(const char* r) {
    return std::strcmp(r, "LEAKAGE") == 0 || std::strcmp(r, "OVERTEMP") == 0 ||
           std::strcmp(r, "OVERLOAD") == 0;
}

void publish() {
    PowerReport r{};
    r.m = meterGet();
    r.fo = feedersGet();
    r.action = act;
    r.reason = reason;
    r.breaker_closed = breakerClosed();
    r.clock_valid = have_clock;
    r.uptime_s = (halMillis() - boot_ms) / 1000u;
    telemetryPublish(r);
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[20], value[32];
    size_t i = 0;
    // A typed line often arrives padded: a terminal adds a space, a paste adds one.
    // Skipping the padding is what makes `street=1` and ` street=1` one command.
    while (i < len && (payload[i] == ' ' || payload[i] == '\t')) ++i;
    // The read cursor and the write index are two things. With one variable doing
    // both, a padded line left verb[0] uninitialised and the verb was compared
    // against garbage -- which is how a verb this board knows came out refused.
    size_t j = 0;
    while (i < len && j < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[j++] = payload[i++];
    }
    verb[j] = '\0';
    size_t n = 0;
    if (i < len && payload[i] == '=') {
        ++i;
        while (i < len && n < sizeof(value) - 1) value[n++] = payload[i++];
        value[n] = '\0';
    } else {
        value[0] = '\0';
    }

    bool taken = true;
    if (std::strcmp(verb, "pump") == 0) {
        // Only a request makes the board networked. A reclose or a threshold
        // change from an operator's phone must not silently take the ranch's
        // irrigation away by moving the board into a mode where nobody is
        // refreshing the lease.
        networked = true;
        if (value[0] == '1' || value[0] == 'o') pump_lease_ms = PUMP_LEASE_MS;
        else pump_lease_ms = 0;
    } else if (std::strcmp(verb, "lit") == 0) {
        networked = true;
        lights_net_on = value[0] == '1' || value[0] == 'o';
    } else if (std::strcmp(verb, "reclose") == 0) {
        // The operator's own word, and the only thing that clears a latched trip.
        // The lockout timer is cleared with it: an operator standing at the
        // cabinet is a better authority than a stopwatch, and if the fault is
        // still there the rule that tripped re-fires on the next tick.
        manual_latch = false;
        gridReset(gstate);
        feedersReclose();
    } else if (std::strcmp(verb, "stop") == 0) {
        pump_lease_ms = 0;
        lights_net_on = false;
        pump_auto_local = false;
        nvSetI32("pauto", 0);
    } else if (std::strcmp(verb, "resume") == 0) {
        // Restoring automatic operation means restoring the feeders with it: an
        // operator who says "come back on" and then finds the board still holding
        // a four minute stopwatch has been given a command that does nothing.
        pump_auto_local = true;
        manual_latch = false;
        gridReset(gstate);
        feedersReclose();
        nvSetI32("pauto", 1);
    } else if (std::strcmp(verb, "shed") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 20 && v < 100) limits.shed_load_pct = static_cast<float>(v);
        else taken = false;
    } else {
        float f = std::strtof(value, nullptr);
        if (std::strcmp(verb, "uv") == 0 && f > 0.5f && f < 1.0f) limits.under_v_pct = f;
        else if (std::strcmp(verb, "ov") == 0 && f > 1.0f && f < 1.5f) limits.over_v_pct = f;
        else if (std::strcmp(verb, "rcdt") == 0 && f > 30.0f && f < 500.0f) limits.rcd_trip_ma = f;
        else if (std::strcmp(verb, "tmpt") == 0 && f > 50.0f && f < 105.0f) limits.temp_trip_c = f;
        else if (std::strcmp(verb, "deb") == 0 && f > 0.1f && f < 60.0f) limits.debounce_s = f;
        else if (std::strcmp(verb, "nomv") == 0 && f > 100.0f && f < 500.0f) nominal_v = f;
        else if (std::strcmp(verb, "rated") == 0 && f > 1.0f && f < 2500.0f) rated_kva = f;
        else if (std::strcmp(verb, "pf") == 0 && f > 0.2f && f <= 1.0f) site_pf = f;
        else taken = false;
    }
    if (taken) { paramsPersist(); publishLimits(); }
    // A command that was not recognised is reported rather than dropped: the
    // alternative is an operator believing the board has ignored them.
    telemetryNoteAck(verb, value, taken);
    telemetryEvent(taken ? "cmd" : "cmd-unknown", verb);
}

void controlTick(uint32_t dt_ms) {
    clockUpdate();
    meterSample(dt_ms, today_key);
    const Metered& m = meterGet();

    if (pump_lease_ms > 0) {
        pump_lease_ms = pump_lease_ms > dt_ms ? pump_lease_ms - dt_ms : 0;
    }

    // The permit input is the AUTO contact of a hand-off-auto switch: closed, the
    // water controller's request is allowed through; open, the operator has taken
    // the feeder off automatic and nothing on the network may run it. With no
    // network at all the board falls back to its own setting, which is what a
    // switchboard has to do on the day the fibre gets dug up.
    const bool pump_requested = networked ? pump_lease_ms > 0 : pump_auto_local;
    const bool lights_requested = networked ? lights_net_on : pump_auto_local;
    feedersDemand(Feeder::Pump, pumpPermitClosed() && pump_requested);
    feedersDemand(Feeder::Lights, lightsPermitClosed() && lights_requested);

    GridInput in{};
    in.read = m.read;
    in.nominal_v = nominal_v;
    in.rcd_ma = m.rcd_ma;
    in.temp_c = m.temp_c;
    in.breaker_closed = breakerClosed();
    // Confirmed running, not commanded: the overload rule wants to know what the
    // board is actually carrying.
    in.pump_running = feedersPumpRunning();
    in.sensor_fault = m.sensor_fault;

    const GridDecision d = gridEvaluate(limits, gstate, in, dt_ms / 1000.0f);
    act = d.action;
    std::snprintf(reason, sizeof(reason), "%s", d.reason);

    if (d.lockout && needsOperator(d.reason)) manual_latch = true;

    // Nothing may be trusted, so nothing new starts: an unmeasured board is not a
    // healthy board. The contactors that are already closed stay as they are,
    // because dropping the yard lighting because an ADC came loose is its own
    // outage.
    const bool trustworthy = meterTrusted();
    feedersSupplyOk(trustworthy && !m.read.phase_loss);
    feedersShed(d.open_pump && d.action != GridAction::Trip);
    feedersTrip(d.lockout || manual_latch);

    const FeederOutputs before = feedersGet();
    feedersTick(dt_ms);
    const FeederOutputs& now = feedersGet();

    // --- edges worth a line in the log ----------------------------------------
    if (before.contactor_fault != now.contactor_fault) {
        telemetryEvent("contactor-fault", now.contactor_fault ? "NO FEEDBACK" : "CLEARED");
    }
    if (before.contactor_welded != now.contactor_welded) {
        telemetryEvent("contactor-welded", now.contactor_welded ? "STILL CLOSED" : "CLEARED");
    }

    if (act != prev_act || std::strcmp(reason, prev_reason) != 0) {
        prev_act = act;
        std::snprintf(prev_reason, sizeof(prev_reason), "%s", reason);
        switch (act) {
            case GridAction::Trip:
                trips++;
                telemetryEvent("grid-trip", reason);
                break;
            case GridAction::ShedLoad:
                sheds++;
                telemetryEvent("grid-shed", reason);
                break;
            case GridAction::Alarm:
                alarms++;
                telemetryEvent("grid-alarm", reason);
                break;
            case GridAction::Normal:
                telemetryEvent("grid-clear", "");
                break;
        }
    }
}

void supervision() {
    static bool down = false;
    static uint32_t down_ms = 0;
    if (wifiUp()) {
        down = false;
        return;
    }
    if (!down) {
        down = true;
        down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (down_ms + 30000u)) >= 0) {
        down_ms = halMillis();
        wifiReconnect();
    }
}

void healthLine() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "health");
    w.add("trips", static_cast<int>(trips));
    w.add("shed", static_cast<int>(sheds));
    w.add("coil", static_cast<int>(feedersGet().pump_failures));
    w.add("meas", meterTrusted() ? 1 : 0);
    w.add("hz", meterGet().read.hz, 2);
    w.endLine();
    consoleWrite(line, w.size());
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;                 // a stalled loop must not skip a cycle

    if (sched.due(job_control, dt)) {
        controlTick(sched.elapsed(job_control));
        feedWatchdog();
        setStatusLed(act != GridAction::Normal);
    }
    if (sched.due(job_telem, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_persist, dt)) meterPersist();
    if (sched.due(job_health, dt)) healthLine();
    supervision();
}

#if !defined(RANCH_SIM)
void controlTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(20);
    }
}
#endif

}  // namespace

// The host sandbox drives the command channel the same way the broker does, so a
// reclose is exercised rather than assumed. Outside the anonymous namespace
// because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }

#if defined(ARDUINO)
// The console's input, as a command door: in the simulation there is no broker, so
// this is the only way the page can reach this board at all. Same parser the broker
// calls, so a verb the bench takes and a verb the console takes cannot drift apart.
// ARDUINO only: the host sandbox drives simCommand from its own test, and a stdin
// reader there would make a unit test wait on a terminal.
void consoleCommandPump() {
    static char line[40];
    static size_t n = 0;
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            line[n] = '\0';
            if (n) simCommand(line);
            n = 0;
            continue;
        }
        if (n < sizeof(line) - 1) line[n++] = static_cast<char>(c);
        else n = 0;
    }
}
#endif
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    sched.clear();
    feedersInit();
    telemetryInit();
    paramsLoad();
    publishLimits();
    meterInit(site_pf, nominal_v, rated_kva);
    meterLoad();
    gridReset(gstate);
    manual_latch = false;
    pump_lease_ms = 0;
    lights_net_on = false;
    networked = false;
    act = GridAction::Normal;
    prev_act = GridAction::Normal;
    reason[0] = '\0';
    prev_reason[0] = '\0';
    trips = sheds = alarms = 0;
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_control = sched.add("control", CONTROL_PERIOD_MS);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_persist = sched.add("persist", 60000);
    job_health = sched.add("health", 10000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the control loop has to be created
    // here: without it the board prints its banner and then stops.
    xTaskCreatePinnedToCore(controlTask, "power", STACK_METER, nullptr, PRIO_METER, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
#if defined(ARDUINO)
    consoleCommandPump();
#endif
    halSimPump();
    runJobs();
    halDelayMs(20);
#endif
}

}  // namespace ranch

#if !defined(RANCH_SIM)
void setup() { ranch::appSetup(); }
void loop() { vTaskDelete(NULL); }
#endif
