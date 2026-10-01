// State uplink for the water board: one key=value frame per publish, to the
// console and to MQTT.
//
// The field names are the contract with the ranch dashboard, so they name what
// they measure and never two things at once: `barn`/`house` are the valve
// states, `barn_l`/`house_l` are litres for the cycle in progress, `*_tot` are
// the lifetime totals, and `next` is seconds to the next scheduled dispense.
#pragma once

#include <cstddef>

#include "sense.h"
#include "valves.h"
#include "water_safety.h"

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint16_t last_len;
    bool broker_connected;
};

struct WaterReport {
    bool barn_open;
    bool house_open;
    bool pump_on;
    float barn_flow;
    float house_flow;
    float pressure_bar;
    float tank_pct;
    uint32_t next_s;
    float barn_cycle_l;
    float house_cycle_l;
    float barn_total_l;
    float house_total_l;
    WaterAction action;
    const char* reason;
    uint32_t runs;
    uint32_t skipped;
    uint32_t faults;
    bool clock_valid;
    bool sensor_fault;
};

void telemetryInit();
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);
// The programme as this board holds it (`per90/run20/en1`), published as `lim=` so a
// settings row can show the valve controller's number rather than the page's.
void telemetrySetLimits(const char* text);
// Published as `ack=ok:<verb>` / `ack=no:<verb>` for a few seconds after a command.
void telemetryNoteAck(const char* verb, const char* value, bool ok);


void telemetryPublish(const WaterReport& r);

// Events go out on a different source token than the state frame. A dashboard
// that reads the newest line would otherwise take "cycle finished" for "the
// valves are shut" and turn the barn off on screen while it is still running.
void telemetryEvent(const char* kind);
void telemetryEventLitres(const char* kind, bool barn_line, float litres);

void telemetryService();
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch
