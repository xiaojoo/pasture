#include "feeders.h"

#include "board.h"
#include "hal.h"

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
