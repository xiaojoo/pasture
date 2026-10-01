// State uplink: one key=value frame, three sinks (console, MQTT, SD log).
// The frame is built once per publish so all three sinks carry byte-identical
// data and a lost MQTT message cannot make the log disagree with the dashboard.
#pragma once

#include <cstddef>

#include "mission.h"
#include "safety.h"

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint32_t logged;
    uint32_t log_dropped;   // ring buffer overflow while the card was busy
    uint16_t last_len;
    bool broker_connected;
};

void telemetryInit();
// Broker identity comes from NVS, so a fleet of aircraft can share firmware.
// Call before the first publish; the buffers are copied, not borrowed.
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

// Ground-to-aircraft commands. The payload is not NUL-terminated: it is a view
// of the receive buffer that is only valid for the duration of the call, so a
// handler that wants to keep it must copy it first.
using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

// The board's answer to the last command line, published as `ack=ok:<verb>` or
// `ack=no:<verb>` for a few seconds. Same contract as the other four boards, so one
// page-side table can say what any of them did about a button.
void telemetryNoteAck(const char* verb, const char* value, bool ok);

void telemetryPublish(const MissionStatus& ms, const AirState& air);
void telemetryService();               // broker keepalive + log drain
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch
