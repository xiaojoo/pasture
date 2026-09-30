// Pin map and photometric configuration for the ranch lighting board.
//
// One board drives the yard perimeter, the barn dimmer and the house porch, and
// switches them on the sun rather than on a clock, because the ranch's working day
// moves with the season and a fixed 18:00 timer is an hour wrong in June.
//
// Allocation rules, proven by the static_asserts below:
//   1. no GPIO appears twice
//   2. nothing on the SPI flash pins (6-11, 16-17), and no output on the
//      input-only pins (34-36, 39)
//   3. both analog inputs on ADC1: ADC2 cannot be read with WiFi associated
#pragma once

#include <cstddef>

// --- lamps ------------------------------------------------------------------
#define PIN_RELAY_STREET     4      // yard perimeter contactor
#define PIN_RELAY_HOUSE      2      // porch and kitchen outside; the dev kit's own
                                    // LED is on this pin, so a switching is visible
#define PIN_DIM_BARN        27      // 0-10 V driver for the barn high bay

// --- sensors ----------------------------------------------------------------
#define PIN_PIR             35      // input only: barn motion, active high module
#define PIN_LDR             36      // ADC1_CH0: daylight sensor at the eaves
#define PIN_BALLAST         39      // ADC1_CH3 (VN): current transformer on the lamp bus
#define PIN_FAULT           34      // driver fault contact, open = healthy

// --- bus, clock, watchdog ---------------------------------------------------
#define PIN_RTC_SDA         21      // DS3231: the schedule survives a power cut
#define PIN_RTC_SCL         22
#define PIN_WDT_FEED        18

// --- dimmer -----------------------------------------------------------------
// 1 kHz, not the LEDC default: a 0-10 V driver's output filter buzzes at 20 kHz
// and the cattle hear it.
#define DIM_HZ              1000
#define DIM_BITS            10

// --- photometrics -----------------------------------------------------------
// The LDR divider is log-ish, so the conversion below is a calibrated table
// fit, not a physics model: it only has to answer "is it daylight".
#ifndef LDR_MV_AT_DARK
#define LDR_MV_AT_DARK      120.0f
#endif
#ifndef LDR_MV_AT_DAYLIGHT
#define LDR_MV_AT_DAYLIGHT  2600.0f
#endif
#ifndef LUX_AT_DAYLIGHT
#define LUX_AT_DAYLIGHT     400.0f
#endif
#ifndef LUX_SUNRISE
#define LUX_SUNRISE         3.0f
#endif

// --- lamp bus ---------------------------------------------------------------
#define CT_ZERO_MV          2500.0f     // current transformer live zero
#define CT_MV_PER_A         100.0f      // ACS712-20A sensitivity

#ifndef LAMP_NOMINAL_A
#define LAMP_NOMINAL_A      3.2f    // one high bay at full duty
#endif
#ifndef LAMP_DEAD_A
#define LAMP_DEAD_A         0.15f   // commanded on, drawing nothing: driver or lamp dead
#endif
#ifndef LAMP_OVER_A
#define LAMP_OVER_A         5.5f
#endif
#ifndef LAMP_CONFIRM_S
#define LAMP_CONFIRM_S      12.0f   // how long a discrepancy must last to be a fault
#endif

// --- site -------------------------------------------------------------------
// Latitude, longitude and the UTC offset are the only numbers that make the sun
// times local. They are parameters, not constants, because the day the ranch
// moves the board is the day they change.
#ifndef RANCH_LATITUDE
#define RANCH_LATITUDE      30.5
#endif
#ifndef RANCH_LONGITUDE
#define RANCH_LONGITUDE     114.3
#endif
#ifndef RANCH_UTC_OFFSET_H
#define RANCH_UTC_OFFSET_H  8.0
#endif

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/light/state"
#define MQTT_TOPIC_CMD        "ranch/light/cmd"
#define TELEMETRY_HZ          1

// --- task layout ------------------------------------------------------------
#define PRIO_LIGHT            16
#define PRIO_TELEMETRY        10
#define STACK_LIGHT           6144
#define STACK_TELEMETRY       6144
#define TASK_WDT_TIMEOUT_MS   3000

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

constexpr int kOutputs[] = { PIN_RELAY_STREET, PIN_RELAY_HOUSE, PIN_DIM_BARN,
                             PIN_RTC_SDA, PIN_RTC_SCL, PIN_WDT_FEED };
constexpr int kAnalog[] = { PIN_LDR, PIN_BALLAST };
constexpr int kAll[] = {
    PIN_RELAY_STREET, PIN_RELAY_HOUSE, PIN_DIM_BARN, PIN_PIR, PIN_LDR, PIN_BALLAST,
    PIN_FAULT, PIN_RTC_SDA, PIN_RTC_SCL, PIN_WDT_FEED,
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
