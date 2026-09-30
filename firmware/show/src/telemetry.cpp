#include "telemetry.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "frame_codec.h"
#include "hal.h"

namespace ranch {
namespace {

// Sized for the longest frame below with every optional field present. A frame
// that outgrows it is dropped and counted, never truncated.
constexpr size_t FRAME_CAP = 160;

UplinkStats up{};
char frame[FRAME_CAP];

char broker_host[64] = "";
char broker_client[24] = "show";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

}  // namespace

#if defined(RANCH_SIM)
namespace {
// The browser build has no broker. Frames still go to the console, which is what
// the ranch dashboard reads back, so the publish path stays exercised end to end
// and the counters below stay honest about what was not sent.
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
        broker.setBufferSize(1024);             // an uploaded programme is not short
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
                  (client_id && *client_id) ? client_id : "show");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const ShowStatus& s) {
    char rgb[10];
    std::snprintf(rgb, sizeof(rgb), "%06lx", static_cast<unsigned long>(s.colour) & 0xFFFFFFul);

    FrameWriter w(frame, sizeof(frame));
    w.begin("SHOW");
    // The station number, not a serial: two aircraft whose frames cannot be told
    // apart are one aircraft whose telemetry is a lie, and the ground station keys
    // its whole picture on this field.
    w.add("id", s.station);
    // Which act is being flown, 1-based, 0 for "no programme on board". The phase
    // is not published separately because the three fields below it already say it:
    // a returning aircraft reports the slot in n/e/d and the reason in why.
    w.add("act", s.act);
    // What programme this airframe is holding, so an upload can be acked from the
    // aircraft's own words rather than from the ground station hoping the packet
    // arrived: the act count plus a checksum of the record it parsed. A board that
    // refused an upload reports acts=0/ck=0, and a board still flying last night's
    // show reports that show's numbers.
    w.add("acts", s.act_count);
    w.add("ck", static_cast<int>(s.plan_ck));
    w.add("u", s.u, 2);
    // Where this airframe actually is, in local NED metres from the surveyed
    // origin -- the same frame show_core bakes its paths in, so a viewer can
    // compare a reported position against the planned one without a conversion.
    w.add("n", s.rtk.n, 2);
    w.add("e", s.rtk.e, 2);
    w.add("d", s.rtk.d, 2);
    // The colour the programme asked for, not the current going into the pixels:
    // the mirror of this airframe has to be able to compare it against the act it
    // planned. The strip's own headroom, and the red a failsafe return puts on it,
    // are the aircraft's business and are measured, in the sandbox, off the pixels.
    w.add("led", rgb);
    // Two words for "how good is this position", because they are two different
    // devices' opinions: the receiver's own fix type, and whether the carrier
    // phase resolved. A show flown on fix_type=3 with carr_soln=0 is a show flown
    // on decimetres, and the frame has to be able to say that.
    w.add("fix", static_cast<int>(s.fix_type));
    w.add("rtk", static_cast<int>(s.carr_soln));
    w.add("batt", static_cast<int>(s.batt_pct + 0.5f));
    w.add("link", s.link_up ? "up" : "down");
    w.add("up", static_cast<int>(s.uptime_ms / 1000u));
    if (*s.why) w.add("why", s.why);
    w.endLine();

    if (w.overflow()) {
        // A truncated telemetry frame is worse than a missing one: the ground app
        // would read a stale field as fresh. Drop and count it.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

// Nothing to drain here: the flight log belongs to the flight controller, which
// has the card and the airframe's own sensors on it. What is left for this service
// is the broker keepalive, and it is on its own job because a socket that blocks
// must not delay a position target.
void telemetryService() {
    brokerService();
}

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch
