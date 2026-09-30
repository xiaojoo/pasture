#include "meter.h"

#include <cmath>

#include "board.h"
#include "hal.h"

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
