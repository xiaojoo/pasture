// GENERATED FILE - do not edit, edit the project and re-run:
//   node tools/bundle.mjs
// Source: firmware/water + firmware/lib  (15 files, 91.1 KB before bundling)
// The simulated plumbing in hal_sim.cpp is what the valves and meters are wired to here.
//
// Build the same code for hardware with:  pio run -d firmware/water

#define RANCH_SIM 1

/* ==================== src/board.h ==================== */

// Pin map and plant configuration for the ranch water distribution board.
//
// Two lines, one board: the barn dispenses on a programme, the main house line
// is independently valved so a barn cycle can never drain the house. Every
// number that describes the *plumbing* (inrush window, dry-run timeout, tank
// hysteresis) is here, so a site visit that changes a pipe changes one file.
//
// Allocation rules, proven by the static_asserts at the bottom:
//   1. no GPIO appears twice, across inputs, outputs and analog inputs together
//   2. nothing sits on the flash pins (6-11) or flash-PSRAM pins (16,17), and no
//      output is on the input-only pins (34-36,39)
//   3. both analog inputs are on ADC1: ADC2 cannot be read while WiFi is
//      associated, which is most of the life of this board

#include <cstddef>

// --- valves and pump --------------------------------------------------------
#define PIN_VALVE_BARN      27      // 24 V solenoid through a MOSFET
#define PIN_VALVE_HOUSE     26      // main house line, independently valved
#define PIN_PUMP            25      // pressuriser, through the contactor driver
#define PIN_LED_STATUS       2      // the dev kit's own LED, so a boot is visible

// --- metering ---------------------------------------------------------------
// One pulse meter per line: a barn cycle that also moves the house totaliser is
// the fault this board is here to catch, and one shared meter cannot see it.
#define PIN_FLOW_BARN        4      // YF-S201, 7.5 Hz per L/min
#define PIN_FLOW_HOUSE      13
#define PIN_PRESSURE        36      // ADC1_CH0, 0.5-4.5 V for 0-10 bar
#define PIN_LEVEL           39      // ADC1_CH3 (VN), resistive float in the tank

// --- switch inputs (input-only pins, which is all they can be) --------------
// GPIO 34-39 have no internal pull-up and no output driver, so both of these
// must come from a sensor that drives its own line (the rope sensor and the tank
// float are both active module outputs). A bare reed switch here needs an
// external pull-up.
#define PIN_LEAK            35      // rope sensor in the pump house, wet = low
#define PIN_TANK_HIGH       34      // tank full: stop the fill, do not overflow

// --- bus and watchdog -------------------------------------------------------
#define PIN_RTC_SDA         21      // DS3231; keeps the programme through a power cut
#define PIN_RTC_SCL         22
#define PIN_WDT_FEED        18      // external watchdog, must toggle >= 10 Hz

// --- electrical characteristics ---------------------------------------------
#ifndef FLOW_PULSES_PER_LITRE
#define FLOW_PULSES_PER_LITRE 450.0f      // YF-S201: 7.5 Hz per L/min
#endif
#ifndef PRESSURE_MV_AT_ZERO
#define PRESSURE_MV_AT_ZERO   500.0f
#endif
#ifndef PRESSURE_MV_PER_BAR
#define PRESSURE_MV_PER_BAR   400.0f      // 0.5-4.5 V over 0-10 bar
#endif
#ifndef LEVEL_MV_EMPTY
#define LEVEL_MV_EMPTY        900.0f
#endif
#ifndef LEVEL_MV_FULL
#define LEVEL_MV_FULL         3100.0f
#endif

// Solenoid drive: full duty for the inrush window, then pulse-width held down.
// A coil kept at 100 % after pulling in just heats itself and its driver.
#ifndef VALVE_INRUSH_MS
#define VALVE_INRUSH_MS       120
#endif
#ifndef VALVE_HOLD_DUTY
#define VALVE_HOLD_DUTY       0.55f
#endif
#ifndef VALVE_HOLD_HZ
#define VALVE_HOLD_HZ         200
#endif

// --- plant limits -----------------------------------------------------------
#ifndef DRY_RUN_BAR
#define DRY_RUN_BAR           0.30f   // pump running below this is running dry
#endif
#ifndef DRY_RUN_S
#define DRY_RUN_S             2.0f
#endif
#ifndef NO_FLOW_S
#define NO_FLOW_S             6.0f    // valve open, nothing moving: stuck or empty
#endif
#ifndef VALVE_MAX_OPEN_S
#define VALVE_MAX_OPEN_S      900.0f  // a programme may not hold a valve open forever
#endif
#ifndef LEAK_FLOW_LMIN
#define LEAK_FLOW_LMIN        0.35f   // any flow with every valve closed
#endif
#ifndef TANK_REFILL_START_PCT
#define TANK_REFILL_START_PCT 22.0f
#endif
#ifndef TANK_REFILL_STOP_PCT
#define TANK_REFILL_STOP_PCT  96.0f
#endif
#ifndef STAGNATION_DAYS
#define STAGNATION_DAYS       7       // exercise a line that has not run
#endif
#ifndef STAGNATION_RUN_S
#define STAGNATION_RUN_S      20
#endif

// --- programme defaults (overridable from NVS) ------------------------------
#ifndef BARN_PERIOD_S
#define BARN_PERIOD_S         3600    // hourly dispense at the trough
#endif
#ifndef BARN_RUN_S
#define BARN_RUN_S            240
#endif
#ifndef HOUSE_DEFAULT_OPEN
#define HOUSE_DEFAULT_OPEN    1       // the house line runs unless told otherwise
#endif
#ifndef PUMP_LEAD_MS
#define PUMP_LEAD_MS          800     // establish pressure before the valve opens
#endif
#ifndef PUMP_TAIL_MS
#define PUMP_TAIL_MS          1500    // keep the pump after the valve shuts
#endif

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/water/state"
#define MQTT_TOPIC_CMD        "ranch/water/cmd"
#define TELEMETRY_HZ          2

// --- task layout ------------------------------------------------------------
#define PRIO_CONTROL          18
#define PRIO_TELEMETRY        12
#define STACK_CONTROL         6144
#define STACK_TELEMETRY       6144
#define TASK_WDT_TIMEOUT_MS   2000

#ifndef RANCH_NO_PIN_CHECK
namespace ranch {

constexpr bool pinsDisjoint(const int* v, size_t n, size_t i = 1) {
    if (i >= n) return true;
    if (v[i] >= 0) {
        for (size_t j = 0; j < i; ++j) {
            if (v[j] == v[i]) return false;
        }
    }
    return pinsDisjoint(v, n, i + 1);
}

// Classic ESP32: 6-11 and 16-17 are the SPI flash and PSRAM bus.
constexpr bool pinsAvoidFlash(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p >= 6 && p <= 11) return false;
    if (p == 16 || p == 17) return false;
    return pinsAvoidFlash(v, n, i + 1);
}

// 34,35,36,39 have no output driver and no internal pull-ups.
constexpr bool outputsAreDriven(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p == 34 || p == 35 || p == 36 || p == 39) return false;
    return outputsAreDriven(v, n, i + 1);
}

// ADC1 on the classic ESP32 is GPIO 32-39; ADC2 is unavailable with WiFi on.
constexpr bool analogIsAdc1(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p < 32 || p > 39) return false;
    return analogIsAdc1(v, n, i + 1);
}

// The DevKit V1 only breaks out 26 of the ESP32's 34 usable GPIOs: 37 and 38 exist
// on the chip (and are valid ADC1 channels) but never reach a header, and 0 is inside
// the USB-serial bridge's flow-control circuit. An allocation that passes every other
// rule here still cannot be wired on the board a person actually buys, so the set
// below is the header, read off the part the simulator loads.
constexpr bool pinOnHeader(int p) {
    const int k[] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (k[i] == p) return true;
    return false;
}

constexpr bool pinsOnHeader(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    if (!pinOnHeader(v[i])) return false;
    return pinsOnHeader(v, n, i + 1);
}

constexpr int kOutputs[] = { PIN_VALVE_BARN, PIN_VALVE_HOUSE, PIN_PUMP,
                             PIN_LED_STATUS, PIN_RTC_SDA, PIN_RTC_SCL, PIN_WDT_FEED };
constexpr int kAnalog[] = { PIN_PRESSURE, PIN_LEVEL };
constexpr int kAll[] = {
    PIN_VALVE_BARN, PIN_VALVE_HOUSE, PIN_PUMP, PIN_LED_STATUS, PIN_RTC_SDA, PIN_RTC_SCL,
    PIN_WDT_FEED, PIN_FLOW_BARN, PIN_FLOW_HOUSE, PIN_LEAK, PIN_TANK_HIGH,
    PIN_PRESSURE, PIN_LEVEL,
};
constexpr size_t kAllLen = sizeof(kAll) / sizeof(kAll[0]);
constexpr size_t kOutLen = sizeof(kOutputs) / sizeof(kOutputs[0]);
constexpr size_t kAnLen = sizeof(kAnalog) / sizeof(kAnalog[0]);

static_assert(pinsDisjoint(kAll, kAllLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidFlash(kAll, kAllLen), "board.h: a pin sits on the SPI flash bus");
static_assert(outputsAreDriven(kOutputs, kOutLen), "board.h: an output is on an input-only pin");
static_assert(analogIsAdc1(kAnalog, kAnLen), "board.h: an analog input must be on ADC1");
static_assert(pinsOnHeader(kAll, kAllLen), "board.h: a pin is not broken out on the DevKit V1 header");

}  // namespace ranch
#endif

/* ==================== src/hal.h ==================== */

// Hardware abstraction for the water board. Two implementations:
// hal_esp32.cpp drives the real valves, and hal_sim.cpp closes the loop against
// a hydraulic model of the ranch plumbing, so the firmware's own flow and
// pressure readings come from the plant rather than from what the firmware
// hoped the plant would do.
//
// Nothing here allocates, and every call is safe from any task.

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);

// Console sink for telemetry: UART0 on the aircraft, stdout in the host test.
void consoleWrite(const char* data, size_t len);

// Outputs. `duty` is 0..1: full for the inrush window, the holding duty after
// it. The HAL owns the PWM so no caller can set a coil current twice.
void setValvePin(uint8_t pin, float duty);
void setPump(bool on);

// Inputs. The pulse counters are free running and wrap; the caller differences
// them and must handle the wrap (meter.cpp does).
uint32_t flowPulsesBarn();
uint32_t flowPulsesHouse();
bool leakWet();                         // true = water where water should not be
bool tankHigh();                        // tank level switch made

// Analog. The HAL converts the ADC to millivolts and nothing more: the
// engineering units, the filter windows and the out-of-range tests are policy
// about the plumbing, and they live in sense.cpp.
uint16_t analogMillivolts(uint8_t pin);

// Wall clock. Returns false when no time source is valid, which keeps every
// scheduled dispense from firing on a 1970 default.
bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day);
void clockSet(uint16_t year, uint8_t month, uint8_t day,
              uint8_t hour, uint8_t minute, uint8_t second);
bool clockWasSet();                     // a battery-backed RTC that kept time

// Non-volatile parameters, and the totalisers that have to survive a reboot.
bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

// Network.
bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

void feedWatchdog();
void setStatusLed(bool on);
const char* resetReason();

#if defined(RANCH_SIM)
// Advance the simulated plant by the real (browser) or virtual (host test) clock.
void halSimPump();
// The plant's wall clock. There is no RTC on a simulation, and a board with no
// clock never dispenses on schedule, so the test harness has to be able to give
// it one.
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
// Take the clock away without losing the time, so "no schedule without a clock"
// can be tested on a board that had one. A dead coin cell looks like this.
void halSimClock(bool enabled);

// What the simulated plant actually did, so the host test can assert on water
// moved and valves stuck instead of only on "no crash".
struct PlantCounters {
    float barn_litres;
    float house_litres;
    float leaked_litres;
    uint32_t pump_starts;
    uint32_t dry_run_events;
    uint32_t stuck_valve_events;
    uint32_t tank_refills;
    bool leak_active;
    bool tank_high_active;
    float pressure_bar;
    float tank_pct;
    float barn_flow_lmin;      // what is actually moving, for the metering test
    float house_flow_lmin;
};
void halPlant(PlantCounters& out);
void halPlantLeak(bool on);            // fault injection for the counter-proof
void halPlantSupplyLost(bool on);      // mains outage: pump runs, nothing moves
#endif

}  // namespace ranch

/* ==================== ../lib/frame_codec.h ==================== */

// Bounded key=value telemetry codec shared by the MQTT uplink, the serial
// console line and the SD log, so the ground application parses one format no
// matter which transport carried it.
//
//   DRONE,mode=TRANSIT,alt=18.0,wp=3,batt=87,rssi=-62,link=up
//
// Writers never run past the caller's buffer: once it is full every further
// add() fails and overflow() stays set, so a truncated frame is detectable
// instead of silently wrong.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranch {

constexpr uint8_t TELEMETRY_MAX_FIELDS = 24;

class FrameWriter {
public:
    FrameWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {
        if (cap_ > 0) buf_[0] = '\0';
    }

    void begin(const char* source) {
        n_ = 0;
        full_ = false;
        put(source);
    }

    bool add(const char* key, int value) {
        char tmp[24];
        std::snprintf(tmp, sizeof(tmp), "%d", value);
        return add(key, tmp);
    }

    bool add(const char* key, float value, int digits = 1) {
        char tmp[24];
        if (digits <= 0) std::snprintf(tmp, sizeof(tmp), "%.0f", static_cast<double>(value));
        else std::snprintf(tmp, sizeof(tmp), "%.*f", digits, static_cast<double>(value));
        return add(key, tmp);
    }

    bool add(const char* key, const char* value) {
        if (full_) return false;
        put(",");
        put(key);
        put("=");
        put(value);
        return !full_;
    }

    // Ends the frame with a newline so the same buffer can go straight to serial.
    void endLine() {
        if (n_ + 1 < cap_) buf_[n_++] = '\n';
        else full_ = true;
        if (cap_) buf_[n_ < cap_ ? n_ : cap_ - 1] = '\0';
    }

    bool overflow() const { return full_; }
    size_t size() const { return n_; }
    const char* c_str() const { return buf_; }

private:
    void put(const char* s) {
        while (*s != '\0') {
            if (n_ + 1 >= cap_) { full_ = true; return; }
            buf_[n_++] = *s++;
        }
        buf_[n_] = '\0';
    }

    char* buf_;
    size_t cap_;
    size_t n_ = 0;
    bool full_ = false;
};

struct Field {
    const char* key;
    size_t key_len;
    const char* value;
    size_t value_len;
};

// Single pass, no copies: fields point into the caller's buffer.
class FrameReader {
public:
    FrameReader(const char* buf, size_t len) : p_(buf), end_(buf + len) {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\r' || *p_ == '\n')) ++p_;
        const char* comma = static_cast<const char*>(std::memchr(p_, ',', static_cast<size_t>(end_ - p_)));
        source_ = p_;
        source_len_ = comma ? static_cast<size_t>(comma - p_) : static_cast<size_t>(end_ - p_);
        cur_ = comma ? comma + 1 : end_;
    }

    const char* source() const { return source_; }
    size_t sourceLength() const { return source_len_; }

    bool next(Field& out) {
        if (cur_ >= end_) return false;
        const size_t rest = static_cast<size_t>(end_ - cur_);
        const char* eq = static_cast<const char*>(std::memchr(cur_, '=', rest));
        if (!eq) return false;
        const char* comma = static_cast<const char*>(std::memchr(eq + 1, ',', static_cast<size_t>(end_ - eq - 1)));
        const char* stop = comma ? comma : end_;
        while (stop > eq + 1 && (stop[-1] == '\n' || stop[-1] == '\r' || stop[-1] == ' ')) --stop;
        out.key = cur_;
        out.key_len = static_cast<size_t>(eq - cur_);
        out.value = eq + 1;
        out.value_len = static_cast<size_t>(stop - eq - 1);
        cur_ = comma ? comma + 1 : end_;
        return true;
    }

    // Case-sensitive key match, length aware, so a truncated buffer cannot
    // match a longer key that happens to start with the same letters.
    // Const because a lookup walks a private copy of the cursor: reading a field
    // out of a frame must not consume the frame the caller is still working from.
    bool find(const char* key, Field& out) const {
        const size_t klen = std::strlen(key);
        FrameReader r(p_, static_cast<size_t>(end_ - p_));
        Field f{};
        while (r.next(f)) {
            if (f.key_len == klen && std::memcmp(f.key, key, klen) == 0) { out = f; return true; }
        }
        return false;
    }

    bool getFloat(const char* key, float& out) const {
        Field f{};
        if (!find(key, f)) return false;
        char tmp[16];
        const size_t n = f.value_len < sizeof(tmp) - 1 ? f.value_len : sizeof(tmp) - 1;
        std::memcpy(tmp, f.value, n);
        tmp[n] = '\0';
        out = std::strtof(tmp, nullptr);
        return true;
    }

    bool getInt(const char* key, long& out) const {
        Field f{};
        if (!find(key, f)) return false;
        char tmp[16];
        const size_t n = f.value_len < sizeof(tmp) - 1 ? f.value_len : sizeof(tmp) - 1;
        std::memcpy(tmp, f.value, n);
        tmp[n] = '\0';
        out = std::strtol(tmp, nullptr, 10);
        return true;
    }

private:
    const char* p_;
    const char* end_;
    const char* source_;
    size_t source_len_;
    const char* cur_;
};

}  // namespace ranch

/* ==================== ../lib/programme.h ==================== */

// Time-of-day dispense programme. Platform neutral and side-effect free: it
// turns a wall clock and an elapsed tick into "start a run", "still running",
// "end the run", plus the countdown the dashboard shows.
//
// Cycles are anchored to local midnight rather than to boot time, so a board that
// reboots at 03:00 does not hand the trough water at 04:00 forever afterwards.

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

/* ==================== ../lib/scheduler.h ==================== */

// Cooperative rate scheduler: fixed job table, no allocation, no dynamic
// timers. Each task loop calls due() with the elapsed milliseconds and gets a
// true on the ticks it owns. Overrun is measured, not silently dropped, because
// a loop that misses its rate is the first symptom of a stack or priority bug.

#include <cstdint>

namespace ranch {

constexpr uint8_t SCHED_MAX_JOBS = 12;

struct Job {
    uint32_t period_ms;
    uint32_t acc_ms;
    uint32_t late_ms;      // how far behind the last fired tick was
    uint32_t runs;
    uint32_t overruns;
    uint32_t worst_late_ms;
    const char* name;
    bool used;
};

class Scheduler {
public:
    int add(const char* name, uint32_t period_ms) {
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) {
            if (jobs_[i].used) continue;
            jobs_[i] = Job{period_ms, 0, 0, 0, 0, 0, name, true};
            ++count_;
            return i;
        }
        return -1;
    }

    // Returns true when the job is due. dt_ms is the wall time since the last
    // call for this job, so a job that is called twice as often as its rate
    // still fires on schedule.
    bool due(int id, uint32_t dt_ms) {
        if (id < 0 || id >= SCHED_MAX_JOBS || !jobs_[id].used) return false;
        Job& j = jobs_[id];
        j.acc_ms += dt_ms;
        if (j.acc_ms < j.period_ms) return false;
        const uint32_t late = j.acc_ms - j.period_ms;
        j.late_ms = late;
        if (late > j.worst_late_ms) j.worst_late_ms = late;
        if (late > j.period_ms) j.overruns++;
        j.acc_ms = 0;
        j.runs++;
        return true;
    }

    // Wall time covered by the tick that just fired. A job that runs its state
    // machine on period_ms instead of this number loses time whenever the loop
    // is late, and the clock it maintains quietly falls behind reality.
    uint32_t elapsed(int id) const {
        if (id < 0 || id >= SCHED_MAX_JOBS || !jobs_[id].used) return 0;
        return jobs_[id].period_ms + jobs_[id].late_ms;
    }

    // Start over. The host test runs a whole mission twice in one process, and
    // a second setup() must not find the job table full of the first one.
    void clear() {
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) jobs_[i] = Job{};
        count_ = 0;
    }

    uint8_t count() const { return count_; }
    const Job& job(int id) const { return jobs_[id]; }

    // Total CPU spent inside the loops, for the health frame.
    uint32_t totalRuns() const {
        uint32_t n = 0;
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) if (jobs_[i].used) n += jobs_[i].runs;
        return n;
    }

    uint32_t totalOverruns() const {
        uint32_t n = 0;
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) if (jobs_[i].used) n += jobs_[i].overruns;
        return n;
    }

private:
    Job jobs_[SCHED_MAX_JOBS]{};
    uint8_t count_ = 0;
};

}  // namespace ranch

/* ==================== ../lib/valve_logic.h ==================== */

// Solenoid actuator sequencer. Platform neutral: it takes elapsed time and
// observations and returns what the outputs should be, so the whole of
// "inrush, then hold, then refuse to hold forever" is testable without a coil.
//
// A 24 V irrigation solenoid pulls several times its holding current for as long
// as it takes the armature to travel. Driving it at full duty afterwards does not
// keep it more open - it just heats the coil and the MOSFET, and on a solar site
// it is the reason the board's outputs brown out on the second valve.

#include <cstdint>

namespace ranch {

enum class ValveState : uint8_t {
    Closed,      // de-energised
    Inrush,      // full duty, waiting for the armature
    Hold,        // reduced duty, open
    Closing,     // commanded shut, still counted as open until the pulse settles
    Fault,       // stuck or no flow: refuses to re-open until cleared
};

struct ValveLimits {
    uint16_t inrush_ms;         // full-duty window
    float hold_duty;            // 0..1 after the window
    float max_open_s;           // a programme may not hold a valve open forever
    float no_flow_s;            // open with no flow for this long is a fault
    float flow_seen_lmin;       // flow above this counts as "water is moving"
};

constexpr ValveLimits VALVE_DEFAULTS{
    /*inrush_ms*/      120,
    /*hold_duty*/      0.55f,
    /*max_open_s*/     900.0f,
    /*no_flow_s*/      6.0f,
    /*flow_seen_lmin*/ 0.12f,
};

struct Valve {
    ValveState state;
    uint32_t since_ms;          // time in the current state
    float open_seconds;         // total time at full or hold duty this cycle
    float no_flow_seconds;      // time open with nothing moving
    uint32_t cycles;
    uint32_t faults;
    float last_litres;          // moved by the most recent cycle
    float cycle_litres;         // accumulating for the cycle in progress
};

inline void valveInit(Valve& v) {
    v.state = ValveState::Closed;
    v.since_ms = 0;
    v.open_seconds = 0.0f;
    v.no_flow_seconds = 0.0f;
    v.cycles = 0;
    v.faults = 0;
    v.last_litres = 0.0f;
    v.cycle_litres = 0.0f;
}

inline bool valveIsOpenCommand(const Valve& v) {
    return v.state == ValveState::Inrush || v.state == ValveState::Hold;
}

inline float valveDuty(const Valve& v, const ValveLimits& lim) {
    switch (v.state) {
        case ValveState::Inrush: return 1.0f;
        case ValveState::Hold:   return lim.hold_duty;
        default:                 return 0.0f;
    }
}

// Open if the line is allowed to run. Returns false when the valve is latched in
// Fault, which is the point: a stuck valve must not be re-driven every cycle in
// the hope that it frees off.
inline bool valveOpen(Valve& v) {
    if (v.state == ValveState::Fault) return false;
    if (v.state == ValveState::Closed) {
        v.state = ValveState::Inrush;
        v.since_ms = 0;
        v.open_seconds = 0.0f;
        v.no_flow_seconds = 0.0f;
        v.cycle_litres = 0.0f;
        v.cycles++;
    }
    return true;
}

inline void valveClose(Valve& v) {
    if (v.state == ValveState::Fault) return;
    if (v.state != ValveState::Closed) {
        v.state = ValveState::Closing;
        v.since_ms = 0;
        v.last_litres = v.cycle_litres;
    }
}

// Clearing a fault is explicit and only ever comes from an operator command or a
// verified sensor recovery - never from the sequencer deciding it feels better.
inline void valveClearFault(Valve& v) {
    if (v.state == ValveState::Fault) valveInit(v);
}

// One control tick. `flow_lmin` is this line's own meter, so two lines on one
// board cannot hide each other's fault: the house line running while the barn
// valve is open is exactly what this signature makes visible.
inline void valveStep(Valve& v, const ValveLimits& lim, uint32_t dt_ms, float flow_lmin) {
    v.since_ms += dt_ms;
    const float dt_s = dt_ms / 1000.0f;

    switch (v.state) {
        case ValveState::Inrush:
            v.open_seconds += dt_s;
            v.cycle_litres += flow_lmin * dt_s / 60.0f;
            if (v.since_ms >= lim.inrush_ms) {
                v.state = ValveState::Hold;
                v.since_ms = 0;
            }
            break;

        case ValveState::Hold:
            v.open_seconds += dt_s;
            v.cycle_litres += flow_lmin * dt_s / 60.0f;
            if (flow_lmin > lim.flow_seen_lmin) {
                v.no_flow_seconds = 0.0f;
            } else {
                v.no_flow_seconds += dt_s;
            }
            if (v.no_flow_seconds >= lim.no_flow_s || v.open_seconds >= lim.max_open_s) {
                v.state = ValveState::Fault;
                v.faults++;
                v.last_litres = v.cycle_litres;
            }
            break;

        case ValveState::Closing:
            // The meter keeps ticking after the coil drops: a 25 mm valve takes
            // a few hundred milliseconds to shut, and the water already in
            // motion finishes the cycle. Counting it keeps the totaliser honest.
            v.cycle_litres += flow_lmin * dt_s / 60.0f;
            if (flow_lmin <= lim.flow_seen_lmin || v.since_ms > 1500) {
                v.last_litres = v.cycle_litres;
                v.state = ValveState::Closed;
                v.since_ms = 0;
            }
            break;

        case ValveState::Closed:
        case ValveState::Fault:
        default:
            break;
    }
}

inline const char* valveStateName(ValveState s) {
    switch (s) {
        case ValveState::Closed:  return "closed";
        case ValveState::Inrush:  return "inrush";
        case ValveState::Hold:    return "hold";
        case ValveState::Closing: return "closing";
        case ValveState::Fault:   return "fault";
    }
    return "?";
}

}  // namespace ranch

/* ==================== ../lib/water_safety.h ==================== */

// Water board decision table. Platform neutral and side-effect free: it turns
// pressures, flows and switch states into one action plus one reason, and the
// caller decides what that action means on its own plumbing.
//
// Priority is fixed, because a pump about to destroy itself must not be outranked
// by a damp floor sensor that has not debounced yet:
//   close-all > stop-pump > close-one-line > refuse-new-cycles > run

#include <cstdint>

namespace ranch {

enum class WaterAction : uint8_t { Run, Refill, Hold, CloseBarn, CloseHouse, CloseAll };

inline const char* waterActionName(WaterAction a) {
    switch (a) {
        case WaterAction::Run:        return "RUN";
        case WaterAction::Refill:     return "REFILL";
        case WaterAction::Hold:       return "HOLD";
        case WaterAction::CloseBarn:  return "CLOSE BARN";
        case WaterAction::CloseHouse: return "CLOSE HOUSE";
        case WaterAction::CloseAll:   return "CLOSE ALL";
    }
    return "?";
}

struct PlantLimits {
    float dry_run_bar;         // pump running below this is running dry
    float dry_run_s;           // for this long before it counts
    float over_pressure_bar;   // burst risk
    float leak_flow_lmin;      // any flow at all with every valve shut
    float leak_s;              // sustained for this long
    float tank_refill_start;   // per cent
    float tank_refill_stop;    // per cent, with the high switch as the hard stop
};

constexpr PlantLimits PLANT_DEFAULTS{
    /*dry_run_bar*/     0.30f,
    /*dry_run_s*/       2.0f,
    /*over_pressure*/   8.0f,
    /*leak_flow_lmin*/  0.35f,
    /*leak_s*/          3.0f,
    /*tank_refill_start*/ 22.0f,
    /*tank_refill_stop*/  96.0f,
};

struct PlantInput {
    float pressure_bar;
    float tank_pct;
    bool tank_high;             // the float switch made: hard stop, not a percentage
    bool leak;                  // rope sensor
    float barn_flow;            // this line's own meter, L/min
    float house_flow;
    bool barn_open;
    bool house_open;
    bool pump_on;
    bool valve_barn_fault;      // the sequencer already latched a stuck valve
    bool valve_house_fault;
    bool clock_valid;           // no scheduled dispense without a wall clock
};

struct PlantState {
    float dry_run_s;            // accumulators, so a momentary sag does not
    float leak_s;               // trip a shutdown the way a real operator would not
    float stall_s;
};

struct PlantDecision {
    WaterAction action;
    bool allow_dispense;        // the programme may start a new cycle
    bool want_pump;             // the pressuriser should be energised
    bool pump_interlocked;
    // A veto, not a request: when this is false the pump stays off even if a
    // valve is still open. Without it "stop the pump" is a suggestion the output
    // stage overrules, and a dry borehole or a full tank never gets the pump it
    // is supposed to get.
    char reason[20];
};

inline void plantReset(PlantState& s) {
    s.dry_run_s = 0.0f;
    s.leak_s = 0.0f;
    s.stall_s = 0.0f;
}

inline void copyText(char (&dst)[20], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 19; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline PlantDecision plantEvaluate(const PlantLimits& lim, PlantState& st,
                                   const PlantInput& in, float dt_s) {
    PlantDecision out{WaterAction::Run, true, in.pump_on, true, ""};

    // --- sensors that are not reporting yet must not read as "safe" -----------
    // A tank percentage of 0 with no pump running is either empty or disconnected;
    // the caller passes -1 when the channel has not produced a sample.
    const bool tank_known = in.tank_pct >= 0.0f;

    st.dry_run_s = (in.pump_on && in.pressure_bar < lim.dry_run_bar) ? st.dry_run_s + dt_s : 0.0f;
    const float closed_flow = (in.barn_open || in.house_open) ? 0.0f
                              : (in.barn_flow + in.house_flow);
    st.leak_s = (closed_flow > lim.leak_flow_lmin || in.leak) ? st.leak_s + dt_s : 0.0f;

    // --- hard stops ------------------------------------------------------------
    if (in.leak && (in.barn_open || in.house_open)) {
        out.action = WaterAction::CloseAll;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, "LEAK");
        return out;
    }
    if (st.leak_s >= lim.leak_s) {
        // Either the meter moves with nothing open, or the rope has been wet for
        // seconds. Both mean water is going somewhere it should not.
        out.action = WaterAction::CloseAll;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, in.leak ? "LEAK" : "LEAK METER");
        return out;
    }
    if (in.pressure_bar > lim.over_pressure_bar) {
        out.action = WaterAction::CloseAll;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, "OVERPRESSURE");
        return out;
    }
    if (st.dry_run_s >= lim.dry_run_s) {
        // Stop the pump, leave the lines alone: an empty tank is not a reason to
        // close a valve that the house is currently using.
        out.action = WaterAction::Hold;
        out.allow_dispense = false;
        out.want_pump = false;
        out.pump_interlocked = false;
        copyText(out.reason, "DRY RUN");
        return out;
    }

    // --- one line at a time -----------------------------------------------------
    if (in.valve_barn_fault) {
        out.action = WaterAction::CloseBarn;
        out.allow_dispense = false;
        copyText(out.reason, "BARN STUCK");
        return out;
    }
    if (in.valve_house_fault) {
        out.action = WaterAction::CloseHouse;
        copyText(out.reason, "HOUSE STUCK");
        return out;
    }

    // --- refill -----------------------------------------------------------------
    if (in.tank_high) {
        // The float switch is the hard stop. Without the interlock a line that is
        // still open would keep asking for the pump and the tank would overflow.
        out.want_pump = false;
        out.pump_interlocked = false;         // the float switch outranks the percentage
        copyText(out.reason, "TANK FULL");
    } else if (tank_known && in.tank_pct <= lim.tank_refill_start) {
        out.action = WaterAction::Refill;
        out.want_pump = true;
    } else if (tank_known && in.tank_pct >= lim.tank_refill_stop) {
        out.want_pump = false;
    }

    // --- no scheduled water without a clock ------------------------------------
    if (!in.clock_valid) {
        out.allow_dispense = false;    // the house line stays available
        if (*out.reason == '\0') copyText(out.reason, "NO CLOCK");
    }

    return out;
}

}  // namespace ranch

/* ==================== src/sense.h ==================== */

// Measurement layer: turns pulse counts, ADC samples and switch states into the
// plant quantities the rest of the firmware reasons about.
//
// The HAL reports millivolts and pulse totals; the engineering conversion, the
// filter windows and the "this channel has stopped making sense" tests live
// here, because those are decisions about the plumbing rather than about the
// microcontroller.

#include <cstdint>

namespace ranch {

struct LineState {
    float flow_lmin;        // this line's own meter, over a 2 s window
    float litres_cycle;     // since the current cycle opened the valve
    float litres_total;     // since commissioning, persisted to NVS
    uint32_t pulses;        // raw counter
    bool  silent;           // no pulses across the whole rate window
};

struct SenseState {
    LineState barn;
    LineState house;
    float pressure_bar;     // filtered
    float tank_pct;         // filtered, -1 until the channel produces a sample
    bool leak;              // debounced rope sensor
    bool tank_high;
    bool sensor_fault;      // a channel outside its valid input range
};

void senseInit();

// Restore the totalisers. Called once at boot: a board that browned out mid-write
// must not double count the water it already billed.
void senseLoad();

// One control tick. dt_ms is the real time since the previous tick, which every
// window here is built from, so the numbers mean the same thing at 20 Hz and at
// 5 Hz.
void senseSample(uint32_t dt_ms);

const SenseState& senseGet();

// Cycle accounting, owned here so a caller cannot reset an accumulator and make
// the day's total wrong.
void senseCycleBegin(bool barn_line);
float senseCycleLitres(bool barn_line);
float senseLitresTotal(bool barn_line);

// Totalisers to NVS. The slowest thing this board does, so it runs on a minute
// boundary and not every tick.
void sensePersist();
bool sensePersisted();
uint32_t sensePersistFails();

}  // namespace ranch

/* ==================== src/sense.cpp ==================== */


#include <cmath>

namespace ranch {
namespace {

constexpr uint16_t RATE_WINDOW_MS = 2000;
constexpr uint8_t RATE_BUCKETS = 8;                    // 250 ms each
constexpr uint16_t LEAK_DEBOUNCE_MS = 1500;
constexpr uint16_t ADC_LIVE_ZERO_MV = 100;             // below this the wire is open
constexpr uint16_t ADC_RAIL_MV = 4900;                 // above this it is shorted

struct Line {
    uint32_t last_pulses = 0;
    uint16_t bucket[RATE_BUCKETS]{};
    uint8_t head = 0;
    uint16_t carry_ms = 0;
    float litres_total = 0.0f;
    float litres_cycle = 0.0f;
    bool primed = false;        // first sample after boot: no phantom delta
};

Line flow_barn;
Line flow_house;
float pressure_bar = 0.0f;
float tank_pct = -1.0f;
bool leak_latched = false;
uint16_t leak_ms = 0;
bool fault_flag = false;
bool persisted_ok = false;
uint32_t persist_fails = 0;
SenseState state{};

float ema(float previous, float sample, float tau_s, float dt_s) {
    if (dt_s <= 0.0f) return previous;
    const float a = dt_s / (tau_s + dt_s);
    return previous + (sample - previous) * a;
}

// A window of 250 ms buckets instead of an exponential average: a valve that has
// stuck shut stops pulsing dead, and the rule that acts on it needs to know how
// long it has been silent, not what the smoothed value looks like.
float rateAdd(Line& l, uint32_t now_pulses, uint32_t dt_ms, uint32_t& added) {
    uint32_t delta = now_pulses - l.last_pulses;      // the counter wraps, this does not
    l.last_pulses = now_pulses;
    if (!l.primed) {
        l.primed = true;
        delta = 0;
    }
    added = delta;
    l.bucket[l.head] += delta > 65535u ? 65535u : static_cast<uint16_t>(delta);

    l.carry_ms += static_cast<uint16_t>(dt_ms % 65536u);
    while (l.carry_ms >= 250) {
        l.carry_ms -= 250;
        l.head = static_cast<uint8_t>((l.head + 1) % RATE_BUCKETS);
        l.bucket[l.head] = 0;
    }

    uint32_t total = 0;
    for (uint8_t i = 0; i < RATE_BUCKETS; ++i) total += l.bucket[i];
    const float litres = static_cast<float>(total) / FLOW_PULSES_PER_LITRE;
    return litres * (60000.0f / static_cast<float>(RATE_WINDOW_MS));
}

}  // namespace

void senseInit() {
    flow_barn = Line{};
    flow_house = Line{};
    state = SenseState{};
    pressure_bar = 0.0f;
    tank_pct = -1.0f;
    leak_latched = false;
    leak_ms = 0;
    fault_flag = false;
    persisted_ok = false;
    persist_fails = 0;
}

void senseLoad() {
    float v = 0.0f;
    if (nvGetF32("tot_barn", v) && v >= 0.0f) flow_barn.litres_total = v;
    if (nvGetF32("tot_house", v) && v >= 0.0f) flow_house.litres_total = v;
}

void senseSample(uint32_t dt_ms) {
    const float dt_s = dt_ms / 1000.0f;
    uint32_t added = 0;

    state.barn.flow_lmin = rateAdd(flow_barn, flowPulsesBarn(), dt_ms, added);
    flow_barn.litres_total += added / FLOW_PULSES_PER_LITRE;
    flow_barn.litres_cycle += added / FLOW_PULSES_PER_LITRE;
    state.barn.pulses = flow_barn.last_pulses;
    state.barn.litres_cycle = flow_barn.litres_cycle;
    state.barn.litres_total = flow_barn.litres_total;
    state.barn.silent = state.barn.flow_lmin <= 0.001f;

    state.house.flow_lmin = rateAdd(flow_house, flowPulsesHouse(), dt_ms, added);
    flow_house.litres_total += added / FLOW_PULSES_PER_LITRE;
    flow_house.litres_cycle += added / FLOW_PULSES_PER_LITRE;
    state.house.pulses = flow_house.last_pulses;
    state.house.litres_cycle = flow_house.litres_cycle;
    state.house.litres_total = flow_house.litres_total;
    state.house.silent = state.house.flow_lmin <= 0.001f;

    // Pressure: 0.5-4.5 V for 0-10 bar. Under the live zero is either an empty
    // line or a broken signal wire, and the dry-run rule cannot tell those apart,
    // so the out-of-range end raises a fault instead of being clamped away.
    const uint16_t pmv = analogMillivolts(PIN_PRESSURE);
    if (pmv < ADC_LIVE_ZERO_MV || pmv > ADC_RAIL_MV) {
        fault_flag = true;
    } else {
        const float raw = (pmv - PRESSURE_MV_AT_ZERO) / PRESSURE_MV_PER_BAR;
        pressure_bar = ema(pressure_bar, raw < 0.0f ? 0.0f : raw, 1.2f, dt_s);
    }
    state.pressure_bar = pressure_bar;

    // Tank level. Out of range keeps the last percentage and raises the fault: a
    // disconnected float must not read as "empty" and start the pump.
    const uint16_t lmv = analogMillivolts(PIN_LEVEL);
    if (lmv < ADC_LIVE_ZERO_MV || lmv > ADC_RAIL_MV) {
        fault_flag = true;
        if (tank_pct < 0.0f) tank_pct = 0.0f;
    } else {
        float pct = (lmv - LEVEL_MV_EMPTY) * 100.0f / (LEVEL_MV_FULL - LEVEL_MV_EMPTY);
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        tank_pct = tank_pct < 0.0f ? pct : ema(tank_pct, pct, 5.0f, dt_s);
    }
    state.tank_pct = tank_pct;
    state.sensor_fault = fault_flag;

    // The rope sensor is a level, not an edge: it reports wet for as long as the
    // water is there. Debounce the making so a splash while the flow_barn valve snaps
    // shut is not an alarm, and release immediately, because latching a dry floor
    // would keep the flow_house line offline for no reason.
    if (leakWet()) {
        leak_ms += static_cast<uint16_t>(dt_ms);
        if (leak_ms >= LEAK_DEBOUNCE_MS) leak_latched = true;
    } else {
        leak_ms = 0;
        leak_latched = false;
    }
    state.leak = leak_latched;
    state.tank_high = tankHigh();
}

const SenseState& senseGet() { return state; }

void senseCycleBegin(bool barn_line) {
    (barn_line ? flow_barn : flow_house).litres_cycle = 0.0f;
}

float senseCycleLitres(bool barn_line) {
    return (barn_line ? flow_barn : flow_house).litres_cycle;
}

float senseLitresTotal(bool barn_line) {
    return (barn_line ? flow_barn : flow_house).litres_total;
}

void sensePersist() {
    const bool a = nvSetF32("tot_barn", flow_barn.litres_total);
    const bool b = nvSetF32("tot_house", flow_house.litres_total);
    persisted_ok = a && b;
    if (!persisted_ok) persist_fails++;
}

bool sensePersisted() { return persisted_ok; }
uint32_t sensePersistFails() { return persist_fails; }

}  // namespace ranch

/* ==================== src/valves.h ==================== */

// The only module that touches the valve and pump outputs. Everything else asks
// for water; this decides how the coil actually gets energised, in what order,
// and when it refuses.
//
// Sequencing that lives here and nowhere else:
//  - the pressuriser comes up first and stays up a moment after the valve drops,
//    because a solenoid opening against zero pressure hammers its seat
//  - two lines on one board never share a pump request: the demand is a count of
//    open lines plus the refill flag, so closing the barn cannot switch the pump
//    out from under the house line that is still drawing
//  - a latched valve fault is not re-driven by the next cycle

#include <cstdint>

namespace ranch {

enum class WaterLine : uint8_t { Barn, House };

struct Outputs {
    bool barn_open;
    bool house_open;
    bool pump_on;
    ValveState barn_state;
    ValveState house_state;
    float barn_duty;
    float house_duty;
    uint32_t barn_cycles;
    uint32_t house_cycles;
    uint32_t barn_faults;
    uint32_t house_faults;
    float barn_open_s;
    float house_open_s;
    uint32_t pump_starts;
};

void valvesInit();

// The sequencer limits come from NVS at boot. Without this the tuned values are
// read, printed, and then thrown away.
void valvesSetLimits(const ValveLimits& lim);

// Level requests, not edges: the programme and the safety layer may each say "I
// want this line open" and the last tick wins, which is what makes a safety close
// stick even while the programme still thinks it is running.
void valvesDemand(WaterLine line, bool open);
void valvesPumpDemand(bool needed);

// Hard veto from the safety layer: false keeps the pump off even while a valve
// is still open. A line finishing its cycle is not a reason to run the pump dry.
void valvesPumpAllowed(bool allowed);

// Advances the sequencers and writes the outputs. Call once per control tick
// with the measurement for the same tick.
void valvesTick(uint32_t dt_ms, const SenseState& sense);

const Outputs& valvesGet();

// Explicit, and the only way a stuck valve comes back.
void valvesClearFault(WaterLine line);

// The house line is allowed to run on a command while the barn programme is
// suspended; this says whether any line is drawing right now.
bool valvesAnyOpen();

}  // namespace ranch

/* ==================== src/valves.cpp ==================== */



namespace ranch {
namespace {

Valve valve_barn;
Valve valve_house;
ValveLimits limits = VALVE_DEFAULTS;

bool want_barn = false;
bool want_house = false;
bool pump_wanted = false;
bool pump_allowed = true;
bool pump_on = false;
uint32_t pump_uptime_ms = 0;
uint32_t pump_tail_ms = 0;
Outputs out{};

// One tick of one line: honour the demand, advance the sequencer against that
// line's own meter, then write the coil. In that order, so the duty the pin gets
// this tick is the state the sequencer reached *this* tick and not last one.
void driveLine(Valve& v, bool demand, uint8_t pin, float flow_lmin, uint32_t dt_ms,
               bool& open_flag, float& duty_flag, ValveState& state_flag) {
    if (demand && v.state != ValveState::Inrush && v.state != ValveState::Hold &&
        v.state != ValveState::Closing) {
        if (valveOpen(v)) open_flag = true;             // false when latched in fault
    } else if (!demand && (v.state == ValveState::Inrush || v.state == ValveState::Hold)) {
        valveClose(v);
    }

    valveStep(v, limits, dt_ms, flow_lmin);

    const float duty = valveDuty(v, limits);
    open_flag = duty > 0.0f;
    duty_flag = duty;
    state_flag = v.state;
    setValvePin(pin, duty);
}

}  // namespace

void valvesInit() {
    valveInit(valve_barn);
    valveInit(valve_house);
    want_barn = false;
    want_house = false;
    pump_wanted = false;
    pump_allowed = true;
    pump_on = false;
    pump_uptime_ms = 0;
    pump_tail_ms = 0;
    out = Outputs{};
    setValvePin(PIN_VALVE_BARN, 0.0f);
    setValvePin(PIN_VALVE_HOUSE, 0.0f);
    setPump(false);
}

void valvesSetLimits(const ValveLimits& lim) { limits = lim; }

void valvesDemand(WaterLine line, bool open) {
    if (line == WaterLine::Barn) want_barn = open;
    else want_house = open;
}

void valvesPumpDemand(bool needed) { pump_wanted = needed; }

void valvesPumpAllowed(bool allowed) { pump_allowed = allowed; }

void valvesTick(uint32_t dt_ms, const SenseState& sense) {
    // --- pressuriser ---------------------------------------------------------
    // Demand is a level: either line drawing, or a refill wanted. The lead before
    // a valve opens and the tail after it closes are what keep the pump from
    // cycling on a meter that ticks in bursts, and stop a solenoid opening
    // against a dead line.
    const bool any_line = want_barn || want_house;
    const bool need_pump = pump_allowed && (any_line || pump_wanted);

    if (need_pump && !pump_on) {
        pump_on = true;
        pump_uptime_ms = 0;
        pump_tail_ms = 0;
        out.pump_starts++;
    } else if (!need_pump && pump_on) {
        pump_tail_ms += dt_ms;
        if (pump_tail_ms >= PUMP_TAIL_MS) {
            pump_on = false;
            pump_tail_ms = 0;
        }
    } else {
        pump_tail_ms = 0;
    }
    if (pump_on) pump_uptime_ms += dt_ms;
    setPump(pump_on);
    out.pump_on = pump_on;

    // A demanded line waits for the lead window once, then is never held back
    // again while it is already open.
    const bool ready = !any_line || pump_uptime_ms >= PUMP_LEAD_MS;
    const bool barn_demand = want_barn && ready;
    const bool house_demand = want_house && ready;

    if (barn_demand && valve_barn.state == ValveState::Closed) senseCycleBegin(true);
    if (house_demand && valve_house.state == ValveState::Closed) senseCycleBegin(false);

    driveLine(valve_barn, barn_demand, PIN_VALVE_BARN, sense.barn.flow_lmin, dt_ms,
              out.barn_open, out.barn_duty, out.barn_state);
    driveLine(valve_house, house_demand, PIN_VALVE_HOUSE, sense.house.flow_lmin, dt_ms,
              out.house_open, out.house_duty, out.house_state);

    out.barn_cycles = valve_barn.cycles;
    out.house_cycles = valve_house.cycles;
    out.barn_faults = valve_barn.faults;
    out.house_faults = valve_house.faults;
    out.barn_open_s = valve_barn.open_seconds;
    out.house_open_s = valve_house.open_seconds;
}

const Outputs& valvesGet() { return out; }

void valvesClearFault(WaterLine line) {
    valveClearFault(line == WaterLine::Barn ? valve_barn : valve_house);
}

bool valvesAnyOpen() { return out.barn_open || out.house_open; }

}  // namespace ranch

/* ==================== src/telemetry.h ==================== */

// State uplink for the water board: one key=value frame per publish, to the
// console and to MQTT.
//
// The field names are the contract with the ranch dashboard, so they name what
// they measure and never two things at once: `barn`/`house` are the valve
// states, `barn_l`/`house_l` are litres for the cycle in progress, `*_tot` are
// the lifetime totals, and `next` is seconds to the next scheduled dispense.

#include <cstddef>

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

/* ==================== src/telemetry.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

// 256, not 208: the longest frame the sandbox publishes is 204 bytes, and a frame that overflows is dropped whole -- 4 bytes of margin is one long `why=` away from losing the board's readings.
constexpr size_t FRAME_CAP = 256;

UplinkStats up{};
char frame[FRAME_CAP];
char broker_host[64] = "";
char broker_client[24] = "water";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

// The board's answer to the last line typed at its console. `ack=ok:resume` and
// `ack=no:resume` are otherwise indistinguishable on a dashboard that only sees
// valves, and a button that reports nothing is a button nobody can trust.
constexpr uint32_t ACK_HOLD_MS = 6000;
char ack_verb[40] = "";
bool ack_ok = false;
uint32_t ack_ms = 0;
// The programme as this board holds it, handed over already formatted.
char lim[48] = "";

}  // namespace

#if defined(RANCH_SIM)
namespace {
void uplinkMqtt(const char*, size_t) {}
void brokerService() {}
bool brokerUp() { return false; }
}  // namespace
#else
namespace {
#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient wifi_client;
PubSubClient broker(wifi_client);
uint32_t next_connect = 0;
bool callback_wired = false;

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

void onMqttPayload(char* topic, uint8_t* payload, unsigned int len) {
    if (!cmd_fn) return;
    if (std::strcmp(topic, MQTT_TOPIC_CMD) != 0) return;
    cmd_fn(reinterpret_cast<const char*>(payload), len);
}

void brokerService() {
    const uint32_t now = halMillis();
    if (*broker_host == '\0') return;
    if (!broker.connected()) {
        if (static_cast<int32_t>(now - next_connect) < 0) return;
        next_connect = now + 5000;
        broker.setServer(broker_host, broker_port);
        broker.setBufferSize(256);
        if (!callback_wired) {
            broker.setCallback(onMqttPayload);
            callback_wired = true;
        }
        if (broker.connect(broker_client)) broker.subscribe(MQTT_TOPIC_CMD, 0);
    } else {
        broker.loop();
    }
}

bool brokerUp() { return broker.connected(); }
}  // namespace
#endif

void telemetryInit() {
    up = UplinkStats{};
    frame[0] = '\0';
    // A restart must not come back still answering the last command.
    ack_verb[0] = '\0';
    ack_ms = 0;
    lim[0] = '\0';   // appSetup hands the real one over right after this
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "water");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }
void telemetrySetLimits(const char* text) {
    std::snprintf(lim, sizeof(lim), "%s", text ? text : "");
}

void telemetryNoteAck(const char* verb, const char* value, bool ok) {
    // The verb alone cannot answer "what value did the board keep"; an operator
    // changing a trip threshold needs the number back, not just a yes.
    if (value && *value) std::snprintf(ack_verb, sizeof(ack_verb), "%s=%s", verb, value);
    else std::snprintf(ack_verb, sizeof(ack_verb), "%s", verb);
    ack_ok = ok;
    ack_ms = halMillis();
}

void telemetryPublish(const WaterReport& r) {
    FrameWriter w(frame, sizeof(frame));
    w.begin("WATER");
    w.add("barn", r.barn_open ? "on" : "off");
    w.add("house", r.house_open ? "on" : "off");
    w.add("flow", r.barn_flow + r.house_flow, 1);
    w.add("press", r.pressure_bar, 2);
    w.add("next", static_cast<int>(r.next_s > 99999u ? 99999u : r.next_s));
    w.add("pump", r.pump_on ? 1 : 0);
    w.add("tank", r.tank_pct, 0);
    w.add("barn_l", r.barn_cycle_l, 1);
    w.add("house_l", r.house_cycle_l, 1);
    w.add("barn_tot", r.barn_total_l, 0);
    w.add("house_tot", r.house_total_l, 0);
    w.add("act", waterActionName(r.action));
    w.add("runs", static_cast<int>(r.runs));
    w.add("skip", static_cast<int>(r.skipped));
    w.add("fault", static_cast<int>(r.faults));
    w.add("link", wifiUp() ? "up" : "down");
    if (!r.clock_valid) w.add("clock", "unset");
    if (r.sensor_fault) w.add("sensor", "fault");
    if (r.reason && *r.reason) w.add("why", r.reason);
    // The board's answer to the last line typed at it: a dashboard that only sees
    // valves and relays cannot tell "the board did that" from "the page did it to
    // itself", which is the difference between a control and a painting.
    if (*lim) w.add("lim", lim);
    if (*ack_verb && static_cast<int32_t>(halMillis() - ack_ms) < static_cast<int32_t>(ACK_HOLD_MS)) {
        char ack[56];
        std::snprintf(ack, sizeof(ack), "%s:%s", ack_ok ? "ok" : "no", ack_verb);
        w.add("ack", ack);
    }
    w.endLine();

    if (w.overflow()) {
        // A truncated frame would show the dashboard a stale field as fresh.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryEvent(const char* kind) {
    char line[64];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryEventLitres(const char* kind, bool barn_line, float litres) {
    char line[80];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    w.add("line", barn_line ? "barn" : "house");
    w.add("litres", litres, 1);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch

/* ==================== src/hal_sim.cpp ==================== */

// Simulated ranch water system for the browser and the host test.
//
// Layout it models, because the firmware's rules only make sense against it:
//
//   borehole --pump--> header tank --gravity--> barn trough
//                                        \\----> house line
//
// The pump's job is to keep the header tank full; the trough and the house are
// fed by head, not by the pump. So:
//  - "pump running with an empty tank" is a dry run, and pressure collapses
//  - "valve open and nothing moving" is a blocked line or an empty tank, and the
//    sequencer must give up rather than hold the coil forever
//  - a barn run that is too long fills the trough past its brim, which is how the
//    rope sensor gets wet without anyone injecting a fault
// The firmware does not know any of this in advance: it reads pressures and
// pulses, and the tests below only pass if what it decides matches what happened.
#if defined(RANCH_SIM)

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace ranch {
namespace {

// --- the plant --------------------------------------------------------------
constexpr float TANK_LITRES = 2000.0f;      // header tank capacity
constexpr float PUMP_LITMIN = 20.0f;        // borehole yield through the pump
constexpr float HEAD_BAR_FULL = 2.2f;       // static head at a full tank
constexpr float PUMP_BOOST_BAR = 1.6f;
constexpr float BARN_DEMAND_LMIN = 12.0f;   // trough fill rate
constexpr float HOUSE_DEMAND_LMIN = 3.4f;
constexpr float LINE_RESIST_BAR_PER_LMIN = 0.09f;
constexpr float TROUGH_LITRES = 240.0f;     // what the trough can hold
constexpr float TROUGH_DRAIN_LMIN = 1.6f;   // what the cattle drink / the overflow drains

constexpr float LEAK_LMIN = 0.6f;           // the stuck-seat orifice, when injected

struct SimPlant {
    float tank_pct = 68.0f;
    float trough_pct = 20.0f;
    float pressure = 0.0f;
    float barn_flow = 0.0f;
    float house_flow = 0.0f;
    float barn_pulses = 0.0f;
    float house_pulses = 0.0f;
    bool barn_open = false;
    bool house_open = false;
    bool pump = false;
    bool barn_leak = false;                 // injected stuck seat
    bool supply_lost = false;               // injected: borehole dry
    bool stuck_latched = false;
    uint16_t barn_close_ms = 0;
    uint16_t house_close_ms = 0;
    float barn_litres = 0.0f;
    float house_litres = 0.0f;
    float leaked_litres = 0.0f;
    uint32_t pump_starts = 0;
    uint32_t dry_run_events = 0;
    uint32_t stuck_events = 0;
    uint32_t refills = 0;
    bool refill_open = false;
    float coil_barn = 0.0f;                 // last duty written to each coil
    float coil_house = 0.0f;
};

SimPlant plant;

// --- clock ------------------------------------------------------------------
uint32_t g_now = 0;
// The simulation starts with a clock, because neither the browser nor the host
// harness has an RTC or an NTP server. A minute before a scheduled dispense puts
// the first cycle in view immediately; a test that wants another time calls
// halSimTime().
uint32_t sod = 8u * 3600u + 3540u;
uint32_t sod_ms = 0;
uint16_t yy = 2026;
uint8_t mm = 4;
uint8_t dd = 18;
bool clock_valid = true;

#if defined(RANCH_HOST)
uint32_t g_virt = 0;
inline uint32_t rawMillis() { return g_virt; }
inline void rawSleep(uint32_t ms) { g_virt += ms; }
#else
inline uint32_t rawMillis() { return ::millis(); }
inline void rawSleep(uint32_t ms) { ::delay(ms); }
#endif

void plantStep(uint32_t dt_ms);

void pump() {
    const uint32_t t = rawMillis();
    if (t == g_now) return;
    uint32_t dt = t - g_now;
    g_now = t;
    if (dt > 100) dt = 100;
    plantStep(dt);
}

// A valve only passes what the head across it can push, and it takes a second or
// two for the column of water to get moving. That lag is the whole reason the
// firmware has a no-flow timeout: an instant model would hide it.
float lineFlow(bool open, float demand_lmin, float available_bar) {
    if (!open) return 0.0f;
    if (available_bar < 0.35f) return 0.0f;          // cracked seat weeps, that is all
    const float limit = demand_lmin * (available_bar / 2.4f);
    return limit > demand_lmin ? demand_lmin : limit;
}

void plantStep(uint32_t dt_ms) {
    const float dt = dt_ms / 1000.0f;

    // Advance the wall clock with the plant. The milliseconds have to accumulate:
    // at a 20 ms tick, dt/1000 is integer zero, and a clock that never moves
    // silently disables every scheduled cycle.
    if (clock_valid) {
        sod_ms += dt_ms;
        while (sod_ms >= 1000u) {
            sod_ms -= 1000u;
            sod++;
            if (sod >= DAY_SECONDS) {
                sod -= DAY_SECONDS;
                dd++;
                if (dd > 28) {
                    dd = 1;
                    mm++;
                    if (mm > 12) { mm = 1; yy++; }
                }
            }
        }
    }

    const bool barn_current = plant.coil_barn > 0.05f;      // what the coil says
    const bool house_current = plant.coil_house > 0.05f;
    if (barn_current && !plant.barn_open) plant.barn_open = true;
    if (house_current && !plant.house_open) plant.house_open = true;
    // A 25 mm solenoid does not shut the instant the coil drops.
    if (!barn_current && plant.barn_open) {
        plant.barn_close_ms += dt_ms;
        if (plant.barn_close_ms > 400) { plant.barn_open = false; plant.barn_close_ms = 0; }
    } else {
        plant.barn_close_ms = 0;
    }
    if (!house_current && plant.house_open) {
        plant.house_close_ms += dt_ms;
        if (plant.house_close_ms > 400) { plant.house_open = false; plant.house_close_ms = 0; }
    } else {
        plant.house_close_ms = 0;
    }

    // --- pressures -----------------------------------------------------------
    float head = HEAD_BAR_FULL * plant.tank_pct / 100.0f;
    if (plant.supply_lost) head = 0.0f;
    const bool pump_running = plant.pump;
    if (pump_running && !plant.supply_lost && plant.tank_pct > 1.0f) head += PUMP_BOOST_BAR;

    const float draw = (plant.barn_open ? plant.barn_flow : 0.0f) +
                       (plant.house_open ? plant.house_flow : 0.0f);
    const float target = head - LINE_RESIST_BAR_PER_LMIN * draw;
    const float settled = target < 0.0f ? 0.0f : target;
    plant.pressure += (settled - plant.pressure) * (1.0f - std::exp(-dt / 0.6f));

    if (pump_running && plant.pressure < 0.2f) plant.dry_run_events++;

    // --- flows ---------------------------------------------------------------
    // The trough can only accept water until it is full; overrunning the schedule
    // therefore stops filling rather than flooding, and the rope sensor is what
    // notices the difference.
    const float barn_avail = TROUGH_LITRES * (1.0f - plant.trough_pct / 100.0f);
    const float barn_target = lineFlow(plant.barn_open, BARN_DEMAND_LMIN, plant.pressure);
    const float barn_capped = barn_target > barn_avail / dt ? barn_avail / dt : barn_target;
    plant.barn_flow += (barn_capped - plant.barn_flow) * (1.0f - std::exp(-dt / 1.1f));

    const float house_target = lineFlow(plant.house_open, HOUSE_DEMAND_LMIN, plant.pressure);
    plant.house_flow += (house_target - plant.house_flow) * (1.0f - std::exp(-dt / 0.9f));

    // A stuck seat leaks the orifice rate even with the coil cold, and it does so
    // into the trough's overflow, which is exactly what the rope sensor sees.
    const float leak_flow = plant.barn_leak ? LEAK_LMIN : 0.0f;

    const float moved_barn = plant.barn_flow * dt / 60.0f;
    const float moved_house = plant.house_flow * dt / 60.0f;
    const float moved_leak = leak_flow * dt / 60.0f;
    plant.barn_litres += moved_barn;
    plant.house_litres += moved_house;
    plant.leaked_litres += moved_leak;
    plant.barn_pulses += moved_barn * FLOW_PULSES_PER_LITRE;
    plant.house_pulses += moved_house * FLOW_PULSES_PER_LITRE;
    // The leak is on the barn side of the meter and reaches the trough, but the
    // meter only counts turbulence in its own bore, so it stays blind to it.
    plant.trough_pct += (moved_barn + moved_leak) * 100.0f / TROUGH_LITRES;
    plant.tank_pct -= (moved_barn + moved_house) * 100.0f / TANK_LITRES;

    // The trough drinks and spills at a fixed rate; over 100 % is an overflow.
    plant.trough_pct -= TROUGH_DRAIN_LMIN * dt * 100.0f / TROUGH_LITRES;
    if (plant.trough_pct < 0.0f) plant.trough_pct = 0.0f;
    if (plant.trough_pct > 100.0f) plant.trough_pct = 100.0f;

    // --- refill ---------------------------------------------------------------
    if (pump_running && !plant.supply_lost) {
        plant.tank_pct += PUMP_LITMIN * dt * 100.0f / TANK_LITRES;
        if (!plant.refill_open) { plant.refill_open = true; plant.refills++; }
    } else {
        plant.refill_open = false;
    }
    if (plant.tank_pct > 100.0f) plant.tank_pct = 100.0f;
    if (plant.tank_pct < 0.0f) plant.tank_pct = 0.0f;

    // Counted on the edge, not per tick: "how many times did a line fail to
    // deliver" has to mean cycles, not milliseconds.
    const bool now_stuck = (plant.barn_open || plant.house_open) &&
                           plant.pressure < 0.35f &&
                           plant.barn_flow < 0.1f && plant.house_flow < 0.1f;
    if (now_stuck && !plant.stuck_latched) {
        plant.stuck_latched = true;
        plant.stuck_events++;
    } else if (!now_stuck) {
        plant.stuck_latched = false;
    }
}

// --- parameters -------------------------------------------------------------
struct Kv {
    char key[16];
    uint8_t kind;
    int32_t i;
    float f;
    char s[64];
};
Kv nvs[16];

Kv* findKv(const char* key, bool create) {
    const size_t klen = std::strlen(key);
    Kv* slot = nullptr;
    for (auto& k : nvs) {
        if (k.kind && std::strlen(k.key) == klen && std::memcmp(k.key, key, klen) == 0) return &k;
        if (!k.kind && !slot) slot = &k;
    }
    if (!create || !slot) return nullptr;
    std::snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->i = 0;
    slot->f = 0.0f;
    slot->s[0] = '\0';
    return slot;
}

}  // namespace

// --- HAL --------------------------------------------------------------------
void halInit() {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.begin(115200);
#endif
    g_now = rawMillis();
    plant = SimPlant{};
    clock_valid = true;
}

uint32_t halMillis() { return g_now; }

void halDelayMs(uint32_t ms) {
    rawSleep(ms);
    pump();
}

void halSimPump() { pump(); }

void halSimClock(bool enabled) { clock_valid = enabled; }

void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day) {
    sod = second_of_day;
    yy = year;
    mm = month;
    dd = day;
    clock_valid = true;
}

void consoleWrite(const char* data, size_t len) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#else
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
#endif
}

void setValvePin(uint8_t pin, float duty) {
    if (pin == PIN_VALVE_BARN) plant.coil_barn = duty;
    else if (pin == PIN_VALVE_HOUSE) plant.coil_house = duty;
}

void setPump(bool on) {
    if (on && !plant.pump) plant.pump_starts++;
    plant.pump = on;
}

uint32_t flowPulsesBarn() { return static_cast<uint32_t>(plant.barn_pulses); }
uint32_t flowPulsesHouse() { return static_cast<uint32_t>(plant.house_pulses); }

// The rope sensor is wet when the trough has overflowed, or when a fault has been
// injected. Reporting only the first would make the leak rule untestable.
bool leakWet() { return plant.trough_pct >= 99.5f || plant.barn_leak; }
bool tankHigh() { return plant.tank_pct >= 99.5f; }

// Millivolts out of the divider, so the firmware's own conversion and its
// out-of-range tests are what get exercised.
uint16_t analogMillivolts(uint8_t pin) {
    if (pin == PIN_PRESSURE) {
        return static_cast<uint16_t>(PRESSURE_MV_AT_ZERO + plant.pressure * PRESSURE_MV_PER_BAR);
    }
    if (pin == PIN_LEVEL) {
        const float mv = LEVEL_MV_EMPTY + (LEVEL_MV_FULL - LEVEL_MV_EMPTY) * plant.tank_pct / 100.0f;
        return static_cast<uint16_t>(mv);
    }
    return 0;
}

bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (!clock_valid) return false;
    second_of_day = sod;
    year = yy;
    month = mm;
    day = dd;
    return true;
}

void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    sod = static_cast<uint32_t>(hour) * 3600u + static_cast<uint32_t>(minute) * 60u + second;
    yy = year;
    mm = month;
    dd = day;
    clock_valid = true;
}

bool clockWasSet() { return clock_valid; }

bool nvGetI32(const char* key, int32_t& out) {
    Kv* k = findKv(key, false);
    if (!k || k->kind != 1) return false;
    out = k->i;
    return true;
}

bool nvSetI32(const char* key, int32_t value) {
    Kv* k = findKv(key, true);
    if (!k) return false;
    k->kind = 1;
    k->i = value;
    return true;
}

bool nvGetF32(const char* key, float& out) {
    Kv* k = findKv(key, false);
    if (!k || k->kind != 2) return false;
    out = k->f;
    return true;
}

bool nvSetF32(const char* key, float value) {
    Kv* k = findKv(key, true);
    if (!k) return false;
    k->kind = 2;
    k->f = value;
    return true;
}

bool nvGetStr(const char* key, char* buf, size_t cap) {
    Kv* k = findKv(key, false);
    if (!k || k->kind != 3) return false;
    std::snprintf(buf, cap, "%s", k->s);
    return true;
}

bool nvSetStr(const char* key, const char* value) {
    Kv* k = findKv(key, true);
    if (!k) return false;
    k->kind = 3;
    std::snprintf(k->s, sizeof(k->s), "%s", value);
    return true;
}

bool wifiConnect(const char*, const char*) { return true; }
bool wifiUp() { return true; }
int wifiRssi() { return -52; }
void wifiReconnect() {}

void feedWatchdog() {}
void setStatusLed(bool) {}                   // there is no lamp in the simulation
const char* resetReason() { return "sim"; }

// --- the plant's own accounting ----------------------------------------------
void halPlant(PlantCounters& out) {
    out.barn_litres = plant.barn_litres;
    out.house_litres = plant.house_litres;
    out.leaked_litres = plant.leaked_litres;
    out.pump_starts = plant.pump_starts;
    out.dry_run_events = plant.dry_run_events;
    out.stuck_valve_events = plant.stuck_events;
    out.tank_refills = plant.refills;
    out.leak_active = leakWet();
    out.tank_high_active = tankHigh();
    out.pressure_bar = plant.pressure;
    out.tank_pct = plant.tank_pct;
    out.barn_flow_lmin = plant.barn_flow;
    out.house_flow_lmin = plant.house_flow;
}

void halPlantLeak(bool on) { plant.barn_leak = on; }
void halPlantSupplyLost(bool on) { plant.supply_lost = on; }

}  // namespace ranch

#endif  // RANCH_SIM

/* ==================== src/main.cpp ==================== */

// Water board entry point: parameters, the control loop, and the ground commands.
//
// One cooperative task runs sensing, the decision table, the programme and the
// outputs in that fixed order every tick. There is no locking and none is wanted:
// the state that matters is "what the plant was doing when the decision was made",
// and a snapshot taken in the middle of an update describes a plant that never
// existed.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(RANCH_SIM)
#include <Arduino.h>
#endif

#ifndef RANCH_FW_VERSION
#define RANCH_FW_VERSION "dev"
#endif

namespace ranch {
namespace {

Scheduler sched;
int job_control = -1, job_telem = -1, job_persist = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

Programme prog{};
ProgrammeState pstate{};
PlantState plant_state{};
PlantLimits plant_limits = PLANT_DEFAULTS;
ValveLimits valve_limits = VALVE_DEFAULTS;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "water-1";
uint16_t mqtt_port = MQTT_PORT;

bool house_user_on = HOUSE_DEFAULT_OPEN != 0;
bool house_blocked = false;             // closed by a fault, not by a command
uint64_t house_exercise_until = 0;
uint32_t day = 0;
uint32_t second_of_day = 0;
bool have_clock = false;
bool clock_anchored = false;
bool day_initialised = false;
uint32_t barn_last_run_day = 0;
uint32_t house_last_run_day = 0;
WaterAction action = WaterAction::Run;
char reason[20] = "";
bool leak_reported = false;
bool tank_high_prev = false;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.add("house", house_user_on ? 1 : 0);
    w.endLine();
    consoleWrite(line, w.size());
}

void paramsLoad() {
    int32_t v = 0;
    float f = 0.0f;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", tag, sizeof(tag)) || !tag[0]) {
        std::snprintf(tag, sizeof(tag), "%s", "water-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    prog.period_s = BARN_PERIOD_S;
    prog.run_s = BARN_RUN_S;
    prog.phase_s = 0;
    prog.enabled = true;
    if (nvGetI32("period", v) && v >= 60 && v <= static_cast<int32_t>(DAY_SECONDS)) prog.period_s = static_cast<uint32_t>(v);
    if (nvGetI32("run_s", v) && v >= 5 && v <= 3600) prog.run_s = static_cast<uint32_t>(v);
    if (nvGetI32("phase", v) && v >= 0 && v < static_cast<int32_t>(DAY_SECONDS)) prog.phase_s = static_cast<uint32_t>(v);
    if (nvGetI32("barn_en", v)) prog.enabled = v != 0;
    if (nvGetI32("house_on", v)) house_user_on = v != 0;

    if (nvGetF32("dry_bar", f) && f > 0.0f && f < 5.0f) plant_limits.dry_run_bar = f;
    if (nvGetF32("over_bar", f) && f > 1.0f && f < 12.0f) plant_limits.over_pressure_bar = f;
    if (nvGetF32("leak_lmin", f) && f > 0.01f && f < 5.0f) plant_limits.leak_flow_lmin = f;
    if (nvGetF32("no_flow", f) && f > 1.0f && f < 60.0f) valve_limits.no_flow_s = f;
    if (nvGetF32("max_open", f) && f > 10.0f && f < 7200.0f) valve_limits.max_open_s = f;
}

void clockUpdate() {
    uint16_t y = 0;
    uint8_t mo = 0, dm = 0;
    uint32_t sod = 0;
    have_clock = clockNow(sod, y, mo, dm);
    if (!have_clock) {
        clock_anchored = false;
        return;
    }
    second_of_day = sod;
    // A day number built from the calendar, not from millis(): a reboot at 03:00
    // must not re-run the cycle that already happened at 02:00.
    const uint32_t numbered = static_cast<uint32_t>(y) * 372u +
                              static_cast<uint32_t>(mo) * 31u + dm;
    if (!day_initialised || numbered != day) {
        day = numbered;
        day_initialised = true;
        clock_anchored = false;
        if (!barn_last_run_day) barn_last_run_day = day;
        if (!house_last_run_day) house_last_run_day = day;
    }
    if (!clock_anchored) {
        programmeAnchor(prog, pstate, day, second_of_day);
        clock_anchored = true;
    }
}

void supervision() {
    static bool down = false;
    static uint32_t down_ms = 0;
    if (wifiUp()) {
        down = false;
        return;
    }
    if (!down) {
        down = true;
        down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (down_ms + 30000u)) >= 0) {
        down_ms = halMillis();
        wifiReconnect();
    }
}

void publish() {
    const SenseState& s = senseGet();
    const Outputs& o = valvesGet();
    WaterReport r{};
    r.barn_open = o.barn_open;
    r.house_open = o.house_open;
    r.pump_on = o.pump_on;
    r.barn_flow = s.barn.flow_lmin;
    r.house_flow = s.house.flow_lmin;
    r.pressure_bar = s.pressure_bar;
    r.tank_pct = s.tank_pct;
    r.next_s = have_clock ? programmeCountdown(prog, pstate, day, second_of_day) : 0;
    r.barn_cycle_l = s.barn.litres_cycle;
    r.house_cycle_l = s.house.litres_cycle;
    r.barn_total_l = s.barn.litres_total;
    r.house_total_l = s.house.litres_total;
    r.action = action;
    r.reason = reason;
    r.runs = pstate.runs;
    r.skipped = pstate.skipped;
    r.faults = o.barn_faults + o.house_faults;
    r.clock_valid = have_clock;
    r.sensor_fault = s.sensor_fault;
    telemetryPublish(r);
}

// What this board's programme actually is, as one self-describing string: a settings
// row that shows the page's own number is not a readout of the valve controller.
void publishLimits() {
    char s[32];
    std::snprintf(s, sizeof(s), "per%u/run%u/en%u",
                  static_cast<unsigned>(prog.period_s), static_cast<unsigned>(prog.run_s),
                  static_cast<unsigned>(prog.enabled ? 1u : 0u));
    telemetrySetLimits(s);
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[20], value[32];
    size_t i = 0;
    // A typed line often arrives padded: a terminal adds a space, a paste adds one.
    // Skipping the padding is what makes `street=1` and ` street=1` one command.
    while (i < len && (payload[i] == ' ' || payload[i] == '\t')) ++i;
    // The read cursor and the write index are two things. With one variable doing
    // both, a padded line left verb[0] uninitialised and the verb was compared
    // against garbage -- which is how a verb this board knows came out refused.
    size_t j = 0;
    while (i < len && j < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[j++] = payload[i++];
    }
    verb[j] = '\0';
    size_t n = 0;
    if (i < len && payload[i] == '=') {
        ++i;
        while (i < len && n < sizeof(value) - 1) value[n++] = payload[i++];
        value[n] = '\0';
    } else {
        value[0] = '\0';
    }

    bool taken = true;
    if (std::strcmp(verb, "house") == 0) {
        house_user_on = value[0] == '1' || value[0] == 'o';       // 1 or "on"
        if (house_user_on) house_blocked = false;
        nvSetI32("house_on", house_user_on ? 1 : 0);
    } else if (std::strcmp(verb, "barn") == 0 && value[0] == 'n') {
        // Run once now, without moving the schedule the programme keeps.
        if (have_clock) {
            pstate.running = false;
            pstate.next_due = programmeClock(day, second_of_day);
        }
    } else if (std::strcmp(verb, "stop") == 0) {
        prog.enabled = false;
        house_user_on = false;
        nvSetI32("barn_en", 0);
        nvSetI32("house_on", 0);
    } else if (std::strcmp(verb, "resume") == 0) {
        prog.enabled = true;
        house_user_on = true;
        house_blocked = false;
        valvesClearFault(WaterLine::Barn);
        valvesClearFault(WaterLine::House);
        clock_anchored = false;
        nvSetI32("barn_en", 1);
        nvSetI32("house_on", 1);
    } else if (std::strcmp(verb, "period") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 60 && v <= static_cast<int32_t>(DAY_SECONDS)) {
            prog.period_s = static_cast<uint32_t>(v);
            nvSetI32("period", static_cast<int32_t>(v));
            clock_anchored = false;
        } else {
            taken = false;
        }
    } else if (std::strcmp(verb, "run") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 5 && v <= 3600) {
            prog.run_s = static_cast<uint32_t>(v);
            nvSetI32("run_s", static_cast<int32_t>(v));
        } else {
            taken = false;
        }
    } else {
        taken = false;
    }
    // A command that was not recognised is reported, not silently dropped: the
    // alternative is an operator believing the board has ignored them.
    telemetryNoteAck(verb, value, taken);
    publishLimits();
    telemetryEvent(taken ? "cmd" : "cmd-unknown");
}

