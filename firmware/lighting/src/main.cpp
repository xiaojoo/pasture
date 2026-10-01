// Lighting board entry point: clock, sun times, policy, outputs, uplink.
//
// The sun is recalculated once a day (and on any clock correction), not every
// tick: the times move by seconds per day, and recomputing them at 10 Hz would
// only add a way to get it wrong.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "astro.h"
#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "light_policy.h"
#include "lights.h"
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
int job_light = -1, job_telem = -1, job_burn = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

LightConfig cfg = LIGHT_DEFAULTS;
double latitude = RANCH_LATITUDE;
double longitude = RANCH_LONGITUDE;
double tz_hours = RANCH_UTC_OFFSET_H;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "light-1";
uint16_t mqtt_port = MQTT_PORT;

bool have_clock = false;
uint32_t minute_of_day = 0;
uint32_t sun_day_key = 0xFFFFFFFFu;
float civil_rise = -1.0f;
float civil_set = -1.0f;
float official_rise = -1.0f;
float official_set = -1.0f;

bool street_override = false;
bool house_override = false;
uint32_t override_left_s = 0;

LightMode last_mode = LightMode::Astro;
bool last_street = false;
bool last_house = false;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.endLine();
    consoleWrite(line, w.size());
}

void paramsLoad() {
    int32_t v = 0;
    float f = 0.0f;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", tag, sizeof(tag)) || !tag[0]) std::snprintf(tag, sizeof(tag), "%s", "light-1");
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    // A board that is moved to another barn must be told where it is; the sun
    // times are meaningless without that.
    if (nvGetF32("lat", f) && f > -66.0f && f < 66.0f) latitude = f;
    if (nvGetF32("lon", f) && f > -180.0f && f < 180.0f) longitude = f;
    if (nvGetF32("tz", f) && f > -12.0f && f < 14.0f) tz_hours = f;

    if (nvGetI32("ramp_min", v) && v >= 1 && v <= 120) cfg.dusk_ramp_s = static_cast<uint16_t>(v * 60);
    if (nvGetI32("econ_after", v) && v >= 30 && v <= 600) cfg.economy_after_min = static_cast<uint16_t>(v);
    if (nvGetF32("econ_duty", f) && f > 0.05f && f < 1.0f) cfg.economy_duty = f;
    if (nvGetI32("house_off", v) && v >= 30 && v <= 720) cfg.house_off_min = static_cast<uint16_t>(v);
    if (nvGetI32("override_min", v) && v >= 5 && v <= 720) cfg.override_s = static_cast<uint32_t>(v * 60);
    if (nvGetF32("lux_hold", f) && f > 5.0f && f < 5000.0f) cfg.daylight_lux_hold = f;

    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        char key[12];
        std::snprintf(key, sizeof(key), "h%u", static_cast<unsigned>(i));
        if (nvGetI32(key, v) && v >= 0) lightsSetBurnTenths(static_cast<LampCircuit>(i),
                                                            static_cast<uint32_t>(v));
    }
}

void sunRecalculate(uint16_t year, uint8_t month, uint8_t day) {
    const uint32_t key = static_cast<uint32_t>(year) * 400u + month * 32u + day;
    if (key == sun_day_key) return;
    sun_day_key = key;
    const SunTimes civil = sunTimes(year, month, day, latitude, longitude,
                                    SUN_CIVIL_ZENITH, tz_hours);
    const SunTimes official = sunTimes(year, month, day, latitude, longitude,
                                       SUN_OFFICIAL_ZENITH, tz_hours);
    civil_rise = civil.valid ? civil.sunrise_min : -1.0f;
    civil_set = civil.valid ? civil.sunset_min : -1.0f;
    official_rise = official.valid ? official.sunrise_min : -1.0f;
    official_set = official.valid ? official.sunset_min : -1.0f;
}

void clockTick() {
    uint16_t y = 0;
    uint8_t mo = 0, d = 0;
    uint32_t sod = 0;
    have_clock = clockNow(sod, y, mo, d);
    if (!have_clock) return;
    minute_of_day = sod / 60u;
    sunRecalculate(y, mo, d);
}

void lightTick(uint32_t dt_ms) {
    clockTick();

    LightInput in{};
    in.minute_of_day = minute_of_day;
    in.civil_sunrise_min = civil_rise;
    in.civil_sunset_min = civil_set;
    in.official_sunrise_min = official_rise;
    in.official_sunset_min = official_set;
    in.motion = motionSeen();
    in.motion_age_s = motionAgeMs() / 1000u;
    const LampState& lamps = lightsGet();
    in.lux = lamps.lux;
    in.lux_valid = lamps.lux_valid;
    in.street_override = street_override;
    in.house_override = house_override;
    in.override_left_s = override_left_s;
    in.clock_valid = have_clock;

    const LightOutput o = lightDecide(cfg, in);
    if (override_left_s > 0) {
        override_left_s -= (dt_ms + 999u) / 1000u;
        if (override_left_s > cfg.override_s) override_left_s = 0;   // underflow guard
    }

    lightsRequest(o.street, o.house, o.barn_duty);
    lightsTick(dt_ms);
    last_mode = o.mode;

    // Report a switching event the moment it happens rather than on the next
    // telemetry tick: a lamp that came on two minutes late is a complaint, and
    // the log has to be able to answer it.
    const LampState& now = lightsGet();
    if (now.street != last_street) {
        telemetryEvent(now.street ? "street-on" : "street-off");
        last_street = now.street;
    }
    if (now.house != last_house) {
        telemetryEvent(now.house ? "house-on" : "house-off");
        last_house = now.house;
    }
}

