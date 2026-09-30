#include "panel.h"

#include "board.h"
#include "hal.h"

namespace ranch {
namespace {

LoopRead rd{};
PanelStats stats{};
LoopTracker tr[FIRE_ZONES + 2]{};

// The panel's six inputs in the order the frame reports them, so a zone index means
// the same thing in the log, the frame and the terminal block.
constexpr uint8_t kPins[FIRE_ZONES + 2] = {
    PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER, PIN_MCP, PIN_FLOW,
};

}  // namespace

void panelInit() {
    rd = LoopRead{};
    stats = PanelStats{};
    for (auto& t : tr) loopTrackerReset(t);
    setSiren(false);
    setStrobe(false);
    setPumpPermit(false);
}

void panelSample(const LoopBands& bands) {
    stats.samples++;
    for (uint8_t i = 0; i < FIRE_ZONES + 2; ++i) {
        const uint16_t mv = loopMillivolts(kPins[i]);
        rd.mv[i] = mv;
        const LoopState raw = loopClassify(mv, bands);
        if (raw == LoopState::Unknown) stats.dirty_samples++;
        const LoopState s = loopTrack(tr[i], raw);
        if (i < FIRE_ZONES) rd.zone[i] = s;
        else if (i == FIRE_ZONES) {
            rd.mcp = s;
            rd.manual_call = (s == LoopState::Alarm);
        } else {
            rd.flow = s;
            rd.flow_active = (s == LoopState::Alarm);
        }
    }

    bool sup = rd.mcp == LoopState::Open || rd.mcp == LoopState::Shorted ||
               rd.flow == LoopState::Open || rd.flow == LoopState::Shorted;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) {
        if (rd.zone[i] == LoopState::Open || rd.zone[i] == LoopState::Shorted ||
            rd.zone[i] == LoopState::Unknown) {
            sup = true;
        }
    }
    rd.supervision = sup;
}

const LoopRead& panelLoops() { return rd; }

void panelApply(const FireDecision& d, uint32_t dt_ms) {
    setSiren(d.siren);
    setStrobe(d.strobe);
    setPumpPermit(d.pump_permit);

    // Output time, which is the thing a maintenance visit wants: not "was it told to
    // sound" but for how long the relay actually carried current.
    if (d.siren) stats.siren_on_ms += dt_ms;
    if (d.strobe) stats.strobe_on_ms += dt_ms;
    if (d.pump_permit) stats.pump_permit_ms += dt_ms;

    // The armature relay's auxiliary is the only evidence the building was warned, but
    // it is *judged* in lib/fire_logic.h against its own proof window, because the
    // relay needs several milliseconds to travel. Counting a failure here, on the same
    // tick the coil was commanded, put a bell fault on all eight hourly self-tests of
    // an eight hour run in which the bell sounded every single time.
}

void panelStats(PanelStats& out) { out = stats; }

bool panelSilenceHeld() { return silencePressed(); }
bool panelTestHeld() { return testPressed(); }

}  // namespace ranch
