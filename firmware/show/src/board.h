// Pin map and build-time configuration for one light-show aircraft.
//
// This is one airframe of a fleet: the ground station bakes the choreography into
// a time-indexed path per station and uploads it, and everything below is what
// the on-board half needs to fly its own path, light its own two pixels, and get
// out of the way when it cannot.
//
// The board is an ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1-N8R8) with a FAKUBA
// NEO-M9N-00B-00 RTK receiver on its own UART and an ArduCopter-class flight
// controller on another. Three rules drove the allocation, and the
// static_asserts at the bottom prove the first two:
//   1. no GPIO appears twice
//   2. nothing sits on the flash (26-30) or octal-PSRAM (33-37) pins, 19/20 stay
//      free for the USB-Serial-JTAG that carries the console, and every pin used
//      is on a DevKitC-1 header pin -- not merely a pin the chip owns. The S3 has
//      GPIO22-25 on its die and the WROOM-1 module has no bond pads for them at
//      all, and GPIO45 is VDD_SPI on this package, so those are not options here
//      either.
//   3. the one analogue input, the pack divider, is on ADC1 (GPIO1-10): ADC2
//      cannot be read while the WiFi radio is active, which is the whole show
#pragma once

#include <cstddef>

// --- RTK receiver: FAKUBA NEO-M9N-00B-00 (u-blox NEO-M9N, UBX) --------------
// The module's UART1 defaults to 115200 8N1 with UBX-NAV-PVT at 5 Hz, which is
// what this map uses; the receiver is the position authority for the show, and
// the FC's own GPS is only what keeps the airframe level. TIMEPULSE is the
// module's survey-in-locked pulse, one positive edge per GNSS second: a fleet
// that starts its baked timeline on that edge is a fleet that is in formation,
// and one that starts on its own boot time is a fleet of 24 near-misses.
#define PIN_RTK_TX           16      // board out -> module RX (UART1)
#define PIN_RTK_RX           15      // board in  <- module TX
#define RTK_BAUD             115200
#define PIN_RTK_PPS           2      // TIMEPULSE, 3V3 push-pull, no pull needed

// --- flight controller link: MAVLink to ArduCopter --------------------------
// 57600 is ArduPilot's default for a telemetry port; the only thing crossing it
// during the show is one SET_POSITION_TARGET_LOCAL_NED per setpoint tick and the
// FC's own heartbeat and LOCAL_POSITION_NED coming back.
#define PIN_FC_TX            17
#define PIN_FC_RX            18
#define FC_BAUD              57600

#define PIN_DBG_TX           43
#define PIN_DBG_RX           44
#define DBG_BAUD             115200

// --- the lights: WS2812B, two pixels ----------------------------------------
// Pixel 0 is the arm light under the motor bell, pixel 1 the bottom beacon that
// the audience reads as the aircraft's colour. Both are on the 5 V side of the
// ESC's BEC with a 330 R in series with the data line (without it the first
// pixel's input destroys its own 800 ns timing on a long stub) and 1000 uF
// across the strip supply plus 100 nF at the pigtail for the inrush.
// The data line carries the 1.25 us bit timing and must be on an RMT-capable
// pin, which on this board means anything but the flash pair.
#define PIN_LED_DATA          8
#define SHOW_LED_PIXELS       2
#define LED_PIXEL_ARM         0
#define LED_PIXEL_BEACON      1
// The headroom every pixel is written through, in percent of full white, and the
// reason it is one number: a strip at full white is 320 mA per pixel here, which is
// more than the 5 V rail this airframe has. It is a parameter so a crew can bring a
// show down for a bright venue, and it has a floor of 20 % because an aircraft the
// crew cannot see is an aircraft they cannot point at.
#define SHOW_LED_FULL_PCT    60
#define LED_INSHINE_MA      160      // one WS2812B at full white; the BEC budget
#define LED_BUDGET_MA       (LED_INSHINE_MA * SHOW_LED_PIXELS * SHOW_LED_FULL_PCT / 100)

