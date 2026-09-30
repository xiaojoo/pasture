// Entry point: parameters, the job table, and the task layout.
//
// Shape of the concurrency, and why:
//  - one cooperative scheduler task at PRIO_MISSION runs the mission, the
//    uplink and the diagnostics. These share the airframe snapshot, and running
//    them in a known order is a smaller correctness risk on an aircraft than a
//    priority ceiling or a mutex around it.
//  - MAVLink input and the safety supervisor run every iteration, not on a
//    schedule: bytes arrive continuously and the debounce timers in the
//    failsafe table are in real seconds.
//  - the camera is the only thing on its own task, at the lowest priority,
//    because it is the only thing allowed to block for a long time.
//  - the Arduino loop task is deleted rather than used, so the mission loop gets
//    a real priority instead of the core's default.
//  - the external watchdog is fed by the safety job only. If the scheduler
//    stops, the watchdog stops, and the aircraft loses control inputs instead
//    of continuing on a frozen mission.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "mavlink.h"
#include "mission.h"
#include "safety.h"
#include "scheduler.h"
#include "telemetry.h"
#include "video.h"

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
int job_safety = -1, job_mission = -1, job_telem = -1, job_uplink = -1, job_health = -1;
MavParser parser;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;
bool launched = false;
bool link_down = false;
uint32_t link_down_ms = 0;
uint32_t saved_captures = 0;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char craft_tag[16] = "drone-1";
uint16_t mqtt_port = MQTT_PORT;
// Zero means "never launch on a clock". A multirotor that arms and takes off
// because a timer expired is not an autonomous aircraft, it is an unsupervised
// one: the launch has to be asked for (the GCS `takeoff`, or the technician
// closing the arm switch and then commanding it). The timer stays available as a
// bench test mode and is read from NVS as launch_ms when someone wants it.
uint32_t auto_launch_ms = 0;
bool launch_requested = false;
bool switch_last = false;

// The factory patrol: the ranch's inspection points in the order the walk is
// actually done, in metres from the pad and above ground level. Altitude
// becomes AMSL at launch, once the home position is latched.
struct RoutePoint { float north, east, agl, dwell_s; uint8_t camera; };

const RoutePoint kFactory[] = {
    {   0.0f,   0.0f, 18.0f, 0.0f, 0 },   // pad, climb out
    {  38.0f,  12.0f, 18.0f, 4.0f, 1 },   // main house
    {  70.0f, -34.0f, 20.0f, 5.0f, 1 },   // barn
    {  22.0f, -78.0f, 18.0f, 3.0f, 1 },   // feed store
    { -34.0f, -96.0f, 26.0f, 6.0f, 1 },   // water tower
    { -78.0f, -40.0f, 18.0f, 4.0f, 1 },   // pump house
    { -66.0f,  36.0f, 16.0f, 3.0f, 1 },   // cattle yard
    { -20.0f,  70.0f, 18.0f, 4.0f, 1 },   // machinery shed
    {  14.0f,  46.0f, 22.0f, 3.0f, 1 },   // switch room
};
constexpr uint8_t kFactoryCount = sizeof(kFactory) / sizeof(kFactory[0]);

Mission route{};

// A stored route is a semicolon list of comma fields: n,e,agl,dwell,camera.
// Parsed with strtod against the caller's buffer, so a corrupt parameter can
// neither allocate nor walk off the end of the waypoint array, and cannot
// produce a half-record the aircraft would fly somewhere unexpected.
bool routeParse(const char* text, Mission& m) {
    missionReset(m);
    const char* p = text;
    while (p && *p && m.count < MISSION_MAX_WAYPOINTS) {
        char* end = nullptr;
        double v[5] = {0, 0, 0, 0, 0};
        v[0] = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        int field = 1;
        while (field < 5 && *p == ',') {
            ++p;
            v[field] = std::strtod(p, &end);
            if (end == p) return false;        // ",," or a trailing comma
            p = end;
            ++field;
        }
        if (field < 5) return false;           // incomplete record
        Waypoint w{};
        w.north = static_cast<float>(v[0]);
        w.east = static_cast<float>(v[1]);
        w.alt = static_cast<float>(v[2]);       // above ground until launch
        w.dwell_s = static_cast<float>(v[3]);
        w.camera = static_cast<uint8_t>(v[4]);
        if (!missionAdd(m, w)) break;
        while (*p == ';') ++p;
    }
    return m.count > 0;
}

void routeLoadOrFactory() {
    char buf[512];
    if (nvGetStr("route", buf, sizeof(buf)) && routeParse(buf, route)) return;
    missionReset(route);
    for (uint8_t i = 0; i < kFactoryCount; ++i) {
        Waypoint w{};
        w.north = kFactory[i].north;
        w.east = kFactory[i].east;
        w.alt = kFactory[i].agl;
        w.dwell_s = kFactory[i].dwell_s;
        w.camera = kFactory[i].camera;
        if (!missionAdd(route, w)) break;
    }
}

void paramsLoad() {
    int32_t v = 0;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", craft_tag, sizeof(craft_tag)) || !craft_tag[0]) {
        std::snprintf(craft_tag, sizeof(craft_tag), "%s", "drone-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);
    if (nvGetI32("launch_ms", v) && v >= 0) auto_launch_ms = static_cast<uint32_t>(v);

    // The decision table is loadable: a fleet that flies without anyone
    // watching the dashboard needs the ground-link rule switched off, and that
    // belongs in a parameter rather than in a second binary.
    FailsafeConfig cfg = safetyFailsafeConfig();
    if (nvGetI32("unattended", v) && v) cfg.gcs_timeout_ms = 0x7FFFFFFFu / 8u;
    safetySetFailsafeConfig(cfg);
}

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", craft_tag);
    w.add("wp", route.count);
    w.endLine();
    consoleWrite(line, w.size());
}

// Diagnostic line, on its own source token so the dashboard cannot mistake it
// for a state frame: a health line that is parsed as telemetry would report
// mode=TRANSIT forever after the last real frame.
void health() {
    UplinkStats u{};
    telemetryStats(u);
    char line[128];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "health");
    w.add("over", static_cast<int>(sched.totalOverruns()));
    w.add("runs", static_cast<int>(sched.totalRuns()));
    w.add("rxcrc", static_cast<int>(parser.crcErrors()));
    w.add("rxdrop", static_cast<int>(parser.dropped()));
    w.add("pub", static_cast<int>(u.published));
    w.add("mqtt", u.broker_connected ? 1 : 0);
    w.add("failed", static_cast<int>(u.mqtt_failed));
    w.add("logged", static_cast<int>(u.logged));
    w.add("nologue", static_cast<int>(u.log_dropped));
    w.add("arm", missionArmName());
    w.endLine();
    consoleWrite(line, w.size());
}

void drainFc() {
    uint8_t buf[128];
    // Bounded per call: a flooded UART must not starve the safety supervisor,
    // which shares this task.
    for (int batch = 0; batch < 4; ++batch) {
        const int n = fcRead(buf, batch == 0 ? sizeof(buf) : 32);
        if (n <= 0) break;
        MavMessage m{};
        for (int i = 0; i < n; ++i) {
            if (!parser.push(buf[i], m)) continue;
            safetyOnMavlink(m);
            missionOnMavlink(m);
        }
    }
}

void savePhoto() {
    MissionStatus ms{};
    missionStatus(ms);
    if (ms.captures == saved_captures) return;
    saved_captures = ms.captures;
    if (!ms.captures) return;
    char path[32];
    std::snprintf(path, sizeof(path), "/photo/%06lu.jpg", static_cast<unsigned long>(ms.captures));
    videoSaveSnapshot(path);
}

void tryLaunch() {
    if (launched || route.count == 0) return;
    // Either somebody asked for this flight, or a bench deliberately configured the
    // timer. The two are kept apart so the log can say which one lifted it.
    const bool timed = auto_launch_ms > 0 &&
                       static_cast<int32_t>(halMillis() - (boot_ms + auto_launch_ms)) >= 0;
    if (!launch_requested && !timed) return;
    if (!safetyAirHasFix()) return;

    int32_t lat_e7 = 0, lon_e7 = 0;
    float home_alt = 0.0f;
    if (!safetyHomeGeodetic(lat_e7, lon_e7, home_alt)) return;

    // AGL to AMSL here, not in the planner: the route is authored on a map in
    // metres above the pad, and the FC is commanded in absolute altitude. The
    // copy keeps `route` in the units it was written in, so a relaunch after a
    // landing cannot add the home altitude twice.
    Mission m = route;
    for (uint8_t i = 0; i < m.count; ++i) m.items[i].alt += home_alt;
    m.arrival_m = RANCH_SUPERVISE_ARRIVAL_M;

    if (!missionUpload(m, MissionSource::Rtc)) return;
    // Upload only. The hand-over sequence in missionTick takes the aircraft
    // from AUTO to airborne one confirmed step at a time, and the planner's
    // phase follows what the FC reports instead of what we hoped for.
    launched = true;
}

