// State uplink for one show aircraft: one key=value frame, three sinks (console,
// MQTT, the log ring). The frame is composed once so all three carry byte-identical
// data, and the field order below is the contract the dashboard parses -- it is the
// same frame the ground station's own mirror of this airframe reads, so a reordering
// here is a silent misfeed there.
#pragma once

#include <cstddef>

#include "show.h"

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;    // broker down, or a frame that did not fit
    uint16_t last_len;
    bool broker_connected;
};

void telemetryInit();
// Broker identity comes from NVS, so a fleet of aircraft can share firmware.
// Call before the first publish; the buffers are copied, not borrowed.
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

// Ground-to-aircraft commands. The payload is not NUL-terminated: it is a view of
// the receive buffer that is only valid for the duration of the call, and a show
// upload is long enough that a handler which wants to keep it must copy it first.
using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const ShowStatus& st);
void telemetryService();               // broker keepalive + log drain
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch
