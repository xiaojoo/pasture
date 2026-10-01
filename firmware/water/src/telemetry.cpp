#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"

namespace ranch {
namespace {

// 256, not 208: the longest frame the sandbox publishes is 204 bytes, and a frame that overflows is dropped whole -- 4 bytes of margin is one long `why=` away from losing the board's readings.
constexpr size_t FRAME_CAP = 256;

UplinkStats up{};
char frame[FRAME_CAP];
char broker_host[64] = "";
char broker_client[24] = "water";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

// The board's answer to the last line typed at its console. `ack=ok:resume` and
// `ack=no:resume` are otherwise indistinguishable on a dashboard that only sees
// valves, and a button that reports nothing is a button nobody can trust.
constexpr uint32_t ACK_HOLD_MS = 6000;
char ack_verb[40] = "";
bool ack_ok = false;
uint32_t ack_ms = 0;
// The programme as this board holds it, handed over already formatted.
char lim[48] = "";

}  // namespace

#if defined(RANCH_SIM)
namespace {
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

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

void onMqttPayload(char* topic, uint8_t* payload, unsigned int len) {
    if (!cmd_fn) return;
    if (std::strcmp(topic, MQTT_TOPIC_CMD) != 0) return;
    cmd_fn(reinterpret_cast<const char*>(payload), len);
}

void brokerService() {
    const uint32_t now = halMillis();
    if (*broker_host == '\0') return;
    if (!broker.connected()) {
        if (static_cast<int32_t>(now - next_connect) < 0) return;
        next_connect = now + 5000;
        broker.setServer(broker_host, broker_port);
        broker.setBufferSize(256);
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
    // A restart must not come back still answering the last command.
    ack_verb[0] = '\0';
    ack_ms = 0;
    lim[0] = '\0';   // appSetup hands the real one over right after this
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "water");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }
void telemetrySetLimits(const char* text) {
    std::snprintf(lim, sizeof(lim), "%s", text ? text : "");
}

void telemetryNoteAck(const char* verb, const char* value, bool ok) {
    // The verb alone cannot answer "what value did the board keep"; an operator
    // changing a trip threshold needs the number back, not just a yes.
    if (value && *value) std::snprintf(ack_verb, sizeof(ack_verb), "%s=%s", verb, value);
    else std::snprintf(ack_verb, sizeof(ack_verb), "%s", verb);
    ack_ok = ok;
    ack_ms = halMillis();
}


void telemetryPublish(const WaterReport& r) {
    FrameWriter w(frame, sizeof(frame));
    w.begin("WATER");
    w.add("barn", r.barn_open ? "on" : "off");
    w.add("house", r.house_open ? "on" : "off");
    w.add("flow", r.barn_flow + r.house_flow, 1);
    w.add("press", r.pressure_bar, 2);
    w.add("next", static_cast<int>(r.next_s > 99999u ? 99999u : r.next_s));
    w.add("pump", r.pump_on ? 1 : 0);
    w.add("tank", r.tank_pct, 0);
    w.add("barn_l", r.barn_cycle_l, 1);
    w.add("house_l", r.house_cycle_l, 1);
    w.add("barn_tot", r.barn_total_l, 0);
    w.add("house_tot", r.house_total_l, 0);
    w.add("act", waterActionName(r.action));
    w.add("runs", static_cast<int>(r.runs));
    w.add("skip", static_cast<int>(r.skipped));
    w.add("fault", static_cast<int>(r.faults));
    w.add("link", wifiUp() ? "up" : "down");
    if (!r.clock_valid) w.add("clock", "unset");
    if (r.sensor_fault) w.add("sensor", "fault");
    if (r.reason && *r.reason) w.add("why", r.reason);
    // The board's answer to the last line typed at it: a dashboard that only sees
    // valves and relays cannot tell "the board did that" from "the page did it to
    // itself", which is the difference between a control and a painting.
    if (*lim) w.add("lim", lim);
    if (*ack_verb && static_cast<int32_t>(halMillis() - ack_ms) < static_cast<int32_t>(ACK_HOLD_MS)) {
        char ack[56];
        std::snprintf(ack, sizeof(ack), "%s:%s", ack_ok ? "ok" : "no", ack_verb);
        w.add("ack", ack);
    }
    w.endLine();

    if (w.overflow()) {
        // A truncated frame would show the dashboard a stale field as fresh.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryEvent(const char* kind) {
    char line[64];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryEventLitres(const char* kind, bool barn_line, float litres) {
    char line[80];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    w.add("line", barn_line ? "barn" : "house");
    w.add("litres", litres, 1);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch
