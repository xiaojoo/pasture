// Water board entry point: parameters, the control loop, and the ground commands.
//
// One cooperative task runs sensing, the decision table, the programme and the
// outputs in that fixed order every tick. There is no locking and none is wanted:
// the state that matters is "what the plant was doing when the decision was made",
// and a snapshot taken in the middle of an update describes a plant that never
// existed.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "programme.h"
#include "scheduler.h"
#include "sense.h"
#include "telemetry.h"
#include "valve_logic.h"
#include "valves.h"
#include "water_safety.h"

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

Scheduler sched;
int job_control = -1, job_telem = -1, job_persist = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

Programme prog{};
ProgrammeState pstate{};
PlantState plant_state{};
PlantLimits plant_limits = PLANT_DEFAULTS;
ValveLimits valve_limits = VALVE_DEFAULTS;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "water-1";
uint16_t mqtt_port = MQTT_PORT;

bool house_user_on = HOUSE_DEFAULT_OPEN != 0;
bool house_blocked = false;             // closed by a fault, not by a command
uint64_t house_exercise_until = 0;
uint32_t day = 0;
uint32_t second_of_day = 0;
bool have_clock = false;
bool clock_anchored = false;
bool day_initialised = false;
uint32_t barn_last_run_day = 0;
uint32_t house_last_run_day = 0;
WaterAction action = WaterAction::Run;
char reason[20] = "";
bool leak_reported = false;
bool tank_high_prev = false;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.add("house", house_user_on ? 1 : 0);
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
        std::snprintf(tag, sizeof(tag), "%s", "water-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    prog.period_s = BARN_PERIOD_S;
    prog.run_s = BARN_RUN_S;
    prog.phase_s = 0;
    prog.enabled = true;
    if (nvGetI32("period", v) && v >= 60 && v <= static_cast<int32_t>(DAY_SECONDS)) prog.period_s = static_cast<uint32_t>(v);
    if (nvGetI32("run_s", v) && v >= 5 && v <= 3600) prog.run_s = static_cast<uint32_t>(v);
    if (nvGetI32("phase", v) && v >= 0 && v < static_cast<int32_t>(DAY_SECONDS)) prog.phase_s = static_cast<uint32_t>(v);
    if (nvGetI32("barn_en", v)) prog.enabled = v != 0;
    if (nvGetI32("house_on", v)) house_user_on = v != 0;

    if (nvGetF32("dry_bar", f) && f > 0.0f && f < 5.0f) plant_limits.dry_run_bar = f;
    if (nvGetF32("over_bar", f) && f > 1.0f && f < 12.0f) plant_limits.over_pressure_bar = f;
    if (nvGetF32("leak_lmin", f) && f > 0.01f && f < 5.0f) plant_limits.leak_flow_lmin = f;
    if (nvGetF32("no_flow", f) && f > 1.0f && f < 60.0f) valve_limits.no_flow_s = f;
    if (nvGetF32("max_open", f) && f > 10.0f && f < 7200.0f) valve_limits.max_open_s = f;
}

void clockUpdate() {
    uint16_t y = 0;
    uint8_t mo = 0, dm = 0;
    uint32_t sod = 0;
    have_clock = clockNow(sod, y, mo, dm);
    if (!have_clock) {
        clock_anchored = false;
        return;
    }
    second_of_day = sod;
    // A day number built from the calendar, not from millis(): a reboot at 03:00
    // must not re-run the cycle that already happened at 02:00.
    const uint32_t numbered = static_cast<uint32_t>(y) * 372u +
                              static_cast<uint32_t>(mo) * 31u + dm;
    if (!day_initialised || numbered != day) {
        day = numbered;
        day_initialised = true;
        clock_anchored = false;
        if (!barn_last_run_day) barn_last_run_day = day;
        if (!house_last_run_day) house_last_run_day = day;
    }
    if (!clock_anchored) {
        programmeAnchor(prog, pstate, day, second_of_day);
        clock_anchored = true;
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

void publish() {
    const SenseState& s = senseGet();
    const Outputs& o = valvesGet();
    WaterReport r{};
    r.barn_open = o.barn_open;
    r.house_open = o.house_open;
    r.pump_on = o.pump_on;
    r.barn_flow = s.barn.flow_lmin;
    r.house_flow = s.house.flow_lmin;
    r.pressure_bar = s.pressure_bar;
    r.tank_pct = s.tank_pct;
    r.next_s = have_clock ? programmeCountdown(prog, pstate, day, second_of_day) : 0;
    r.barn_cycle_l = s.barn.litres_cycle;
    r.house_cycle_l = s.house.litres_cycle;
    r.barn_total_l = s.barn.litres_total;
    r.house_total_l = s.house.litres_total;
    r.action = action;
    r.reason = reason;
    r.runs = pstate.runs;
    r.skipped = pstate.skipped;
    r.faults = o.barn_faults + o.house_faults;
    r.clock_valid = have_clock;
    r.sensor_fault = s.sensor_fault;
    telemetryPublish(r);
}


// What this board's programme actually is, as one self-describing string: a settings
// row that shows the page's own number is not a readout of the valve controller.
void publishLimits() {
    char s[32];
    std::snprintf(s, sizeof(s), "per%u/run%u/en%u",
                  static_cast<unsigned>(prog.period_s), static_cast<unsigned>(prog.run_s),
                  static_cast<unsigned>(prog.enabled ? 1u : 0u));
    telemetrySetLimits(s);
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
    if (std::strcmp(verb, "house") == 0) {
        house_user_on = value[0] == '1' || value[0] == 'o';       // 1 or "on"
        if (house_user_on) house_blocked = false;
        nvSetI32("house_on", house_user_on ? 1 : 0);
    } else if (std::strcmp(verb, "barn") == 0 && value[0] == 'n') {
        // Run once now, without moving the schedule the programme keeps.
        if (have_clock) {
            pstate.running = false;
            pstate.next_due = programmeClock(day, second_of_day);
        }
    } else if (std::strcmp(verb, "stop") == 0) {
        prog.enabled = false;
        house_user_on = false;
        nvSetI32("barn_en", 0);
        nvSetI32("house_on", 0);
    } else if (std::strcmp(verb, "resume") == 0) {
        prog.enabled = true;
        house_user_on = true;
        house_blocked = false;
        valvesClearFault(WaterLine::Barn);
        valvesClearFault(WaterLine::House);
        clock_anchored = false;
        nvSetI32("barn_en", 1);
        nvSetI32("house_on", 1);
    } else if (std::strcmp(verb, "period") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 60 && v <= static_cast<int32_t>(DAY_SECONDS)) {
            prog.period_s = static_cast<uint32_t>(v);
            nvSetI32("period", static_cast<int32_t>(v));
            clock_anchored = false;
        } else {
            taken = false;
        }
    } else if (std::strcmp(verb, "run") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 5 && v <= 3600) {
            prog.run_s = static_cast<uint32_t>(v);
            nvSetI32("run_s", static_cast<int32_t>(v));
        } else {
            taken = false;
        }
    } else {
        taken = false;
    }
    // A command that was not recognised is reported, not silently dropped: the
    // alternative is an operator believing the board has ignored them.
    telemetryNoteAck(verb, value, taken);
    publishLimits();
    telemetryEvent(taken ? "cmd" : "cmd-unknown");
}

