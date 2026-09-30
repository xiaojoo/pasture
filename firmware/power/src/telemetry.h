// State uplink for the switchboard board.
//
// The field names are the contract with the ranch dashboard, and each one is
// allowed to mean exactly one thing: `va`/`vb`/`vc` are phase-to-neutral volts,
// `ia`/`ib`/`ic` are the same legs' current, `load` is per cent of the board's
// rated kVA, `pump`/`lit` are what the contactors are *doing* (confirmed, not
// commanded), and `act`/`why` are the protection decision and its one reason.
// `link=down` and `ok=0` say the board could not measure, which is not the same
// claim as "the supply is fine".
#pragma once

#include <cstddef>

#include "feeders.h"
#include "meter.h"
#include "power_quality.h"

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint16_t last_len;
    bool broker_connected;
};

struct PowerReport {
    Metered m;                  // copy: the publisher must not outlive the tick it reports
    FeederOutputs fo;
    GridAction action;
    const char* reason;
    bool breaker_closed;
    bool clock_valid;
    uint32_t uptime_s;
};

void telemetryInit();
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const PowerReport& r);

// Events use a different source token than the state frame on purpose: a
// dashboard that reads the newest POWER line as state would otherwise take
// "trip" for a measurement and blink the whole board off.
void telemetryEvent(const char* kind, const char* why);

void telemetryService();
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch
