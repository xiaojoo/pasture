// Measurement layer: turns pulse counts, ADC samples and switch states into the
// plant quantities the rest of the firmware reasons about.
//
// The HAL reports millivolts and pulse totals; the engineering conversion, the
// filter windows and the "this channel has stopped making sense" tests live
// here, because those are decisions about the plumbing rather than about the
// microcontroller.
#pragma once

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