void controlTick(uint32_t dt_ms) {
    clockUpdate();
    senseSample(dt_ms);
    const SenseState& s = senseGet();
    // A copy, not a reference: valvesTick() writes through the accessor, and a
    // reference would make every "did it just close" test below read the new
    // value and never see the edge.
    const Outputs prev = valvesGet();

    PlantInput pin{};
    pin.pressure_bar = s.pressure_bar;
    pin.tank_pct = s.tank_pct;
    pin.tank_high = s.tank_high;
    pin.leak = s.leak;
    pin.barn_flow = s.barn.flow_lmin;
    pin.house_flow = s.house.flow_lmin;
    pin.barn_open = prev.barn_open;
    pin.house_open = prev.house_open;
    pin.pump_on = prev.pump_on;
    pin.valve_barn_fault = prev.barn_state == ValveState::Fault;
    pin.valve_house_fault = prev.house_state == ValveState::Fault;
    pin.clock_valid = have_clock;
    const PlantDecision d = plantEvaluate(plant_limits, plant_state, pin, dt_ms / 1000.0f);
    action = d.action;
    std::snprintf(reason, sizeof(reason), "%s", d.reason);

    // --- the barn programme ---------------------------------------------------
    bool barn_want = false;
    if (have_clock) {
        const bool may_start = d.allow_dispense && d.action != WaterAction::CloseAll;
        uint32_t countdown = 0;
        const Dispense disp = programmeTick(prog, pstate, day, second_of_day, may_start, countdown);
        barn_want = (disp == Dispense::Start || disp == Dispense::Running);
        if (disp == Dispense::Start) {
            barn_last_run_day = day;
            telemetryEvent("cycle-start");
        }
    }

    // --- the house line: an operator preference, vetoed by the plant ----------
    bool house_want = house_user_on && !house_blocked;
    if (d.action == WaterAction::CloseAll || d.action == WaterAction::CloseHouse) {
        house_want = false;
        if (prev.house_open && d.action == WaterAction::CloseHouse) house_blocked = true;
    }
    if (prev.house_open) house_last_run_day = day;

    // A branch nobody has drawn through in a week is a branch that grows biofilm
    // and, in a frost, freezes. Ten seconds a week, in the first hour of the day.
    if (have_clock && house_user_on && !prev.house_open && house_exercise_until == 0 &&
        programmeNeedsExercise(day - house_last_run_day, STAGNATION_DAYS, second_of_day, 3600u)) {
        house_exercise_until = programmeClock(day, second_of_day) + STAGNATION_RUN_S;
        pstate.exercise++;
        telemetryEvent("house-exercise");
    }
    if (house_exercise_until) {
        const uint64_t t = programmeClock(day, second_of_day);
        if (t < house_exercise_until) house_want = true;
        else house_exercise_until = 0;
    }

    if (d.action == WaterAction::CloseAll || d.action == WaterAction::CloseBarn ||
        d.action == WaterAction::Hold) {
        barn_want = false;
    }

    valvesDemand(WaterLine::Barn, barn_want);
    valvesDemand(WaterLine::House, house_want);
    valvesPumpAllowed(d.pump_interlocked);
    valvesPumpDemand(d.want_pump);
    valvesTick(dt_ms, s);

    // --- edges worth recording -------------------------------------------------
    const Outputs& now = valvesGet();
    if (prev.barn_open && !now.barn_open) {
        telemetryEventLitres("cycle-end", true, senseCycleLitres(true));
    }
    if (prev.house_open && !now.house_open) {
        telemetryEventLitres("cycle-end", false, senseCycleLitres(false));
    }
    if (prev.barn_state != ValveState::Fault && now.barn_state == ValveState::Fault) {
        telemetryEvent("barn-stuck");
    }
    if (prev.house_state != ValveState::Fault && now.house_state == ValveState::Fault) {
        telemetryEvent("house-stuck");
        house_blocked = true;
    }
    if (!tank_high_prev && s.tank_high) telemetryEvent("tank-full");
    tank_high_prev = s.tank_high;
    if (!leak_reported && s.leak) {
        leak_reported = true;
        telemetryEvent("leak");
    } else if (leak_reported && !s.leak) {
        leak_reported = false;
    }
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;                 // a stalled loop must not skip a cycle

    if (sched.due(job_control, dt)) {
        // The job's own elapsed time, not the loop's: the pulse-rate window and
        // the valve timers are in seconds, and feeding them a fraction of the
        // time that really passed makes the meter read high.
        controlTick(sched.elapsed(job_control));
        feedWatchdog();
        setStatusLed(valvesGet().barn_open || valvesGet().house_open);
    }
    if (sched.due(job_telem, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_persist, dt)) sensePersist();
    supervision();
}

#if !defined(RANCH_SIM)
void controlTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(20);
    }
}
#endif

}  // namespace

#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }

#if defined(ARDUINO)
// The console's input, as a command door: in the simulation there is no broker, so
// this is the only way the page can reach this board at all. Same parser the broker
// calls, so a verb the bench takes and a verb the console takes cannot drift apart.
// ARDUINO only: the host sandbox drives simCommand from its own test, and a stdin
// reader there would make a unit test wait on a terminal.
void consoleCommandPump() {
    static char line[40];
    static size_t n = 0;
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            line[n] = '\0';
            if (n) simCommand(line);
            n = 0;
            continue;
        }
        if (n < sizeof(line) - 1) line[n++] = static_cast<char>(c);
        else n = 0;
    }
}
#endif
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    // Establish the loop's own state too. A reboot is not a cold process image:
    // after a brownout the day latch and the fault latches still hold whatever the
    // last cycle left, and a board that thinks it already reported a leak stays
    // silent about the next one.
    day_initialised = false;
    clock_anchored = false;
    leak_reported = false;
    tank_high_prev = false;
    house_exercise_until = 0;
    house_blocked = false;
    barn_last_run_day = 0;
    house_last_run_day = 0;
    action = WaterAction::Run;
    reason[0] = '\0';
    sched.clear();
    valvesInit();
    senseInit();
    telemetryInit();
    paramsLoad();
    publishLimits();
    valvesSetLimits(valve_limits);
    senseLoad();
    programmeInit(pstate);
    plantReset(plant_state);
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_control = sched.add("control", 50);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_persist = sched.add("persist", 60000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the control loop has to be
    // created here: without it the board prints its banner and then stops.
    xTaskCreatePinnedToCore(controlTask, "water", STACK_CONTROL, nullptr, PRIO_CONTROL,
                            nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
#if defined(ARDUINO)
    consoleCommandPump();
#endif
    halSimPump();
    runJobs();
    halDelayMs(20);
#endif
}

}  // namespace ranch

#if !defined(RANCH_SIM)
void setup() { ranch::appSetup(); }
void loop() { vTaskDelete(NULL); }
#endif

/* ==================== simulator entry point ==================== */

void setup() { ranch::appSetup(); }
void loop() { ranch::appLoop(); }
