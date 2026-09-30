// Pin map and plant configuration for the ranch water distribution board.
//
// Two lines, one board: the barn dispenses on a programme, the main house line
// is independently valved so a barn cycle can never drain the house. Every
// number that describes the *plumbing* (inrush window, dry-run timeout, tank
// hysteresis) is here, so a site visit that changes a pipe changes one file.
//
// Allocation rules, proven by the static_asserts at the bottom:
//   1. no GPIO appears twice, across inputs, outputs and analog inputs together
//   2. nothing sits on the flash pins (6-11) or flash-PSRAM pins (16,17), and no
//      output is on the input-only pins (34-36,39)
//   3. both analog inputs are on ADC1: ADC2 cannot be read while WiFi is
//      associated, which is most of the life of this board
#pragma once

#include <cstddef>

// --- valves and pump --------------------------------------------------------
#define PIN_VALVE_BARN      27      // 24 V solenoid through a MOSFET
#define PIN_VALVE_HOUSE     26      // main house line, independently valved
#define PIN_PUMP            25      // pressuriser, through the contactor driver
#define PIN_LED_STATUS       2      // the dev kit's own LED, so a boot is visible

// --- metering ---------------------------------------------------------------
// One pulse meter per line: a barn cycle that also moves the house totaliser is
// the fault this board is here to catch, and one shared meter cannot see it.
#define PIN_FLOW_BARN        4      // YF-S201, 7.5 Hz per L/min
#define PIN_FLOW_HOUSE      13
#define PIN_PRESSURE        36      // ADC1_CH0, 0.5-4.5 V for 0-10 bar
#define PIN_LEVEL           39      // ADC1_CH3 (VN), resistive float in the tank

// --- switch inputs (input-only pins, which is all they can be) --------------
// GPIO 34-39 have no internal pull-up and no output driver, so both of these
// must come from a sensor that drives its own line (the rope sensor and the tank
// float are both active module outputs). A bare reed switch here needs an
// external pull-up.
#define PIN_LEAK            35      // rope sensor in the pump house, wet = low
#define PIN_TANK_HIGH       34      // tank full: stop the fill, do not overflow

// --- bus and watchdog -------------------------------------------------------
#define PIN_RTC_SDA         21      // DS3231; keeps the programme through a power cut
#define PIN_RTC_SCL         22
#define PIN_WDT_FEED        18      // external watchdog, must toggle >= 10 Hz

// --- electrical characteristics ---------------------------------------------
#ifndef FLOW_PULSES_PER_LITRE
#define FLOW_PULSES_PER_LITRE 450.0f      // YF-S201: 7.5 Hz per L/min
#endif
#ifndef PRESSURE_MV_AT_ZERO
#define PRESSURE_MV_AT_ZERO   500.0f
#endif
#ifndef PRESSURE_MV_PER_BAR
#define PRESSURE_MV_PER_BAR   400.0f      // 0.5-4.5 V over 0-10 bar
#endif
#ifndef LEVEL_MV_EMPTY
#define LEVEL_MV_EMPTY        900.0f
#endif
#ifndef LEVEL_MV_FULL
#define LEVEL_MV_FULL         3100.0f
#endif

// Solenoid drive: full duty for the inrush window, then pulse-width held down.
// A coil kept at 100 % after pulling in just heats itself and its driver.
#ifndef VALVE_INRUSH_MS
#define VALVE_INRUSH_MS       120
#endif
#ifndef VALVE_HOLD_DUTY
#define VALVE_HOLD_DUTY       0.55f
#endif
#ifndef VALVE_HOLD_HZ
#define VALVE_HOLD_HZ         200
#endif

// --- plant limits -----------------------------------------------------------
#ifndef DRY_RUN_BAR
#define DRY_RUN_BAR           0.30f   // pump running below this is running dry
#endif
#ifndef DRY_RUN_S
#define DRY_RUN_S             2.0f
#endif
#ifndef NO_FLOW_S
#define NO_FLOW_S             6.0f    // valve open, nothing moving: stuck or empty
#endif
#ifndef VALVE_MAX_OPEN_S
#define VALVE_MAX_OPEN_S      900.0f  // a programme may not hold a valve open forever
#endif
#ifndef LEAK_FLOW_LMIN
#define LEAK_FLOW_LMIN        0.35f   // any flow with every valve closed
#endif
#ifndef TANK_REFILL_START_PCT
#define TANK_REFILL_START_PCT 22.0f
#endif
#ifndef TANK_REFILL_STOP_PCT
#define TANK_REFILL_STOP_PCT  96.0f
#endif
#ifndef STAGNATION_DAYS
#define STAGNATION_DAYS       7       // exercise a line that has not run
#endif
#ifndef STAGNATION_RUN_S
#define STAGNATION_RUN_S      20
#endif

// --- programme defaults (overridable from NVS) ------------------------------
#ifndef BARN_PERIOD_S
#define BARN_PERIOD_S         3600    // hourly dispense at the trough
#endif
#ifndef BARN_RUN_S
#define BARN_RUN_S            240
#endif
#ifndef HOUSE_DEFAULT_OPEN
#define HOUSE_DEFAULT_OPEN    1       // the house line runs unless told otherwise
#endif
#ifndef PUMP_LEAD_MS
#define PUMP_LEAD_MS          800     // establish pressure before the valve opens
#endif
#ifndef PUMP_TAIL_MS
#define PUMP_TAIL_MS          1500    // keep the pump after the valve shuts
#endif

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/water/state"
#define MQTT_TOPIC_CMD        "ranch/water/cmd"
#define TELEMETRY_HZ          2

// --- task layout ------------------------------------------------------------
#define PRIO_CONTROL          18
#define PRIO_TELEMETRY        12
#define STACK_CONTROL         6144
#define STACK_TELEMETRY       6144
#define TASK_WDT_TIMEOUT_MS   2000

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

// Classic ESP32: 6-11 and 16-17 are the SPI flash and PSRAM bus.
constexpr bool pinsAvoidFlash(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p >= 6 && p <= 11) return false;
    if (p == 16 || p == 17) return false;
    return pinsAvoidFlash(v, n, i + 1);
}

// 34,35,36,39 have no output driver and no internal pull-ups.
constexpr bool outputsAreDriven(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p == 34 || p == 35 || p == 36 || p == 39) return false;
    return outputsAreDriven(v, n, i + 1);
}

// ADC1 on the classic ESP32 is GPIO 32-39; ADC2 is unavailable with WiFi on.
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

constexpr int kOutputs[] = { PIN_VALVE_BARN, PIN_VALVE_HOUSE, PIN_PUMP,
                             PIN_LED_STATUS, PIN_RTC_SDA, PIN_RTC_SCL, PIN_WDT_FEED };
constexpr int kAnalog[] = { PIN_PRESSURE, PIN_LEVEL };
constexpr int kAll[] = {
    PIN_VALVE_BARN, PIN_VALVE_HOUSE, PIN_PUMP, PIN_LED_STATUS, PIN_RTC_SDA, PIN_RTC_SCL,
    PIN_WDT_FEED, PIN_FLOW_BARN, PIN_FLOW_HOUSE, PIN_LEAK, PIN_TANK_HIGH,
    PIN_PRESSURE, PIN_LEVEL,
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
