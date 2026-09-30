// Pin map and electrical configuration for the ranch switchboard monitor.
//
// What this board is: the thing that knows how much power the ranch is pulling,
// whether the supply is still healthy, and whether the water pump is allowed to
// start. It watches three phases, one residual-current transformer and the
// cabinet temperature, and it holds the two contactors that the shed/trip logic
// opens.
//
// Analog channel budget, which is what shaped the design: the classic ESP32 has
// eight ADC1 pins (32-39, six of them on the header) and ADC2 cannot be read with
// WiFi associated. Three
// phases of voltage and three of current use all six, so the temperature and the
// leakage transformer go to an ADS1115 on the same I2C bus as the RTC. That is
// not decoration - it is the reason this board has a second ADC on it.
//
// Proven by the static_asserts below: no duplicate pin, nothing on the SPI
// flash pins (6-11, 16-17), no output on the input-only pins (34-36, 39), and
// every analog input on ADC1.
#pragma once

#include <cstddef>

// --- measurement ------------------------------------------------------------
#define PIN_VA              36      // ADC1_CH0, ZMPT101 front end
#define PIN_VB              39      // ADC1_CH3 (VN)
#define PIN_VC              32      // ADC1_CH4
#define PIN_IA              33      // ADC1_CH5, SCT-013 style with a burden
#define PIN_IB              34      // ADC1_CH6
#define PIN_IC              35      // ADC1_CH7

// --- external ADC (cabinet temperature + residual current) and the RTC ------
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define ADS1115_ADDR        0x48
#define RTC_ADDR            0x68      // DS3231, the other half of the same bus
#define ADS_CH_TEMP         0
#define ADS_CH_RCD          1
// 2.048 V full scale: the temperature sensor and the RCD rectifier both stay
// inside a couple of volts, and the narrowest range gives the least-significant
// bit that matters.
#define ADS_FSR_MV          2048.0f
#define ADS_BITS            15

// --- contacts and outputs ---------------------------------------------------
#define PIN_ZC_A            23      // ZMPT101B comparator output on phase A:
                                    // frequency has to come from a zero crossing,
                                    // and the RMS front ends are DC by nature
#define PIN_BRK_AUX         18      // main breaker auxiliary, dry make to GND
#define PIN_PUMP_FB         19      // contactor auxiliary, dry make to GND
                                    // (both pulled up, so a closed contact reads LOW)
#define PIN_PUMP            25      // water pump contactor coil
#define PIN_LIGHTS          26      // yard lighting feeder contactor
#define PIN_WDT_FEED         4      // external watchdog, must toggle >= 10 Hz
#define PIN_LED_STATUS       2      // the dev kit's own LED

// --- local hand/off/auto switches -------------------------------------------
// A switchboard has to stay operable with the network down, so each feeder has a
// physical permit input and the MQTT request runs in parallel with it, not in
// place of it.
//
// Both are read active-high behind an internal pull-down, and the direction is a
// boot-safety decision rather than a style one: a permit wired the obvious way - a
// switch to ground on a pull-up - makes "pump permitted" the same level as the
// strapping state that puts the module into download mode on GPIO0 or lowers the
// flash voltage on GPIO12. Active-high with a pull-down boots with the switch open
// as "off" and with it closed as "on", and neither is a boot mode.
// Neither pin has a boot function at all now: 13 and 27 are ordinary IO on the
// DevKit V1 header, which is also why the two permits no longer sit on GPIO0 or on
// an ADC1 channel. The level is still wrong for the few hundred milliseconds before
// the pin is configured, which is why halInit() forces both coils off and the LEDC
// channels idle before anything reads a permit.
#define PIN_IN_PUMP         13      // permit the water pump feeder locally
#define PIN_IN_LIGHTS      27       // permit the yard lighting feeder locally

// --- what this board does not measure ---------------------------------------
// The front ends are rectified: they give a true RMS *magnitude* per channel and
// no phase angle, so power factor is a site parameter, not a reading. Every
// protection rule below works on volts, load percent, frequency, leakage and
// temperature, none of which need it; only kW and kWh carry the assumption, and
// the frame says so. A site that needs metered energy buys a Modbus meter.
#ifndef SITE_POWER_FACTOR
#define SITE_POWER_FACTOR   0.92f   // pump-and-motor average for this ranch
#endif

// --- transformer and divider scale ------------------------------------------
// Calibrated on the bench against a meter, and the two numbers that must be
// re-measured if the front end is ever rebuilt.
#ifndef NOMINAL_PHASE_VOLTS
#define NOMINAL_PHASE_VOLTS  230.0f
#endif
#ifndef VOLT_MV_PER_VOLT
#define VOLT_MV_PER_VOLT     4.0f     // 230 V rms presents about 920 mV
#endif
#ifndef CT_ZERO_MV
#define CT_ZERO_MV           1650.0f  // the burden's virtual ground
#endif
// The board's full-load current is 45 kVA / 3 / 230 V = 65 A per leg, so the
// burden is wound at 15 mV per amp: 65 A lands at 2.6 V and 100 A at 3.15 V, both
// inside the ADC. A 30 A clip-on cannot be used on this leg without a second
// turns ratio, and a 100 mV/A one saturates at 16 A, which is one pump.
// Anti-parallel silicon across the burden keeps a starting motor's 5x inrush
// pinned at the top of the range instead of letting the node fly.
#ifndef CT_MV_PER_AMP
#define CT_MV_PER_AMP        15.0f
#endif
#ifndef RCD_MV_PER_MA
#define RCD_MV_PER_MA        10.0f    // after the rectifier, 0 mA at 0 V
#endif
#ifndef TEMP_MV_PER_C
#define TEMP_MV_PER_C        10.0f    // LM35DZ at 10 mV/degC
#endif
#ifndef TEMP_OFFSET_MV
#define TEMP_OFFSET_MV       0.0f
#endif

