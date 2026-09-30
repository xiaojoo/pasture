// Three-phase electrical measurement maths. Platform neutral: it turns per-phase
// RMS values into power, energy, load factor, unbalance and phase loss, so the
// arithmetic a maintenance electrician would check with a multimeter can be
// proven on a host.
//
// Everything is in SI on the way in (volts, amps, seconds) and the frame units
// (kW, kWh, %, Hz, mA) on the way out.
#pragma once

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
