#include "lights.h"

#include <cmath>

#include "board.h"
#include "hal.h"

namespace ranch {
namespace {

// Percent of full per second the driver may move. A cold high bay takes a few
// seconds to strike, and the inrush on a bus that is already carrying a lamp
// makes an instant step the fastest way to trip the driver.
constexpr float SLEW_PER_SECOND = 0.08f;
constexpr float MIN_USEFUL_DUTY = 0.05f;
constexpr uint16_t ADC_OPEN_MV = 100;
constexpr uint16_t ADC_RAIL_MV = 4900;

float duty_target = 0.0f;
float duty_now = 0.0f;
bool want_street = false;
bool want_house = false;
bool street_now = false;
bool house_now = false;
LampState state{};
uint32_t burn[LAMP_COUNT] = {0, 0, 0};
float dead_s = 0.0f;
float over_s = 0.0f;
uint32_t burn_carry = 0;

// An LDR on a resistor divider is logarithmic in illuminance, so the calibration
// is an exponential fit between the two measured points rather than a straight
// line, which would report "daylight" for a bright moon.
float luxFromMillivolts(uint16_t mv) {
    const float lo = LDR_MV_AT_DARK;
    const float hi = LDR_MV_AT_DAYLIGHT;
    const float x = static_cast<float>(mv);
    if (x <= lo) return 0.0f;
    float t = (x - lo) / (hi - lo);
    if (t > 1.0f) t = 1.0f;
    const float ratio = LUX_AT_DAYLIGHT / LUX_SUNRISE;
    return LUX_SUNRISE * std::pow(ratio, t);
}

}  // namespace

void lightsInit() {
    duty_target = 0.0f;
    duty_now = 0.0f;
    want_street = false;
    want_house = false;
    street_now = false;
    house_now = false;
    state = LampState{};
    dead_s = 0.0f;
    over_s = 0.0f;
    burn_carry = 0;
    setRelayStreet(false);
    setRelayHouse(false);
    setDimDuty(0.0f);
}

void lightsRequest(bool street, bool house, float barn_duty) {
    if (barn_duty < 0.0f) barn_duty = 0.0f;
    if (barn_duty > 1.0f) barn_duty = 1.0f;
    want_street = street;
    want_house = house;
    duty_target = barn_duty;
}

void lightsTick(uint32_t dt_ms) {
    const float dt_s = dt_ms / 1000.0f;

    // --- contactors ----------------------------------------------------------
    street_now = want_street && !state.fault.latched;
    house_now = want_house;
    setRelayStreet(street_now);
    setRelayHouse(house_now);

    // --- dimmer slew ---------------------------------------------------------
    const float step = SLEW_PER_SECOND * dt_s;
    if (duty_now < duty_target) {
        duty_now += step;
        if (duty_now > duty_target) duty_now = duty_target;
    } else if (duty_now > duty_target) {
        duty_now -= step;
        if (duty_now < duty_target) duty_now = duty_target;
    }
    // Over-current is the one condition that cuts the output on its own.
    if (state.fault.latched) duty_now = 0.0f;
    setDimDuty(duty_now);
    state.duty_now = duty_now;
    state.duty_target = duty_target;
    state.street = street_now;
    state.house = house_now;

    // --- what the lamp actually did ------------------------------------------
    const uint16_t amv = analogMillivolts(PIN_BALLAST);
    if (amv < ADC_OPEN_MV || amv > ADC_RAIL_MV) {
        state.amps = 0.0f;
        // No current reading is not evidence of a dead lamp: it is evidence of a
        // broken sensor, and only the driver's own contact may fault the circuit.
        dead_s = 0.0f;
        over_s = 0.0f;
    } else {
        const float a = (amv - CT_ZERO_MV) / CT_MV_PER_A;
        state.amps = a > -0.15f && a < 0.15f ? 0.0f : a;
    }
    state.fault.driver_contact = driverFaultContact();

    const bool commanded_bright = duty_now > MIN_USEFUL_DUTY;
    if (commanded_bright && state.amps < LAMP_DEAD_A) {
        dead_s += dt_s;
        if (dead_s >= LAMP_CONFIRM_S) state.fault.barn_dead = true;
    } else if (!commanded_bright) {
        dead_s = 0.0f;
    }
    if (commanded_bright && state.amps > LAMP_OVER_A) {
        over_s += dt_s;
        if (over_s >= 2.0f) {
            // Two seconds, not twelve: a bus that is over its rating is already
            // hot, and the point of the check is to stop before the magic smoke.
            state.fault.barn_over = true;
            state.fault.latched = true;
        }
    } else {
        over_s = 0.0f;
    }

    // --- the sky --------------------------------------------------------------
    const uint16_t lmv = analogMillivolts(PIN_LDR);
    if (lmv < ADC_OPEN_MV || lmv > ADC_RAIL_MV) {
        state.lux_valid = false;
    } else {
        state.lux_valid = true;
        state.lux = luxFromMillivolts(lmv);
    }

    // --- burn hours -----------------------------------------------------------
    burn_carry += dt_ms;
    while (burn_carry >= 100u) {
        burn_carry -= 100u;
        if (street_now) burn[LAMP_STREET]++;
        if (house_now) burn[LAMP_HOUSE]++;
        if (duty_now > MIN_USEFUL_DUTY) burn[LAMP_BARN]++;
    }
    state.seconds_to_full = duty_now > duty_target
                                ? 0u
                                : static_cast<uint32_t>((duty_target - duty_now) / SLEW_PER_SECOND);
}

const LampState& lightsGet() { return state; }

uint32_t lightsBurnTenths(LampCircuit which) {
    return which < LAMP_COUNT ? burn[which] : 0u;
}

void lightsSetBurnTenths(LampCircuit which, uint32_t tenths) {
    if (which < LAMP_COUNT) burn[which] = tenths;
}

void lightsClearLatch() {
    state.fault.latched = false;
    state.fault.barn_over = false;
    over_s = 0.0f;
}

}  // namespace ranch
