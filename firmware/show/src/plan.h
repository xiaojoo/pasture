// The programme as it crosses the radio, and the one lane in it that belongs to
// this airframe.
//
// The upload is a compact line, not a file: a show is small enough to read out
// over a console and it has to survive a technician typing it by hand.
//
//   SHOWPLAN,<drones>,<separation>,<max speed>,<geofence>,<rtl alt>,<act count>
//     ;A,<shape>,<scale>,<alt>,<hold s>,<move s>,<colour>     once per act
//
// The round trip is asserted in test_sandbox.cpp: format a plan, parse it back,
// and every number has to come out where it went in, including the colours, which
// are the one field that is not a number.
//
// The aircraft never stores the whole plan: planLane() runs show_core's sampling
// and assignment for all the stations and keeps only this station's point per act,
// which is the trajectory it was uploaded to fly. Everything else is the ground
// station's business.
#pragma once

#include <cstddef>

#include "show_core.h"

namespace ranch {

constexpr size_t SHOW_PLAN_MAX_TEXT = 1024;    // the longest upload this build takes
constexpr const char* SHOW_PLAN_TAG = "SHOWPLAN";

// Writes the plan and returns the number of bytes used, or 0 when the buffer was
// too small or the plan is not representable. Never truncates silently: either
// the whole plan is there or nothing is.
size_t planFormat(const ShowPlan& plan, char* buf, size_t cap);

// Parses in place with strtod, so a corrupt record cannot allocate and cannot
// walk off the end of the act array. Returns false on the first field that is
// not a number, on a missing act, and on an act count that does not match the
// records that follow it -- a half-parsed programme is a programme that flies
// somewhere nobody approved.
bool planParse(const char* text, ShowPlan& out);

// The one lane this airframe flies. `at[i]` is the point station `station` ends up
// with in act i, taken out of show_core's sample + assign + **repair**, so it is the
// same point the ground station certified -- not the point a re-derivation without the
// repair pass would have produced.
struct ShowLane {
    int station = 0;
    int act_count = 0;
    ShowPoint at[SHOW_MAX_ACTS] = {};
    uint32_t colour[SHOW_MAX_ACTS] = {};
    // Where the aircraft lifts off from: the surveyed origin, which is also
    // station 0's neighbourhood and, in practice, the middle of the pad.
    ShowPoint pad = {};
};

// False when the station is outside the fleet or the plan has no acts.
//
// `table` is the caller's workspace and has to hold `plan.act_count * plan.drones`
// points: the repair pass decides which destination belongs to which airframe by
// looking at every airframe's destination, so it cannot run out of one station's row.
// The caller owns it for exactly this reason -- the ground station can put it on a
// heap and the aircraft can put it in the one task that unpacks an upload, instead of
// both paying for it in .bss forever. It is not touched in flight.
bool planBuildLane(const ShowPlan& plan, int station, ShowLane& out, ShowPoint* table);

// The height layer this station bulges through while it moves between two
// formation points, straight from show_core.
float planLaneOffset(const ShowLane& lane, int act, float separation_m);

// The ack number for a record: a 16-bit rolling sum over its exact bytes. The ground
// station computes the same number in JavaScript (ranch/js/show/plan.js) and compares
// it against the `ck=` field of the aircraft's own telemetry frame, so "it has a
// two-act show" and "it has *this* show" are different claims and only the second one
// is checked. `.probe/cross.sh` prints both implementations' numbers for the same
// records.
uint16_t planChecksum(const char* text, size_t len);

}  // namespace ranch
