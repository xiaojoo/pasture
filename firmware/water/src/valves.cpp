#include "valves.h"

#include "board.h"
#include "hal.h"

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
