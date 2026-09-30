#include "plan.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranch {
namespace {

// Reads one number out of the upload text, refusing a field that is not there.
// `p` is left pointing at the separator that stopped the scan.
bool readNum(const char*& p, double& out) {
    if (p == nullptr || *p == '\0') return false;
    char* end = nullptr;
    out = std::strtod(p, &end);
    if (end == p) return false;
    p = end;
    return true;
}

bool eat(const char*& p, char c) {
    if (p != nullptr && *p == c) { ++p; return true; }
    return false;
}

}  // namespace

size_t planFormat(const ShowPlan& plan, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0) return 0;
    if (plan.drones <= 0 || plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) return 0;
    char tmp[SHOW_PLAN_MAX_TEXT];
    size_t n = static_cast<size_t>(std::snprintf(tmp, sizeof(tmp),
                 "%s,%d,%.2f,%.2f,%.2f,%.2f,%d", SHOW_PLAN_TAG, plan.drones,
                 static_cast<double>(plan.separation_m), static_cast<double>(plan.max_speed_ms),
                 static_cast<double>(plan.geofence_m), static_cast<double>(plan.rtl_alt_m),
                 plan.act_count));
    if (n >= sizeof(tmp)) return 0;
    for (int i = 0; i < plan.act_count; ++i) {
        const ShowAct& a = plan.acts[i];
        const size_t used = static_cast<size_t>(std::snprintf(
            tmp + n, sizeof(tmp) - n, ";A,%d,%.2f,%.2f,%.2f,%.2f,%06lx",
            static_cast<int>(a.shape), static_cast<double>(a.scale_m),
            static_cast<double>(a.alt_m), static_cast<double>(a.hold_s),
            static_cast<double>(a.move_s), static_cast<unsigned long>(a.colour)));
        if (used == 0 || used >= sizeof(tmp) - n) return 0;
        n += used;
    }
    if (n + 1 > cap) return 0;                 // never hand back half a programme
    std::memcpy(buf, tmp, n);
    buf[n] = '\0';
    return n;
}

bool planParse(const char* text, ShowPlan& out) {
    if (text == nullptr) return false;
    const char* p = text;
    const size_t tag_len = std::strlen(SHOW_PLAN_TAG);
    if (std::strncmp(p, SHOW_PLAN_TAG, tag_len) != 0) return false;
    p += tag_len;
    if (!eat(p, ',')) return false;

    double v = 0.0;
    if (!readNum(p, v) || v <= 0.0 || v > SHOW_MAX_DRONES) return false;
    out.drones = static_cast<int>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.separation_m = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.max_speed_ms = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.geofence_m = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.rtl_alt_m = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v) || v <= 0.0 || v > SHOW_MAX_ACTS) return false;
    const int acts = static_cast<int>(v);

    ShowPlan parsed{};
    parsed.drones = out.drones;
    parsed.separation_m = out.separation_m;
    parsed.max_speed_ms = out.max_speed_ms;
    parsed.geofence_m = out.geofence_m;
    parsed.rtl_alt_m = out.rtl_alt_m;
    parsed.act_count = 0;
    for (int i = 0; i < acts; ++i) {
        if (!eat(p, ';')) return false;         // a missing act record
        if (!eat(p, 'A')) return false;         // a record type this build does not know
        if (!eat(p, ',')) return false;
        double s = 0.0, scale = 0.0, alt = 0.0, hold = 0.0, move = 0.0, colour = -1.0;
        if (!readNum(p, s)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, scale)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, alt)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, hold)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, move)) return false;
        if (!eat(p, ',')) return false;
        // The colour is six hex digits, which strtod would happily read "00e0a0"
        // as zero times ten to the something, so it gets its own base.
        {
            char* end = nullptr;
            colour = static_cast<double>(std::strtoul(p, &end, 16));
            if (end == p) return false;
            p = end;
        }
        if (s < 0.0 || s >= SHOW_SHAPE_COUNT) return false;
        ShowAct a{};
        a.shape = static_cast<uint8_t>(s);
        a.scale_m = static_cast<float>(scale);
        a.alt_m = static_cast<float>(alt);
        a.hold_s = static_cast<float>(hold);
        a.move_s = static_cast<float>(move);
        a.colour = static_cast<uint32_t>(colour) & 0xFFFFFFu;
        parsed.acts[parsed.act_count++] = a;
    }
    // Trailing blanks are a fact of radio life; anything else means the record was
    // cut short, and an act that did not arrive is an aircraft that flies the
    // wrong shape and never learns it did.
    while (*p == ' ' || *p == '\r' || *p == '\n') ++p;
    if (*p != '\0') return false;
    if (parsed.act_count != acts) return false;
    out = parsed;
    return true;
}

bool planBuildLane(const ShowPlan& plan, int station, ShowLane& out, ShowPoint* table) {
    if (plan.drones <= 0 || plan.drones > SHOW_MAX_DRONES) return false;
    if (plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) return false;
    if (station < 0 || station >= plan.drones) return false;
    if (table == nullptr) return false;

    // The whole compiler, not a re-derivation of part of it. Sampling and greedy
    // assignment alone reproduce the ground station's routing; the repair pass that
    // follows them is what makes the routing *certified*, and it decides per pair, so
    // an aircraft that ran the first two steps and skipped the third would fly a
    // meeting the show was cleared for. Measured on a 24-ship three-act programme: the
    // ground station signs 2.64 m, sample+assign alone gives stations 17 and 18 0.39 m.
    int swaps = 0;
    if (showCompilePlan(plan, table, plan.act_count * plan.drones, &swaps) != plan.act_count)
        return false;

    out.station = station;
    out.pad = ShowPoint{};
    out.act_count = 0;
    for (int i = 0; i < plan.act_count; ++i) {
        out.at[i] = table[i * plan.drones + station];
        out.colour[i] = plan.acts[i].colour;
        out.act_count = i + 1;
    }
    return true;
}

float planLaneOffset(const ShowLane& lane, int act, float separation_m) {
    if (act < 0 || act >= lane.act_count) return 0.0f;
    // The layer is chosen from where the aircraft is going, not from who it is:
    // two airframes headed for neighbouring cells end up in different layers, and
    // that is the only reason a head-on meeting in the middle of a transition has
    // a chance of not happening.
    return showLane(lane.at[act], separation_m);
}

uint16_t planChecksum(const char* text, size_t len) {
    uint16_t ck = 0;
    for (size_t i = 0; i < len; ++i) {
        ck = static_cast<uint16_t>(ck * 31u + static_cast<unsigned char>(text[i]));
    }
    return ck;
}

}  // namespace ranch
