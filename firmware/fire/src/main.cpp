// Fire panel entry point: loop bands, the alarm decision, the dial-out, and the two
// buttons on the door.
//
// One cooperative task scans the loops, decides, and drives the relays in that fixed
// order. The split into a radio task and a panel task is the obvious design and the
// wrong one: the decision needs a set of loop states that were all true at the same
// instant, and a scan that can land in the middle of it describes a building that
// never existed. The six ADC reads cost about a millisecond, so a 20 ms tick carries
// the scan, the decision every 50 ms, and the dial every second without anybody
// needing a mutex.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "fire_logic.h"
#include "frame_codec.h"
#include "hal.h"
#include "panel.h"
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

Scheduler sched;
int job_scan = -1, job_control = -1, job_dial = -1, job_persist = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

FireLimits lim = FIRE_DEFAULTS;
FireState fstate{};
LoopBands bands = LOOP_DEFAULT_BANDS;
FireDecision last_d{};

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "fire-1";
uint16_t mqtt_port = MQTT_PORT;

bool have_clock = false;
uint32_t second_of_day = 0;
FireLevel dial_level = FireLevel::Normal;
char prev_reason[20] = "";
bool silence_edge = false;
bool test_edge = false;
uint8_t sil_hold = 0;
uint8_t test_hold = 0;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("arm", keyArmed() ? 1 : 0);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.endLine();
    consoleWrite(line, w.size());
}

void paramsLoad() {
    int32_t v = 0;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", tag, sizeof(tag)) || !tag[0]) {
        std::snprintf(tag, sizeof(tag), "%s", "fire-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    if (nvGetI32("confirm", v) && v >= 5 && v <= 300) lim.confirm_s = static_cast<uint16_t>(v);
    if (nvGetI32("silence", v) && v >= 10 && v <= 3600) lim.silence_s = static_cast<uint16_t>(v);
    if (nvGetI32("silmax", v) && v >= 1 && v <= 10) lim.silence_max = static_cast<uint8_t>(v);
    if (nvGetI32("testper", v) && v >= 60 && v <= 86400) lim.test_period_s = static_cast<uint16_t>(v);
    if (nvGetI32("testms", v) && v >= 50 && v <= 2000) lim.test_ms = static_cast<uint16_t>(v);
    if (nvGetI32("proofms", v) && v >= 20 && v <= 1000) lim.siren_proof_ms = static_cast<uint16_t>(v);
    if (nvGetI32("faultz", v) && v >= 1 && v <= FIRE_ZONES) {
        lim.fault_zones_max = static_cast<uint8_t>(v);
    }

    // The band edges are electrical facts about *this* installation: a site rewired
    // with a 10k pull-up reads a different mid-rail, and a panel that kept the old
    // thresholds would call every healthy loop dirty.
    if (nvGetI32("b_shi", v) && v > 0 && v < 800) bands.short_hi_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_alo", v) && v > 100 && v < 1500) bands.alarm_lo_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_ahi", v) && v > 200 && v < 2000) bands.alarm_hi_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_nlo", v) && v > 500 && v < 2800) bands.normal_lo_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_nhi", v) && v > 800 && v < 3200) bands.normal_hi_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_olo", v) && v > 1500 && v <= 3300) bands.open_lo_mv = static_cast<uint16_t>(v);
    // Bands typed over each other would make every reading ambiguous, so the order is
    // enforced rather than trusted.
    if (bands.alarm_hi_mv <= bands.alarm_lo_mv) bands.alarm_hi_mv = bands.alarm_lo_mv + 200;
    if (bands.normal_lo_mv <= bands.alarm_hi_mv) bands.normal_lo_mv = bands.alarm_hi_mv + 400;
    if (bands.normal_hi_mv <= bands.normal_lo_mv) bands.normal_hi_mv = bands.normal_lo_mv + 400;
    if (bands.open_lo_mv <= bands.normal_hi_mv) bands.open_lo_mv = bands.normal_hi_mv + 600;
    if (bands.open_lo_mv > LOOP_RAIL_MV) bands.open_lo_mv = LOOP_RAIL_MV;
}

void paramsPersist() {
    nvSetI32("confirm", lim.confirm_s);
    nvSetI32("silence", lim.silence_s);
    nvSetI32("silmax", lim.silence_max);
    nvSetI32("testper", lim.test_period_s);
    nvSetI32("faultz", lim.fault_zones_max);
    nvSetI32("b_shi", bands.short_hi_mv);
    nvSetI32("b_alo", bands.alarm_lo_mv);
    nvSetI32("b_ahi", bands.alarm_hi_mv);
    nvSetI32("b_nlo", bands.normal_lo_mv);
    nvSetI32("b_nhi", bands.normal_hi_mv);
    nvSetI32("b_olo", bands.open_lo_mv);
}

