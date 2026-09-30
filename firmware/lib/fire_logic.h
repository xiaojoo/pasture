// Fire panel logic: supervised detector loops, the alarm decision, and the two
// things a panel must never let an operator do by accident -- silence a bell that
// has a reason to ring, and start the sprinkler pump on one detector's opinion.
//
// Platform neutral and side-effect free like the rest of firmware/lib: loop
// milliVolts and contact states go in, one level plus one reason comes out, and
// the caller decides which relay that closes.
//
// Two decisions here are deliberate and are the ones a maintenance technician will
// question:
//
//  - a single zone alarms the building but does NOT start the pump. The pump needs
//    a second, independent confirmation (another zone, a manual call point, water
//    already moving, or twenty seconds of the first alarm persisting). One detector
//    cooking dust should not flood a barn; twenty seconds of the same detector
//    saying "fire" should.
//  - silence is refused while the cause is still present, and a silence that expires
//    while the alarm is still latched re-sounds. A bell that can be silenced until
//    the next alarm is not a bell.
#pragma once

#include <cstdint>
#include <cstring>

namespace ranch {

constexpr uint8_t FIRE_ZONES = 4;      // house, barn, store room, switch room

// --- the supervised loop ------------------------------------------------------
// One analog input per zone, a 4k7 pull-up to 3V3, and an end-of-line resistor in
// the last device. The loop resistance therefore appears as four distinct bands:
//
//   dead short across the loop      0 ohm    ~0.00 V   a shorted run, not a fire
//   detector relay closed        1 kohm    ~0.58 V   ALARM
//   end of line in circuit       4.7 kohm  ~1.65 V   healthy
//   wire cut / tamper open          inf    ~3.30 V   SUPERVISION
//
// The gaps between the bands are Unknown rather than a nearest-guess: a loop
// sitting at 1.0 V is dirty, corroded or mid-transition, and a panel that picked a
// side for it would be reporting something it did not measure.
struct LoopBands {
    uint16_t short_hi_mv;
    uint16_t alarm_lo_mv, alarm_hi_mv;
    uint16_t normal_lo_mv, normal_hi_mv;
    uint16_t open_lo_mv;
};

constexpr LoopBands LOOP_DEFAULT_BANDS{
    /*short_hi*/  300,
    /*alarm_lo*/  400,   /*alarm_hi*/  760,
    /*normal_lo*/ 1350,  /*normal_hi*/ 2000,
    /*open_lo*/   2900,
};

enum class LoopState : uint8_t {
    Unknown,
    Normal,
    Alarm,           // relay closed the 1k path
    Open,            // cut or tamper: the loop is no longer watching anything
    Shorted,         // 0 ohm: a wiring fault that looks like an alarm and must be named
};

inline const char* loopStateName(LoopState s) {
    switch (s) {
        case LoopState::Normal:  return "ok";
        case LoopState::Alarm:   return "alarm";
        case LoopState::Open:    return "open";
        case LoopState::Shorted: return "short";
        case LoopState::Unknown: return "dirty";
    }
    return "?";
}

inline LoopState loopClassify(uint16_t mv, const LoopBands& b) {
    if (mv < b.short_hi_mv) return LoopState::Shorted;
    if (mv >= b.alarm_lo_mv && mv <= b.alarm_hi_mv) return LoopState::Alarm;
    if (mv >= b.normal_lo_mv && mv <= b.normal_hi_mv) return LoopState::Normal;
    if (mv >= b.open_lo_mv) return LoopState::Open;
    return LoopState::Unknown;
}

// Relay contacts bounce and a loop being re-terminated wiggles through the bands.
// A state is only accepted once it has been seen for `hold` consecutive samples,
// and the hold is counted per zone rather than globally so one chattering loop
// cannot delay the reading of the other three.
constexpr uint8_t LOOP_HOLD_SAMPLES = 3;

struct LoopTracker {
    LoopState state;
    LoopState candidate;
    uint8_t count;
};

inline void loopTrackerReset(LoopTracker& t) {
    t.state = LoopState::Unknown;
    t.candidate = LoopState::Unknown;
    t.count = 0;
}

inline LoopState loopTrack(LoopTracker& t, LoopState sample, uint8_t hold = LOOP_HOLD_SAMPLES) {
    if (sample == t.state) {
        t.candidate = sample;
        t.count = 0;
        return t.state;
    }
    if (sample != t.candidate) {
        t.candidate = sample;
        t.count = 1;
        return t.state;
    }
    t.count++;
    if (t.count >= hold) {
        t.state = sample;
        t.count = 0;
    }
    return t.state;
}

// --- the panel decision ------------------------------------------------------
enum class FireLevel : uint8_t { Normal, Fault, Alarm, Emergency };

inline const char* fireLevelName(FireLevel l) {
    switch (l) {
        case FireLevel::Normal:    return "NORMAL";
        case FireLevel::Fault:     return "FAULT";
        case FireLevel::Alarm:     return "ALARM";
        case FireLevel::Emergency: return "EMERGENCY";
    }
    return "?";
}

struct FireLimits {
    uint16_t confirm_s;          // an unconfirmed alarm escalates to pump-worthy after this
    uint16_t silence_s;          // how long a silenced bell stays quiet
    uint16_t silence_max;        // silences allowed before the panel refuses more
    uint16_t test_period_s;      // how often the panel proves it can make noise
    uint16_t test_ms;            // and for how long it pulses the siren
    uint16_t siren_proof_ms;     // within this the armature relay must prove it sounded
    uint8_t fault_zones_max;     // that many loops out of service is an outage of its own
};

constexpr FireLimits FIRE_DEFAULTS{
    /*confirm_s*/      20,
    /*silence_s*/     120,
    /*silence_max*/     3,
    /*test_period_s*/ 3600,
    /*test_ms*/         200,
    /*siren_proof_ms*/  150,
    /*fault_zones_max*/ 2,
};

struct FireInput {
    LoopState zone[FIRE_ZONES];
    bool manual_call;        // the break-glass is its own supervised loop
    bool flow;               // sprinkler/wet-pipe flow switch: water is already moving
    bool key_armed;
    bool siren_feedback;     // armature relay auxiliary
    bool silence_edge;       // true on the tick the button was pressed
    bool test_edge;          // ditto for the test button
    bool clock_valid;
};

struct FireState {
    float alarm_s;           // how long the panel has been in alarm
    float silence_left_s;
    float test_left_s;
    float next_test_s;
    float unfed_s;           // how long a commanded bell has gone without proof
    bool siren_cmd;          // the previous tick's bell command, for that proof
    uint8_t silences;
    bool latched;            // an alarm stays until reset even if the loop goes back
    bool siren_failed;
    bool reset_edge;         // the panel's own RESET, from the command channel
    uint16_t alarm_zones;    // bit set per zone that has ever alarmed
    uint16_t fault_zones;
};

struct FireDecision {
    FireLevel level;
    bool siren;
    bool strobe;
    bool pump_permit;
    bool dial;               // the uplink should be shouting
    bool silenced;
    bool bypassed;           // key switch off: indications live, outputs dead
    bool supervision;        // at least one loop is not watching anything
    uint16_t zone_bits;
    char reason[20];
};

inline void fireReset(FireState& s) {
    s.alarm_s = 0.0f;
    s.silence_left_s = 0.0f;
    s.test_left_s = 0.0f;
    s.next_test_s = FIRE_DEFAULTS.test_period_s;
    s.unfed_s = 0.0f;
    s.siren_cmd = false;
    s.silences = 0;
    s.latched = false;
    s.siren_failed = false;
    s.reset_edge = false;
    s.alarm_zones = 0;
    s.fault_zones = 0;
}

inline void fireReason(char (&dst)[20], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 19; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline uint8_t fireZoneCount(uint16_t bits) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) if (bits & (1u << i)) n++;
    return n;
}

inline bool fireSilenceAllowed(const FireLimits& lim, const FireInput& in, const FireState& st) {
    // Silence quiets the announcement, never the reason, and two things make it
    // safe to offer it during an alarm: it expires while the cause is still latched
    // (the bell comes back by itself), and a manual call point cannot be silenced at
    // all -- a broken glass is a person asking for attention, and hanging up on them
    // from the control room is not this panel's call to make.
    if (in.manual_call) return false;
    if (st.silences >= lim.silence_max) return false;
    return true;
}

inline FireDecision fireEvaluate(const FireLimits& lim, FireState& st,
                                 const FireInput& in, float dt_s) {
    FireDecision out{FireLevel::Normal, false, false, false, false, false, false, false, 0, ""};
    fireReason(out.reason, "");

    uint16_t bits = 0;
    uint16_t faults = 0;
    uint8_t unknown = 0;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) {
        switch (in.zone[i]) {
            case LoopState::Alarm:   bits |= static_cast<uint16_t>(1u << i); break;
            case LoopState::Open:
            case LoopState::Shorted: faults |= static_cast<uint16_t>(1u << i); break;
            case LoopState::Unknown: unknown++; break;
            case LoopState::Normal:  break;
        }
    }

    // The latched alarm is what the building heard. A loop coming back to normal
    // does not un-ring that; `reset` is the only thing that does. So reset is gated
    // on the *cause*, never on the latch -- gating on the latch would make reset
    // unable to do the one thing it exists for. A reset with smoke still present is
    // refused rather than performed and immediately re-asserted, because a panel that
    // cleared on a burning zone would look safe for exactly one frame, and that is
    // the frame nobody is looking at.
    const bool manual = in.manual_call;
    if (bits || manual) {
        st.latched = true;
        st.alarm_zones |= bits;
    }
    st.fault_zones |= faults;
    const bool latched_alarm = st.latched;

    const bool cause_present = bits != 0 || manual;
    const bool reset_ok = st.reset_edge && !cause_present && faults == 0 && unknown == 0;
    if (st.reset_edge && !reset_ok && out.reason[0] == '\0') {
        fireReason(out.reason, "RESET BLOCKED");
    }
    st.reset_edge = false;
    if (reset_ok) {
        fireReset(st);
        out.level = FireLevel::Normal;
        fireReason(out.reason, "RESET");
        return out;
    }

    if (latched_alarm) st.alarm_s += dt_s;
    else st.alarm_s = 0.0f;

    if (st.silence_left_s > 0.0f) {
        st.silence_left_s -= dt_s;
        if (st.silence_left_s < 0.0f) st.silence_left_s = 0.0f;
    }
    if (st.test_left_s > 0.0f) {
        st.test_left_s -= dt_s;
        if (st.test_left_s < 0.0f) st.test_left_s = 0.0f;
    }

    // The periodic proof that the panel can still make noise. Not a nicety: what
    // every silent-fire investigation has in common is that nobody looked at the
    // bell between the week it worked and the night it was needed.
    st.next_test_s -= dt_s;
    if (in.test_edge || st.next_test_s <= 0.0f) {
        st.test_left_s = lim.test_ms / 1000.0f;
        st.next_test_s = lim.test_period_s;
        if (in.test_edge && out.reason[0] == '\0') fireReason(out.reason, "MANUAL TEST");
    }
    const bool testing = st.test_left_s > 0.0f;

    if (in.silence_edge) {
        if (fireSilenceAllowed(lim, in, st)) {
            st.silence_left_s = lim.silence_s;
            st.silences++;
        } else if (out.reason[0] == '\0') {
            fireReason(out.reason, "SILENCE REFUSED");
        }
    }
    const bool silenced = st.silence_left_s > 0.0f;

    // A pump is permitted on a second opinion, never on one detector's. The four
    // independent opinions available here are: two zones, a person who broke the
    // glass, water that is already moving, and one zone that has not stopped
    // alarming for `confirm_s`. The last is deliberately time rather than another
    // device -- the ranch has one detector per building, and a barn that has been
    // saying "fire" for twenty seconds should not need a second one to agree.
    const bool confirmed = fireZoneCount(bits) >= 2 || manual || in.flow ||
                           (bits != 0 && st.alarm_s >= lim.confirm_s);

    // Two or more loops out of service means the buildings behind them are no
    // The bell is proven only when it was commanded on the previous tick and the
    // armature relay's auxiliary did not close. Judged against the previous command
    // because a relay that chatters open again after closing has to be caught, not
    // excused by this tick's own optimistic reading of the coil.
    if (st.siren_cmd && !in.siren_feedback) {
        st.unfed_s += dt_s;
        if (st.unfed_s > lim.siren_proof_ms / 1000.0f) st.siren_failed = true;
    } else {
        st.unfed_s = 0.0f;
    }

    // longer watched, which is reportable whether or not anything is burning.
    const bool supervision = faults != 0 || unknown > 0 || st.siren_failed;
    const bool outage = fireZoneCount(st.fault_zones) >= lim.fault_zones_max;
    const bool bypassed = !in.key_armed;

    out.zone_bits = bits;
    out.bypassed = bypassed;
    out.silenced = silenced;
    out.supervision = supervision || outage;
    out.dial = latched_alarm || out.supervision || bypassed;
    out.pump_permit = latched_alarm && confirmed && !bypassed;
    out.siren = (latched_alarm || testing) && !silenced && !bypassed;
    // Silence is an audible courtesy only. The strobe and the uplink keep going,
    // because whoever quieted the bell in the control room still has to be told the
    // barn is alight.
    out.strobe = (latched_alarm || testing) && !bypassed;
    st.siren_cmd = out.siren;

    if (latched_alarm && confirmed) out.level = FireLevel::Emergency;
    else if (latched_alarm) out.level = FireLevel::Alarm;
    else if (bypassed || out.supervision) out.level = FireLevel::Fault;
    else out.level = FireLevel::Normal;

    if (out.reason[0] == '\0') {
        if (latched_alarm) fireReason(out.reason, confirmed ? "CONFIRMED FIRE" : "SINGLE ZONE");
        else if (bypassed) fireReason(out.reason, "KEY BYPASSED");
        else if (st.siren_failed) fireReason(out.reason, "SIREN FAILED");
        else if (faults) fireReason(out.reason, "LOOP FAULT");
        else if (unknown) fireReason(out.reason, "LOOP DIRTY");
    }
    return out;
}

}  // namespace ranch