void controlTick(uint32_t dt_ms) {
    clockUpdate();
    senseSample(dt_ms);
    const SenseState& s = senseGet();
    // A copy, not a reference: valvesTick() writes through the accessor, and a
    // reference would make every "did it just close" test below read the new
    // value and never see the edge.
    const Outputs prev = valvesGet();

    PlantInput pin{};
    pin.pressure_bar = s.pressure_bar;
    pin.tank_pct = s.tank_pct;
    pin.tank_high = s.tank_high;
    pin.leak = s.leak;
    pin.barn_flow = s.barn.flow_lmin;
    pin.house_flow = s.house.flow_lmin;
    pin.barn_open = prev.barn_open;
    pin.house_open = prev.house_open;
    pin.pump_on = prev.pump_on;
    pin.valve_barn_fault = prev.barn_state == ValveState::Fault;
    pin.valve_house_fault = prev.house_state == ValveState::Fault;
    pin.clock_valid = have_clock;
    const PlantDecision d = plantEvaluate(plant_limits, plant_state, pin, dt_ms / 1000.0f);
    action = d.action;
    std::snprintf(reason, sizeof(reason), "%s", d.reason);

    // --- the barn programme ---------------------------------------------------
    bool barn_want = false;
    if (have_clock) {
        const bool may_start = d.allow_dispense && d.action != WaterAction::CloseAll;
        uint32_t countdown = 0;
        const Dispense disp = programmeTick(prog, pstate, day, second_of_day, may_start, countdown);
        barn_want = (disp == Dispense::Start || disp == Dispense::Running);
        if (disp == Dispense::Start) {
            barn_last_run_day = day;
            telemetryEvent("cycle-start");
        }
    }

    // --- the house line: an operator preference, vetoed by the plant ----------
    bool house_want = house_user_on && !house_blocked;
    if (d.action == WaterAction::CloseAll || d.action == WaterAction::CloseHouse) {
        house_want = false;
        if (prev.house_open && d.action == WaterAction::CloseHouse) house_blocked = true;
    }
    if (prev.house_open) house_last_run_day = day;

    // A branch nobody has drawn through in a week is a branch that grows biofilm
    // and, in a frost, freezes. Ten seconds a week, in the first hour of the day.
    if (have_clock && house_user_on && !prev.house_open && house_exercise_until == 0 &&
        programmeNeedsExercise(day - house_last_run_day, STAGNATION_DAYS, second_of_day, 3600u)) {
        house_exercise_until = programmeClock(day, second_of_day) + STAGNATION_RUN_S;
        pstate.exercise++;
        telemetryEvent("house-exercise");
    }
    if (house_exercise_until) {
        const uint64_t t = programmeClock(day, second_of_day);
        if (t < house_exercise_until) house_want = true;
        else house_exercise_until = 0;
    }

    if (d.action == WaterAction::CloseAll || d.action == WaterAction::CloseBarn ||
        d.action == WaterAction::Hold) {
        barn_want = false;
    }

    valvesDemand(WaterLine::Barn, barn_want);
    valvesDemand(WaterLine::House, house_want);
    valvesPumpAllowed(d.pump_interlocked);
    valvesPumpDemand(d.want_pump);
    valvesTick(dt_ms, s);

    // --- edges worth recording -------------------------------------------------
    const Outputs& now = valvesGet();
    if (prev.barn_open && !now.barn_open) {
        telemetryEventLitres("cycle-end", true, senseCycleLitres(true));
    }
    if (prev.house_open && !now.house_open) {
        telemetryEventLitres("cycle-end", false, senseCycleLitres(false));
    }
    if (prev.barn_state != ValveState::Fault && now.barn_state == ValveState::Fault) {
        telemetryEvent("barn-stuck");
    }
    if (prev.house_state != ValveState::Fault && now.house_state == ValveState::Fault) {
        telemetryEvent("house-stuck");
        house_blocked = true;
    }
    if (!tank_high_prev && s.tank_high) telemetryEvent("tank-full");
    tank_high_prev = s.tank_high;
    if (!leak_reported && s.leak) {
        leak_reported = true;
        telemetryEvent("leak");
    } else if (leak_reported && !s.leak) {
        leak_reported = false;
    }
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;                 // a stalled loop must not skip a cycle

    if (sched.due(job_control, dt)) {
        // The job's own elapsed time, not the loop's: the pulse-rate window and
        // the valve timers are in seconds, and feeding them a fraction of the
        // time that really passed makes the meter read high.
        controlTick(sched.elapsed(job_control));
        feedWatchdog();
        setStatusLed(valvesGet().barn_open || valvesGet().house_open);
    }
    if (sched.due(job_telem, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_persist, dt)) sensePersist();
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
    // Establish the loop's own state too. A reboot is not a cold process image:
    // after a brownout the day latch and the fault latches still hold whatever the
    // last cycle left, and a board that thinks it already reported a leak stays
    // silent about the next one.
    day_initialised = false;
    clock_anchored = false;
    leak_reported = false;
    tank_high_prev = false;
    house_exercise_until = 0;
    house_blocked = false;
    barn_last_run_day = 0;
    house_last_run_day = 0;
    action = WaterAction::Run;
    reason[0] = '\0';
    sched.clear();
    valvesInit();
    senseInit();
    telemetryInit();
    paramsLoad();
    publishLimits();
    valvesSetLimits(valve_limits);
    senseLoad();
    programmeInit(pstate);
    plantReset(plant_state);
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_control = sched.add("control", 50);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_persist = sched.add("persist", 60000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the control loop has to be
    // created here: without it the board prints its banner and then stops.
    xTaskCreatePinnedToCore(controlTask, "water", STACK_CONTROL, nullptr, PRIO_CONTROL,
                            nullptr, 1);
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
