// The only module that touches the loops and the relays.
//
// Sampling and driving live together on purpose: the decision needs a loop state
// that was debounced over samples, and the outputs need a place where "commanded"
// and "proven" are kept apart. Both are invisible from `main.cpp`, and both are
// where a panel goes wrong -- a raw ADC reading promoted straight to "alarm" makes
// a chattering relay page the fire service, and a coil drive counted as a sounding
// bell makes a silent building.
#pragma once

#include <cstdint>

#include "fire_logic.h"

namespace ranch {

struct LoopRead {
    LoopState zone[FIRE_ZONES];
    LoopState mcp;
    LoopState flow;
    uint16_t mv[FIRE_ZONES + 2];       // what the panel actually saw, for the log
    bool manual_call;                  // the call point's alarm band
    bool flow_active;                  // water is moving
    bool supervision;                  // any loop open, shorted or unreadable
};

struct PanelStats {
    uint32_t siren_on_ms;              // time the bell relay was driven
    uint32_t strobe_on_ms;
    uint32_t pump_permit_ms;
    uint32_t samples;
    uint32_t dirty_samples;            // loop readings that fell between the bands
};

void panelInit();

// One loop scan. Call every SAMPLE_PERIOD_MS with the bands the panel was
// configured with -- a site rewired for a 10k pull-up has to be able to say so.
void panelSample(const LoopBands& bands);

const LoopRead& panelLoops();

// Drives the relays from a decision and counts what the outputs were asked to do.
void panelApply(const FireDecision& d, uint32_t dt_ms);

void panelStats(PanelStats& out);

// The button levels. Edges are main.cpp's business: a held silence button must not
// re-silence every tick, and a momentary contact that bounces must not either.
bool panelSilenceHeld();
bool panelTestHeld();

}  // namespace ranch
