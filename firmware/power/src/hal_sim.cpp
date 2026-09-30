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

#include "hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "board.h"

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
