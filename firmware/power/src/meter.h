// The measurement side of the switchboard board: raw front ends in, RMS values,
// frequency, leakage, temperature and energy out.
#pragma once

#include <cstdint>

#include "power_meter.h"

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
