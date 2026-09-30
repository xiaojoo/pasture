// Pin map and build-time configuration for the ranch inspection drone.
//
// Everything hardware-specific lives here or behind hal.h. A board spin that
// moves a connector changes one number in this file and nothing else.
//
// This map is for an ESP32-S3-DevKitC-1 with an OV2640 module on a 14-pin
// adapter, so we own every wire. Three rules drove the allocation, and the
// static_asserts at the bottom prove the first two:
//   1. no GPIO appears twice
//   2. nothing sits on the flash (26-30) or octal-PSRAM (33-37) pins, and
//      19/20 stay free for the USB-Serial-JTAG that carries the console
//   3. both analogue inputs are on ADC1 (GPIO1-10): ADC2 cannot be read while
//      the WiFi radio is active, which is most of the flight
#pragma once

#include <cstddef>

// --- UART allocation --------------------------------------------------------
// FC telemetry 1 goes to the flight controller at 57600 (ArduPilot default).
// The debug UART is a spare only: ARDUINO_USB_CDC_ON_BOOT routes Serial over
// USB, so the console does not occupy a UART.
#define PIN_FC_TX            17
#define PIN_FC_RX            18
#define FC_BAUD              57600

#define PIN_DBG_TX           43
#define PIN_DBG_RX           44
#define DBG_BAUD             115200

// --- camera (OV2640) --------------------------------------------------------
// XCLK sits on GPIO3, an ADC1 channel this build never reads, so the camera
// costs no analogue capability. SCCB is on 4/5 so the sensor can be probed.
#define CAM_PIN_PWDN        -1
#define CAM_PIN_RESET       -1
#define CAM_PIN_XCLK         3
#define CAM_PIN_SIOD         4
#define CAM_PIN_SIOC         5
#define CAM_PIN_VSYNC        6
#define CAM_PIN_D7           7
#define CAM_PIN_D6           8
#define CAM_PIN_D5           9
#define CAM_PIN_D4          10
#define CAM_PIN_D3          11
#define CAM_PIN_D2          12
#define CAM_PIN_D1          13
#define CAM_PIN_D0          14
#define CAM_PIN_HREF        15
#define CAM_PIN_PCLK        16
#define CAM_XCLK_HZ   20000000

// --- safety hardware --------------------------------------------------------
// RCPIN is the FC's own kill input; we drive it through a transistor, so
// "high" means "do not allow motors".
#define PIN_SAFE_OUT        38      // output, active-high = safe
#define PIN_ARM_IN          39      // input, pull-up, low = arm switch closed
#define PIN_WDT_FEED        40      // external watchdog, must toggle >=10 Hz
#define PIN_LED_STATUS      41
#define PIN_BUZZER          42

// Battery pack: divider 10k/33k gives 4.3x, calibrated in params.
#define PIN_BATT_ADC        1       // ADC1_CH0
#define BATT_DIVIDER        4.3f
#define BATT_CELLS          4

// Current monitor for the payload bus (ACS712-20A, 100 mV/A, 2.5 V offset).
#define PIN_CURR_ADC        2       // ADC1_CH1
#define CURR_MV_PER_A       100.0f
#define CURR_ZERO_MV        2500.0f

// SD card for the flight log (hardware SPI). The S3's usable SPI-capable
// spare pins are 45/46/47/48; 45 is also VDD_SPI on some packages, so this
// build routes SCK to 46 and leaves 45 unconnected.
#define PIN_SD_CS           21
#define PIN_SD_SCK          46
#define PIN_SD_MOSI         47
#define PIN_SD_MISO         48

// --- flight envelope --------------------------------------------------------
#ifndef RANCH_CRUISE_MS
#define RANCH_CRUISE_MS      9.0f
#endif
#ifndef RANCH_CLIMB_MS
#define RANCH_CLIMB_MS       2.5f
#endif
#ifndef RANCH_MISSION_ALT_M
#define RANCH_MISSION_ALT_M  18.0f
#endif
#ifndef RANCH_GEOFENCE_R_M
#define RANCH_GEOFENCE_R_M   180.0f
#endif
#ifndef RANCH_GEOFENCE_CEIL_M
#define RANCH_GEOFENCE_CEIL_M 120.0f
#endif
// The supervisor's "we are at the waypoint" radius, which must be wider than
// the flight controller's own acceptance radius: the aircraft stops at WP_RADIUS
// and holds there, so a supervisor that waits to get closer than the FC does can
// never observe the arrival it is looking for. With both at 2 m this raced on a
// centimetre-scale disagreement and dropped about a quarter of the captures.
#ifndef RANCH_SUPERVISE_ARRIVAL_M
#define RANCH_SUPERVISE_ARRIVAL_M 4.0f
#endif

// --- video ------------------------------------------------------------------
// 640x480 @ ~14 fps is what the S3 can push over 2.4 GHz while the mission
// radio link still gets its 5 Hz telemetry. Falling back to 320x240 doubles it.
#ifndef RANCH_VIDEO_W
#define RANCH_VIDEO_W        640
#endif
#ifndef RANCH_VIDEO_H
#define RANCH_VIDEO_H        480
#endif
#ifndef RANCH_VIDEO_Q
#define RANCH_VIDEO_Q        24       // esp32-cam quality, lower is better
#endif
#ifndef RANCH_VIDEO_MAX_FRAME_KB
#define RANCH_VIDEO_MAX_FRAME_KB 96
#endif
#define VIDEO_PORT           81       // MJPEG over HTTP multipart
#define VIDEO_FPS_TARGET     14

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT            1883
#define MQTT_KEEPALIVE_S     10
#define MQTT_TOPIC_STATE     "ranch/drone/state"
#define MQTT_TOPIC_EVENT     "ranch/drone/event"
#define MQTT_TOPIC_CMD       "ranch/drone/cmd"
#define TELEMETRY_HZ         5

// --- task layout ------------------------------------------------------------
// Priorities: safety above everything that can block, video lowest so it can
// never starve the mission loop.
#define PRIO_SAFETY          24
#define PRIO_MISSION         18
#define PRIO_MAVLINK_RX      16
#define PRIO_TELEMETRY       12
#define PRIO_VIDEO            4

#define STACK_SAFETY        4096
#define STACK_MISSION       6144
#define STACK_MAVLINK       4096
#define STACK_TELEMETRY     6144
#define STACK_VIDEO        12288

#define TASK_WDT_TIMEOUT_MS  2000

// --- allocation proof -------------------------------------------------------
// Runs on the host too, so firmware/lib tests catch a bad pin edit even when
// the cross-compiler is unavailable.
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
    if (p > 0 && (p == 19 || p == 20 || (p >= 26 && p <= 30) || (p >= 33 && p <= 37))) return false;
    return pinsAvoidReserved(v, n, i + 1);
}

constexpr int kPinMap[] = {
    PIN_FC_TX, PIN_FC_RX, PIN_DBG_TX, PIN_DBG_RX,
    CAM_PIN_XCLK, CAM_PIN_SIOD, CAM_PIN_SIOC, CAM_PIN_VSYNC,
    CAM_PIN_D7, CAM_PIN_D6, CAM_PIN_D5, CAM_PIN_D4,
    CAM_PIN_D3, CAM_PIN_D2, CAM_PIN_D1, CAM_PIN_D0,
    CAM_PIN_HREF, CAM_PIN_PCLK,
    PIN_SAFE_OUT, PIN_ARM_IN, PIN_WDT_FEED, PIN_LED_STATUS, PIN_BUZZER,
    PIN_BATT_ADC, PIN_CURR_ADC,
    PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO,
};
constexpr size_t kPinMapLen = sizeof(kPinMap) / sizeof(kPinMap[0]);

static_assert(pinsDisjoint(kPinMap, kPinMapLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidReserved(kPinMap, kPinMapLen),
              "board.h: a pin sits on flash, octal PSRAM, or the USB-JTAG pair");
// ADC2 is unavailable while WiFi is associated, so the fuel gauge must not use it.
static_assert(PIN_BATT_ADC >= 1 && PIN_BATT_ADC <= 10, "battery sense must be on ADC1");
static_assert(PIN_CURR_ADC >= 1 && PIN_CURR_ADC <= 10, "current sense must be on ADC1");

}  // namespace ranch
#endif
