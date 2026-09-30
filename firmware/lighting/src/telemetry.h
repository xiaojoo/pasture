// State uplink for the lighting board.
//
// `sunrise`/`sunset` are reported as minutes past local midnight so the dashboard
// can show what the board decided the sky would do, and `why` names the rule that
// won. A lamp that is off because it is daytime must not look like a lamp that is
// off because the board is dead.
#pragma once

#include <cstddef>

#include "light_policy.h"
#include "lights.h"

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint16_t last_len;
    bool broker_connected;
};

struct LightReport {
    bool street;
    bool house;
    float barn_duty;
    float lux;
    bool lux_valid;
    float amps;
    LightMode mode;
    float sunrise_min;
    float sunset_min;
    uint32_t minute_of_day;
    bool clock_valid;
    bool motion;
    bool override_pending;
    uint32_t override_left_s;
    LampFault fault;
    uint32_t burn_tenths[LAMP_COUNT];
};

void telemetryInit();
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const LightReport& r);
void telemetryEvent(const char* kind);

void telemetryService();
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch
