// Drone light-show core: shape sampling, aircraft-to-point assignment, baked
// trajectories, the checks that decide whether a plan may fly at all, and the
// per-aircraft return slots that keep a hundred simultaneous RTLs from converging
// on one coordinate.
//
// Platform neutral and side-effect free like the rest of firmware/lib: geometry and
// a clock go in, positions and verdicts come out. The ground station decides whether
// to upload, the flight controller decides how to hold position, and neither is this
// file's business.
//
// What is deliberately copied from how the real shows are built (Intel Shooting Star,
// DJI/DXL, 高巨/一飞 and the rest all converge on the same shape):
//
//  - The choreography is compiled **offline** into one time-indexed path per
//    aircraft and uploaded before the show. In flight nobody pilots: each aircraft
//    follows its own baked path and holds position with RTK, and the ground station
//    supervises. That is why `showPath()` is a pure function of (shape A, shape B,
//    progress) and there is no "joystick" anywhere in here.
//  - A shape is a set of points; the show is a sequence of shapes. Which aircraft
//    gets which point is its own solved step (`showAssign`) -- a naive index-to-index
//    mapping makes aircraft fly across the field through each other at every change.
//  - Separation is checked on the **baked paths**, not on the shape outlines: two
//    formations that are each perfectly spaced can still cross in the middle of a
//    transition. `showValidate` samples the transitions for exactly that.
//  - Every aircraft has its own return slot, offset from the launch origin by its
//    own station number. A mass failsafe that sends 100 aircraft to one point is a
//    collision, not a recovery.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ranch {

constexpr int SHOW_MAX_DRONES = 128;      // one ground station, this build
constexpr int SHOW_MAX_ACTS = 16;
constexpr float SHOW_PI = 3.14159265358979f;

// Local NED metres, origin at the surveyed launch point, `d` positive downwards --
// the same convention MAVLink's LOCAL_POSITION_NED uses, so a baked point can be
// uploaded without a unit conversion anywhere.
struct ShowPoint {
    float n = 0.0f;
    float e = 0.0f;
    float d = 0.0f;
};

enum ShowShape : uint8_t {
    SHAPE_RING = 0,     // a circle, seen edge-on or from below
    SHAPE_GRID,         // a filled lattice: the one that reads as a wall of light
    SHAPE_HEART,        // the classic parametric heart
    SHAPE_WAVE,         // a travelling sine across the field
    SHAPE_COLUMN,       // vertical threads, one per station column
    SHAPE_ARC,          // a bow: cheap, and the one that scales to any count
    SHOW_SHAPE_COUNT
};

struct ShowAct {
    uint8_t shape = SHAPE_RING;
    float scale_m = 18.0f;        // the shape's half-extent
    float alt_m = 40.0f;          // height above the launch origin, positive up
    float hold_s = 12.0f;         // time held as a formation
    float move_s = 6.0f;          // time spent getting there from the previous act
    uint32_t colour = 0x00e0a0;   // 0xRRGGBB, the WS2812 state for the whole act
};

struct ShowPlan {
    int drones = 24;
    ShowAct acts[SHOW_MAX_ACTS];
    int act_count = 0;
    // The three numbers a show is actually judged on. All of them are the values a
    // real operator writes in a risk assessment, not tuning knobs.
    float separation_m = 2.0f;    // minimum aircraft-to-aircraft distance
    float max_speed_ms = 6.0f;    // what the airframe can hold in a 5 m/s wind
    float geofence_m = 120.0f;    // lateral limit from the surveyed origin
    float rtl_alt_m = 35.0f;      // climb/cruise height on return
};

// Why a plan may not fly. A bitmask: an operator needs every reason at once, and a
// dialog that shows one reason per press is a dialog that gets closed without the
// last one being read.
enum ShowRefusal : uint32_t {
    SHOW_OK = 0,
    SHOW_BAD_COUNT = 1u << 0,       // zero or more aircraft than this build supports
    SHOW_BAD_ACTS = 1u << 1,        // no acts, or a transition/hold that is not a time
    SHOW_SEPARATION = 1u << 2,      // two paths come closer than separation_m
    SHOW_TOO_FAST = 1u << 3,        // a baked path needs more than max_speed_ms
    SHOW_OUTSIDE = 1u << 4,         // a point or a path leaves the geofence
    SHOW_ALTITUDE = 1u << 5,        // below the grid or above the ceiling for this site
    SHOW_BATTERY = 1u << 6,         // the show plus the return does not fit the pack
};

// --- shape sampling ------------------------------------------------------------
// Fills `out` with exactly `n` points for the shape, spread so the formation does
// not bunch up when the count changes. Deterministic and order-stable: the same
// (shape, n) always produces the same cloud, which is what makes an uploaded show
// reproducible between the rehearsal and the night.
inline int showSampleShape(ShowShape s, int n, float scale_m, float alt_m,
                           ShowPoint* out, int cap) {
    if (n <= 0 || out == nullptr || cap <= 0) return 0;
    const int count = n < cap ? n : cap;
    const float d = -alt_m;                       // NED: up is negative
    switch (s) {
        case SHAPE_RING: {
            // One circle; with more than one ring the outer ones are the same shape
            // at a larger radius, which is how a "ring" formation stays legible at
            // 100 aircraft instead of becoming one fat circle.
            const int rings = count > 28 ? 3 : 1;
            int made = 0;
            for (int r = 0; r < rings && made < count; ++r) {
                // 0.4/0.7/1.0 of the requested half-extent: the outermost ring is the
                // shape's own size, not one scale-unit beyond it.
                // The outermost ring is the shape's own size and a single ring is
                // that size too -- putting the only ring at 0.4 of it packed 24
                // aircraft onto a 7 m circle, 1.9 m apart, which the density check
                // then correctly refused.
                const float radius = scale_m * (rings > 1 ? (0.4f + 0.6f * r / (rings - 1)) : 1.0f);
                const int in_ring = (count - made) / (rings - r);
                const int take = (r == rings - 1) ? (count - made) : in_ring;
                for (int i = 0; i < take; ++i) {
                    const float a = 2.0f * SHOW_PI * static_cast<float>(i) / take;
                    out[made].n = radius * std::sin(a);
                    out[made].e = radius * std::cos(a);
                    out[made].d = d;
                    ++made;
                }
            }
            return made;
        }
        case SHAPE_GRID: {
            int side = static_cast<int>(std::sqrt(static_cast<double>(count)) + 0.9999);
            if (side < 1) side = 1;
            for (int i = 0; i < count; ++i) {
                const int row = i / side;
                const int col = i % side;
                const float span = static_cast<float>(side - 1);
                out[i].n = span > 0.0f ? (-scale_m + 2.0f * scale_m * col / span) : 0.0f;
                out[i].e = span > 0.0f ? (-scale_m + 2.0f * scale_m * row / span) : 0.0f;
                out[i].d = d;
            }
            return count;
        }
        case SHAPE_HEART: {
            // The standard parametric heart, scaled to the same half-extent as the
            // other shapes so a show's geofence does not move between acts.
            for (int i = 0; i < count; ++i) {
                const float t = 2.0f * SHOW_PI * static_cast<float>(i) / count;
                const float x = 16.0f * std::pow(std::sin(t), 3.0f);
                const float y = 13.0f * std::cos(t) - 5.0f * std::cos(2 * t)
                        - 2.0f * std::cos(3 * t) - std::cos(4 * t);
                // Divided by 17, not 16: this parametrisation reaches -17 below the
                // origin and +12 above it, so the naive normalisation put a 19 m
                // point on an 18 m shape and the geofence check caught it.
                out[i].e = x * (scale_m / 17.0f);
                out[i].n = y * (scale_m / 17.0f);
                out[i].d = d;
            }
            return count;
        }
        case SHAPE_WAVE: {
            for (int i = 0; i < count; ++i) {
                const float u = count > 1 ? static_cast<float>(i) / (count - 1) : 0.5f;
                out[i].e = -scale_m + 2.0f * scale_m * u;
                out[i].n = 0.0f;
                // A wave only reads as one from the side, so the ripple is put in the
                // vertical axis rather than the horizontal one -- and the amplitude is
                // a fraction of the shape, not of the field: 0.35 put 10 m of drop
                // between neighbours 2.6 m apart, which is a vertical wall and fails
                // the separation rule on its own formation.
                out[i].d = d - 0.06f * scale_m * std::sin(6.0f * SHOW_PI * u);
            }
            return count;
        }
        case SHAPE_COLUMN: {
            const int cols = count / 4 > 1 ? count / 4 : 1;
            int made = 0;
            for (int c = 0; c < cols && made < count; ++c) {
                const float u = cols > 1 ? static_cast<float>(c) / (cols - 1) : 0.5f;
                const int per = (made == 0) ? count / cols : count / cols;
                for (int i = 0; i < per && made < count; ++i) {
                    const float v = per > 1 ? static_cast<float>(i) / (per - 1) : 0.5f;
                    out[made].e = -scale_m + 2.0f * scale_m * u;
                    out[made].n = 0.0f;
                    out[made].d = d + 0.45f * scale_m * (2.0f * v - 1.0f);
                    ++made;
                }
            }
            return made;
        }
        case SHAPE_ARC: {
            for (int i = 0; i < count; ++i) {
                const float u = count > 1 ? static_cast<float>(i) / (count - 1) : 0.5f;
                const float a = SHOW_PI * (0.15f + 0.7f * u);
                out[i].e = scale_m * std::cos(a);
                out[i].n = scale_m * std::sin(a);
                out[i].d = d;
            }
            return count;
        }
        default:
            return 0;
    }
}

// --- assignment ----------------------------------------------------------------
// Greedy nearest-neighbour matching from the previous formation to the next, in
// station order. Not optimal -- the Hungarian assignment is, and is what the real
// tools use -- but it is stable, runs in n^2 with no allocation, and removes the
// failure that actually matters: aircraft swapping sides of the field through each
// other because point 7 of shape A and point 7 of shape B are 90 m apart.
// `order[i]` is the index into `to` that station `i` should fly to.
inline void showAssign(const ShowPoint* from, const ShowPoint* to, int n, int* order) {
    if (from == nullptr || to == nullptr || order == nullptr || n <= 0) return;
    bool taken[SHOW_MAX_DRONES] = {};
    for (int i = 0; i < n; ++i) {
        float best = 1e30f;
        int at = -1;
        for (int j = 0; j < n; ++j) {
            if (taken[j]) continue;
            const float dn = to[j].n - from[i].n;
            const float de = to[j].e - from[i].e;
            const float dd = to[j].d - from[i].d;
            const float d2 = dn * dn + de * de + dd * dd;
            if (d2 < best) { best = d2; at = j; }
        }
        if (at < 0) at = i;                       // nothing left; stay put
        order[i] = at;
        taken[at] = true;
    }
}

// --- the baked path -------------------------------------------------------------
// Position along one transition at progress `u` in [0,1]. Smoothstep, because the
// aircraft has to leave and arrive at zero horizontal speed: a linear ramp would
// start the move at full speed and the first second of every transition would be a
// lurch that the position controller spends the rest of it recovering from.
inline ShowPoint showPath(const ShowPoint& a, const ShowPoint& b, float u) {
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    const float s = u * u * (3.0f - 2.0f * u);
    ShowPoint p;
    p.n = a.n + (b.n - a.n) * s;
    p.e = a.e + (b.e - a.e) * s;
    p.d = a.d + (b.d - a.d) * s;
    return p;
}

// The altitude lane one station uses while it is moving. Greedy assignment stops
// aircraft crossing the field through each other's formations, but two stations can
// still meet head-on in the middle of a transition -- measured at 0.03 m apart for a
// 24-aircraft ring-to-grid. Real shows resolve this by flying the transitions in
// stacked layers: each aircraft bulges up or down by its lane for the length of the
// move and returns to the formation height as it arrives, so `showPath` is unchanged
// at both ends.
inline float showLane(const ShowPoint& dest, float separation_m) {
    constexpr int lanes = 4;
    // A checkerboard over the destination, in cells the size of the separation rule:
    // aircraft headed for nearby points land in different lanes, which is the whole
    // point. Indexing by station number instead -- the first version -- left stations
    // 16 and 18 (adjacent cells, same-ish lane) meeting 0.8 m apart on a wave crest,
    // because a station number says nothing about where an aircraft is going.
    const int cx = static_cast<int>(std::floor(dest.n / separation_m));
    const int cz = static_cast<int>(std::floor(dest.e / separation_m));
    const int k = ((cx + cz) % lanes + lanes) % lanes;
    return (static_cast<float>(k) - (lanes - 1) * 0.5f) * separation_m * 2.0f;
}

// A transition with its lane applied: the same endpoints, a bulge in between.
inline ShowPoint showPathLane(const ShowPoint& a, const ShowPoint& b, float u, float lane_m) {
    ShowPoint p = showPath(a, b, u);
    p.d -= lane_m * std::sin(SHOW_PI * u);       // NED: up is the negative direction
    return p;
}

// Where station `i` returns to. Spread over a grid around the origin on purpose:
// with one shared RTL point, a hundred-aircraft failsafe puts a hundred airframes
// into the same column of air at the same time.
inline ShowPoint showReturnSlot(const ShowPlan& plan, int i) {
    const int side = static_cast<int>(std::sqrt(static_cast<double>(plan.drones > 0 ? plan.drones : 1)) + 0.9999);
    const int row = i / side;
    const int col = i % side;
    const float step = 8.0f;                      // wider than separation_m
    ShowPoint p;
    p.n = (static_cast<float>(col) - (side - 1) * 0.5f) * step;
    p.e = (static_cast<float>(row) - (side - 1) * 0.5f) * step;
    p.d = -plan.rtl_alt_m;
    return p;
}

// Total show time, transitions included: the number the battery check is against.
inline float showDuration(const ShowPlan& plan) {
    float t = 0.0f;
    for (int i = 0; i < plan.act_count && i < SHOW_MAX_ACTS; ++i) {
        t += plan.acts[i].move_s + plan.acts[i].hold_s;
    }
    return t;
}

// --- deconfliction ------------------------------------------------------------
// Greedy assignment plus altitude lanes still leaves a pair of stations meeting
// head-on inside a transition: measured, stations 16 and 18 passed 0.03 m apart on a
// 24-aircraft wave. Real show compilers resolve that with a repair pass -- swap the
// two aircraft's destinations and keep the swap if the show gets less tight. The
// formations are unchanged by a swap (each cloud is a permutation of the same set of
// points), so the only thing that moves is who flies where.
constexpr int SHOW_SAMPLES = 32;

// Clearance between two stations across one transition, sampled.
inline float showPairClearance(const ShowPoint& aFrom, const ShowPoint& aTo,
                               const ShowPoint& bFrom, const ShowPoint& bTo,
                               float aLane, float bLane) {
    float best = 1e30f;
    for (int s = 0; s < SHOW_SAMPLES; ++s) {
        const float u = static_cast<float>(s) / (SHOW_SAMPLES - 1);
        const ShowPoint pa = showPathLane(aFrom, aTo, u, aLane);
        const ShowPoint pb = showPathLane(bFrom, bTo, u, bLane);
        const float dn = pa.n - pb.n, de = pa.e - pb.e, dd = pa.d - pb.d;
        const float d = std::sqrt(dn * dn + de * de + dd * dd);
        if (d < best) best = d;
    }
    return best;
}

// The clearance of the tightest other aircraft against one station, over every
// transition. Recomputing only this -- instead of the whole show's worst pair -- is
// what keeps the repair pass usable at 128 aircraft.
inline float showStationWorst(const ShowPoint* pos, int actCount, int n, int k, float separation) {
    float worst = 1e30f;
    for (int i = 0; i + 1 < actCount; ++i) {
        for (int o = 0; o < n; ++o) {
            if (o == k) continue;
            const float d = showPairClearance(pos[i * n + k], pos[(i + 1) * n + k],
                                              pos[i * n + o], pos[(i + 1) * n + o],
                                              showLane(pos[(i + 1) * n + k], separation),
                                              showLane(pos[(i + 1) * n + o], separation));
            if (d < worst) worst = d;
        }
    }
    return worst;
}

// The show's single tightest pair.
//
// The pair is named with a one-millimetre dead band: two candidates that differ by
// less than that are the same problem, and the rule picks the one that comes first in
// station order so that this file and the JavaScript mirror name the *same* pair. A
// bare `d < worst` let float rounding (ulp 3.8e-6 at 40 m) pick a different winner than
// the double arithmetic does, and the repair pass then made different swaps on the two
// sides. The reported clearance is still the true minimum.
inline float showWorstPair(const ShowPoint* pos, int actCount, int n, float separation,
                           int& atAct, int& outA, int& outB) {
    constexpr float TIE_BAND_M = 0.001f;
    float worst = 1e30f;
    atAct = -1; outA = -1; outB = -1;
    for (int i = 0; i + 1 < actCount; ++i) {
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float d = showPairClearance(pos[i * n + a], pos[(i + 1) * n + a],
                                                  pos[i * n + b], pos[(i + 1) * n + b],
                                                  showLane(pos[(i + 1) * n + a], separation),
                                                  showLane(pos[(i + 1) * n + b], separation));
                if (d < worst - TIE_BAND_M) {
                    worst = d; atAct = i; outA = a; outB = b;
                } else if (d < worst) {
                    worst = d;
                }
            }
        }
    }
    return worst;
}

// Repair the compiled show in place; returns how many swaps it took. Stops when the
// show is clear, when a pass cannot improve it, or after `maxPasses`.
inline int showDeconflict(ShowPoint* pos, int actCount, int n, float separation, int maxPasses) {
    // No swap can get under the spacing inside a formation itself: an act is a
    // permutation of the same cloud whoever flies it, so its tightest pair is a
    // property of the shape and the count, not of the routing. When that floor is
    // already under the rule the show is going to be refused for density whatever the
    // repair does -- measured on a 100-aircraft plan that is simply too dense, running
    // the pass anyway burned the whole budget (400 passes, ~0.8 s on the ground station,
    // on every keystroke) and refused all the same.
    float staticWorst = 1e30f;
    for (int i = 0; i < actCount; ++i) {
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float dn = pos[i * n + a].n - pos[i * n + b].n;
                const float de = pos[i * n + a].e - pos[i * n + b].e;
                const float dd = pos[i * n + a].d - pos[i * n + b].d;
                const float d = std::sqrt(dn * dn + de * de + dd * dd);
                if (d < staticWorst) staticWorst = d;
            }
        }
    }
    if (staticWorst < separation) return 0;

    int swaps = 0;
    for (int pass = 0; pass < maxPasses; ++pass) {
        int i = -1, a = -1, b = -1;
        const float worst = showWorstPair(pos, actCount, n, separation, i, a, b);
        if (i < 0 || worst >= separation) break;

        const float beforeB = showStationWorst(pos, actCount, n, b, separation);
        bool repaired = false;
        // Try handing b's destination to someone else. a is tried first (the direct
        // swap), then every other station, and a swap is kept only if the tightest
        // thing about *both* aircraft involved gets better -- otherwise the repair
        // just moves the collision somewhere else and the next pass undoes it.
        for (int c = 0; c < n && !repaired; ++c) {
            if (c == b) continue;
            const float beforeC = showStationWorst(pos, actCount, n, c, separation);
            const ShowPoint tmp = pos[(i + 1) * n + b];
            pos[(i + 1) * n + b] = pos[(i + 1) * n + c];
            pos[(i + 1) * n + c] = tmp;
            const float afterB = showStationWorst(pos, actCount, n, b, separation);
            const float afterC = showStationWorst(pos, actCount, n, c, separation);
            if (afterB > worst + 0.001f && afterC > worst + 0.001f
                && afterB > beforeB - 0.001f && afterC >= beforeC - 0.001f) {
                ++swaps;
                repaired = true;
                break;
            }
            pos[(i + 1) * n + c] = pos[(i + 1) * n + b];
            pos[(i + 1) * n + b] = tmp;
        }
        if (!repaired) break;
    }
    return swaps;
}

// Sample, assign, repair: the compiled station-ordered positions of every act, laid
// out act-major in `pos` (which must hold actCount * drones points), plus the number
// of swaps the repair pass needed. This is the function both the go/no-go decision
// and the aircraft read from, so a show is judged on exactly the paths it will fly.
inline int showCompilePlan(const ShowPlan& plan, ShowPoint* pos, int cap, int* swaps) {
    const int n = plan.drones;
    if (n <= 0 || n > SHOW_MAX_DRONES || plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) return -1;
    if (cap < plan.act_count * n) return -1;
    ShowPoint cloud[SHOW_MAX_DRONES];
    for (int i = 0; i < plan.act_count; ++i) {
        const ShowAct& a = plan.acts[i];
        if (showSampleShape(static_cast<ShowShape>(a.shape), n, a.scale_m, a.alt_m, cloud, SHOW_MAX_DRONES) != n)
            return -1;
        for (int k = 0; k < n; ++k) pos[i * n + k] = cloud[k];
        if (i > 0) {
            int order[SHOW_MAX_DRONES];
            showAssign(pos + (i - 1) * n, pos + i * n, n, order);
            ShowPoint held[SHOW_MAX_DRONES];
            for (int k = 0; k < n; ++k) held[k] = pos[i * n + order[k]];
            for (int k = 0; k < n; ++k) pos[i * n + k] = held[k];
        }
    }
    const int made = showDeconflict(pos, plan.act_count, n, plan.separation_m, 4 * n);
    if (swaps) *swaps = made;
    return plan.act_count;
}

// Everything that has to be true before an upload is worth pressing.
inline uint32_t showValidate(const ShowPlan& plan, float batt_pct, float wind_ms) {
    uint32_t why = SHOW_OK;
    if (plan.drones <= 0 || plan.drones > SHOW_MAX_DRONES) why |= SHOW_BAD_COUNT;
    if (plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) why |= SHOW_BAD_ACTS;
    for (int i = 0; i < plan.act_count; ++i) {
        const ShowAct& a = plan.acts[i];
        if (a.hold_s <= 0.0f || a.move_s <= 0.0f || a.scale_m <= 0.0f) why |= SHOW_BAD_ACTS;
        if (a.alt_m < 10.0f || a.alt_m > 120.0f) why |= SHOW_ALTITUDE;
    }
    if (why != SHOW_OK) return why;

    // Sample, assign, repair -- all of it inside showCompilePlan, so the verdict and
    // the aircraft read the same paths. The buffer is 128 points per act and lives on
    // the caller's stack: this runs on the ground station, and an aircraft is only
    // ever told which point is its own.
    //
    // Not static, and that is load-bearing: with these arrays static, a shape that
    // forgot to write one field inherited the previous plan's value and the altitude
    // check passed on a wave sitting at ground level.
    ShowPoint pos[SHOW_MAX_ACTS * SHOW_MAX_DRONES];
    int swaps = 0;
    if (showCompilePlan(plan, pos, SHOW_MAX_ACTS * SHOW_MAX_DRONES, &swaps) < 0) {
        why |= SHOW_BAD_COUNT;
        return why;
    }
    const int n = plan.drones;

    // The geofence and the altitude ceiling are checked on the paths, not just the
    // endpoints: a straight line between two points that are both inside can still
    // bulge outside, and a lane bulge can lift an aircraft over the ceiling.
    for (int i = 0; i < plan.act_count; ++i) {
        for (int sm = 0; sm < SHOW_SAMPLES; ++sm) {
            for (int k = 0; k < n; ++k) {
                ShowPoint q = pos[i * n + k];
                if (i + 1 < plan.act_count) {
                    q = showPathLane(pos[i * n + k], pos[(i + 1) * n + k],
                                     static_cast<float>(sm) / (SHOW_SAMPLES - 1),
                                     showLane(pos[(i + 1) * n + k], plan.separation_m));
                }
                if (std::sqrt(q.n * q.n + q.e * q.e) > plan.geofence_m) why |= SHOW_OUTSIDE;
                if (-q.d < 5.0f || -q.d > 120.0f) why |= SHOW_ALTITUDE;
            }
        }
    }

    // Speed: the distance a station covers during its transition divided by the
    // transition time, at the steepest part of the smoothstep (1.5x the average, the
    // derivative of u^2(3-2u) at u = 0.5). Wind is added rather than multiplied: the
    // airframe has to hold a ground point, so the worst-case airspeed is the path
    // speed plus the wind.
    for (int i = 0; i + 1 < plan.act_count; ++i) {
        const float dt = plan.acts[i + 1].move_s;
        for (int k = 0; k < n; ++k) {
            const float dn = pos[(i + 1) * n + k].n - pos[i * n + k].n;
            const float de = pos[(i + 1) * n + k].e - pos[i * n + k].e;
            const float dd = pos[(i + 1) * n + k].d - pos[i * n + k].d;
            const float v = 1.5f * std::sqrt(dn * dn + de * de + dd * dd) / dt;
            if (v + wind_ms > plan.max_speed_ms) why |= SHOW_TOO_FAST;
        }
    }

    // Separation inside each formation: a shape that is too dense for the count is
    // unsafe while it is simply sitting there, and no swap can fix that -- the cloud
    // is the same set of points whoever flies it.
    for (int i = 0; i < plan.act_count; ++i) {
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float dn = pos[i * n + a].n - pos[i * n + b].n;
                const float de = pos[i * n + a].e - pos[i * n + b].e;
                const float dd = pos[i * n + a].d - pos[i * n + b].d;
                if (std::sqrt(dn * dn + de * de + dd * dd) < plan.separation_m) why |= SHOW_SEPARATION;
            }
        }
    }

    // Separation on the repaired paths. This is the number the editor shows next to
    // the verdict, because "it passes" is less useful than "it passes with 2.4 m to
    // spare on a 2 m rule".
    int wi = 0, wa = 0, wb = 0;
    const float tightest = showWorstPair(pos, plan.act_count, n, plan.separation_m, wi, wa, wb);
    if (tightest < plan.separation_m) why |= SHOW_SEPARATION;

    // Endurance: the show, plus the return at cruise speed, plus a two-minute hold
    // on the pad for the crew. Packs are sized in minutes of hover; anything that
    // lands with less than that reserve is a show that ends in a swim.
    const float need_s = showDuration(plan) + 90.0f + 120.0f;
    constexpr float PACK_HOVER_S = 22.0f * 60.0f;      // the pack on this airframe
    if (batt_pct * PACK_HOVER_S < need_s * 100.0f) why |= SHOW_BATTERY;
    return why;
}

}  // namespace ranch