void supervision() {
    // A link that has been down for half a minute is not a fade: ask the stack
    // to re-associate instead of waiting for its own backoff.
    const bool up = wifiUp();
    if (up) {
        link_down = false;
        return;
    }
    if (!link_down) {
        link_down = true;
        link_down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (link_down_ms + 30000u)) >= 0) {
        link_down_ms = halMillis();
        wifiReconnect();
    }
}

// "nmea"-free command grammar from the ground app: a verb, optionally
// "verb=value". Anything else is ignored rather than guessed at.
void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[16], value[64];
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
    missionOnCommand(verb, value);
    if (std::strcmp(verb, "safe") == 0) safetyForceSafe(value[0] == '1');
    // The two commands a person actually reaches for: take the safety off, then
    // send it up. `land` also withdraws a launch that has not happened yet, so a
    // cancelled sortie cannot lift off behind the operator's back.
    else if (std::strcmp(verb, "arm") == 0) safetyForceSafe(value[0] != '1');
    else if (std::strcmp(verb, "takeoff") == 0) launch_requested = true;
    else if (std::strcmp(verb, "land") == 0) launch_requested = false;
    safetyNoteGcsContact();
}

uint8_t ledPhase() {
    const uint32_t period = safetyIsSafe() ? 800u : 150u;
    return static_cast<uint8_t>((halMillis() / period) & 1u);
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;              // a stalled loop must not teleport

    drainFc();
    safetyUpdate(dt);

    if (sched.due(job_safety, dt)) {
        armSwitchClosed();
        const bool closed = armSwitchIsClosed();
        if (closed != switch_last) {
            switch_last = closed;
            safetyForceSafe(!closed);
        }
        safetyKick();
        setStatusLed(ledPhase());
    }
    if (sched.due(job_mission, dt)) {
        tryLaunch();
        missionTick(sched.elapsed(job_mission));
        savePhoto();
    }
    if (sched.due(job_telem, dt)) {
        MissionStatus ms{};
        missionStatus(ms);
        telemetryPublish(ms, safetyAir());
#if defined(RANCH_SIM)
        // In the simulation the console is the ground station and it just took
        // the frame, so the dashboard is by definition in contact.
        safetyNoteGcsContact();
#else
        UplinkStats u{};
        telemetryStats(u);
        if (u.broker_connected) safetyNoteGcsContact();
#endif
    }
    // The log drain does SD card I/O and the broker keepalive does socket I/O,
    // so neither runs on the mission job: a slow card must not delay a setpoint.
    if (sched.due(job_uplink, dt)) telemetryService();
    if (sched.due(job_health, dt)) health();
    supervision();
}

#if !defined(RANCH_SIM)
void loopTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(4);
    }
}
#endif

}  // namespace

// The host sandbox drives the command channel the way the ground station does, so
// "arm, then take off" is exercised as a command rather than as a timer nobody had
// to press. Outside the anonymous namespace because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    launched = false;
    launch_requested = false;
    saved_captures = 0;
    link_down = false;
    link_down_ms = 0;
    parser = MavParser{};
    sched.clear();

    armSwitchClosed();
    // Power-on state of the airframe's arm switch is the kill output's state:
    // a key in the ARM position releases the relay, an open key holds it.
    // Afterwards only a change of the switch moves it, so a command from the
    // ground is not stepped on every 20 ms by a pin read.
    switch_last = armSwitchIsClosed();
    safetyInit();
    safetyForceSafe(!switch_last);
    missionInit();
    telemetryInit();
    paramsLoad();
    routeLoadOrFactory();
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_safety = sched.add("safety", 20);
    job_mission = sched.add("mission", 50);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_uplink = sched.add("uplink", 100);
    job_health = sched.add("health", 5000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, craft_tag);
    if (videoInit()) videoSetStreaming(true);

#if !defined(RANCH_SIM)
    xTaskCreatePinnedToCore(loopTask, "ranch", STACK_MISSION, nullptr, PRIO_MISSION, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
    halSimPump();
    runJobs();
    halDelayMs(4);
#endif
}

}  // namespace ranch

#if !defined(RANCH_SIM)
void setup() { ranch::appSetup(); }
void loop() { vTaskDelete(NULL); }
#endif
