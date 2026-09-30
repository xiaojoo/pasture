// GENERATED FILE - do not edit, edit the project and re-run:
//   node tools/bundle.mjs
// Source: firmware/power + firmware/lib  (14 files, 91.0 KB before bundling)
// The simulated grid in hal_sim.cpp sags under the loads the board switches.
//
// Build the same code for hardware with:  pio run -d firmware/power

#define RANCH_SIM 1

/* ==================== src/board.h ==================== */

// Pin map and electrical configuration for the ranch switchboard monitor.
//
// What this board is: the thing that knows how much power the ranch is pulling,
// whether the supply is still healthy, and whether the water pump is allowed to
// start. It watches three phases, one residual-current transformer and the
// cabinet temperature, and it holds the two contactors that the shed/trip logic
// opens.
//
// Analog channel budget, which is what shaped the design: the classic ESP32 has
// eight ADC1 pins (32-39, six of them on the header) and ADC2 cannot be read with
// WiFi associated. Three
// phases of voltage and three of current use all six, so the temperature and the
// leakage transformer go to an ADS1115 on the same I2C bus as the RTC. That is
// not decoration - it is the reason this board has a second ADC on it.
//
// Proven by the static_asserts below: no duplicate pin, nothing on the SPI
// flash pins (6-11, 16-17), no output on the input-only pins (34-36, 39), and
// every analog input on ADC1.

#include <cstddef>

// --- measurement ------------------------------------------------------------
#define PIN_VA              36      // ADC1_CH0, ZMPT101 front end
#define PIN_VB              39      // ADC1_CH3 (VN)
#define PIN_VC              32      // ADC1_CH4
#define PIN_IA              33      // ADC1_CH5, SCT-013 style with a burden
#define PIN_IB              34      // ADC1_CH6
#define PIN_IC              35      // ADC1_CH7

// --- external ADC (cabinet temperature + residual current) and the RTC ------
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define ADS1115_ADDR        0x48
#define RTC_ADDR            0x68      // DS3231, the other half of the same bus
#define ADS_CH_TEMP         0
#define ADS_CH_RCD          1
// 2.048 V full scale: the temperature sensor and the RCD rectifier both stay
// inside a couple of volts, and the narrowest range gives the least-significant
// bit that matters.
#define ADS_FSR_MV          2048.0f
#define ADS_BITS            15

// --- contacts and outputs ---------------------------------------------------
#define PIN_ZC_A            23      // ZMPT101B comparator output on phase A:
                                    // frequency has to come from a zero crossing,
                                    // and the RMS front ends are DC by nature
#define PIN_BRK_AUX         18      // main breaker auxiliary, dry make to GND
#define PIN_PUMP_FB         19      // contactor auxiliary, dry make to GND
                                    // (both pulled up, so a closed contact reads LOW)
#define PIN_PUMP            25      // water pump contactor coil
#define PIN_LIGHTS          26      // yard lighting feeder contactor
#define PIN_WDT_FEED         4      // external watchdog, must toggle >= 10 Hz
#define PIN_LED_STATUS       2      // the dev kit's own LED

// --- local hand/off/auto switches -------------------------------------------
// A switchboard has to stay operable with the network down, so each feeder has a
// physical permit input and the MQTT request runs in parallel with it, not in
// place of it.
//
// Both are read active-high behind an internal pull-down, and the direction is a
// boot-safety decision rather than a style one: a permit wired the obvious way - a
// switch to ground on a pull-up - makes "pump permitted" the same level as the
// strapping state that puts the module into download mode on GPIO0 or lowers the
// flash voltage on GPIO12. Active-high with a pull-down boots with the switch open
// as "off" and with it closed as "on", and neither is a boot mode.
// Neither pin has a boot function at all now: 13 and 27 are ordinary IO on the
// DevKit V1 header, which is also why the two permits no longer sit on GPIO0 or on
// an ADC1 channel. The level is still wrong for the few hundred milliseconds before
// the pin is configured, which is why halInit() forces both coils off and the LEDC
// channels idle before anything reads a permit.
#define PIN_IN_PUMP         13      // permit the water pump feeder locally
#define PIN_IN_LIGHTS      27       // permit the yard lighting feeder locally

// --- what this board does not measure ---------------------------------------
// The front ends are rectified: they give a true RMS *magnitude* per channel and
// no phase angle, so power factor is a site parameter, not a reading. Every
// protection rule below works on volts, load percent, frequency, leakage and
// temperature, none of which need it; only kW and kWh carry the assumption, and
// the frame says so. A site that needs metered energy buys a Modbus meter.
#ifndef SITE_POWER_FACTOR
#define SITE_POWER_FACTOR   0.92f   // pump-and-motor average for this ranch
#endif

// --- transformer and divider scale ------------------------------------------
// Calibrated on the bench against a meter, and the two numbers that must be
// re-measured if the front end is ever rebuilt.
#ifndef NOMINAL_PHASE_VOLTS
#define NOMINAL_PHASE_VOLTS  230.0f
#endif
#ifndef VOLT_MV_PER_VOLT
#define VOLT_MV_PER_VOLT     4.0f     // 230 V rms presents about 920 mV
#endif
#ifndef CT_ZERO_MV
#define CT_ZERO_MV           1650.0f  // the burden's virtual ground
#endif
// The board's full-load current is 45 kVA / 3 / 230 V = 65 A per leg, so the
// burden is wound at 15 mV per amp: 65 A lands at 2.6 V and 100 A at 3.15 V, both
// inside the ADC. A 30 A clip-on cannot be used on this leg without a second
// turns ratio, and a 100 mV/A one saturates at 16 A, which is one pump.
// Anti-parallel silicon across the burden keeps a starting motor's 5x inrush
// pinned at the top of the range instead of letting the node fly.
#ifndef CT_MV_PER_AMP
#define CT_MV_PER_AMP        15.0f
#endif
#ifndef RCD_MV_PER_MA
#define RCD_MV_PER_MA        10.0f    // after the rectifier, 0 mA at 0 V
#endif
#ifndef TEMP_MV_PER_C
#define TEMP_MV_PER_C        10.0f    // LM35DZ at 10 mV/degC
#endif
#ifndef TEMP_OFFSET_MV
#define TEMP_OFFSET_MV       0.0f
#endif

// --- the site ---------------------------------------------------------------
#ifndef RATED_KVA
#define RATED_KVA            45.0f     // the transformer this board is fed from
#endif
#ifndef PUMP_KW
#define PUMP_KW              7.5f      // what shedding the pump is worth
#endif

// --- task layout ------------------------------------------------------------
#define PRIO_METER          16
#define PRIO_TELEMETRY      10
#define STACK_METER         6144
#define STACK_TELEMETRY     6144
#define TASK_WDT_TIMEOUT_MS 3000

// Sampling windows: the mains period is 20 ms at 50 Hz, and a window that is not
// a whole number of periods makes the RMS reading wobble with the load.
#define SAMPLE_WINDOW_MS    200
#define CONTROL_PERIOD_MS   100
#define TELEMETRY_HZ        2

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/power/state"
#define MQTT_TOPIC_CMD        "ranch/power/cmd"

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

constexpr bool pinsAvoidFlash(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p >= 6 && p <= 11) return false;
    if (p == 16 || p == 17) return false;
    return pinsAvoidFlash(v, n, i + 1);
}

constexpr bool outputsAreDriven(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p == 34 || p == 35 || p == 36 || p == 39) return false;
    return outputsAreDriven(v, n, i + 1);
}

// ADC1 on the classic ESP32 is GPIO 32-39 and nothing else: CH4-CH7 are 32-35 and
// CH0-CH3 are 36-39. Of those eight, the DevKit V1 header carries six (36 and 39 come
// out as VP and VN; 37 and 38 stay inside the module).
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

constexpr int kOutputs[] = { PIN_PUMP, PIN_LIGHTS, PIN_WDT_FEED, PIN_LED_STATUS,
                             PIN_I2C_SDA, PIN_I2C_SCL };
constexpr int kAnalog[] = { PIN_VA, PIN_VB, PIN_VC, PIN_IA, PIN_IB, PIN_IC };
constexpr int kAll[] = {
    PIN_VA, PIN_VB, PIN_VC, PIN_IA, PIN_IB, PIN_IC,
    PIN_I2C_SDA, PIN_I2C_SCL, PIN_ZC_A, PIN_BRK_AUX, PIN_PUMP_FB,
    PIN_IN_PUMP, PIN_IN_LIGHTS,
    PIN_PUMP, PIN_LIGHTS, PIN_WDT_FEED, PIN_LED_STATUS,
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

// Hardware abstraction for the switchboard monitor. Two implementations:
// hal_esp32.cpp reads the real front ends, hal_sim.cpp closes the loop against a
// model of the ranch's supply and loads, so the board's volts, amps and
// frequency come from a grid that sags when the pump starts rather than from the
// numbers the firmware expected to see.

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);
void consoleWrite(const char* data, size_t len);

// Rectified RMS front ends: the ADC reading is a DC quantity proportional to the
// channel's RMS value, which is why the frequency has to arrive on a digital
// input from the comparator instead.
uint16_t analogMillivolts(uint8_t pin);

// Transitions on phase A's comparator, as a time span rather than a count.
// Drains whatever accumulated since the last call into the interval between the
// first and last transition and the number of intervals that span covers.
//
// Period measurement rather than counting, because counting over a fixed window is
// wrong by up to half a transition: at 47.2 Hz a one second window reports 47.00
// or 47.50 on alternate samples, which straddles a 47.5 Hz threshold and resets
// the under-frequency debounce every other tick. That never trips.
void zeroCrossSpan(uint32_t& microseconds, uint16_t& intervals);

// External ADC (ADS1115) for the two channels that do not fit in ADC1. Returns
// false when the bus did not answer, which the meter turns into a sensor fault
// instead of a zero.
bool extAdcMV(uint8_t channel, float& millivolts);

// Contactors.
void setPump(bool on);
void setLights(bool on);
bool pumpFeedbackClosed();        // the contactor's own auxiliary
bool breakerClosed();             // main breaker auxiliary
bool pumpPermitClosed();          // the local hand/off/auto switch
bool lightsPermitClosed();

// Wall clock: today's energy resets on a local midnight, and only an RTC knows
// what that was after a power cut.
bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day);
void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second);
bool clockWasSet();

bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

void feedWatchdog();
const char* resetReason();
void setStatusLed(bool on);

#if defined(RANCH_SIM)
void halSimPump();
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
void halSimClock(bool enabled);

// Faults the plant can be told about, so a protection rule can be shown to
// actually react rather than only being able to report "all well".
enum SimFault : uint8_t {
    SIM_FAULT_NONE = 0,
    SIM_FAULT_PHASE_B,       // one leg collapses while the others stay up
    SIM_FAULT_RCD,           // leakage current climbs past the trip level
    SIM_FAULT_OVERTEMP,      // cabinet fan failed
    SIM_FAULT_BREAKER_OPEN,  // main breaker opened by hand
    SIM_FAULT_FREQ_LOW,      // a generator-sized supply drooping
    SIM_FAULT_EXT_ADC,       // the I2C ADC stops answering
    SIM_FAULT_SENSE_A,       // phase A's voltage sense comes off, its load keeps drawing
};
void halSimFault(SimFault which, bool on);
void halSimDemandScale(float scale);      // multiply the site's load curve
void halSimPermit(bool pump_line, bool closed);   // the local hand/off/auto switches

// What the simulated switchboard actually did, so the test can assert on the
// difference between "the board believed it was fine" and "nothing was energised".
struct GridCounters {
    float kw;
    float kva;
    float hz;
    float volts[3];
    float amps[3];
    float rcd_ma;
    float temp_c;
    bool breaker_closed;
    bool pump_energised;
    bool lights_energised;
    uint32_t pump_starts;
    uint32_t brownouts;       // times the pump was asked to run into a dead leg
};
void halGrid(GridCounters& out);
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

/* ==================== ../lib/power_meter.h ==================== */

// Three-phase electrical measurement maths. Platform neutral: it turns per-phase
// RMS values into power, energy, load factor, unbalance and phase loss, so the
// arithmetic a maintenance electrician would check with a multimeter can be
// proven on a host.
//
// Everything is in SI on the way in (volts, amps, seconds) and the frame units
// (kW, kWh, %, Hz, mA) on the way out.

#include <cmath>
#include <cstdint>

namespace ranch {

struct PhaseRms {
    float volts;
    float amps;
    float pf;            // displacement+distortion power factor, 0..1
    float hz;
};

struct PowerRead {
    float volts[3];      // per phase, so a rule can look at one leg
    float kw;            // real power, all phases
    float kva;           // apparent
    float kvar;          // reactive (from kva/kw)
    float pf;            // overall, not the average of the phases
    float hz;            // mean of the three legs' measured frequency
    float load_pct;      // against the rated apparent power
    bool phase_loss;     // a leg has collapsed while the others did not
    float unbalance_pct; // max deviation from the mean voltage
};

// The ratio checks below are expressed against the supply's nominal voltage as a
// parameter rather than against a learned average, so a slow sag is caught. There
// is deliberately no default: a board measuring a 415 V service has to say so.

// A day key rather than a timestamp: the ranch's "today" resets at local
// midnight, and the RTC is the only thing that knows what midnight is.
inline uint16_t dayKey(uint16_t year, uint8_t month, uint8_t day) {
    return static_cast<uint16_t>((year % 100u) * 372u + month * 31u + day);
}

struct Energy {
    float kwh_total;
    float kwh_today;
    uint16_t day;
    uint32_t resets;      // days rolled over while powered
};

inline void energyInit(Energy& e) {
    e.kwh_total = 0.0f;
    e.kwh_today = 0.0f;
    e.day = 0;
    e.resets = 0;
}

// Integrate real power, not current: a water pump at 0.6 power factor does not
// cost the ranch the kVA it draws.
inline void energyAdd(Energy& e, float kw, float dt_s, uint16_t day) {
    if (e.day != 0 && day != e.day) {
        e.kwh_today = 0.0f;
        e.resets++;
    }
    e.day = day;
    const float kwh = kw * dt_s / 3600.0f;
    e.kwh_today += kwh;
    e.kwh_total += kwh;
}

// Sum of the phases, each with its own power factor. Averaging the power factors
// first and multiplying the totals is the usual way this number ends up wrong on
// a site with one single-phase load on a three-phase feed.
//
// The nominal phase-to-neutral voltage is a parameter, not the constant below: a
// board rebuilt onto a 415 V delta service must not decide that every leg is a
// lost phase.
inline PowerRead measurePower(const PhaseRms p[3], float rated_kva, float nominal_phase_v) {
    PowerRead r{};
    const float leg_live = 0.5f * nominal_phase_v;
    float sum_va = 0.0f, sum_w = 0.0f, sum_vars = 0.0f;
    float v[3], vs = 0.0f;
    float hz_sum = 0.0f;
    uint8_t hz_n = 0;
    for (int i = 0; i < 3; ++i) {
        v[i] = p[i].volts;
        r.volts[i] = p[i].volts;
        vs += v[i];
        // A collapsed leg reports a frequency nobody can trust, so the average
        // is taken over the legs that are actually up.
        if (p[i].volts > leg_live && p[i].hz > 0.0f) {
            hz_sum += p[i].hz;
            hz_n++;
        }
        const float va = p[i].volts * p[i].amps;
        const float w = va * p[i].pf;
        sum_va += va;
        sum_w += w;
        // Reactive per phase, from the same triangle the meter would show.
        const float sin2 = 1.0f - p[i].pf * p[i].pf;
        sum_vars += va * std::sqrt(sin2 < 0.0f ? 0.0f : sin2);
    }
    r.hz = hz_n ? hz_sum / static_cast<float>(hz_n) : 0.0f;

    r.kw = sum_w / 1000.0f;
    r.kva = sum_va / 1000.0f;
    r.kvar = sum_vars / 1000.0f;
    r.pf = sum_va > 0.0f ? sum_w / sum_va : 0.0f;
    r.load_pct = rated_kva > 0.0f ? r.kva / rated_kva * 100.0f : 0.0f;

    // Phase loss: one leg under half of nominal while another is healthy. Both
    // conditions are needed - a supply that has simply been switched off is not
    // a lost phase, and a single collapsed leg on a running site is.
    const float mean = vs / 3.0f;
    int live = 0, dead = 0;
    float worst = 0.0f;
    for (int i = 0; i < 3; ++i) {
        if (v[i] > leg_live) live++;
        else dead++;
        const float d = std::fabs(v[i] - mean) / (mean > 0.0f ? mean : 1.0f);
        if (d > worst) worst = d;
    }
    r.phase_loss = dead > 0 && live > 0;
    r.unbalance_pct = worst * 100.0f;
    return r;
}

// Frequency from the comparator's transitions, measured as a period: the phase A
// input produces two transitions per mains cycle, so n intervals over t
// microseconds is n * 1e6 / (2t) hertz.
//
// This deliberately is not a count over a fixed window. A count is wrong by up to
// half a transition, which at one second and 47.2 Hz is 0.25 Hz either way - more
// than the distance to an under-frequency threshold, so the reading alternates
// between "in limit" and "out of limit" and a debounce that resets whenever the
// condition clears never reaches its time.
inline float frequencyFromSpan(uint32_t microseconds, uint16_t intervals) {
    if (microseconds == 0u || intervals < 1u) return 0.0f;
    return static_cast<float>(intervals) * 1000000.0f / (2.0f * static_cast<float>(microseconds));
}

// Current from the CT's milliVolts. The burden resistor and the CT ratio are
// board facts, and the zero offset matters: an ACS-style part sits at half the
// supply, so a raw reading of 1250 mV is 0 A, not 12.5 A.
inline float ampsFromMillivolts(uint16_t mv, uint16_t zero_mv, float mv_per_amp) {
    const float raw = (static_cast<float>(mv) - static_cast<float>(zero_mv)) / mv_per_amp;
    return raw < 0.0f ? -raw : raw;
}

}  // namespace ranch

/* ==================== ../lib/power_quality.h ==================== */

// Switchboard protection and load-shedding decisions. Platform neutral and side
// effect free, like the aircraft's failsafe table: measurements go in, one
// action plus one reason comes out, and the caller decides which contactor that
// opens.
//
// Order matters and is deliberate:
//   leakage > over-temperature > frequency > breaker > phase loss > overload > unbalance > voltage
// A live-earth leakage trips even when everything else is perfect, because the
// thing that rule protects is a person leaning on a water trough.

#include <cstdint>