void clockUpdate() {
    uint16_t y = 0;
    uint8_t mo = 0, dm = 0;
    have_clock = clockNow(second_of_day, y, mo, dm);
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
    FireReport r{};
    r.loops = panelLoops();
    r.d = last_d;
    r.stats = PanelStats{};
    panelStats(r.stats);
    r.uptime_s = (halMillis() - boot_ms) / 1000u;
    r.second_of_day = second_of_day;
    r.clock_valid = have_clock;
    r.siren_failed = fstate.siren_failed;
    r.silence_left_s = fstate.silence_left_s;
    r.silences = fstate.silences;
    telemetryPublish(r);
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[20], value[32];
    size_t i = 0;
    while (i < len && i < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[i] = payload[i];
        ++i;
    }
    verb[i] = '\0';
    size_t n = 0;
    if (i < len && payload[i] == '=') {
        ++i;
        while (i < len && n < sizeof(value) - 1) value[n++] = payload[i++];
        value[n] = '\0';
    } else {
        value[0] = '\0';
    }

    bool taken = true;
    if (std::strcmp(verb, "reset") == 0) {
        // A remote reset is a reset, not an override: the same gate the door button
        // goes through, so an MQTT credential cannot do what a key cannot.
        fstate.reset_edge = true;
    } else if (std::strcmp(verb, "silence") == 0) {
        silence_edge = true;
    } else if (std::strcmp(verb, "test") == 0) {
        test_edge = true;
    } else if (std::strcmp(verb, "confirm") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 5 && v <= 300) {
            lim.confirm_s = static_cast<uint16_t>(v);
            paramsPersist();
        } else {
            taken = false;
        }
    } else {
        taken = false;
    }
    telemetryEvent(taken ? "cmd" : "cmd-unknown", verb);
}

// A held button is one press. The three-sample agreement is the same rule the loops
// use, for the same reason: a contact bouncing on its way to closing should not spend
// the panel's three allowed silences in one press.
void buttonEdges() {
    const bool sil = panelSilenceHeld();
    const bool test = panelTestHeld();
    if (sil) {
        if (++sil_hold >= 3) {
            silence_edge = true;
            sil_hold = 0;
        }
    } else {
        sil_hold = 0;
    }
    if (test) {
        if (++test_hold >= 3) {
            test_edge = true;
            test_hold = 0;
        }
    } else {
        test_hold = 0;
    }
}

void controlTick(uint32_t dt_ms) {
    clockUpdate();

    const LoopRead& L = panelLoops();
    FireInput in{};
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) in.zone[i] = L.zone[i];
    in.manual_call = L.manual_call;
    in.flow = L.flow_active;
    in.key_armed = keyArmed();
    in.siren_feedback = sirenFeedbackClosed();
    in.silence_edge = silence_edge;
    in.test_edge = test_edge;
    in.clock_valid = have_clock;
    silence_edge = false;
    test_edge = false;

    const FireDecision d = fireEvaluate(lim, fstate, in, dt_ms / 1000.0f);
    last_d = d;
    panelApply(d, dt_ms);

    if (std::strcmp(d.reason, "RESET") == 0) telemetryEvent("reset", "");
    else if (std::strcmp(d.reason, "RESET BLOCKED") == 0) telemetryEvent("reset-refused", "");
    if (std::strcmp(d.reason, "SILENCE REFUSED") == 0) telemetryEvent("silence-refused", "");

    if (d.level != dial_level || std::strcmp(d.reason, prev_reason) != 0) {
        dial_level = d.level;
        std::snprintf(prev_reason, sizeof(prev_reason), "%s", d.reason);
        // An alarm is announced when it starts and then kept asserted, because the
        // message that matters is the one that is still true when the radio comes up.
        telemetryDial(d.level != FireLevel::Normal, d.reason);
    }
    setStatusLed(d.level == FireLevel::Emergency || d.level == FireLevel::Alarm);
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;                 // a stalled loop must not skip a cycle

    if (sched.due(job_scan, dt)) {
        panelSample(bands);
        buttonEdges();
    }
    if (sched.due(job_control, dt)) controlTick(sched.elapsed(job_control));
    if (sched.due(job_dial, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_persist, dt)) paramsPersist();
    supervision();
}

#if !defined(RANCH_SIM)
void panelTask(void*) {
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
// reset, a silence and a bell test are exercised rather than assumed. Outside the
// anonymous namespace because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }
void simLimits(uint16_t confirm_s, uint16_t silence_s, uint16_t test_period_s) {
    lim.confirm_s = confirm_s;
    lim.silence_s = silence_s;
    lim.test_period_s = test_period_s;
    fstate.next_test_s = test_period_s;
}
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    sched.clear();
    panelInit();
    telemetryInit();
    paramsLoad();
    fireReset(fstate);
    last_d = FireDecision{};
    last_d.level = FireLevel::Normal;
    dial_level = FireLevel::Normal;
    prev_reason[0] = '\0';
    silence_edge = false;
    test_edge = false;
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_scan = sched.add("scan", SAMPLE_PERIOD_MS);
    job_control = sched.add("control", CONTROL_PERIOD_MS);
    job_dial = sched.add("dial", 1000 / DIAL_HZ);
    job_persist = sched.add("persist", 300000);

    // The first scan has to have happened before the first decision, or the panel
    // opens with every loop Unknown and announces a supervision fault it never
    // measured.
    panelSample(bands);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the panel loop has to be created
    // here: without it the board prints its banner and then stops.
    xTaskCreatePinnedToCore(panelTask, "panel", STACK_PANEL, nullptr, PRIO_PANEL, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
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
