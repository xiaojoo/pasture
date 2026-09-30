// Time-of-day dispense programme. Platform neutral and side-effect free: it
// turns a wall clock and an elapsed tick into "start a run", "still running",
// "end the run", plus the countdown the dashboard shows.
//
// Cycles are anchored to local midnight rather than to boot time, so a board that
// reboots at 03:00 does not hand the trough water at 04:00 forever afterwards.
#pragma once

#include <cstdint>

namespace ranch {

constexpr uint32_t DAY_SECONDS = 86400u;
constexpr uint32_t NO_DUE = 0xFFFFFFFFu;

struct Programme {
    uint32_t period_s;      // dispense every period_s of local time
    uint32_t run_s;         // and hold the valve open for this long
    uint32_t phase_s;       // offset of the first cycle after midnight
    bool enabled;
};

struct ProgrammeState {
    uint64_t next_due;      // absolute local seconds
    uint64_t started_at;    // absolute local seconds of the run in progress
    bool running;
    uint32_t runs;
    uint32_t skipped;       // cycles refused by the plant (no pressure, leak...)
    uint32_t exercise;      // anti-stagnation runs
};

enum class Dispense : uint8_t { None, Start, Running, Finish };

inline uint64_t programmeClock(uint32_t day, uint32_t second_of_day) {
    return static_cast<uint64_t>(day) * DAY_SECONDS + second_of_day;
}

// Aligns the programme to the clock it was given. Called after a time sync, when
// the board wakes from deep sleep, and after any clock correction: without it the
// next cycle can be 23 hours away because the old anchor was a different day.
inline void programmeAnchor(const Programme& p, ProgrammeState& s, uint32_t day,
                            uint32_t second_of_day) {
    const uint64_t t = programmeClock(day, second_of_day);
    const uint64_t period = p.period_s ? p.period_s : DAY_SECONDS;
    const uint64_t phase = p.phase_s;
    // The first cycle strictly after the clock we were handed. A board that wakes
    // at 03:00 must not then dispense at 04:00 for the rest of its life because
    // the anchor came from boot time instead of from the wall clock.
    uint64_t next = phase;
    if (t >= phase) {
        const uint64_t n = (t - phase) / period + 1;
        next = phase + n * period;
    }
    s.next_due = next;
    if (!s.running) s.started_at = 0;
}

inline void programmeInit(ProgrammeState& s) {
    s.next_due = NO_DUE;
    s.started_at = 0;
    s.running = false;
    s.runs = 0;
    s.skipped = 0;
    s.exercise = 0;
}

// `allowed` is the plant's veto: the programme decides when water *should* flow
// and the safety layer decides whether it may. Returning Start while allowed is
// false would count a skipped cycle and try again on the next tick, which is both
// a lie in the log and a pump cycling every 50 ms.
inline Dispense programmeTick(const Programme& p, ProgrammeState& s, uint32_t day,
                              uint32_t second_of_day, bool allowed, uint32_t& next_in_s) {
    next_in_s = 0;
    if (!p.enabled || s.next_due == NO_DUE) return Dispense::None;
    const uint64_t t = programmeClock(day, second_of_day);

    if (s.running) {
        if (t >= s.started_at + p.run_s) {
            s.running = false;
            return Dispense::Finish;
        }
        next_in_s = 0;
        return Dispense::Running;
    }

    if (t >= s.next_due) {
        if (!allowed) {
            // Drop this cycle rather than queueing it: a missed dispense is not
            // two dispenses worth of water an hour later.
            s.skipped++;
            s.next_due += p.period_s ? p.period_s : DAY_SECONDS;
            if (t > s.next_due) programmeAnchor(p, s, day, second_of_day);
            return Dispense::None;
        }
        s.running = true;
        s.started_at = t;
        s.runs++;
        s.next_due += p.period_s ? p.period_s : DAY_SECONDS;
        return Dispense::Start;
    }

    const uint64_t left = s.next_due > t ? s.next_due - t : 0;
    next_in_s = left > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(left);
    return Dispense::None;
}

// Seconds until the next scheduled cycle, without touching any state. The
// dashboard shows this every frame, and a read path that called programmeTick
// would count a skipped cycle for every line it printed.
inline uint32_t programmeCountdown(const Programme& p, const ProgrammeState& s,
                                   uint32_t day, uint32_t second_of_day) {
    if (!p.enabled || s.next_due == NO_DUE) return 0;
    const uint64_t t = programmeClock(day, second_of_day);
    if (s.running) return 0;
    if (t >= s.next_due) return 0;
    const uint64_t left = s.next_due - t;
    return left > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(left);
}

// A line nobody has run in a week grows biofilm and, in a frost, freezes. The
// exercise window is deliberately narrow: it is meant to move water, not to
// empty the tank.
inline bool programmeNeedsExercise(uint32_t days_since_last_run, uint32_t after_days,
                                   uint32_t window_of_day, uint32_t exercise_window) {
    if (days_since_last_run < after_days) return false;
    return window_of_day < exercise_window;
}

}  // namespace ranch