namespace ranch {

enum class GridAction : uint8_t { Normal, Alarm, ShedLoad, Trip };

inline const char* gridActionName(GridAction a) {
    switch (a) {
        case GridAction::Normal:   return "NORMAL";
        case GridAction::Alarm:    return "ALARM";
        case GridAction::ShedLoad: return "SHED";
        case GridAction::Trip:     return "TRIP";
    }
    return "?";
}

struct GridLimits {
    float under_v_pct;             // per cent of nominal phase-to-neutral
    float over_v_pct;
    float hz_lo;
    float hz_hi;
    float shed_load_pct;           // sustained load that sheds non-critical feeders
    float trip_load_pct;           // sustained load that trips the board
    float unbalance_max_pct;
    float rcd_alarm_ma;
    float rcd_trip_ma;
    float temp_alarm_c;
    float temp_trip_c;
    float debounce_s;              // every rule must persist this long before acting
    float reclose_lockout_s;       // how long a trip keeps the feeders open
};

constexpr GridLimits GRID_DEFAULTS{
    /*under_v_pct*/   0.86f,     // -14 %: the supply tolerance, not a brownout probe
    /*over_v_pct*/    1.10f,
    /*hz_lo*/         47.5f,     // GB/IEC supply tolerance
    /*hz_hi*/         52.5f,
    /*shed_load_pct*/ 90.0f,
    /*trip_load_pct*/ 115.0f,
    /*unbalance_max*/  15.0f,
    /*rcd_alarm_ma*/  30.0f,
    /*rcd_trip_ma*/   100.0f,
    /*temp_alarm_c*/  55.0f,
    /*temp_trip_c*/   75.0f,
    /*debounce_s*/     2.0f,
    /*lockout_s*/    300.0f,
};

struct GridInput {
    PowerRead read;
    float nominal_v;        // the phase-to-neutral this board was set for
    float rcd_ma;
    float temp_c;
    bool breaker_closed;    // auxiliary contact on the main breaker
    bool pump_running;      // the biggest non-critical load on this board
    bool sensor_fault;      // an analog channel outside its valid range
};

struct GridState {
    float under_s, over_s, freq_s, phase_s, shed_s, overload_s;
    float unb_s, rcd_s, temp_s;
    float lockout_s;
};

struct GridDecision {
    GridAction action;
    bool open_pump;         // shed the water pump first: no animal dies of a dry hour
    bool lockout;           // feeders stay open; nothing may reclose automatically
    char reason[20];
};

inline void gridReset(GridState& s) {
    s.under_s = s.over_s = s.freq_s = s.phase_s = 0.0f;
    s.shed_s = s.overload_s = s.unb_s = s.rcd_s = s.temp_s = 0.0f;
    s.lockout_s = 0.0f;
}

inline void gridReason(char (&dst)[20], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 19; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// Compared per leg, because a board that averages the three phases calls a lost
// phase "slightly low".
inline bool anyLegBelow(const PowerRead& r, float lo) {
    for (int i = 0; i < 3; ++i) {
        if (r.volts[i] < lo) return true;
    }
    return false;
}

inline bool anyLegAbove(const PowerRead& r, float hi) {
    for (int i = 0; i < 3; ++i) {
        if (r.volts[i] > hi) return true;
    }
    return false;
}

inline GridDecision gridEvaluate(const GridLimits& lim, GridState& st,
                                 const GridInput& in, float dt_s) {
    GridDecision out{GridAction::Normal, false, false, ""};
    const float v_lo = lim.under_v_pct * in.nominal_v;
    const float v_hi = lim.over_v_pct * in.nominal_v;
    // Nothing is being measured: an unplug is not a brownout, and shedding the
    // pump because an ADC came loose would be the board's fault, not the grid's.
    const bool live = in.read.kva > 0.0f || in.read.hz > 10.0f;

    st.under_s = (live && anyLegBelow(in.read, v_lo)) ? st.under_s + dt_s : 0.0f;
    st.over_s = (live && anyLegAbove(in.read, v_hi)) ? st.over_s + dt_s : 0.0f;
    const bool freq = in.read.hz > 1.0f && (in.read.hz < lim.hz_lo || in.read.hz > lim.hz_hi);
    st.freq_s = freq ? st.freq_s + dt_s : 0.0f;
    st.phase_s = in.read.phase_loss ? st.phase_s + dt_s : 0.0f;
    st.shed_s = in.read.load_pct >= lim.shed_load_pct ? st.shed_s + dt_s : 0.0f;
    st.overload_s = in.read.load_pct >= lim.trip_load_pct ? st.overload_s + dt_s : 0.0f;
    st.unb_s = in.read.unbalance_pct > lim.unbalance_max_pct ? st.unb_s + dt_s : 0.0f;
    st.rcd_s = in.rcd_ma > lim.rcd_alarm_ma ? st.rcd_s + dt_s : 0.0f;
    st.temp_s = in.temp_c > lim.temp_alarm_c ? st.temp_s + dt_s : 0.0f;
    if (st.lockout_s > 0.0f) st.lockout_s -= dt_s;

    if (in.sensor_fault) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "SENSOR FAULT");
        return out;
    }

    // --- instantaneous, no debounce: these two are already fault currents -----
    if (in.rcd_ma >= lim.rcd_trip_ma) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "LEAKAGE");
        return out;
    }
    if (in.temp_c >= lim.temp_trip_c) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "OVERTEMP");
        return out;
    }
    if (st.freq_s >= lim.debounce_s) {
        out.action = GridAction::Trip;
        out.lockout = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "FREQUENCY");
        return out;
    }
    // The main breaker open while we still think we are serving loads is the one
    // condition that must never auto-reclose, lockout timer or not.
    if (!in.breaker_closed) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        gridReason(out.reason, "BREAKER OPEN");
        return out;
    }

    if (st.phase_s >= lim.debounce_s) {
        // A single-phase motor on a collapsed leg will not start and will overheat;
        // taking the pump off is cheaper than replacing it.
        out.action = GridAction::ShedLoad;
        out.open_pump = true;
        gridReason(out.reason, "PHASE LOSS");
        return out;
    }
    if (st.overload_s >= lim.debounce_s) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        st.lockout_s = lim.reclose_lockout_s;
        gridReason(out.reason, "OVERLOAD");
        return out;
    }
    if (st.shed_s >= lim.debounce_s) {
        out.action = GridAction::ShedLoad;
        out.open_pump = true;
        gridReason(out.reason, "OVERLOAD");
        return out;
    }
    if (st.under_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "UNDERVOLTAGE");
        return out;
    }
    if (st.over_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "OVERVOLTAGE");
        return out;
    }
    if (st.unb_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "UNBALANCE");
        return out;
    }
    if (st.rcd_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "LEAKAGE WARN");
        return out;
    }
    if (st.temp_s >= lim.debounce_s) {
        out.action = GridAction::Alarm;
        gridReason(out.reason, "OVERTEMP WARN");
        return out;
    }

    if (st.lockout_s > 0.0f) {
        out.action = GridAction::Trip;
        out.lockout = true;
        out.open_pump = true;
        gridReason(out.reason, "LOCKED OUT");
    }
    return out;
}

}  // namespace ranch

/* ==================== src/meter.h ==================== */

// The measurement side of the switchboard board: raw front ends in, RMS values,
// frequency, leakage, temperature and energy out.

#include <cstdint>

namespace ranch {

struct Metered {
    PhaseRms phase[3];        // what each leg is doing on its own
    PowerRead read;           // the three-phase totals derived from them
    float rcd_ma;             // residual current, mA
    float temp_c;             // cabinet temperature
    float pf;                 // the site parameter, see board.h
    Energy energy;
    bool sensor_fault;        // a channel outside its valid range, or the bus silent
};

void meterInit(float power_factor, float nominal_v, float rated_kva);
void meterLoad();             // restore kWh totals from NVS
void meterPersist();

// One control tick. The nominal voltage is passed in because a site rebuilt onto
// a 415 V delta service must not have to recompile.
void meterSample(uint32_t dt_ms, uint16_t day_key);

const Metered& meterGet();

// True when the analog chain has never produced a plausible sample. Every
// protection rule reads this before believing a measurement.
bool meterTrusted();

}  // namespace ranch

/* ==================== src/meter.cpp ==================== */


#include <cmath>

namespace ranch {
namespace {

// Time constants. A rectified RMS front end already smooths a cycle, so the
// filter here is only there to stop the display and the debounce timers from
// twitching on a 100 ms load step; 0.3 s is short enough that a real sag still
// arrives inside its own debounce window.
constexpr float TAU_VOLT_S = 0.3f;
constexpr float TAU_AMP_S = 0.3f;
constexpr float TAU_AUX_S = 2.0f;
constexpr uint16_t FREQ_WINDOW_MS = 1000;
constexpr uint16_t ADC_OPEN_MV = 60;
constexpr uint16_t ADC_RAIL_MV = 3250;
// 40 mV across the burden is 2.7 A with this board's 15 mV/A: above the noise
// floor of a CT with nothing in its aperture, below what any real load draws.
constexpr float CT_AMPS_PRESENT_MV = 40.0f;

// A channel that leaves its valid range for five seconds is a fault; one that
// comes back for ten is a fault that has gone. A latched-forever flag would keep
// alarming after a terminal is re-torqued, and a one-tick flag would alarm on a
// single blister.
constexpr uint16_t BAD_TRIP_TICKS = 50;
constexpr uint16_t GOOD_CLEAR_TICKS = 100;

Metered m{};
float filt_v[3] = {0.0f, 0.0f, 0.0f};
float filt_i[3] = {0.0f, 0.0f, 0.0f};
float filt_rcd = 0.0f;
float filt_temp = 25.0f;
uint16_t freq_ms = 0;
uint32_t freq_us = 0;
uint16_t freq_int = 0;
float measured_hz = 0.0f;
float meter_nominal_v = NOMINAL_PHASE_VOLTS;
float meter_rated_kva = RATED_KVA;
bool primed = false;
uint16_t bad_ticks = 0;
uint16_t good_ticks = 0;

float ema(float previous, float sample, float tau_s, float dt_s) {
    if (dt_s <= 0.0f) return previous;
    const float a = dt_s / (tau_s + dt_s);
    return previous + (sample - previous) * a;
}

// board.h states the divider the other way round: 230 V presents 920 mV, so four
// milliVolts of ADC per volt of mains.
float millivoltsToVolts(uint16_t mv) {
    return static_cast<float>(mv) / VOLT_MV_PER_VOLT;
}

float millivoltsToAmps(uint16_t mv) {
    return ampsFromMillivolts(mv, static_cast<uint16_t>(CT_ZERO_MV), CT_MV_PER_AMP);
}

void noteChannel(bool suspect) {
    if (suspect) {
        bad_ticks++;
        good_ticks = 0;
        if (bad_ticks >= BAD_TRIP_TICKS) m.sensor_fault = true;
    } else {
        good_ticks++;
        bad_ticks = 0;
        if (good_ticks >= GOOD_CLEAR_TICKS) m.sensor_fault = false;
    }
}

}  // namespace

void meterInit(float power_factor, float nominal, float rated) {
    m = Metered{};
    m.pf = power_factor > 0.2f && power_factor <= 1.0f ? power_factor : SITE_POWER_FACTOR;
    meter_nominal_v = nominal > 100.0f && nominal < 500.0f ? nominal : NOMINAL_PHASE_VOLTS;
    meter_rated_kva = rated > 1.0f ? rated : RATED_KVA;
    energyInit(m.energy);
    for (int i = 0; i < 3; ++i) {
        filt_v[i] = 0.0f;
        filt_i[i] = 0.0f;
    }
    filt_rcd = 0.0f;
    filt_temp = 25.0f;
    measured_hz = 0.0f;
    freq_ms = 0;
    freq_us = 0;
    freq_int = 0;
    bad_ticks = 0;
    good_ticks = 0;
    primed = false;
}

void meterLoad() {
    float v = 0.0f;
    if (nvGetF32("kwh_tot", v) && v >= 0.0f) m.energy.kwh_total = v;
    if (nvGetF32("kwh_day", v) && v >= 0.0f) m.energy.kwh_today = v;
    int32_t d = 0;
    if (nvGetI32("kwh_daykey", d) && d > 0) m.energy.day = static_cast<uint16_t>(d);
}

void meterPersist() {
    nvSetF32("kwh_tot", m.energy.kwh_total);
    nvSetF32("kwh_day", m.energy.kwh_today);
    nvSetI32("kwh_daykey", static_cast<int32_t>(m.energy.day));
}

void meterSample(uint32_t dt_ms, uint16_t day_key) {
    const float dt_s = dt_ms / 1000.0f;
    bool suspect = false;

    const uint16_t raw_v[3] = {
        analogMillivolts(PIN_VA), analogMillivolts(PIN_VB), analogMillivolts(PIN_VC),
    };
    const uint16_t raw_i[3] = {
        analogMillivolts(PIN_IA), analogMillivolts(PIN_IB), analogMillivolts(PIN_IC),
    };
    for (int i = 0; i < 3; ++i) {
        // A voltage channel sitting at its floor is ambiguous: it is what a lost
        // phase reads, and also what a disconnected sense wire reads. The current
        // channel on the same leg breaks the tie - amps behind a zero volt leg
        // means the sense came off, no amps means the leg really is down.
        // Believing the wrong one either sheds a working pump or ignores a
        // collapsed leg, and only one of those damages a motor.
        const bool amps_flowing =
            std::fabs(static_cast<float>(raw_i[i]) - CT_ZERO_MV) > CT_AMPS_PRESENT_MV;
        if (raw_v[i] > ADC_RAIL_MV) {
            suspect = true;                                  // divider shorted
        } else if (raw_v[i] < ADC_OPEN_MV) {
            if (amps_flowing) suspect = true;                 // sense wire off a live leg
            else filt_v[i] = 0.0f;                            // the leg is down
        } else {
            filt_v[i] = ema(filt_v[i], millivoltsToVolts(raw_v[i]), TAU_VOLT_S, dt_s);
        }

        if (raw_i[i] < ADC_OPEN_MV || raw_i[i] > ADC_RAIL_MV) {
            suspect = true;                                   // CT off, burden shorted
        } else {
            filt_i[i] = ema(filt_i[i], millivoltsToAmps(raw_i[i]), TAU_AMP_S, dt_s);
        }
        m.phase[i].volts = filt_v[i];
        m.phase[i].amps = filt_i[i];
        m.phase[i].pf = m.pf;
        m.phase[i].hz = measured_hz;
    }

    float mv = 0.0f;
    if (extAdcMV(ADS_CH_RCD, mv)) filt_rcd = ema(filt_rcd, mv / RCD_MV_PER_MA, TAU_AUX_S, dt_s);
    else suspect = true;
    if (extAdcMV(ADS_CH_TEMP, mv)) {
        filt_temp = ema(filt_temp, (mv - TEMP_OFFSET_MV) / TEMP_MV_PER_C, TAU_AUX_S, dt_s);
    } else {
        suspect = true;
    }
    m.rcd_ma = filt_rcd;
    m.temp_c = filt_temp;
    noteChannel(suspect);

    // Frequency from the period of the last second's transitions. The span is
    // drained and published in one place, because a rate that two callers both
    // drain is a rate that silently halves.
    uint32_t span_us = 0;
    uint16_t span_int = 0;
    zeroCrossSpan(span_us, span_int);
    freq_us += span_us;
    freq_int += span_int;
    freq_ms += static_cast<uint16_t>(dt_ms);
    if (freq_ms >= FREQ_WINDOW_MS) {
        // The denominator is the span the intervals actually cover, not the tick
        // counter that decided to publish now.
        measured_hz = frequencyFromSpan(freq_us, freq_int);
        freq_ms = 0;
        freq_us = 0;
        freq_int = 0;
    }

    m.read = measurePower(m.phase, meter_rated_kva, meter_nominal_v);
    m.read.hz = measured_hz;

    if (!primed) {
        // The first tick has no previous sample: charging the energy counter with
        // a filter still at zero would under-read, and with a filter still at the
        // inrush value would over-read for the rest of the day.
        primed = true;
    } else {
        energyAdd(m.energy, m.read.kw, dt_s, day_key);
    }
}

const Metered& meterGet() { return m; }

bool meterTrusted() { return !m.sensor_fault && m.read.hz > 10.0f; }

}  // namespace ranch

/* ==================== src/feeders.h ==================== */

// The only module that drives a contactor coil on this board.
//
// Everything else expresses a wish: the water controller asks for the pump, the
// lighting controller asks for the yard feeders, the protection logic says "shed"
// or "trip". This module turns those into coil states with the rules that only
// the output stage is allowed to enforce:
//
//  - a trip is a veto, not a request. While the lockout is latched no demand of
//    any kind can energise anything, and the shed timer cannot expire its way
//    back into a live fault.
//  - shed is sticky within a tick but not across one. The protection layer
//    re-evaluates every 100 ms; if it stops saying "shed" the pump may come back
//    without a reclose command, which is what a momentary overload deserves.
//  - the coil is confirmed against its own auxiliary. A contactor that hums and
//    never closes is a burnt coil or a jammed armature, and the ranch finds that
//    out from a counter rather than from dry troughs two hours later. Staying
//    closed when the coil is dropped is worse: it means a shed or a trip did
//    nothing, so both directions are checked and both latch.
//  - a phase that has collapsed is not a place to start a three-phase motor. The
//    demand is held, not dropped, so the pump starts by itself when the supply
//    returns.

#include <cstdint>

namespace ranch {

enum class Feeder : uint8_t { Pump, Lights };

struct FeederOutputs {
    bool pump;                   // coil state we commanded
    bool lights;
    bool pump_closed;            // auxiliary feedback, only meaningful when energised
    bool lights_closed;
    bool trip_latched;           // nothing may close until an explicit reclose
    bool shed;                   // the protection layer is currently refusing the pump
    bool held;                   // a demand is waiting on the trip/shed to clear
    bool contactor_fault;        // commanded, and the auxiliary never closed
    bool contactor_welded;       // commanded open, and the auxiliary never opened
    uint32_t pump_starts;
    uint32_t pump_failures;
    uint32_t light_starts;
    uint32_t shed_s;             // how long the pump has been shed, for the frame
    uint32_t lockout_s;          // remaining trip lockout
};

void feedersInit();

// Wishes. Level, not edge: the last tick wins, so a protection close sticks even
// while the programme is still asking for water.
void feedersDemand(Feeder which, bool on);

// The protection layer's two authorities.
void feedersShed(bool on);           // shed the non-critical feeder while held
void feedersTrip(bool on);           // latch: opens everything and stays open
void feedersSupplyOk(bool ok);       // a collapsed leg parks a pending start

// Explicit and the only way a latch clears. Also clears a confirmed contactor
// fault, because the coil is what gets replaced.
void feedersReclose();

// Writes the coils. Call once per control tick.
void feedersTick(uint32_t dt_ms);

const FeederOutputs& feedersGet();

// What the rest of the firmware should report as "the pump is running": commanded
// AND confirmed. A motor that is being fed a single phase is not running, and a
// dashboard that says otherwise is the reason nobody trusts dashboards.
bool feedersPumpRunning();
bool feedersLightsRunning();

}  // namespace ranch

/* ==================== src/feeders.cpp ==================== */



namespace ranch {
namespace {

// A contactor's armature travels in tens of milliseconds. 250 ms is generous
// enough for a cold coil on a sagging supply and short enough that the ranch
// learns about a burnt coil inside one irrigation window.
constexpr uint16_t FEEDBACK_MS = 250;

struct {
    bool demand_pump;
    bool demand_lights;
    bool shed;
    bool trip;
    bool supply_ok;
    bool fault_latched;      // commanded closed and the auxiliary never made
    bool welded;             // commanded open and the auxiliary never dropped
    bool was_pump;
    bool was_lights;
    uint16_t close_ms;
    uint16_t open_ms;
    FeederOutputs o;
} st{};

}  // namespace

void feedersInit() {
    st = {};
    st.supply_ok = true;
    st.o = FeederOutputs{};
    setPump(false);
    setLights(false);
}

void feedersDemand(Feeder which, bool on) {
    if (which == Feeder::Pump) st.demand_pump = on;
    else st.demand_lights = on;
}

void feedersShed(bool on) { st.shed = on; }

void feedersTrip(bool on) {
    st.trip = on;
    if (on) st.shed = false;   // a trip outranks a shed; the latch is what sticks
}

void feedersSupplyOk(bool ok) { st.supply_ok = ok; }

void feedersReclose() {
    st.trip = false;
    st.shed = false;
    st.fault_latched = false;
    st.welded = false;
    st.close_ms = 0;
    st.open_ms = 0;
    st.o.trip_latched = false;
    st.o.shed = false;
    st.o.contactor_fault = false;
    st.o.contactor_welded = false;
    st.o.shed_s = 0;
    st.o.lockout_s = 0;
}

void feedersTick(uint32_t dt_ms) {
    FeederOutputs& o = st.o;

    o.trip_latched = st.trip;
    o.shed = st.shed && !st.trip;
    o.contactor_fault = st.fault_latched;
    o.contactor_welded = st.welded;

    // A coil that will not prove it closed stays off. Nothing here re-drives it:
    // the point of latching is that the next cycle cannot quietly try again on the
    // down-winding that is holding the armature.
    const bool may_run = !st.trip && !o.shed && !st.fault_latched && !st.welded;
    // Starting and running are two different authorities. Without a measurement
    // the board cannot prove the legs are all up, and that is a reason not to
    // close a contactor on a three-phase motor - it is not a reason to open one
    // that is already carrying the pump, which is its own transient and leaves the
    // troughs dry on the strength of an ADC that came loose.
    const bool may_start = may_run && st.supply_ok;
    // A lamp is not an induction motor: an unproven supply is no reason to drop
    // the yard lighting, so the lights answer only to the trip and to a welded
    // contactor.
    const bool lights_allowed = !st.trip && !st.welded;

    o.pump = st.demand_pump && (st.was_pump ? may_run : may_start);
    o.lights = st.demand_lights && lights_allowed;
    // The demand is still there, which is the difference between "the pump is off"
    // and "the water controller is waiting and will get its pump back on its own".
    o.held = (st.demand_pump && !o.pump) || (st.demand_lights && !o.lights);

    setPump(o.pump);
    setLights(o.lights);

    // How long the shed has been running, in whole seconds, so the frame can say
    // "shed for 40 s" rather than only "shed".
    if (o.shed) o.shed_s += dt_ms / 1000u;
    else o.shed_s = 0;
    if (st.trip) o.lockout_s += dt_ms / 1000u;

    // The auxiliary is read every tick rather than only when the coil changed: it
    // is also the input the protection layer asks about, and a contactor that
    // chatters out after closing must not stay believed.
    o.pump_closed = pumpFeedbackClosed();
    o.lights_closed = o.lights;      // the yard feeder has no auxiliary; the coil is all we get

    if (o.pump) {
        st.open_ms = 0;
        if (o.pump_closed) st.close_ms = 0;
        else {
            st.close_ms = static_cast<uint16_t>(st.close_ms + dt_ms);
            if (st.close_ms >= FEEDBACK_MS) {
                st.fault_latched = true;
                st.close_ms = 0;
                o.pump = false;
                o.contactor_fault = true;
                o.pump_failures++;
                setPump(false);
            }
        }
    } else {
        st.close_ms = 0;
        if (o.pump_closed) {
            st.open_ms = static_cast<uint16_t>(st.open_ms + dt_ms);
            if (st.open_ms >= FEEDBACK_MS) {
                st.welded = true;
                st.open_ms = 0;
                o.contactor_welded = true;
                o.pump_failures++;
            }
        } else {
            st.open_ms = 0;
        }
    }

    // Starts are edges, not levels: a count that ticks up every 100 ms while the
    // pump runs says nothing about the contactor, which is the only reason to
    // keep the number.
    if (o.pump && !st.was_pump) o.pump_starts++;
    st.was_pump = o.pump;
    if (o.lights && !st.was_lights) o.light_starts++;
    st.was_lights = o.lights;
}

const FeederOutputs& feedersGet() { return st.o; }

bool feedersPumpRunning() {
    // Commanded AND confirmed. The protection layer feeds this back in as
    // pump_running, so an answer built on the coil alone would let a welded
    // contactor report "shed" while it is still drawing 7.5 kW.
    return st.o.pump && st.o.pump_closed;
}

bool feedersLightsRunning() { return st.o.lights; }

}  // namespace ranch

/* ==================== src/telemetry.h ==================== */

// State uplink for the switchboard board.
//
// The field names are the contract with the ranch dashboard, and each one is
// allowed to mean exactly one thing: `va`/`vb`/`vc` are phase-to-neutral volts,
// `ia`/`ib`/`ic` are the same legs' current, `load` is per cent of the board's
// rated kVA, `pump`/`lit` are what the contactors are *doing* (confirmed, not
// commanded), and `act`/`why` are the protection decision and its one reason.
// `link=down` and `ok=0` say the board could not measure, which is not the same
// claim as "the supply is fine".

#include <cstddef>

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

/* ==================== src/telemetry.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

constexpr size_t FRAME_CAP = 256;

UplinkStats up{};
char frame[FRAME_CAP];
char broker_host[64] = "";
char broker_client[24] = "power";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

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
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "power");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const PowerReport& r) {
    const PowerRead& d = r.m.read;
    FrameWriter w(frame, sizeof(frame));
    w.begin("POWER");
    w.add("va", d.volts[0], 1);
    w.add("vb", d.volts[1], 1);
    w.add("vc", d.volts[2], 1);
    w.add("ia", r.m.phase[0].amps, 1);
    w.add("ib", r.m.phase[1].amps, 1);
    w.add("ic", r.m.phase[2].amps, 1);
    w.add("kw", d.kw, 1);
    w.add("kva", d.kva, 1);
    w.add("pf", d.pf, 2);
    w.add("hz", d.hz, 2);
    w.add("load", d.load_pct, 0);
    w.add("unb", d.unbalance_pct, 0);
    w.add("rcd", r.m.rcd_ma, 0);
    w.add("temp", r.m.temp_c, 0);
    w.add("brk", r.breaker_closed ? 1 : 0);
    // Confirmed, not commanded: this is the field that decides whether the
    // dashboard is showing the switchboard or showing an intention.
    w.add("pump", r.fo.pump && r.fo.pump_closed ? 1 : 0);
    w.add("lit", r.fo.lights ? 1 : 0);
    w.add("kwhd", r.m.energy.kwh_today, 1);
    w.add("kwh", r.m.energy.kwh_total, 1);
    w.add("act", gridActionName(r.action));
    w.add("up", static_cast<int>(r.uptime_s > 999999u ? 999999u : r.uptime_s));
    w.add("link", wifiUp() ? "up" : "down");
    // Only carried when they say something; an always-present ok=1 is a field
    // nobody reads and every reader has to remember to check.
    if (r.m.sensor_fault) w.add("ok", 0);
    if (d.phase_loss) w.add("phl", 1);
    if (r.fo.held) w.add("held", 1);
    if (r.fo.contactor_fault) w.add("coil", "fault");
    if (r.fo.contactor_welded) w.add("coil", "welded");
    if (r.fo.shed) w.add("shed", static_cast<int>(r.fo.shed_s));
    if (r.fo.trip_latched) w.add("lock", static_cast<int>(r.fo.lockout_s));
    if (!r.clock_valid) w.add("clock", "unset");
    if (r.reason && *r.reason) w.add("why", r.reason);
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

void telemetryEvent(const char* kind, const char* why) {
    char line[72];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    if (why && *why) w.add("why", why);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch

/* ==================== src/hal_sim.cpp ==================== */

// A model of the ranch's supply and everything hung off it.
//
// The board measures milliVolts, so that is the only language this file speaks:
// it decides what the grid is doing, what the loads draw, and then presents the
// front-end milliVolts those produce. Nothing here hands the firmware a float of
// "volts" or a boolean of "phase loss" - those are the firmware's job, and the
// whole point of closing the loop is that the numbers arrive through the same
// dividers, burdens and debounce timers the real board has to use.
//
// What the model has to get right for the tests to mean anything:
//  - voltage sags with current, through the transformer's own regulation, so a
//    pump starting on a weak leg can actually reach an undervoltage window
//  - a collapsed leg takes its single-phase loads with it and stalls the
//    three-phase pump, which is the failure the shedding rule exists for
//  - leakage and cabinet temperature behave like thermal and insulation
//    quantities: they approach a new target over seconds, not on one tick
#if defined(RANCH_SIM)

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace ranch {
namespace {

// 5% of a 230 V leg at the board's rated 65 A. Site loads are converted to
// current at the *nominal* voltage and dropped across that impedance: the
// self-consistent quadratic (current depends on the voltage the current sags)
// resolves within a percent of this at every load the board can carry, and one
// explicit step is easier to audit than a solver.
constexpr float SOURCE_OHM = 0.177f;

// The ranch's single-phase load is not evenly split, and the unbalance rule is
// only testable if the model knows that.
constexpr float SHARE[3] = {0.40f, 0.33f, 0.27f};
constexpr float LIGHTS_KW = 2.6f;
constexpr uint16_t INRUSH_MS = 400;
constexpr float INRUSH_TIMES = 5.0f;
constexpr float STALL_TIMES = 1.6f;      // a pump on two phases: high current, no work
constexpr uint16_t ARMATURE_MS = 20;
// The burden's anti-parallel clamp: the node cannot exceed these no matter what
// the primary does, which is why an inrush reads as a pegged channel rather than
// as a sensor off its rails.
constexpr float ADC_CLAMP_MV = 3200.0f;

uint32_t g_now = 0;
uint32_t sod = 6u * 3600u + 30u;      // 06:30, the morning water window
uint32_t sod_ms = 0;
uint16_t yy = 2026;
uint8_t mm = 4;
uint8_t dd = 18;
bool sim_clock_ok = true;

bool pump_coil = false;
bool lights_coil = false;
bool aux_pump = false;
bool permit_pump = true;
bool permit_lights = true;
bool breaker = true;
uint16_t armature_ms = 0;
uint16_t inrush_ms = 0;
bool was_aux = false;
bool counted_brownout = false;

bool f_phase_b = false;
bool f_rcd = false;
bool f_overtemp = false;
bool f_breaker = false;
bool f_freq = false;
bool f_ext = false;
bool f_sense_a = false;
float demand_scale = 1.0f;

float leg_v[3] = {NOMINAL_PHASE_VOLTS, NOMINAL_PHASE_VOLTS, NOMINAL_PHASE_VOLTS};
float leg_i[3] = {0.0f, 0.0f, 0.0f};
float hz = 50.02f;
float rcd_ma = 6.0f;
float temp_c = 22.0f;
// The comparator's own timeline. Kept in microseconds because the quantity the
// firmware measures is a period, and a millisecond clock would put a 0.1 Hz error
// in every reading at 50 Hz.
uint32_t zc_clock_us = 0;
uint32_t zc_next_us = 0;
uint32_t zc_first_us = 0;
uint32_t zc_last_us = 0;
uint16_t zc_trans = 0;
GridCounters counters{};

#if defined(RANCH_HOST)
uint32_t g_virt = 0;
inline uint32_t rawMillis() { return g_virt; }
inline void rawSleep(uint32_t ms) { g_virt += ms; }
#else
inline uint32_t rawMillis() { return ::millis(); }
inline void rawSleep(uint32_t ms) { ::delay(ms); }
#endif

float gaussian(float x, float centre, float width) {
    const float d = (x - centre) / width;
    return std::exp(-0.5f * d * d);
}

// Morning feed, evening water, and the office trailer drawing all day.
float siteBaseKw() {
    const float hour = (sod % 86400u) / 3600.0f;
    const float morning = 5.0f * gaussian(hour, 7.0f, 1.2f);
    const float evening = 4.5f * gaussian(hour, 19.0f, 1.5f);
    return (3.0f + morning + evening) * demand_scale;
}

float ambientC() {
    const float hour = (sod % 86400u) / 3600.0f;
    return 12.0f + 14.0f * gaussian(hour, 14.0f, 5.0f);
}

void approach(float& value, float target, float tau_s, float dt_s) {
    if (tau_s <= 0.0f) { value = target; return; }
    value += (target - value) * (1.0f - std::exp(-dt_s / tau_s));
}

uint16_t voltsToMV(float v) {
    float mv = v * VOLT_MV_PER_VOLT;
    if (mv < 0.0f) mv = 0.0f;
    if (mv > ADC_CLAMP_MV) mv = ADC_CLAMP_MV;
    return static_cast<uint16_t>(mv);
}

uint16_t ampsToMV(float a) {
    float mv = CT_ZERO_MV + std::fabs(a) * CT_MV_PER_AMP;
    if (mv < 0.0f) mv = 0.0f;
    if (mv > ADC_CLAMP_MV) mv = ADC_CLAMP_MV;
    return static_cast<uint16_t>(mv);
}

void gridStep(uint32_t dt_ms);

void pump() {
    const uint32_t t = rawMillis();
    if (t == g_now) return;
    uint32_t dt = t - g_now;
    g_now = t;
    if (dt > 100) dt = 100;
    gridStep(dt);
}

void gridStep(uint32_t dt_ms) {
    const float dt_s = dt_ms / 1000.0f;

    if (sim_clock_ok) {
        sod_ms += dt_ms;
        while (sod_ms >= 1000u) {
            sod_ms -= 1000u;
            sod++;
            if (sod >= 86400u) {
                sod -= 86400u;
                dd++;
                if (dd > 28) {
                    dd = 1;
                    mm++;
                    if (mm > 12) { mm = 1; yy++; }
                }
            }
        }
    }

    // The contactor's armature: the coil closes, and the auxiliary - the only
    // thing the firmware can actually see - follows a few milliseconds later.
    if (pump_coil && !aux_pump) {
        armature_ms = static_cast<uint16_t>(armature_ms + dt_ms);
        if (armature_ms >= ARMATURE_MS) { aux_pump = true; armature_ms = 0; counters.pump_starts++; }
    } else if (!pump_coil && aux_pump) {
        aux_pump = false;
        armature_ms = 0;
    }

    const float phase_open_v[3] = {NOMINAL_PHASE_VOLTS,
                                   f_phase_b ? 0.0f : NOMINAL_PHASE_VOLTS,
                                   NOMINAL_PHASE_VOLTS};
    bool dead[3] = {phase_open_v[0] < 1.0f, phase_open_v[1] < 1.0f, phase_open_v[2] < 1.0f};
    uint8_t dead_n = 0;
    for (bool d : dead) if (d) dead_n++;
    const bool stalled = aux_pump && dead_n > 0;

    if (inrush_ms > 0 && aux_pump) {
        inrush_ms = static_cast<uint16_t>(inrush_ms > dt_ms ? inrush_ms - dt_ms : 0);
    }
    if (aux_pump && !was_aux) inrush_ms = INRUSH_MS;
    was_aux = aux_pump;

    const float rated_pump_a = PUMP_KW * 1000.0f / (3.0f * NOMINAL_PHASE_VOLTS * SITE_POWER_FACTOR);
    float amps[3] = {0.0f, 0.0f, 0.0f};
    const float base_kw = siteBaseKw();

    // Currents first, at the nominal voltage: a leg that has collapsed carries
    // nothing, so the site's single-phase load on that leg simply stops drawing.
    for (int k = 0; k < 3; ++k) {
        if (!dead[k]) {
            amps[k] += base_kw * SHARE[k] * 1000.0f /
                       (NOMINAL_PHASE_VOLTS * SITE_POWER_FACTOR);
        }
    }
    if (aux_pump) {
        // A stalled induction motor draws over the two remaining legs and does
        // almost no mechanical work: its kVA jumps while its kW does not, which is
        // exactly why the shed rule reads load percent and not kilowatts.
        const float times = inrush_ms > 0 ? INRUSH_TIMES : (stalled ? STALL_TIMES : 1.0f);
        for (int k = 0; k < 3; ++k) if (!dead[k]) amps[k] += rated_pump_a * times;
    }
    if (lights_coil) {
        for (int k = 0; k < 3; ++k) {
            if (!dead[k]) {
                amps[k] += LIGHTS_KW * SHARE[k] * 1000.0f /
                           (NOMINAL_PHASE_VOLTS * SITE_POWER_FACTOR);
            }
        }
    }

    float kw = 0.0f;
    float kva = 0.0f;
    for (int k = 0; k < 3; ++k) {
        leg_i[k] = amps[k];
        leg_v[k] = dead[k] ? 0.0f : phase_open_v[k] - amps[k] * SOURCE_OHM;
        if (leg_v[k] < 0.0f) leg_v[k] = 0.0f;
        // The same products the firmware will compute from the front ends, so a
        // disagreement between the two is a bug in one of them rather than a
        // number this file handed over.
        kva += leg_v[k] * leg_i[k] / 1000.0f;
        kw += leg_v[k] * leg_i[k] * SITE_POWER_FACTOR / 1000.0f;
    }

    // Frequency: a stiff grid with a small droop, or a generator-sized one sagging
    // under the same load.
    const float hz_target = f_freq ? 47.2f : 50.02f - 0.0008f * kw;
    approach(hz, hz_target, 1.0f, dt_s);

    // Insulation leakage grows with what is energised; a faulted pump winding is
    // the case the 100 mA rule is written for.
    const float rcd_target = f_rcd ? 150.0f : 6.0f + (aux_pump ? 0.35f * PUMP_KW : 0.0f);
    approach(rcd_ma, rcd_target, 2.0f, dt_s);

    const float load_frac = kva / RATED_KVA;
    approach(temp_c, f_overtemp ? 88.0f : ambientC() + 34.0f * load_frac, 12.0f, dt_s);

    // Zero crossings on phase A's comparator, stamped on the microsecond clock.
    // Two transitions per mains cycle, and the interval follows the frequency the
    // grid has *now*, so a droop shows up as a longer period the way the real
    // comparator's square wave does.
    zc_clock_us += static_cast<uint32_t>(dt_ms) * 1000u;
    uint32_t half_us = static_cast<uint32_t>(500000.0f / (hz > 1.0f ? hz : 1.0f));
    if (half_us < 1u) half_us = 1u;
    if (zc_next_us == 0u) zc_next_us = zc_clock_us + half_us;
    while (zc_clock_us >= zc_next_us) {
        if (zc_trans == 0u) zc_first_us = zc_next_us;
        zc_trans++;
        zc_last_us = zc_next_us;
        zc_next_us += half_us;
    }

    if (stalled && !counted_brownout) {
        counters.brownouts++;
        counted_brownout = true;
    } else if (!stalled) {
        counted_brownout = false;
    }

    counters.kw = kw;
    counters.kva = kva;
    counters.hz = hz;
    counters.rcd_ma = rcd_ma;
    counters.temp_c = temp_c;
    counters.breaker_closed = breaker && !f_breaker;
    counters.pump_energised = aux_pump;
    counters.lights_energised = lights_coil;
    for (int k = 0; k < 3; ++k) {
        counters.volts[k] = leg_v[k];
        counters.amps[k] = leg_i[k];
    }
}

struct Kv {
    char key[16];
    uint8_t kind;
    int32_t i;
    float f;
    char s[64];
};
Kv nvs_store[16];

Kv* findKv(const char* key, bool create) {
    const size_t klen = std::strlen(key);
    Kv* slot = nullptr;
    for (auto& k : nvs_store) {
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

void halInit() {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.begin(115200);
#endif
    g_now = rawMillis();
    pump_coil = false;
    lights_coil = false;
    aux_pump = false;
    breaker = true;
    counters = GridCounters{};
}

uint32_t halMillis() { return g_now; }
void halDelayMs(uint32_t ms) { rawSleep(ms); pump(); }
void halSimPump() { pump(); }

void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day) {
    sod = second_of_day;
    yy = year;
    mm = month;
    dd = day;
    sod_ms = 0;
    sim_clock_ok = true;
}

void halSimClock(bool enabled) { sim_clock_ok = enabled; }

void halSimFault(SimFault which, bool on) {
    switch (which) {
        case SIM_FAULT_PHASE_B:      f_phase_b = on; break;
        case SIM_FAULT_RCD:          f_rcd = on; break;
        case SIM_FAULT_OVERTEMP:     f_overtemp = on; break;
        case SIM_FAULT_BREAKER_OPEN: f_breaker = on; break;
        case SIM_FAULT_FREQ_LOW:     f_freq = on; break;
        case SIM_FAULT_EXT_ADC:      f_ext = on; break;
        case SIM_FAULT_SENSE_A:      f_sense_a = on; break;
        default: break;
    }
}

void halSimDemandScale(float scale) { demand_scale = scale > 0.0f ? scale : 1.0f; }

void consoleWrite(const char* data, size_t len) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#else
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
#endif
}

uint16_t analogMillivolts(uint8_t pin) {
    // A floating voltage input sits at its floor while the load behind it keeps
    // drawing, which is exactly the reading the meter has to tell apart from a
    // lost phase.
    if (pin == PIN_VA && f_sense_a) return 0;
    if (pin == PIN_VA) return voltsToMV(leg_v[0]);
    if (pin == PIN_VB) return voltsToMV(leg_v[1]);
    if (pin == PIN_VC) return voltsToMV(leg_v[2]);
    if (pin == PIN_IA) return ampsToMV(leg_i[0]);
    if (pin == PIN_IB) return ampsToMV(leg_i[1]);
    if (pin == PIN_IC) return ampsToMV(leg_i[2]);
    return 0;
}

void zeroCrossSpan(uint32_t& microseconds, uint16_t& intervals) {
    // Same contract as the ESP32 back-end: the span from the first transition in
    // the window to the last, and the intervals between them. A window with one
    // transition has no interval to measure, and reporting a rate for it would be
    // inventing the number.
    if (zc_trans >= 2u) {
        microseconds = zc_last_us - zc_first_us;
        intervals = static_cast<uint16_t>(zc_trans - 1u);
    } else {
        microseconds = 0u;
        intervals = 0u;
    }
    zc_trans = 0;
    zc_first_us = zc_last_us;
}

bool extAdcMV(uint8_t channel, float& millivolts) {
    if (f_ext) return false;
    if (channel == ADS_CH_RCD) millivolts = rcd_ma * RCD_MV_PER_MA;
    else if (channel == ADS_CH_TEMP) millivolts = temp_c * TEMP_MV_PER_C + TEMP_OFFSET_MV;
    else return false;
    if (millivolts < 0.0f) millivolts = 0.0f;
    if (millivolts > ADS_FSR_MV) millivolts = ADS_FSR_MV;
    return true;
}

void setPump(bool on) { pump_coil = on; }
void setLights(bool on) { lights_coil = on; }
bool pumpFeedbackClosed() { return aux_pump; }

// Both switches ship closed so the simulated cabinet does something on its own in
// the browser; a test that wants to watch a stop opens them.
bool pumpPermitClosed() { return permit_pump; }
bool lightsPermitClosed() { return permit_lights; }

void halSimPermit(bool pump_line, bool on) {
    if (pump_line) permit_pump = on;
    else permit_lights = on;
}
bool breakerClosed() { return breaker && !f_breaker; }

// The dev kit's own LED. In the browser the panel mirrors the frame instead, so
// this is for the person standing at the cabinet with a multimeter.
void setStatusLed(bool) {}

bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (!sim_clock_ok) return false;
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
    sod_ms = 0;
    sim_clock_ok = true;
}

bool clockWasSet() { return sim_clock_ok; }

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
int wifiRssi() { return -55; }
void wifiReconnect() {}

void feedWatchdog() {}
const char* resetReason() { return "sim"; }

void halGrid(GridCounters& out) { out = counters; }

}  // namespace ranch

#endif  // RANCH_SIM

/* ==================== src/main.cpp ==================== */

// Switchboard board entry point: protection thresholds, the control loop, and the
// commands the rest of the ranch sends it.
//
// One cooperative task measures, decides and drives in that fixed order every
// tick. The order is the design: the meters have to be read before the decision
// can be made, the decision before the contactors move, and the contactor
// feedback has to be read after it moved so the confirmation lands in the same
// frame the dashboard sees.
//
// What may open a coil is deliberately hard to reach. The protection layer can
// only ever refuse; the demand comes from the local hand-off-auto switch and the
// water controller's request, and a trip that came from leakage, temperature or
// an overload stays open until a person says otherwise.
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

// The water controller renews its pump request once a second. Five seconds of
// silence is four missed refreshes, and after that the pump stops on its own:
// a request that outlives the thing that made it would run a tank dry.
constexpr uint32_t PUMP_LEASE_MS = 5000;

Scheduler sched;
int job_control = -1, job_telem = -1, job_persist = -1, job_health = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

GridLimits limits = GRID_DEFAULTS;
GridState gstate{};
float nominal_v = NOMINAL_PHASE_VOLTS;
float rated_kva = RATED_KVA;
float site_pf = SITE_POWER_FACTOR;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "power-1";
uint16_t mqtt_port = MQTT_PORT;

uint32_t pump_lease_ms = 0;
bool lights_net_on = false;
bool networked = false;         // a request has arrived at least once, or the broker is up
bool pump_auto_local = true;    // what a standalone board drives its pump feeder with
uint16_t today_key = 0;
bool have_clock = false;
bool manual_latch = false;
GridAction act = GridAction::Normal;
GridAction prev_act = GridAction::Normal;
char reason[20] = "";
char prev_reason[20] = "";
uint32_t trips = 0, sheds = 0, alarms = 0;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.add("rated", rated_kva, 0);
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
        std::snprintf(tag, sizeof(tag), "%s", "power-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);
    if (nvGetI32("pauto", v)) pump_auto_local = v != 0;

    // Every threshold is range-checked on the way in. A limit that arrives as a
    // typo is worse than no limit at all, because it looks configured.
    if (nvGetF32("uv", f) && f > 0.5f && f < 1.0f) limits.under_v_pct = f;
    if (nvGetF32("ov", f) && f > 1.0f && f < 1.5f) limits.over_v_pct = f;
    if (nvGetF32("hzlo", f) && f > 40.0f && f < 50.0f) limits.hz_lo = f;
    if (nvGetF32("hzhi", f) && f > 50.0f && f < 65.0f) limits.hz_hi = f;
    if (nvGetF32("shed", f) && f > 20.0f && f < 100.0f) limits.shed_load_pct = f;
    if (nvGetF32("trip", f) && f > 100.0f && f < 400.0f) limits.trip_load_pct = f;
    if (nvGetF32("unb", f) && f > 1.0f && f < 50.0f) limits.unbalance_max_pct = f;
    if (nvGetF32("rcda", f) && f > 5.0f && f < 100.0f) limits.rcd_alarm_ma = f;
    if (nvGetF32("rcdt", f) && f > 30.0f && f < 500.0f) limits.rcd_trip_ma = f;
    if (nvGetF32("tmpa", f) && f > 30.0f && f < 70.0f) limits.temp_alarm_c = f;
    if (nvGetF32("tmpt", f) && f > 50.0f && f < 105.0f) limits.temp_trip_c = f;
    if (nvGetF32("deb", f) && f > 0.1f && f < 60.0f) limits.debounce_s = f;
    if (nvGetF32("lock", f) && f > 5.0f && f < 3600.0f) limits.reclose_lockout_s = f;
    if (nvGetF32("nomv", f) && f > 100.0f && f < 500.0f) nominal_v = f;
    if (nvGetF32("rated", f) && f > 1.0f && f < 2500.0f) rated_kva = f;
    if (nvGetF32("pf", f) && f > 0.2f && f <= 1.0f) site_pf = f;
    // The two leakage levels must not cross, whatever combination was restored.
    if (limits.rcd_trip_ma <= limits.rcd_alarm_ma) limits.rcd_alarm_ma = limits.rcd_trip_ma * 0.3f;
    if (limits.temp_trip_c <= limits.temp_alarm_c) limits.temp_alarm_c = limits.temp_trip_c * 0.75f;
    if (limits.trip_load_pct <= limits.shed_load_pct) limits.shed_load_pct = limits.trip_load_pct * 0.78f;
}

void paramsPersist() {
    nvSetF32("uv", limits.under_v_pct);
    nvSetF32("ov", limits.over_v_pct);
    nvSetF32("hzlo", limits.hz_lo);
    nvSetF32("hzhi", limits.hz_hi);
    nvSetF32("shed", limits.shed_load_pct);
    nvSetF32("trip", limits.trip_load_pct);
    nvSetF32("rcdt", limits.rcd_trip_ma);
    nvSetF32("tmpt", limits.temp_trip_c);
    nvSetF32("nomv", nominal_v);
    nvSetF32("rated", rated_kva);
    nvSetF32("pf", site_pf);
}

void clockUpdate() {
    uint16_t y = 0;
    uint8_t mo = 0, dm = 0;
    uint32_t sod = 0;
    have_clock = clockNow(sod, y, mo, dm);
    // Without a clock there is no "today": the daily counter keeps accumulating
    // against the last day key it knew and the frame says clock=unset, which is a
    // claim the dashboard can act on. A silent 0 would not be.
    if (have_clock) today_key = dayKey(y, mo, dm);
}

// Leakage, cabinet temperature and a sustained overload are the three that must
// not re-energise themselves. Frequency and an open breaker are different: the
// grid recovers, and someone re-closes the breaker by hand, which the auxiliary
// contact reports.
bool needsOperator(const char* r) {
    return std::strcmp(r, "LEAKAGE") == 0 || std::strcmp(r, "OVERTEMP") == 0 ||
           std::strcmp(r, "OVERLOAD") == 0;
}

void publish() {
    PowerReport r{};
    r.m = meterGet();
    r.fo = feedersGet();
    r.action = act;
    r.reason = reason;
    r.breaker_closed = breakerClosed();
    r.clock_valid = have_clock;
    r.uptime_s = (halMillis() - boot_ms) / 1000u;
    telemetryPublish(r);
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[20], value[32];
    size_t i = 0;
    while (i < len && i < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[i] = payload[i];
        ++i;
    }
    verb[i] = '\0';
    size_t n = 0;
    if (i < len && payload[i] == '=') {
        ++i;
        while (i < len && n < sizeof(value) - 1) value[n++] = payload[i++];
        value[n] = '\0';
    } else {
        value[0] = '\0';
    }

    bool taken = true;
    if (std::strcmp(verb, "pump") == 0) {
        // Only a request makes the board networked. A reclose or a threshold
        // change from an operator's phone must not silently take the ranch's
        // irrigation away by moving the board into a mode where nobody is
        // refreshing the lease.
        networked = true;
        if (value[0] == '1' || value[0] == 'o') pump_lease_ms = PUMP_LEASE_MS;
        else pump_lease_ms = 0;
    } else if (std::strcmp(verb, "lit") == 0) {
        networked = true;
        lights_net_on = value[0] == '1' || value[0] == 'o';
    } else if (std::strcmp(verb, "reclose") == 0) {
        // The operator's own word, and the only thing that clears a latched trip.
        // The lockout timer is cleared with it: an operator standing at the
        // cabinet is a better authority than a stopwatch, and if the fault is
        // still there the rule that tripped re-fires on the next tick.
        manual_latch = false;
        gridReset(gstate);
        feedersReclose();
    } else if (std::strcmp(verb, "stop") == 0) {
        pump_lease_ms = 0;
        lights_net_on = false;
        pump_auto_local = false;
        nvSetI32("pauto", 0);
    } else if (std::strcmp(verb, "resume") == 0) {
        // Restoring automatic operation means restoring the feeders with it: an
        // operator who says "come back on" and then finds the board still holding
        // a four minute stopwatch has been given a command that does nothing.
        pump_auto_local = true;
        manual_latch = false;
        gridReset(gstate);
        feedersReclose();
        nvSetI32("pauto", 1);
    } else if (std::strcmp(verb, "shed") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 20 && v < 100) limits.shed_load_pct = static_cast<float>(v);
        else taken = false;
    } else {
        float f = std::strtof(value, nullptr);
        if (std::strcmp(verb, "uv") == 0 && f > 0.5f && f < 1.0f) limits.under_v_pct = f;
        else if (std::strcmp(verb, "ov") == 0 && f > 1.0f && f < 1.5f) limits.over_v_pct = f;
        else if (std::strcmp(verb, "rcdt") == 0 && f > 30.0f && f < 500.0f) limits.rcd_trip_ma = f;
        else if (std::strcmp(verb, "tmpt") == 0 && f > 50.0f && f < 105.0f) limits.temp_trip_c = f;
        else if (std::strcmp(verb, "deb") == 0 && f > 0.1f && f < 60.0f) limits.debounce_s = f;
        else if (std::strcmp(verb, "nomv") == 0 && f > 100.0f && f < 500.0f) nominal_v = f;
        else if (std::strcmp(verb, "rated") == 0 && f > 1.0f && f < 2500.0f) rated_kva = f;
        else if (std::strcmp(verb, "pf") == 0 && f > 0.2f && f <= 1.0f) site_pf = f;
        else taken = false;
    }
    if (taken) paramsPersist();
    // A command that was not recognised is reported rather than dropped: the
    // alternative is an operator believing the board has ignored them.
    telemetryEvent(taken ? "cmd" : "cmd-unknown", verb);
}

void controlTick(uint32_t dt_ms) {
    clockUpdate();
    meterSample(dt_ms, today_key);
    const Metered& m = meterGet();

    if (pump_lease_ms > 0) {
        pump_lease_ms = pump_lease_ms > dt_ms ? pump_lease_ms - dt_ms : 0;
    }

    // The permit input is the AUTO contact of a hand-off-auto switch: closed, the
    // water controller's request is allowed through; open, the operator has taken
    // the feeder off automatic and nothing on the network may run it. With no
    // network at all the board falls back to its own setting, which is what a
    // switchboard has to do on the day the fibre gets dug up.
    const bool pump_requested = networked ? pump_lease_ms > 0 : pump_auto_local;
    const bool lights_requested = networked ? lights_net_on : pump_auto_local;
    feedersDemand(Feeder::Pump, pumpPermitClosed() && pump_requested);
    feedersDemand(Feeder::Lights, lightsPermitClosed() && lights_requested);

    GridInput in{};
    in.read = m.read;
    in.nominal_v = nominal_v;
    in.rcd_ma = m.rcd_ma;
    in.temp_c = m.temp_c;
    in.breaker_closed = breakerClosed();
    // Confirmed running, not commanded: the overload rule wants to know what the
    // board is actually carrying.
    in.pump_running = feedersPumpRunning();
    in.sensor_fault = m.sensor_fault;

    const GridDecision d = gridEvaluate(limits, gstate, in, dt_ms / 1000.0f);
    act = d.action;
    std::snprintf(reason, sizeof(reason), "%s", d.reason);

    if (d.lockout && needsOperator(d.reason)) manual_latch = true;

    // Nothing may be trusted, so nothing new starts: an unmeasured board is not a
    // healthy board. The contactors that are already closed stay as they are,
    // because dropping the yard lighting because an ADC came loose is its own
    // outage.
    const bool trustworthy = meterTrusted();
    feedersSupplyOk(trustworthy && !m.read.phase_loss);
    feedersShed(d.open_pump && d.action != GridAction::Trip);
    feedersTrip(d.lockout || manual_latch);

    const FeederOutputs before = feedersGet();
    feedersTick(dt_ms);
    const FeederOutputs& now = feedersGet();

    // --- edges worth a line in the log ----------------------------------------
    if (before.contactor_fault != now.contactor_fault) {
        telemetryEvent("contactor-fault", now.contactor_fault ? "NO FEEDBACK" : "CLEARED");
    }
    if (before.contactor_welded != now.contactor_welded) {
        telemetryEvent("contactor-welded", now.contactor_welded ? "STILL CLOSED" : "CLEARED");
    }

    if (act != prev_act || std::strcmp(reason, prev_reason) != 0) {
        prev_act = act;
        std::snprintf(prev_reason, sizeof(prev_reason), "%s", reason);
        switch (act) {
            case GridAction::Trip:
                trips++;
                telemetryEvent("grid-trip", reason);
                break;
            case GridAction::ShedLoad:
                sheds++;
                telemetryEvent("grid-shed", reason);
                break;
            case GridAction::Alarm:
                alarms++;
                telemetryEvent("grid-alarm", reason);
                break;
            case GridAction::Normal:
                telemetryEvent("grid-clear", "");
                break;
        }
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

void healthLine() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "health");
    w.add("trips", static_cast<int>(trips));
    w.add("shed", static_cast<int>(sheds));
    w.add("coil", static_cast<int>(feedersGet().pump_failures));
    w.add("meas", meterTrusted() ? 1 : 0);
    w.add("hz", meterGet().read.hz, 2);
    w.endLine();
    consoleWrite(line, w.size());
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;                 // a stalled loop must not skip a cycle

    if (sched.due(job_control, dt)) {
        controlTick(sched.elapsed(job_control));
        feedWatchdog();
        setStatusLed(act != GridAction::Normal);
    }
    if (sched.due(job_telem, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_persist, dt)) meterPersist();
    if (sched.due(job_health, dt)) healthLine();
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

// The host sandbox drives the command channel the same way the broker does, so a
// reclose is exercised rather than assumed. Outside the anonymous namespace
// because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    sched.clear();
    feedersInit();
    telemetryInit();
    paramsLoad();
    meterInit(site_pf, nominal_v, rated_kva);
    meterLoad();
    gridReset(gstate);
    manual_latch = false;
    pump_lease_ms = 0;
    lights_net_on = false;
    networked = false;
    act = GridAction::Normal;
    prev_act = GridAction::Normal;
    reason[0] = '\0';
    prev_reason[0] = '\0';
    trips = sheds = alarms = 0;
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_control = sched.add("control", CONTROL_PERIOD_MS);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_persist = sched.add("persist", 60000);
    job_health = sched.add("health", 10000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the control loop has to be created
    // here: without it the board prints its banner and then stops.
    xTaskCreatePinnedToCore(controlTask, "power", STACK_METER, nullptr, PRIO_METER, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
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
