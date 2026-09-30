#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"

namespace ranch {
namespace {

constexpr size_t FRAME_CAP = 256;

UplinkStats up{};
char frame[FRAME_CAP];
char broker_host[64] = "";
char broker_client[24] = "power";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

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
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "power");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const PowerReport& r) {
    const PowerRead& d = r.m.read;
    FrameWriter w(frame, sizeof(frame));
    w.begin("POWER");
    w.add("va", d.volts[0], 1);
    w.add("vb", d.volts[1], 1);
    w.add("vc", d.volts[2], 1);
    w.add("ia", r.m.phase[0].amps, 1);
    w.add("ib", r.m.phase[1].amps, 1);
    w.add("ic", r.m.phase[2].amps, 1);
    w.add("kw", d.kw, 1);
    w.add("kva", d.kva, 1);
    w.add("pf", d.pf, 2);
    w.add("hz", d.hz, 2);
    w.add("load", d.load_pct, 0);
    w.add("unb", d.unbalance_pct, 0);
    w.add("rcd", r.m.rcd_ma, 0);
    w.add("temp", r.m.temp_c, 0);
    w.add("brk", r.breaker_closed ? 1 : 0);
    // Confirmed, not commanded: this is the field that decides whether the
    // dashboard is showing the switchboard or showing an intention.
    w.add("pump", r.fo.pump && r.fo.pump_closed ? 1 : 0);
    w.add("lit", r.fo.lights ? 1 : 0);
    w.add("kwhd", r.m.energy.kwh_today, 1);
    w.add("kwh", r.m.energy.kwh_total, 1);
    w.add("act", gridActionName(r.action));
    w.add("up", static_cast<int>(r.uptime_s > 999999u ? 999999u : r.uptime_s));
    w.add("link", wifiUp() ? "up" : "down");
    // Only carried when they say something; an always-present ok=1 is a field
    // nobody reads and every reader has to remember to check.
    if (r.m.sensor_fault) w.add("ok", 0);
    if (d.phase_loss) w.add("phl", 1);
    if (r.fo.held) w.add("held", 1);
    if (r.fo.contactor_fault) w.add("coil", "fault");
    if (r.fo.contactor_welded) w.add("coil", "welded");
    if (r.fo.shed) w.add("shed", static_cast<int>(r.fo.shed_s));
    if (r.fo.trip_latched) w.add("lock", static_cast<int>(r.fo.lockout_s));
    if (!r.clock_valid) w.add("clock", "unset");
    if (r.reason && *r.reason) w.add("why", r.reason);
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

void telemetryEvent(const char* kind, const char* why) {
    char line[72];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    if (why && *why) w.add("why", why);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch
