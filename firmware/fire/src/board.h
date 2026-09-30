// Pin map and electrical configuration for the ranch fire panel.
//
// What this board is: four supervised detector loops, the manual call point, the
// flow switch, the bell and the pump permit. It is the board that has to keep
// working when everything else has stopped being interesting, which is why the
// loop supervision is analog rather than a contacted "fine" LED.
//
// The analog channel budget shaped this design the same way it shaped the
// switchboard board: the classic ESP32 has eight ADC1 pins (32-39, six on the
// header) and ADC2 cannot
// be read while WiFi is associated. A fire panel that loses its uplink is still a
// fire panel, but one that mis-reads a loop because the radio came up is not. Six
// loops, six pins, no second ADC: four detector zones plus the call point plus the
// flow switch.
//
// Proven by the static_asserts below: no duplicate pin, nothing on the SPI flash
// pins (6-11, 16-17), no output on the input-only pins (34-36, 39), and every
// supervised loop on ADC1.
#pragma once

#include <cstddef>

// --- supervised loops (see lib/fire_logic.h for the four bands) ---------------
// Each input is a 4k7 pull-up to 3V3 with the loop's end-of-line resistor in
// series; the detector's relay shorts the 1k alarm resistor when it operates.
#define PIN_Z_HOUSE         39      // ADC1_CH3 (VN), main house
#define PIN_Z_BARN          36      // ADC1_CH0, cow shed
#define PIN_Z_STORE         32      // ADC1_CH4, store / feed room
#define PIN_Z_POWER         33      // ADC1_CH5, switch room -- the fire that starts here
#define PIN_MCP             34      // ADC1_CH6, break-glass call point
#define PIN_FLOW            35      // ADC1_CH7, wet-pipe flow switch

// --- outputs ------------------------------------------------------------------
#define PIN_SIREN           25      // internal bell relay, 24 V side
#define PIN_STROBE          27      // external beacons
#define PIN_PUMP            14      // sprinkler pump permit, dry contact
#define PIN_WDT_FEED         4      // external watchdog, must toggle >= 10 Hz
#define PIN_LED_STATUS       2      // the dev kit's own LED

// --- inputs -------------------------------------------------------------------
#define PIN_SIREN_FB        26      // armature relay auxiliary: the bell moved
#define PIN_KEY_ARM          5      // key switch, ARM / BYPASS, dry make to GND
#define PIN_BTN_SILENCE     13      // panel silence, momentary to GND
#define PIN_BTN_TEST        18      // bell test, momentary to GND
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define RTC_ADDR            0x68    // DS3231: the log has to survive a power cut

// A dry contact to GND on an internal pull-up. Every one of these is inverted by
// the HAL, so a snapped wire reads as the safe state: key missing reads BYPASS and
// raises a fault rather than silently disarming the building.
#ifndef LOOP_PULLUP_OHM
#define LOOP_PULLUP_OHM     4700u   // the precision resistor the loop hangs off
#endif
#ifndef LOOP_EOL_OHM
#define LOOP_EOL_OHM        4700u   // end of line, fitted in the last device
#endif
#ifndef LOOP_ALARM_OHM
#define LOOP_ALARM_OHM      1000u   // shorted by the detector relay
#endif
#ifndef LOOP_RAIL_MV
#define LOOP_RAIL_MV        3300u
#endif

// --- task layout --------------------------------------------------------------
#define PRIO_PANEL          18      // the highest task on the board, deliberately
#define PRIO_DIAL           10
#define STACK_PANEL         6144
#define STACK_DIAL          6144
#define TASK_WDT_TIMEOUT_MS 2000

// The loop scan is 20 ms: a detector relay takes longer than that to pull in, and
// three samples of hysteresis (60 ms) is still far inside the time it takes smoke
// to reach a ceiling device.
#define SAMPLE_PERIOD_MS    20
#define CONTROL_PERIOD_MS   50
#define DIAL_HZ             1       // steady-state reporting; an event goes out at once

// --- uplink -------------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/fire/state"
#define MQTT_TOPIC_CMD        "ranch/fire/cmd"
#define MQTT_TOPIC_DIAL       "ranch/fire/dial"   // kept asserted while a condition exists

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

// No strapping pin may be an output: a bell relay that is driven high while the
// chip resets would hold GPIO0 low through the download-mode divider... which is
// exactly the GPIO0 trap the switchboard board documents. Here the rule is simpler:
// the six loop pins and the two strapping pins (0, 2 used as status LED only).
constexpr int kOutputs[] = { PIN_SIREN, PIN_STROBE, PIN_PUMP, PIN_WDT_FEED,
                             PIN_I2C_SDA, PIN_I2C_SCL };
constexpr int kLoops[] = { PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER,
                           PIN_MCP, PIN_FLOW };
constexpr int kAll[] = {
    PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER, PIN_MCP, PIN_FLOW,
    PIN_SIREN, PIN_STROBE, PIN_PUMP, PIN_WDT_FEED, PIN_LED_STATUS,
    PIN_SIREN_FB, PIN_KEY_ARM, PIN_BTN_SILENCE, PIN_BTN_TEST,
    PIN_I2C_SDA, PIN_I2C_SCL,
};
constexpr size_t kAllLen = sizeof(kAll) / sizeof(kAll[0]);
constexpr size_t kOutLen = sizeof(kOutputs) / sizeof(kOutputs[0]);
constexpr size_t kLoopLen = sizeof(kLoops) / sizeof(kLoops[0]);

static_assert(pinsDisjoint(kAll, kAllLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidFlash(kAll, kAllLen), "board.h: a pin sits on the SPI flash bus");
static_assert(outputsAreDriven(kOutputs, kOutLen), "board.h: an output is on an input-only pin");
static_assert(analogIsAdc1(kLoops, kLoopLen), "board.h: a supervised loop must be on ADC1");
static_assert(pinsOnHeader(kAll, kAllLen), "board.h: a pin is not broken out on the DevKit V1 header");

}  // namespace ranch
#endif