void publish() {
    const LampState& l = lightsGet();
    LightReport r{};
    r.street = l.street;
    r.house = l.house;
    r.barn_duty = l.duty_now;
    r.lux = l.lux;
    r.lux_valid = l.lux_valid;
    r.amps = l.amps;
    r.mode = last_mode;
    r.sunrise_min = civil_rise;
    r.sunset_min = civil_set;
    r.minute_of_day = minute_of_day;
    r.clock_valid = have_clock;
    r.motion = motionSeen();
    r.override_pending = override_left_s > 0;
    r.override_left_s = override_left_s;
    r.fault = l.fault;
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) r.burn_tenths[i] = lightsBurnTenths(static_cast<LampCircuit>(i));
    telemetryPublish(r);
}

void persistBurn() {
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        char key[12];
        std::snprintf(key, sizeof(key), "h%u", static_cast<unsigned>(i));
        nvSetI32(key, static_cast<int32_t>(lightsBurnTenths(static_cast<LampCircuit>(i))));
    }
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[16], value[24];
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

    const bool on = value[0] == '1' || value[0] == 'o';
    bool taken = true;
    if (std::strcmp(verb, "street") == 0) {
        street_override = on;
        override_left_s = cfg.override_s;
    } else if (std::strcmp(verb, "house") == 0) {
        house_override = on;
        override_left_s = cfg.override_s;
    } else if (std::strcmp(verb, "auto") == 0) {
        override_left_s = 0;
        street_override = false;
        house_override = false;
    } else if (std::strcmp(verb, "clear") == 0) {
        lightsClearLatch();
    } else if (std::strcmp(verb, "time") == 0) {
        // "time=2026-04-18T19:42:00" from the app: the RTC is the fallback, not
        // the assumption, and a board with a dead coin cell must still be
        // switchable on the right schedule.
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
        if (std::sscanf(value, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) == 6 &&
            y > 2000 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h < 24 && mi < 60) {
            clockSet(static_cast<uint16_t>(y), static_cast<uint8_t>(mo), static_cast<uint8_t>(d),
                     static_cast<uint8_t>(h), static_cast<uint8_t>(mi), static_cast<uint8_t>(se));
            sun_day_key = 0xFFFFFFFFu;
        } else {
            taken = false;
        }
    } else {
        taken = false;
    }
    telemetryNoteAck(verb, value, taken);
    telemetryEvent(taken ? "cmd" : "cmd-unknown");
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
    } else if (static_cast<int32_t>(halMillis() - (down_ms + 60000u)) >= 0) {
        down_ms = halMillis();
        wifiReconnect();
    }
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;

    if (sched.due(job_light, dt)) {
        lightTick(sched.elapsed(job_light));
        feedWatchdog();
    }
    if (sched.due(job_telem, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_burn, dt)) persistBurn();
    supervision();
}

#if !defined(RANCH_SIM)
void lightTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(50);
    }
}
#endif

}  // namespace

// The console's input, as a command door.
//
// In the simulation there is no broker (telemetry.cpp compiles an empty uplink under
// RANCH_SIM), so the ground command channel that reaches this board on the ranch does
// not exist in the browser -- the page can read the console and nothing else, which
// made every lamp switch on the page a scene-only animation. This reads the other
// direction of the same port the LIGHT frame is printed to, through the very
// `onGroundCommand` the broker would call, so a verb the bench accepts and a verb the
// console accepts cannot drift apart.
//
// ARDUINO only: the host sandbox drives `simCommand` from its own test, and giving
// that build a stdin reader would make a unit test wait on a terminal.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }

#if defined(ARDUINO)
void consoleCommandPump() {
    static char line[40];
    static size_t n = 0;
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;                    // CRLF from a terminal
        if (c == '\n') {
            line[n] = '\0';
            if (n) simCommand(line);
            n = 0;
            continue;
        }
        if (n < sizeof(line) - 1) line[n++] = static_cast<char>(c);
        else n = 0;   // a line long enough to be nonsense: drop it, keep the port clean
    }
}
#endif
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    override_left_s = 0;
    street_override = false;
    house_override = false;
    sun_day_key = 0xFFFFFFFFu;
    sched.clear();
    lightsInit();
    telemetryInit();
    paramsLoad();
    clockTick();
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_light = sched.add("light", 100);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_burn = sched.add("burn", 60000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the schedule needs a task of its
    // own or the board boots, banners, and stops thinking.
    xTaskCreatePinnedToCore(lightTask, "light", STACK_LIGHT, nullptr, PRIO_LIGHT,
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
    halDelayMs(50);
#endif
}

}  // namespace ranch

#if !defined(RANCH_SIM)
void setup() { ranch::appSetup(); }
void loop() { vTaskDelete(NULL); }
#endif