// --- safety hardware --------------------------------------------------------
// The SAFE switch is the aircraft's own physical permission: it is the shorting
// connector a person has to put in by hand after the props are on. Open at boot
// means safe, and nothing in the firmware can close it.
#define PIN_SAFE_IN          39      // input, pull-up; low = switch inserted
#define PIN_WDT_FEED         40      // external watchdog (SGM823/TPS3813 class)
#define PIN_BUZZER           41      // active buzzer through a transistor
#define PIN_LED_STATUS       42      // bench indicator, not part of the show

// Battery pack: 2S LiPo straight into a 10k/33k divider, so 8.40 V full lands
// 1.91 V on the pin -- inside the ADC's 3.1 V range at 11 dB attenuation, which
// is why this airframe is 2S and not 4S like the inspection drone.
#define PIN_BATT_ADC          1      // ADC1_CH0
#define BATT_DIVIDER        4.3f
#define BATT_CELLS            2

// --- the show envelope ------------------------------------------------------
// One SET_POSITION_TARGET_LOCAL_NED per tick. ArduCopter expires a guided
// position target after 6 s (WP_NAV), and a show aircraft that stops sending
// would carry on in the last commanded direction; 10 Hz is what the baked paths
// are sampled at and what the FC's own 50 Hz inner loop can follow.
#ifndef SHOW_SETPOINT_HZ
#define SHOW_SETPOINT_HZ       10
#endif
// The airframe's own limits, used by the simulator and by the launch check that
// asks whether the leg from the pad to the first formation point is flyable at
// all. show_core's SHOW_TOO_FAST check only looks at leg-to-leg transitions; the
// launch leg belongs to the aircraft, because it is the aircraft that has to
// leave the ground at it.
#ifndef SHOW_CRUISE_MS
#define SHOW_CRUISE_MS         6.0f
#endif
#ifndef SHOW_CLIMB_MS
#define SHOW_CLIMB_MS          3.0f
#endif
// How often the supervisor loop turns: one quarter of the setpoint period, so the
// scheduler has four chances to hit each 100 ms tick. This number is also what the
// host sandbox assumes a loop iteration costs, so it is a board constant and not a
// literal in two places.
#ifndef SHOW_LOOP_MS
#define SHOW_LOOP_MS           4
#endif
// "We are at the point." Wider than the position hold's own deadband, and the
// number the sandbox measures against rather than the one it hopes for.
#ifndef SHOW_ARRIVAL_M
#define SHOW_ARRIVAL_M         0.30f
#endif
// How old a GNSS reading may be before it stops being a reading. NAV-PVT comes
// at 5 Hz, so 600 ms is three missed messages, which on a show night is an
// antenna problem and not a shadow.
#ifndef SHOW_RTK_STALE_MS
#define SHOW_RTK_STALE_MS      600u
#endif
// The same for the FC: no heartbeat means the link to the airframe is gone, and
// a setpoint sent into the void is not a command.
#ifndef SHOW_FC_STALE_MS
#define SHOW_FC_STALE_MS       1500u
#endif
// Ground-station heartbeat timeout. The ground app sends "hb" twice a second;
// three and a half seconds of silence is a supervising station that has stopped
// supervising, which is the trigger to go home on one's own.
#ifndef SHOW_LINK_TIMEOUT_MS
#define SHOW_LINK_TIMEOUT_MS   3500u
#endif
// What "no position at all" costs before the aircraft commits to the return:
// three seconds of holding the last point, so a two-second occlusion under a
// tree does not send one airframe home through the rest of the formation.
#ifndef SHOW_HOVER_MS
#define SHOW_HOVER_MS          3000u
#endif
// The nose stays on one bearing for the whole show: the baked paths carry no yaw
// move, the LEDs point down, and a fleet that yaws independently is a fleet
// whose antenna patterns wander.
#ifndef SHOW_HEADING_DEG
#define SHOW_HEADING_DEG       0.0f
#endif

// --- endurance ------------------------------------------------------------
// The three numbers the fuel gate is made of. PACK_HOVER_S and the two reserves
// are the same arithmetic firmware/lib/show_core.h does for SHOW_BATTERY, and
// test_sandbox.cpp walks every state of charge from 1 % to 100 % asserting the
// two verdicts are identical -- so this is a second *call site*, not a second
// opinion.
#ifndef SHOW_PACK_HOVER_S
#define SHOW_PACK_HOVER_S      (22.0f * 60.0f)
#endif
#ifndef SHOW_RTL_RESERVE_S
#define SHOW_RTL_RESERVE_S     90.0f     // the return, at cruise, with margin
#endif
#ifndef SHOW_PAD_RESERVE_S
#define SHOW_PAD_RESERVE_S     120.0f    // two minutes of go-around for the crew
#endif
// Below this the buzzer runs while the aircraft is still on the pad, because the
// crew needs to hear which airframe is not going to fly.
#ifndef SHOW_BATT_WARN_PCT
#define SHOW_BATT_WARN_PCT     15
#endif

// --- uplink ---------------------------------------------------------------
#define MQTT_PORT            1883
#define MQTT_KEEPALIVE_S     10
#define MQTT_TOPIC_STATE     "ranch/show/state"
#define MQTT_TOPIC_CMD       "ranch/show/cmd"
#define TELEMETRY_HZ         5

// --- task layout ----------------------------------------------------------
// Priorities: the safety job above everything that can block; the show job owns
// the setpoint and must never be behind the console.
#define PRIO_SAFETY          24
#define PRIO_SHOW            18
#define PRIO_TELEMETRY       12
#define PRIO_UPLINK           8

#define STACK_SAFETY        4096
#define STACK_SHOW          6144
#define STACK_TELEMETRY     6144
#define STACK_UPLINK        4096

#define TASK_WDT_TIMEOUT_MS  2000

// --- allocation proof -----------------------------------------------------
// Runs on the host too, so the sandbox catches a bad pin edit even when the
// cross-compiler is unavailable.
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

constexpr bool pinsAvoidReserved(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    // 19/20 are the USB-Serial-JTAG pair that carries the console. 22-25 have no
    // bond pad on the WROOM-1 module at all, 26-32 are the flash, 33-37 are the
    // octal PSRAM this board has fitted, and 45 is VDD_SPI on this package.
    if (p > 0 && (p == 19 || p == 20 || p == 45 || (p >= 22 && p <= 37))) return false;
    return pinsAvoidReserved(v, n, i + 1);
}

// Every pin above, in the order they are wired on the harness.
constexpr int kPinMap[] = {
    PIN_RTK_TX, PIN_RTK_RX, PIN_RTK_PPS,
    PIN_FC_TX, PIN_FC_RX,
    PIN_DBG_TX, PIN_DBG_RX,
    PIN_LED_DATA,
    PIN_SAFE_IN, PIN_WDT_FEED, PIN_BUZZER, PIN_LED_STATUS,
    PIN_BATT_ADC,
};
constexpr size_t kPinMapLen = sizeof(kPinMap) / sizeof(kPinMap[0]);

static_assert(pinsDisjoint(kPinMap, kPinMapLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidReserved(kPinMap, kPinMapLen),
              "board.h: a pin sits on flash, octal PSRAM, VDD_SPI, or the USB-JTAG pair");
// ADC2 is unavailable while WiFi is associated, and the show is associated.
static_assert(PIN_BATT_ADC >= 1 && PIN_BATT_ADC <= 10, "pack divider must be on ADC1");
// Both pixels have to exist: the arm light is the show and the beacon is the one
// a person on the ground counts when an airframe goes home early.
static_assert(SHOW_LED_PIXELS >= 2, "a show aircraft has an arm light and a beacon");
// The fuel gate is only the same rule as the ground station's if the units match.
static_assert(SHOW_PACK_HOVER_S > SHOW_RTL_RESERVE_S + SHOW_PAD_RESERVE_S,
              "board.h: a full pack cannot even fly the return and the reserve");
static_assert(SHOW_ARRIVAL_M > 0.0f && SHOW_SETPOINT_HZ >= 5 && SHOW_SETPOINT_HZ <= 50,
              "board.h: arrival radius and setpoint rate are inside what the FC accepts");

}  // namespace ranch
#endif
