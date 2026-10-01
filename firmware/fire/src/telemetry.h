// State uplink for the fire panel.
//
// `z=` carries four digits, one per zone, left to right house / barn / store /
// switch room, and each digit is a loop state: 0 healthy, 1 in alarm, 2 loop open,
// 3 loop shorted, 4 unreadable. It is a code because a fire frame has to be small
// enough to survive a bad radio, and it is documented here because a code nobody can
// read is how a "z=2030" gets argued about at 2 a.m.
//
// `siren=1` is what the relay was *commanded* to do; `pfb=` and the dial event are
// what proves the outputs actually moved. A dashboard that shows only the former is
// showing intentions, and on this board that is the difference between a notified
// building and a quiet one.
#pragma once

#include <cstddef>

#include "panel.h"

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint32_t dial_asserts;
    uint16_t last_len;
    bool broker_connected;
};

struct FireReport {
    LoopRead loops;
    FireDecision d;
    PanelStats stats;
    uint32_t uptime_s;
    uint32_t second_of_day;
    bool clock_valid;
    bool siren_failed;         // a commanded bell that never proved it moved
    float silence_left_s;
    uint8_t silences;
};

void telemetryInit();
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);
// Published as `ack=ok:<verb>` / `ack=no:<verb>` for a few seconds after a command.
void telemetryNoteAck(const char* verb, const char* value, bool ok);
// The panel's own timers as it holds them (`cfm30/sil30/tst86400`), published as
// `lim=`. Names ride with their numbers so a reordered list cannot silently mis-pair.
void telemetrySetLimits(const char* text);


void telemetryPublish(const FireReport& r);

// Events go out on a different source token than the state frame, so a dashboard
// reading the newest FIRE line as state cannot take "reset" for "all clear".
void telemetryEvent(const char* kind, const char* why);

// The dial-out line is separate from the state frame and is *kept asserted*: a
// single "fire" message lost to a brownout is a fire nobody was told about, so this
// repeats for as long as the condition stands.
void telemetryDial(bool active, const char* reason);

void telemetryService();
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();
const char* telemetryLastEvent();

}  // namespace ranch
