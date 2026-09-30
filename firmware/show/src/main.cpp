// Entry point: parameters, the job table, and the task layout for one aircraft of
// the show.
//
// Shape of the concurrency, and why:
//  - one cooperative task at PRIO_SHOW runs the programme, the two serial drains
//    and the uplink. They share the aircraft's one snapshot of itself, and running
//    them in a known order is a smaller correctness risk on an airframe than a
//    mutex around it.
//  - the safety job runs every iteration as well as on its own rate: the external
//    watchdog wants an edge at least ten times a second, and the SAFE switch is
//    the one input that must never be sampled late.
//  - the position target is the only thing on this board with a deadline. The
//    console, the broker and the strip all have to be able to wait for it, which
//    is why the strip is latched inside the show job and the log is not written
//    there at all.
//  - the Arduino loop task is deleted rather than used, so the show loop gets a
//    real priority instead of the core's default.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "mavlink.h"
#include "plan.h"
#include "rtk.h"
#include "scheduler.h"
#include "show.h"
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
int job_safety = -1, job_show = -1, job_telem = -1, job_uplink = -1, job_health = -1;
int job_console = -1;
MavParser fc_parser;
uint32_t last_loop_ms = 0;
bool wifi_down = false;
uint32_t wifi_down_ms = 0;
bool safe_last = false;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char craft_tag[16] = "show-0";
uint16_t mqtt_port = MQTT_PORT;

void paramsLoad() {
    int32_t v = 0;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    // The station number is this aircraft's identity in the programme. It is a
    // parameter and not a pin strapping because the fleet is numbered by the
    // ground station, which has to be able to tell an airframe to change its
    // number after a swap on the pad.
    int32_t station = 0;
    if (nvGetI32("station", station)) showSetStation(static_cast<int>(station));

    // The surveyed launch point: the origin every baked path is measured from.
    int32_t lat = 0, lon = 0, alt = 0;
    if (nvGetI32("lat_e7", lat) && nvGetI32("lon_e7", lon)) {
        if (!nvGetI32("alt_mm", alt)) alt = 0;
        rtkSetOrigin(lat, lon, alt);
    }

    if (!nvGetStr("tag", craft_tag, sizeof(craft_tag)) || !craft_tag[0]) {
        std::snprintf(craft_tag, sizeof(craft_tag), "show-%d", showStation());
    }
}

void banner() {
    char line[112];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", craft_tag);
    w.add("id", showStation());
    w.add("pps", 1);
    w.endLine();
    consoleWrite(line, w.size());
}

// An event line, on the same source token as the boot banner and never on SHOW,
// so the dashboard cannot mistake an occurrence for a state: a frame that is
// parsed as telemetry would report the upload's position for the next five frames.
void event(const char* what, const char* detail) {
    char line[128];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", what);
    if (detail && *detail) w.add("why", detail);
    w.add("id", showStation());
    w.endLine();
    consoleWrite(line, w.size());
}

void health() {
    UplinkStats u{};
    telemetryStats(u);
    const ShowStatus& s = showStatus();
    char line[128];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "health");
    w.add("over", static_cast<int>(sched.totalOverruns()));
    w.add("runs", static_cast<int>(sched.totalRuns()));
    w.add("rxcrc", static_cast<int>(fc_parser.crcErrors()));
    w.add("rxdrop", static_cast<int>(fc_parser.dropped()));
    w.add("pub", static_cast<int>(u.published));
    w.add("mqtt", u.broker_connected ? 1 : 0);
    w.add("failed", static_cast<int>(u.mqtt_failed));
    w.add("set", static_cast<int>(s.setpoints));
    w.add("src", static_cast<int>(s.source));
    w.add("hacc", static_cast<int>(s.hacc_cm));
    w.add("ph", showPhaseName(s.phase));
    w.endLine();
    consoleWrite(line, w.size());
}

// The RTK receiver's UART. Bounded per call for the same reason the flight
// controller's is: a receiver that has been left talking at 5 Hz must not be able
// to starve the job that puts position targets on the wire.
void drainRtk() {
    uint8_t buf[128];
    for (int batch = 0; batch < 4; ++batch) {
        const int n = rtkRead(buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) rtkFeed(buf[i]);
    }
    rtkUpdate(halMillis());
}

void drainFc() {
    uint8_t buf[128];
    for (int batch = 0; batch < 4; ++batch) {
        const int n = fcRead(buf, sizeof(buf));
        if (n <= 0) break;
        MavMessage m{};
        for (int i = 0; i < n; ++i) {
            if (fc_parser.push(buf[i], m)) showOnMavlink(m);
        }
    }
}

// The ground station's grammar: a verb, optionally "verb=value". Anything else is
// ignored rather than guessed at, because a mis-parsed upload is an aircraft that
// flies a shape nobody drew.
//
// There is no "arm" verb here. The permission that lets the motors turn on this
// airframe is a pin a person shorts by hand, and a packet that could close it
// would make that pin decorative.
void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[16];
    size_t i = 0;
    while (i < len && i < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[i] = payload[i];
        ++i;
    }
    verb[i] = '\0';
    const char* value = nullptr;
    size_t vlen = 0;
    if (i < len && payload[i] == '=') {
        value = payload + i + 1;
        vlen = len - (i + 1);
    }

    if (std::strcmp(verb, "show") == 0 && value) {
        char why[24];
        const bool ok = showUpload(value, vlen, why, sizeof(why));
        event(ok ? "upload" : "upload-refused", ok ? "" : why);
    } else if (std::strcmp(verb, "station") == 0 && value) {
        char num[8];
        size_t n = 0;
        while (n < vlen && n < sizeof(num) - 1) { num[n] = value[n]; ++n; }
        num[n] = '\0';
        showSetStation(static_cast<int>(std::strtol(num, nullptr, 10)));
        event("station", "");
    } else if (std::strcmp(verb, "origin") == 0 && value) {
        char buf[64];
        size_t n = 0;
        while (n < vlen && n < sizeof(buf) - 1) { buf[n] = value[n]; ++n; }
        buf[n] = '\0';
        char* end = nullptr;
        const double lat = std::strtod(buf, &end);
        if (end != buf && *end == ',') {
            const char* p = end + 1;
            const double lon = std::strtod(p, &end);
            double alt = 0.0;
            if (end != p && *end == ',') alt = std::strtod(end + 1, nullptr);
            rtkSetOrigin(static_cast<int32_t>(lat * 1e7), static_cast<int32_t>(lon * 1e7),
                         static_cast<int32_t>(alt * 1000.0));
            nvSetI32("lat_e7", static_cast<int32_t>(lat * 1e7));
            nvSetI32("lon_e7", static_cast<int32_t>(lon * 1e7));
            nvSetI32("alt_mm", static_cast<int32_t>(alt * 1000.0));
            event("origin", "");
        } else {
            event("origin-refused", "WANT LAT,LON,ALT");
        }
    } else if (std::strcmp(verb, "start") == 0) {
        showRequestLaunch();
        event("launch", showReason());
    } else if (std::strcmp(verb, "land") == 0) {
        showLand();
        event("land", "");
    } else if (std::strcmp(verb, "led") == 0 && value) {
        char num[8];
        size_t n = 0;
        while (n < vlen && n < sizeof(num) - 1) { num[n] = value[n]; ++n; }
        num[n] = '\0';
        const long pct = std::strtol(num, nullptr, 10);
        if (pct >= 20 && pct <= 100) {
            nvSetI32("led_pct", static_cast<int32_t>(pct));
            event("led", "");
        } else {
            event("led-refused", "WANT 20..100");
        }
    }
    // Any command at all is evidence the ground station is awake, which is what
    // the link timeout counts on. A heartbeat is a command with no arguments.
    showNoteGcsContact();
}

// The buzzer has two jobs and they sound different on purpose: a chirp on the pad
// for a pack that will not fly, and a steady tone in the air for an aircraft that
// is coming home by itself. A person on the ground cannot see which of twenty-four
// airframes has a low pack, and they can always hear one.
void sounds() {
    const ShowStatus& s = showStatus();
    const bool airborne = s.phase == SHOW_CLIMB || s.phase == SHOW_MOVE || s.phase == SHOW_HOLD
                       || s.phase == SHOW_HOVER
                       || s.phase == SHOW_RETURN || s.phase == SHOW_AT_SLOT;
    const bool lost = !s.link_up || s.source == POS_NONE;
    bool on = false;
    if (airborne && lost) on = true;
    else if (!airborne && (s.batt_pct < static_cast<float>(SHOW_BATT_WARN_PCT)
                           || (s.phase == SHOW_REFUSED && lost))) {
        on = ((halMillis() / 200u) & 1u) != 0u;
    }
    setBuzzer(on);
}

