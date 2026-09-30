#include "sense.h"

#include <cmath>

#include "board.h"
#include "hal.h"

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