// --- the site ---------------------------------------------------------------
#ifndef RATED_KVA
#define RATED_KVA            45.0f     // the transformer this board is fed from
#endif
#ifndef PUMP_KW
#define PUMP_KW              7.5f      // what shedding the pump is worth
#endif

// --- task layout ------------------------------------------------------------
#define PRIO_METER          16
#define PRIO_TELEMETRY      10
#define STACK_METER         6144
#define STACK_TELEMETRY     6144
#define TASK_WDT_TIMEOUT_MS 3000

// Sampling windows: the mains period is 20 ms at 50 Hz, and a window that is not
// a whole number of periods makes the RMS reading wobble with the load.
#define SAMPLE_WINDOW_MS    200
#define CONTROL_PERIOD_MS   100
#define TELEMETRY_HZ        2

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/power/state"
#define MQTT_TOPIC_CMD        "ranch/power/cmd"

#ifndef RANCH_NO_PIN_CHECK
namespace ranch {

constexpr bool pinsDisjoint(const int* v, size_t n, size_t i = 1) {
    if (i >= n) return true;
    if (v[i] >= 0) {
        for (size_t j = 0; j < i; ++j) {
            if (v[j] == v[i]) return false;
        }
    }
    return pinsDisjoint(v, n, i + 1);
}

constexpr bool pinsAvoidFlash(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p >= 6 && p <= 11) return false;
    if (p == 16 || p == 17) return false;
    return pinsAvoidFlash(v, n, i + 1);
}

constexpr bool outputsAreDriven(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p == 34 || p == 35 || p == 36 || p == 39) return false;
    return outputsAreDriven(v, n, i + 1);
}

// ADC1 on the classic ESP32 is GPIO 32-39 and nothing else: CH4-CH7 are 32-35 and
// CH0-CH3 are 36-39. Of those eight, the DevKit V1 header carries six (36 and 39 come
// out as VP and VN; 37 and 38 stay inside the module).
constexpr bool analogIsAdc1(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p < 32 || p > 39) return false;
    return analogIsAdc1(v, n, i + 1);
}


// The DevKit V1 only breaks out 26 of the ESP32's 34 usable GPIOs: 37 and 38 exist
// on the chip (and are valid ADC1 channels) but never reach a header, and 0 is inside
// the USB-serial bridge's flow-control circuit. An allocation that passes every other
// rule here still cannot be wired on the board a person actually buys, so the set
// below is the header, read off the part the simulator loads.
constexpr bool pinOnHeader(int p) {
    const int k[] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (k[i] == p) return true;
    return false;
}

constexpr bool pinsOnHeader(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    if (!pinOnHeader(v[i])) return false;
    return pinsOnHeader(v, n, i + 1);
}

constexpr int kOutputs[] = { PIN_PUMP, PIN_LIGHTS, PIN_WDT_FEED, PIN_LED_STATUS,
                             PIN_I2C_SDA, PIN_I2C_SCL };
constexpr int kAnalog[] = { PIN_VA, PIN_VB, PIN_VC, PIN_IA, PIN_IB, PIN_IC };
constexpr int kAll[] = {
    PIN_VA, PIN_VB, PIN_VC, PIN_IA, PIN_IB, PIN_IC,
    PIN_I2C_SDA, PIN_I2C_SCL, PIN_ZC_A, PIN_BRK_AUX, PIN_PUMP_FB,
    PIN_IN_PUMP, PIN_IN_LIGHTS,
    PIN_PUMP, PIN_LIGHTS, PIN_WDT_FEED, PIN_LED_STATUS,
};
constexpr size_t kAllLen = sizeof(kAll) / sizeof(kAll[0]);
constexpr size_t kOutLen = sizeof(kOutputs) / sizeof(kOutputs[0]);
constexpr size_t kAnLen = sizeof(kAnalog) / sizeof(kAnalog[0]);

static_assert(pinsDisjoint(kAll, kAllLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidFlash(kAll, kAllLen), "board.h: a pin sits on the SPI flash bus");
static_assert(outputsAreDriven(kOutputs, kOutLen), "board.h: an output is on an input-only pin");
static_assert(analogIsAdc1(kAnalog, kAnLen), "board.h: an analog input must be on ADC1");
static_assert(pinsOnHeader(kAll, kAllLen), "board.h: a pin is not broken out on the DevKit V1 header");

}  // namespace ranch
#endif
