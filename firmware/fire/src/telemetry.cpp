#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"

namespace ranch {
namespace {

// 272, not 224: the longest frame the sandbox publishes is 204 bytes; `lim=` and a transient `ack=` with a 32-character value do not fit beside a long `why=`.
constexpr size_t FRAME_CAP = 272;

UplinkStats up{};
char frame[FRAME_CAP];
char last_event[64] = "";
char broker_host[64] = "";
char broker_client[24] = "fire";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;
// The board's answer to the last line typed at its console, kept for a few seconds
// so a dashboard polling at 2 Hz cannot miss it.
constexpr uint32_t ACK_HOLD_MS = 6000;
char ack_verb[40] = "";
bool ack_ok = false;
uint32_t ack_ms = 0;
// The panel's timers as it holds them, handed over already formatted.
char lim_text[64] = "";


char loopDigit(LoopState s) {
    switch (s) {
        case LoopState::Normal:  return '0';
        case LoopState::Alarm:   return '1';
        case LoopState::Open:    return '2';
        case LoopState::Shorted: return '3';
        case LoopState::Unknown: return '4';
    }
    return '?';
}

}  // namespace

#if defined(RANCH_SIM)
namespace {
void uplinkMqtt(const char*, size_t) {}
[[maybe_unused]] void uplinkDialMqtt(const char*, size_t) {}
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
bool dial_held = false;

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

// The dial-out is retained, so whatever broker the ranch has on the other end keeps
// the last fire condition until someone clears it. A non-retained "fire" published
// in the second the broker reconnects is a message to nobody.
void uplinkDialMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_DIAL, reinterpret_cast<const uint8_t*>(s), n, true)) {
        up.mqtt_sent++;
        up.dial_asserts++;
    } else {
        up.mqtt_failed++;
    }
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
    lim_text[0] = '\0';   // appSetup hands the timers over right after this
    last_event[0] = '\0';
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "fire");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }
void telemetrySetLimits(const char* text) {
    std::snprintf(lim_text, sizeof(lim_text), "%s", text ? text : "");
}

void telemetryNoteAck(const char* verb, const char* value, bool ok) {
    // The verb alone cannot answer "what value did the board keep"; an operator
    // changing a trip threshold needs the number back, not just a yes.
    if (value && *value) std::snprintf(ack_verb, sizeof(ack_verb), "%s=%s", verb, value);
    else std::snprintf(ack_verb, sizeof(ack_verb), "%s", verb);
    ack_ok = ok;
    ack_ms = halMillis();
}


void telemetryPublish(const FireReport& r) {
    char z[8];
    z[0] = loopDigit(r.loops.zone[0]);
    z[1] = loopDigit(r.loops.zone[1]);
    z[2] = loopDigit(r.loops.zone[2]);
    z[3] = loopDigit(r.loops.zone[3]);
    z[4] = '\0';

    FrameWriter w(frame, sizeof(frame));
    w.begin("FIRE");
    w.add("lvl", fireLevelName(r.d.level));
    w.add("z", z);
    w.add("mcp", r.loops.manual_call ? 1 : 0);
    w.add("flow", r.loops.flow_active ? 1 : 0);
    w.add("arm", r.d.bypassed ? 0 : 1);
    w.add("siren", r.d.siren ? 1 : 0);
    w.add("strb", r.d.strobe ? 1 : 0);
    w.add("pump", r.d.pump_permit ? 1 : 0);
    // The pump's own contactor, seen through the permit's feedback: a permit that is
    // closed with nothing running behind it is a dry riser, and the log has to be
    // able to say which of the two it saw.
    w.add("pfb", pumpPermitIsClosed() ? 1 : 0);
    w.add("sil", r.d.silenced ? 1 : 0);
    if (r.d.supervision) w.add("sup", 1);
    if (r.loops.mcp == LoopState::Open || r.loops.mcp == LoopState::Shorted) w.add("mcps", "fault");
    if (r.loops.flow == LoopState::Open || r.loops.flow == LoopState::Shorted) w.add("flows", "fault");
    w.add("sirenm", static_cast<int>(r.stats.siren_on_ms / 1000u));
    // The decision layer's proof, not this module's guess: 1 means the bell was
    // commanded and the armature relay did not close inside the proof window.
    w.add("bellfail", r.siren_failed ? 1 : 0);
    w.add("sil_n", static_cast<int>(r.silences));
    w.add("sil_left", static_cast<int>(r.silence_left_s));
    w.add("up", static_cast<int>(r.uptime_s > 999999u ? 999999u : r.uptime_s));
    w.add("link", wifiUp() ? "up" : "down");
    if (r.clock_valid) {
        // Modulo before formatting: the RTC is read as seconds-of-day and a device
        // that has been told a leap second or two can hand back 86400, which would
        // otherwise print as "24:00:00" in a log that is supposed to be evidence.
        const uint32_t sod = r.second_of_day % 86400u;
        char ts[16];
        std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", static_cast<int>(sod / 3600u),
                      static_cast<int>((sod / 60u) % 60u), static_cast<int>(sod % 60u));
        w.add("t", ts);
    } else {
        w.add("clock", "unset");
    }
    if (r.d.reason[0] != '\0') w.add("why", r.d.reason);
    // The board's answer to the last line typed at it: a dashboard that only sees
    // valves and relays cannot tell "the board did that" from "the page did it to
    // itself", which is the difference between a control and a painting.
    if (*lim_text) w.add("lim", lim_text);
    if (*ack_verb && static_cast<int32_t>(halMillis() - ack_ms) < static_cast<int32_t>(ACK_HOLD_MS)) {
        char ack[56];
        std::snprintf(ack, sizeof(ack), "%s:%s", ack_ok ? "ok" : "no", ack_verb);
        w.add("ack", ack);
    }
    w.endLine();

    if (w.overflow()) {
        // A truncated frame would show the dashboard a stale field as fresh, and on
        // this board that field could be the one that says the bell failed.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryEvent(const char* kind, const char* why) {
    char line[64];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    if (why && *why) w.add("why", why);
    w.endLine();
    consoleWrite(line, w.size());
    // Kept for the diagnostic reader. A refused reset and a silence refusal last one
    // frame each in the state stream, which is exactly the kind of thing that has to
    // be readable after the fact rather than caught in the act.
    std::snprintf(last_event, sizeof(last_event), "%s", line);
}

void telemetryDial(bool active, const char* reason) {
    char line[80];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", active ? "dial" : "dial-clear");
    if (reason && *reason) w.add("why", reason);
    w.endLine();
    consoleWrite(line, w.size());
#if !defined(RANCH_SIM)
    if (active) {
        uplinkDialMqtt(line, w.size());
        dial_held = true;
    } else if (dial_held) {
        // One clear message, then silence: the retained dial line has to be emptied,
        // not left holding yesterday's fire.
        uplinkDialMqtt(line, w.size());
        dial_held = false;
    }
#endif
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }
const char* telemetryLastEvent() { return last_event; }

}  // namespace ranch