void supervision() {
    // A link that has been down for half a minute is not a fade: ask the stack to
    // re-associate instead of waiting for its own backoff.
    const bool up = wifiUp();
    if (up) {
        wifi_down = false;
        return;
    }
    if (!wifi_down) {
        wifi_down = true;
        wifi_down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (wifi_down_ms + 30000u)) >= 0) {
        wifi_down_ms = halMillis();
        wifiReconnect();
    }
}

// The USB console: how a programme gets into an aircraft that is not on the radio
// yet. plan.h promises the upload "has to survive a technician typing it by hand",
// and the simulator's serial monitor delivers exactly that -- bytes into UART0 RX.
// The line is assembled here rather than in the HAL so the aircraft's UART and the
// sandbox's queue are read by one parser.
constexpr size_t CONSOLE_LINE_MAX = SHOW_PLAN_MAX_TEXT + 32;
char console_line[CONSOLE_LINE_MAX];
size_t console_len = 0;
bool console_overflow = false;

void consoleJob() {
    char buf[64];
    const int n = consoleRead(buf, sizeof(buf));
    for (int i = 0; i < n; ++i) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            if (console_len && !console_overflow) onGroundCommand(console_line, console_len);
            console_len = 0;
            console_overflow = false;
            continue;
        }
        if (console_len < sizeof(console_line) - 1) {
            console_line[console_len++] = buf[i];
        } else {
            // Longer than anything this build takes: keep reading to the end of the
            // line so the next one starts clean, but never run half of this one.
            console_overflow = true;
        }
    }
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;              // a stalled loop must not teleport

    drainRtk();
    drainFc();

    if (sched.due(job_safety, dt)) {
        const bool closed = safeSwitchClosed();
        if (closed != safe_last) {
            safe_last = closed;
            event(closed ? "safe-released" : "safe-asserted", "SAFE SWITCH");
        }
        feedWatchdog();
        sounds();
        // The bench indicator: one flash per second while it waits, two while it
        // flies, none while it is safe. None of that is on the show strip: the
        // crew's lamp must stay readable with the programme's pixels dark.
        const ShowStatus& s = showStatus();
        const uint32_t period = (s.phase == SHOW_MOVE || s.phase == SHOW_HOLD) ? 250u : 1000u;
        setStatusLed(!s.safe_closed ? false : ((now / period) & 1u) != 0u);
    }
    if (sched.due(job_show, dt)) {
        showTick(sched.elapsed(job_show));
    }
    if (sched.due(job_telem, dt)) {
        telemetryPublish(showStatus());
    }
    if (sched.due(job_uplink, dt)) telemetryService();
    if (sched.due(job_health, dt)) health();
    if (sched.due(job_console, dt)) consoleJob();
    supervision();
}

#if !defined(RANCH_SIM)
void loopTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(SHOW_LOOP_MS);
    }
}
#endif

}  // namespace

// The host sandbox drives the command channel the way the ground station does, so
// "upload, then start" is exercised as traffic rather than as a timer nobody had to
// press. Outside the anonymous namespace because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }
#endif

void appSetup() {
    halInit();
    last_loop_ms = halMillis();
    wifi_down = false;
    wifi_down_ms = 0;
    fc_parser = MavParser{};
    sched.clear();

    rtkInit();
    showInit();
    telemetryInit();
    paramsLoad();

    // Ask the receiver for the one message this aircraft flies on, at the rate the
    // path was baked at. If it never answers, the position stays absent and the
    // programme never starts, which is the only safe reading of a silent UART.
    uint8_t cfg[32];
    const size_t n1 = rtkCfgNavPvt(cfg, sizeof(cfg), 5);
    if (n1) rtkWrite(cfg, n1);
    const size_t n2 = rtkCfgNavRate(cfg, sizeof(cfg), 200, 1);
    if (n2) rtkWrite(cfg, n2);

    safe_last = safeSwitchIsClosed();
    banner();
    telemetrySetCommandHandler(onGroundCommand);

    job_safety = sched.add("safety", 20);
    job_show = sched.add("show", 1000 / SHOW_SETPOINT_HZ);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_uplink = sched.add("uplink", 100);
    job_health = sched.add("health", 5000);
    job_console = sched.add("console", 20);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, craft_tag);

#if !defined(RANCH_SIM)
    xTaskCreatePinnedToCore(loopTask, "ranch", STACK_SHOW, nullptr, PRIO_SHOW, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
    halSimPump();
    runJobs();
    halDelayMs(SHOW_LOOP_MS);
#endif
}

}  // namespace ranch

#if !defined(RANCH_SIM)
void setup() { ranch::appSetup(); }
void loop() { vTaskDelete(NULL); }
#endif
