#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"

namespace ranch {
namespace {

constexpr size_t FRAME_CAP = 192;

UplinkStats up{};
char frame[FRAME_CAP];
char broker_host[64] = "";
char broker_client[24] = "light";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

// The board's answer to the last line anybody typed at it, kept for a few seconds so
// a dashboard polling at 2 Hz cannot miss it. `ack=ok:street` and `ack=no:street`
// look identical on the console and on a wire; only this field tells them apart.
constexpr uint32_t ACK_HOLD_MS = 6000;
char ack_verb[40] = "";
bool ack_ok = false;
uint32_t ack_ms = 0;

// Minutes past local midnight as hh:mm, which is what a person reading the
// dashboard wants; the float version is what the policy works in.
void hhmm(char (&dst)[12], float minutes) {
    if (minutes < 0.0f || minutes >= 1440.0f) {
        std::snprintf(dst, sizeof(dst), "%s", "--:--");
        return;
    }
    // Clamped rather than wrapped, so the range is something the compiler can
    // see and a formatted time cannot ask for more buffer than it has.
    int m = static_cast<int>(minutes + 0.5f);
    if (m < 0) m = 0;
    if (m > 1439) m = 1439;
    std::snprintf(dst, sizeof(dst), "%02d:%02d", m / 60, m % 60);
}

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
    // A board restart must not come back still answering somebody's last command.
    ack_verb[0] = '\0';
    ack_ok = false;
    ack_ms = 0;
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "light");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryNoteAck(const char* verb, const char* value, bool ok) {
    // The verb alone cannot answer "what value did the board keep"; an operator
    // changing a trip threshold needs the number back, not just a yes.
    if (value && *value) std::snprintf(ack_verb, sizeof(ack_verb), "%s=%s", verb, value);
    else std::snprintf(ack_verb, sizeof(ack_verb), "%s", verb);
    ack_ok = ok;
    ack_ms = halMillis();
}

void telemetryPublish(const LightReport& r) {
    char rise[12], set[12];
    hhmm(rise, r.sunrise_min);
    hhmm(set, r.sunset_min);

    FrameWriter w(frame, sizeof(frame));
    w.begin("LIGHT");
    // The board's own reading of the clock, in minutes past local midnight. Every
    // other field is a decision, and a decision cannot be checked against the
    // sky without knowing when the board thought it was.
    w.add("t", static_cast<int>(r.minute_of_day));
    w.add("street", r.street ? "on" : "off");
    w.add("house", r.house ? "on" : "off");
    w.add("duty", r.barn_duty * 100.0f, 0);
    w.add("lux", r.lux_valid ? r.lux : -1.0f, 1);
    w.add("amp", r.amps, 2);
    w.add("mode", lightModeName(r.mode));
    w.add("rise", rise);
    w.add("set", set);
    w.add("link", wifiUp() ? "up" : "down");
    if (r.motion) w.add("motion", 1);
    if (r.override_pending) w.add("manual", static_cast<int>(r.override_left_s));
    if (!r.clock_valid) w.add("clock", "unset");
    if (r.fault.barn_dead) w.add("fault", "LAMP");
    else if (r.fault.barn_over) w.add("fault", "OVERCURRENT");
    else if (r.fault.driver_contact) w.add("fault", "DRIVER");
    w.add("h_street", static_cast<int>(r.burn_tenths[LAMP_STREET] / 36000u));
    // The answer to the last command, for as long as it is still the answer. A page
    // that only sees the lamps move cannot tell "the board did that" from "the page
    // did that to itself", which is the difference between a control and a painting.
    if (*ack_verb && static_cast<int32_t>(halMillis() - ack_ms) < static_cast<int32_t>(ACK_HOLD_MS)) {
        char ack[56];
        std::snprintf(ack, sizeof(ack), "%s:%s", ack_ok ? "ok" : "no", ack_verb);
        w.add("ack", ack);
    }
    w.endLine();

    if (w.overflow()) {
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

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch
