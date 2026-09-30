#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"
#include "video.h"

namespace ranch {
namespace {

constexpr uint16_t LOG_SLOTS = 16;
constexpr size_t LOG_SLOT = 128;
// Sized for the longest frame below with every optional field present. A frame
// that outgrows it is dropped and counted, never truncated.
constexpr size_t FRAME_CAP = 208;

UplinkStats up{};
char frame[FRAME_CAP];
char log_[LOG_SLOTS][LOG_SLOT];
uint8_t log_head = 0, log_tail = 0, log_len = 0;

char broker_host[64] = "";
char broker_client[24] = "drone";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

void logPush(const char* s, size_t n) {
    if (log_len == LOG_SLOTS) {
        // Overwrite the oldest entry: a full ring must not stop the aircraft,
        // and the newest state is the one the ground app acts on.
        up.log_dropped++;
        log_tail = static_cast<uint8_t>((log_tail + 1) % LOG_SLOTS);
        log_len--;
    }
    std::snprintf(log_[log_head], LOG_SLOT, "%.*s", static_cast<int>(n), s);
    log_head = static_cast<uint8_t>((log_head + 1) % LOG_SLOTS);
    log_len++;
}

}  // namespace

#if defined(RANCH_SIM)
namespace {
// The browser build has no broker. Frames still go to the console, which is
// what the ranch dashboard reads back, so the publish path stays exercised
// end to end and the counters below stay honest about what was not sent.
void uplinkMqtt(const char*, size_t) {}
void brokerService() {}
bool brokerUp() { return false; }
}  // namespace
#else
namespace {
#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient wifi_client;
PubSubClient broker(wifi_client);
uint32_t next_connect = 0;
bool callback_wired = false;

// The only topic this firmware listens to is the command one, and the payload
// arrives as a length-delimited view of the receive buffer.
void onMqttPayload(char* topic, uint8_t* payload, unsigned int len) {
    if (!cmd_fn) return;
    if (std::strcmp(topic, MQTT_TOPIC_CMD) != 0) return;
    cmd_fn(reinterpret_cast<const char*>(payload), len);
}

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

void brokerService() {
    const uint32_t now = halMillis();
    if (*broker_host == '\0') return;
    if (!broker.connected()) {
        if (static_cast<int32_t>(now - next_connect) < 0) return;
        next_connect = now + 5000;              // one attempt per 5 s, forever
        broker.setServer(broker_host, broker_port);
        broker.setBufferSize(512);
        if (!callback_wired) {
            broker.setCallback(onMqttPayload);
            callback_wired = true;
        }
        if (broker.connect(broker_client)) broker.subscribe(MQTT_TOPIC_CMD, 0);
    } else {
        broker.loop();
    }
}

bool brokerUp() { return broker.connected(); }
}  // namespace
#endif

void telemetryInit() {
    up = UplinkStats{};
    frame[0] = '\0';
    log_head = log_tail = log_len = 0;
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "drone");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const MissionStatus& ms, const AirState& air) {
    VideoStats v{};
    videoStats(v);

    int32_t home_lat = 0, home_lon = 0;
    float home_alt = 0.0f;
    const bool has_home = safetyHomeGeodetic(home_lat, home_lon, home_alt);

    FrameWriter w(frame, sizeof(frame));
    w.begin("DRONE");
    w.add("mode", ms.mode);
    // Two altitudes with different references, both named: `alt` is what the GNSS
    // reported above sea level, `agl` is what a person watching the aircraft
    // means by height. Reporting only one makes a dashboard guess.
    w.add("alt", air.alt_m, 1);
    w.add("agl", has_home ? air.alt_m - home_alt : air.alt_m, 1);
    // Where the nose points, in degrees from north: the FC's own MAVLink ATTITUDE
    // yaw. It holds its last value while the link is down, which is why `link`
    // travels in the same frame -- a heading without a link is a memory, not a
    // reading, and a dashboard that shows the FPV horizon has to be able to tell
    // the two apart.
    w.add("hdg", air.heading_deg, 1);
    // The same ATTITUDE message's other two angles, in degrees: the FPV horizon is
    // drawn from these, and a horizon built from the sticks instead of from the
    // aircraft is a picture of the pilot's hands.
    w.add("roll", air.roll_deg, 1);
    w.add("pitch", air.pitch_deg, 1);
    w.add("wp", ms.target + 1);
    w.add("left", ms.remaining_m, 0);
    w.add("batt", air.batt_pct);
    w.add("mv", air.batt_mv_cell);
    w.add("amp", air.batt_a, 1);
    w.add("rssi", static_cast<int>(air.rssi_dbm));
    w.add("fix", air.fix_type);
    w.add("sats", air.sats);
    w.add("shots", static_cast<int>(ms.captures));
    w.add("fps", v.fps);
    w.add("safe", safetyIsSafe() ? 1 : 0);
    w.add("link", wifiUp() ? "up" : "down");
    w.add("up", static_cast<int>(air.uptime_ms / 1000u));
    if (*safetyReason()) w.add("why", safetyReason());
    if (ms.failsafe != Action::Continue) w.add("fs", actionName(ms.failsafe));
    w.endLine();

    if (w.overflow()) {
        // A truncated telemetry frame is worse than a missing one: the ground
        // app would read a stale field as fresh. Drop and count it.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    logPush(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryService() {
    while (log_len) {
        const char* s = log_[log_tail];
        log_tail = static_cast<uint8_t>((log_tail + 1) % LOG_SLOTS);
        log_len--;
        char path[24];
        std::snprintf(path, sizeof(path), "/log/%06lu.csv", static_cast<unsigned long>(up.logged + 1));
        if (sdOpen(path)) {
            sdAppend(s, std::strlen(s));
            sdClose();
            up.logged++;
        } else {
            up.log_dropped++;
            log_tail = static_cast<uint8_t>((log_tail + LOG_SLOTS - 1) % LOG_SLOTS);
            log_len++;
            break;                      // no card: stop draining, keep RAM ring
        }
    }
    brokerService();
}

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch
