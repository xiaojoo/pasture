// GENERATED FILE - do not edit, edit the project and re-run:
//   node tools/bundle.mjs
// Source: firmware/show + firmware/lib  (17 files, 167.6 KB before bundling)
// The receiver, flight controller, airframe, strip and pack in hal_sim.cpp are what the programme is flown against.
//
// Build the same code for hardware with:  pio run -d firmware/show

#define RANCH_SIM 1

/* ==================== src/board.h ==================== */

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

/* ==================== src/hal.h ==================== */

// Hardware abstraction for one show aircraft. Two implementations:
// hal_esp32.cpp drives the real pins (NeoPixel over RMT, two UARTs, an ADC), and
// hal_sim.cpp stands in for the things this board is wired to -- a NEO-M9N that
// knows where it is, an ArduCopter that follows setpoints at a bounded speed, a
// WS2812B strip, a pack that runs down and an external watchdog that resets the
// airframe if the feed stops.
//
// Nothing in this header allocates, and every call is safe to make from any task.

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);

// Console sink for telemetry and the boot banner: USB-CDC on the aircraft,
// stdout in the browser and host builds. Never blocks on a reader.
void consoleWrite(const char* data, size_t len);

// Console source: what a person at the USB-C port (or the simulator's serial
// monitor) has typed. Returns how many bytes it took, 0 when nothing is waiting,
// and never blocks. Line assembly lives in main.cpp, not here, so the aircraft's
// UART and the sandbox's queue are parsed by the same code.
int consoleRead(char* buf, size_t cap);

// RTK receiver (UBX, one UART of its own).
int  rtkAvailable();
int  rtkRead(uint8_t* buf, size_t cap);
void rtkWrite(const uint8_t* buf, size_t len);   // UBX configuration frames out
// True once per GNSS second, on the rising edge of the receiver's TIMEPULSE.
// A show that starts on this edge starts on the same absolute second as every
// other airframe in it, which is what makes a baked timeline flyable.
bool rtkPpsEdge();

// Flight controller link (MAVLink).
int  fcAvailable();
int  fcRead(uint8_t* buf, size_t cap);
void fcWrite(const uint8_t* buf, size_t len);

// The lights. Pixels are staged into the strip's buffer by ledSetPixel() and
// moved out on the wire by ledCommit(); committing is the only call that has
// interrupts-disabled work in it, which is why it is not per pixel.
void ledSetPixel(uint8_t index, uint32_t rgb);
void ledCommit();

// Safety inputs and outputs. The SAFE switch is read, never written: closing it
// is a person's decision. The buzzer is the crew's audible channel for "this
// airframe is not flying" and for a pack that is too low to start.
bool safeSwitchClosed();         // sampled now
bool safeSwitchIsClosed();       // the last sample
void setBuzzer(bool on);
bool buzzerIsOn();
void feedWatchdog();             // the external watchdog wants an edge, >=10 Hz
void setStatusLed(bool on);

// Analogy. Pack voltage with the divider already divided out.
float batteryVolts();

// Non-volatile parameters. Return false when the key is absent or the stored
// type does not match.
bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

// Network. The ground station reaches this airframe over MQTT on this radio and
// nowhere else; there is no second link.
bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

// Reset reason string, for the boot banner and the first telemetry frame.
const char* resetReason();

#if defined(RANCH_SIM)
// Advance the synthetic airframe, receiver and strip by the real (browser) or
// virtual (host test) clock.
void halSimPump();

// What the model actually saw, so a host test can assert on "the strip is
// physically showing this colour" and "the FC latched this many setpoints"
// instead of only on "no crash".
struct SimCounters {
    uint32_t rtk_frames;         // NAV-PVT messages the receiver put on the wire
    uint32_t cfg_frames;         // UBX configuration frames it accepted from us
    uint32_t fc_frames_in;       // MAVLink frames the FC accepted from the board
    uint32_t setpoints_latched;  // ... of which were position targets
    uint32_t commands_taken;     // arm / mode / land commands the FC obeyed
    uint32_t led_commits;        // times the strip was latched
    uint32_t wdt_feeds;
    uint32_t buzzer_ms;
    uint16_t last_type_mask;     // what the FC decoded out of the last target
    float last_n;                // the same, in metres: the FC's copy of the point
    float last_e;
    float last_d;
    float pack_pct;              // the charge the simulated pack really holds
    uint32_t led_pixel[2];       // what the two pixels are physically showing
};
void halSimCounters(SimCounters& out);

// The state of the sky and the radio, which is all a test is allowed to touch.
// These change what the *receiver* reports, not what the firmware believes: a
// firmware that invented a fix would pass the same test that catches this one.
enum SimFix : uint8_t {
    SIM_FIX_NONE = 0,     // nothing locked: no position, no TIMEPULSE
    SIM_FIX_SINGLE,       // 3D, no correction data
    SIM_FIX_RTK_FLOAT,    // correction present, ambiguity not resolved
    SIM_FIX_RTK_FIXED,    // the fix a show is flown on
};
void halSimFix(SimFix f);
void halSimBatteryPct(float pct);   // swap the pack on the bench, so to speak
// Take the shorting connector out of the aircraft. This is the input every launch
// check is asked about first, and the only way to test that a firmware cannot be
// talked past it is to have a person's hand on the other end of the wire.
void halSimSafeSwitch(bool closed);
void halSimClear();                 // put the aircraft and the sky back
#endif

}  // namespace ranch

/* ==================== src/mavlink.h ==================== */

// MAVLink v2 for the show aircraft: framing, CRC, and the four messages this
// airframe actually exchanges with its flight controller. Written by hand rather
// than generated so the build has no codegen step, and every wire offset below is
// the v2 size-descending order with the XML order it came from in the comment.
//
// The ids and CRC_EXTRA values were derived from the dialect XML with the mavgen
// rule and cross-checked against the table the generated C library ships
// (.probe/crcx.mjs is that check): a message id that is off by one and a CRC_EXTRA
// that is off by one both look exactly like "the FC never answered", which is the
// most expensive kind of bug to find on a show night.
//
// No dynamic allocation, no exceptions, and a corrupt frame can never advance the
// parser past the buffer.

#include <cstddef>
#include <cstdint>

namespace ranch {

constexpr uint8_t MAV_STX = 0xFD;
constexpr size_t  MAV_MAX_PAYLOAD = 64;
constexpr size_t  MAV_FRAME_MAX = 6 + MAV_MAX_PAYLOAD + 2;

enum MavMsg : uint32_t {
    MSG_HEARTBEAT                     = 0,    // crc_extra 50,  len 9
    MSG_LOCAL_POSITION_NED            = 32,   // crc_extra 185, len 28
    MSG_SET_POSITION_TARGET_LOCAL_NED = 84,   // crc_extra 143, len 53
    MSG_COMMAND_LONG                  = 76,   // crc_extra 152, len 33
};

// CRC_EXTRA values from the common dialect.
uint8_t mavCrcExtra(uint32_t msgId);

enum MavCmd : uint16_t {
    CMD_NAV_TAKEOFF            = 22,
    CMD_NAV_LAND               = 21,
    CMD_DO_SET_MODE            = 176,
    CMD_COMPONENT_ARM_DISARM   = 400,
};

// ArduCopter's own custom_mode numbers, carried by DO_SET_MODE. These belong to
// the FC firmware, not to MAVLink, so they are build-time configuration: read
// them off the FC's Mode page and put the real numbers here if the build differs.
// The simulator honours the same numbers, so a wrong one fails on the bench as
// "the mode never changed" rather than in the air.
#ifndef FC_MODE_GUIDED
#define FC_MODE_GUIDED   4
#endif
#ifndef FC_MODE_RTL
#define FC_MODE_RTL      6
#endif
#ifndef FC_MODE_LAND
#define FC_MODE_LAND     9
#endif

// MAV_FRAME_LOCAL_NED: the frame show_core bakes its paths in, so a setpoint
// leaves this file with no unit conversion anywhere behind it.
enum MavFrame : uint8_t {
    FRAME_LOCAL_NED = 1,
};

// Which dimensions the FC is told to ignore. The bits are the dialect's
// POSITION_TARGET_TYPEMASK values, spelled out rather than added in one line
// because "xy + z + yaw locked" is the whole control contract of this airframe:
//   8|16|32      velocity ignored      -- the path is positional, the FC plans it
//   64|128|256   acceleration ignored
//   2048         yaw rate ignored, yaw angle commanded
// x, y, z and yaw are left at zero in the mask, which means "use these".
enum PositionTargetMask : uint16_t {
    TYPEMASK_VELOCITY_IGNORED = 8 | 16 | 32,
    TYPEMASK_ACCEL_IGNORED    = 64 | 128 | 256,
    TYPEMASK_YAW_RATE_IGNORED = 2048,
    TYPEMASK_POSITION_YAW     = TYPEMASK_VELOCITY_IGNORED | TYPEMASK_ACCEL_IGNORED
                              | TYPEMASK_YAW_RATE_IGNORED,       // 0x09F8
};

inline bool hbArmed(uint8_t base_mode) { return (base_mode & 0x80) != 0; }   // MAV_MODE_FLAG_SAFETY_ARMED

struct Crc16 {
    uint16_t v = 0xFFFF;
    void add(uint8_t b) {
        uint8_t x = static_cast<uint8_t>(b ^ (v & 0xFF));
        x ^= static_cast<uint8_t>(x << 4);
        v = static_cast<uint16_t>((v >> 8) ^ (x << 8) ^ (x << 3) ^ (x >> 4));
    }
    void addBytes(const uint8_t* p, size_t n) { while (n--) add(*p++); }
};

// Little-endian writers for the payload area.
class MavWriter {
public:
    MavWriter(uint8_t* payload, size_t cap) : p_(payload), cap_(cap) {}

    void u8(uint8_t v)   { put(v); }
    void i8(int8_t v)    { put(static_cast<uint8_t>(v)); }
    void u16(uint16_t v) { put(v & 0xFF); put(v >> 8); }
    void i16(int16_t v)  { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) put(v >> (8 * i)); }
    void i32(int32_t v)  { u32(static_cast<uint32_t>(v)); }
    void f32(float v)    { uint32_t u; static_assert(sizeof(u) == sizeof(v), "float size");
                           __builtin_memcpy(&u, &v, 4); u32(u); }

    size_t size() const { return n_; }
    bool ok() const { return n_ <= cap_; }

private:
    void put(uint8_t b) { if (n_ < cap_) p_[n_++] = b; else n_ = cap_ + 1; }
    uint8_t* p_;
    size_t cap_;
    size_t n_ = 0;
};

// Assembles a complete v2 frame into out and returns its length, or 0 when the
// message table does not know the id (a silently mis-CRCed frame is worse than a
// refused one).
class MavFrameBuilder {
public:
    MavFrameBuilder(uint8_t sysid, uint8_t compid) : sys_(sysid), comp_(compid) {}
    size_t build(uint32_t msgId, const uint8_t* payload, size_t len, uint8_t* out, size_t cap);

private:
    uint8_t sys_, comp_, seq_ = 0;
};

// Received message, with the payload exposed through typed getters that bound
// every read.
struct MavMessage {
    uint32_t id = 0;
    uint8_t sysid = 0;
    uint8_t compid = 0;
    const uint8_t* payload = nullptr;
    size_t len = 0;

    uint8_t u8At(size_t off) const { return off < len ? payload[off] : 0; }
    uint16_t u16At(size_t off) const {
        return off + 1 < len ? static_cast<uint16_t>(payload[off] | (payload[off + 1] << 8)) : 0;
    }
    int32_t i32At(size_t off) const {
        if (off + 4 > len) return 0;
        return static_cast<int32_t>(payload[off] | (payload[off + 1] << 8) |
                                    (payload[off + 2] << 16) | (static_cast<uint32_t>(payload[off + 3]) << 24));
    }
    float f32At(size_t off) const {
        uint32_t u = static_cast<uint32_t>(i32At(off));
        float f;
        __builtin_memcpy(&f, &u, 4);
        return f;
    }
};

// Stream parser. Feed it whatever the UART gave you; it emits whole, CRC-checked
// messages and reports how many frames it had to discard.
class MavParser {
public:
    bool push(uint8_t b, MavMessage& out);

    uint32_t crcErrors() const { return crc_err_; }
    uint32_t dropped() const { return drop_; }
    uint32_t parsed() const { return ok_; }

private:
    enum State : uint8_t { Idle, Len, Incompat, Compat, Seq, Sys, Comp, Id0, Id1, Id2, Payload, Crc1, Crc2 };
    State st_ = Idle;
    uint8_t buf_[MAV_FRAME_MAX];
    size_t need_ = 0;
    size_t have_ = 0;
    uint32_t msg_id_ = 0;
    uint8_t seq_ = 0, sys_ = 0, comp_ = 0;
    uint32_t crc_err_ = 0, drop_ = 0, ok_ = 0;
};

// --- the messages this airframe sends, and the one it answers ---------------
//   4-byte: custom_mode
//   1-byte: type autopilot base_mode system_status mavlink_version
struct HeartbeatTx {
    uint32_t custom_mode;
    uint8_t type, autopilot, base_mode, system_status;
    void pack(MavWriter& w) const;
};

// Wire order of SET_POSITION_TARGET_LOCAL_NED, from the size-descending rule:
//   4-byte: time_boot_ms x y z vx vy vz afx afy afz yaw yaw_rate
//   2-byte: type_mask
//   1-byte: target_system target_component coordinate_frame
struct PositionTargetTx {
    float x, y, z;             // NED metres from the FC's local origin
    float yaw_rad;
    uint16_t type_mask;
    uint8_t coordinate_frame;
    void pack(MavWriter& w, uint32_t time_boot_ms) const;
};

//   4-byte: param1..param7
//   2-byte: command
//   1-byte: target_system target_component confirmation
struct CommandLongTx {
    float p[7];
    uint16_t command;
    uint8_t target_sys, target_comp;
    void pack(MavWriter& w) const;
};

// --- the two messages this airframe listens for -----------------------------
struct Heartbeat {
    bool armed;
    bool alive;               // a frame that parsed at all
    uint32_t custom_mode;
    uint8_t system_status;
};
Heartbeat decodeHeartbeat(const MavMessage& m);

// The FC's own belief about where it put the airframe, in local NED metres. This
// is a second, independent position: the show's position authority is the RTK
// receiver on its own UART, and the two agreeing is the only evidence there is
// that the aircraft is where the programme thinks it is.
struct LocalNed {
    float n, e, d;
    float vn, ve, vd;
    bool valid;
};
LocalNed decodeLocalPosition(const MavMessage& m);

}  // namespace ranch

/* ==================== src/mavlink.cpp ==================== */


namespace ranch {

uint8_t mavCrcExtra(uint32_t id) {
    switch (id) {
        case MSG_HEARTBEAT:                     return 50;
        case MSG_LOCAL_POSITION_NED:            return 185;
        case MSG_COMMAND_LONG:                  return 152;
        case MSG_SET_POSITION_TARGET_LOCAL_NED: return 143;
        default:                                return 0xFF;   // unknown, refuse to send
    }
}

size_t MavFrameBuilder::build(uint32_t msgId, const uint8_t* payload, size_t len,
                              uint8_t* out, size_t cap) {
    if (len > MAV_MAX_PAYLOAD) return 0;
    if (mavCrcExtra(msgId) == 0xFF) return 0;
    const size_t total = 12 + len;
    if (cap < total) return 0;

    size_t i = 0;
    out[i++] = MAV_STX;
    out[i++] = static_cast<uint8_t>(len);
    out[i++] = 0;                       // incompat flags
    out[i++] = 0;                       // compat flags
    out[i++] = seq_++;
    out[i++] = sys_;
    out[i++] = comp_;
    out[i++] = static_cast<uint8_t>(msgId & 0xFF);
    out[i++] = static_cast<uint8_t>((msgId >> 8) & 0xFF);
    out[i++] = static_cast<uint8_t>((msgId >> 16) & 0xFF);
    for (size_t p = 0; p < len; ++p) out[i++] = payload[p];

    Crc16 crc;
    crc.addBytes(out + 1, 9 + len);     // length .. payload, excluding the STX
    crc.add(mavCrcExtra(msgId));
    out[i++] = static_cast<uint8_t>(crc.v & 0xFF);
    out[i++] = static_cast<uint8_t>(crc.v >> 8);
    return i;
}

bool MavParser::push(uint8_t b, MavMessage& out) {
    switch (st_) {
        case Idle:
            if (b == MAV_STX) { have_ = 0; st_ = Len; }
            return false;
        case Len:      need_ = b; buf_[0] = b; st_ = Incompat; return false;
        case Incompat: buf_[1] = b; st_ = Compat; return false;
        case Compat:   buf_[2] = b; st_ = Seq; return false;
        case Seq:      buf_[3] = b; st_ = Sys; return false;
        case Sys:      sys_ = b; buf_[4] = b; st_ = Comp; return false;
        case Comp:     comp_ = b; buf_[5] = b; st_ = Id0; return false;
        case Id0:      msg_id_ = b; buf_[6] = b; st_ = Id1; return false;
        case Id1:      msg_id_ |= static_cast<uint32_t>(b) << 8; buf_[7] = b; st_ = Id2; return false;
        case Id2:
            msg_id_ |= static_cast<uint32_t>(b) << 16;
            buf_[8] = b;
            if (need_ > MAV_MAX_PAYLOAD) { drop_++; st_ = Idle; return false; }
            have_ = 0;
            st_ = need_ ? Payload : Crc1;
            return false;
        case Payload:
            buf_[9 + have_] = b;
            if (++have_ >= need_) st_ = Crc1;
            return false;
        case Crc1:
            buf_[9 + need_] = b;
            st_ = Crc2;
            return false;
        case Crc2: {
            buf_[10 + need_] = b;
            Crc16 crc;
            crc.addBytes(buf_, 9 + need_);
            const uint8_t extra = mavCrcExtra(msg_id_);
            if (extra == 0xFF) { drop_++; st_ = Idle; return false; }
            crc.add(extra);
            const uint16_t got = static_cast<uint16_t>(buf_[9 + need_] | (buf_[10 + need_] << 8));
            st_ = Idle;
            if (got != crc.v) { crc_err_++; return false; }
            out.id = msg_id_;
            out.sysid = sys_;
            out.compid = comp_;
            out.payload = buf_ + 9;
            out.len = need_;
            ok_++;
            return true;
        }
    }
    st_ = Idle;
    return false;
}

void HeartbeatTx::pack(MavWriter& w) const {
    w.u32(custom_mode);
    w.u8(type);
    w.u8(autopilot);
    w.u8(base_mode);
    w.u8(system_status);
    w.u8(3);                            // mavlink_version, the one field that is not a payload
}

void PositionTargetTx::pack(MavWriter& w, uint32_t time_boot_ms) const {
    w.u32(time_boot_ms);
    w.f32(x); w.f32(y); w.f32(z);
    // The velocity, acceleration and yaw-rate terms are ignored by the FC -- the
    // type mask says so -- but they still occupy their bytes on the wire, and
    // writing zero rather than leaving them uninitialised is what keeps a
    // mis-ordered pack from becoming a 30 m/s command on the next dialect bump.
    w.f32(0.0f); w.f32(0.0f); w.f32(0.0f);
    w.f32(0.0f); w.f32(0.0f); w.f32(0.0f);
    w.f32(yaw_rad);
    w.f32(0.0f);
    w.u16(type_mask);
    w.u8(1);                            // target_system: the FC
    w.u8(1);                            // target_component
    w.u8(coordinate_frame);
}

void CommandLongTx::pack(MavWriter& w) const {
    for (int i = 0; i < 7; ++i) w.f32(p[i]);
    w.u16(command);
    w.u8(target_sys);
    w.u8(target_comp);
    w.u8(0);                            // confirmation
}

Heartbeat decodeHeartbeat(const MavMessage& m) {
    Heartbeat h{};
    h.alive = m.len >= 9;               // custom_mode, type, autopilot, base_mode,
    if (!h.alive) return h;             // system_status, mavlink_version
    h.custom_mode = static_cast<uint32_t>(m.i32At(0));
    h.armed = hbArmed(m.u8At(6));
    h.system_status = m.u8At(7);
    return h;
}

LocalNed decodeLocalPosition(const MavMessage& m) {
    LocalNed l{};
    if (m.len < 28) return l;           // a truncated frame is no position at all
    l.n = m.f32At(4);
    l.e = m.f32At(8);
    l.d = m.f32At(12);
    l.vn = m.f32At(16);
    l.ve = m.f32At(20);
    l.vd = m.f32At(24);
    l.valid = true;
    return l;
}

}  // namespace ranch

/* ==================== ../lib/frame_codec.h ==================== */

// Bounded key=value telemetry codec shared by the MQTT uplink, the serial
// console line and the SD log, so the ground application parses one format no
// matter which transport carried it.
//
//   DRONE,mode=TRANSIT,alt=18.0,wp=3,batt=87,rssi=-62,link=up
//
// Writers never run past the caller's buffer: once it is full every further
// add() fails and overflow() stays set, so a truncated frame is detectable
// instead of silently wrong.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranch {

constexpr uint8_t TELEMETRY_MAX_FIELDS = 24;

class FrameWriter {
public:
    FrameWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {
        if (cap_ > 0) buf_[0] = '\0';
    }

    void begin(const char* source) {
        n_ = 0;
        full_ = false;
        put(source);
    }

    bool add(const char* key, int value) {
        char tmp[24];
        std::snprintf(tmp, sizeof(tmp), "%d", value);
        return add(key, tmp);
    }

    bool add(const char* key, float value, int digits = 1) {
        char tmp[24];
        if (digits <= 0) std::snprintf(tmp, sizeof(tmp), "%.0f", static_cast<double>(value));
        else std::snprintf(tmp, sizeof(tmp), "%.*f", digits, static_cast<double>(value));
        return add(key, tmp);
    }

    bool add(const char* key, const char* value) {
        if (full_) return false;
        put(",");
        put(key);
        put("=");
        put(value);
        return !full_;
    }

    // Ends the frame with a newline so the same buffer can go straight to serial.
    void endLine() {
        if (n_ + 1 < cap_) buf_[n_++] = '\n';
        else full_ = true;
        if (cap_) buf_[n_ < cap_ ? n_ : cap_ - 1] = '\0';
    }

    bool overflow() const { return full_; }
    size_t size() const { return n_; }
    const char* c_str() const { return buf_; }

private:
    void put(const char* s) {
        while (*s != '\0') {
            if (n_ + 1 >= cap_) { full_ = true; return; }
            buf_[n_++] = *s++;
        }
        buf_[n_] = '\0';
    }

    char* buf_;
    size_t cap_;
    size_t n_ = 0;
    bool full_ = false;
};

struct Field {
    const char* key;
    size_t key_len;
    const char* value;
    size_t value_len;
};

// Single pass, no copies: fields point into the caller's buffer.
class FrameReader {
public:
    FrameReader(const char* buf, size_t len) : p_(buf), end_(buf + len) {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\r' || *p_ == '\n')) ++p_;
        const char* comma = static_cast<const char*>(std::memchr(p_, ',', static_cast<size_t>(end_ - p_)));
        source_ = p_;
        source_len_ = comma ? static_cast<size_t>(comma - p_) : static_cast<size_t>(end_ - p_);
        cur_ = comma ? comma + 1 : end_;
    }

    const char* source() const { return source_; }
    size_t sourceLength() const { return source_len_; }

    bool next(Field& out) {
        if (cur_ >= end_) return false;
        const size_t rest = static_cast<size_t>(end_ - cur_);
        const char* eq = static_cast<const char*>(std::memchr(cur_, '=', rest));
        if (!eq) return false;
        const char* comma = static_cast<const char*>(std::memchr(eq + 1, ',', static_cast<size_t>(end_ - eq - 1)));
        const char* stop = comma ? comma : end_;
        while (stop > eq + 1 && (stop[-1] == '\n' || stop[-1] == '\r' || stop[-1] == ' ')) --stop;
        out.key = cur_;
        out.key_len = static_cast<size_t>(eq - cur_);
        out.value = eq + 1;
        out.value_len = static_cast<size_t>(stop - eq - 1);
        cur_ = comma ? comma + 1 : end_;
        return true;
    }

    // Case-sensitive key match, length aware, so a truncated buffer cannot
    // match a longer key that happens to start with the same letters.
    // Const because a lookup walks a private copy of the cursor: reading a field
    // out of a frame must not consume the frame the caller is still working from.
    bool find(const char* key, Field& out) const {
        const size_t klen = std::strlen(key);
        FrameReader r(p_, static_cast<size_t>(end_ - p_));
        Field f{};
        while (r.next(f)) {
            if (f.key_len == klen && std::memcmp(f.key, key, klen) == 0) { out = f; return true; }
        }
        return false;
    }

    bool getFloat(const char* key, float& out) const {
        Field f{};
        if (!find(key, f)) return false;
        char tmp[16];
        const size_t n = f.value_len < sizeof(tmp) - 1 ? f.value_len : sizeof(tmp) - 1;
        std::memcpy(tmp, f.value, n);
        tmp[n] = '\0';
        out = std::strtof(tmp, nullptr);
        return true;
    }

    bool getInt(const char* key, long& out) const {
        Field f{};
        if (!find(key, f)) return false;
        char tmp[16];
        const size_t n = f.value_len < sizeof(tmp) - 1 ? f.value_len : sizeof(tmp) - 1;
        std::memcpy(tmp, f.value, n);
        tmp[n] = '\0';
        out = std::strtol(tmp, nullptr, 10);
        return true;
    }

private:
    const char* p_;
    const char* end_;
    const char* source_;
    size_t source_len_;
    const char* cur_;
};

}  // namespace ranch

/* ==================== ../lib/scheduler.h ==================== */

// Cooperative rate scheduler: fixed job table, no allocation, no dynamic
// timers. Each task loop calls due() with the elapsed milliseconds and gets a
// true on the ticks it owns. Overrun is measured, not silently dropped, because
// a loop that misses its rate is the first symptom of a stack or priority bug.

#include <cstdint>

namespace ranch {

constexpr uint8_t SCHED_MAX_JOBS = 12;

struct Job {
    uint32_t period_ms;
    uint32_t acc_ms;
    uint32_t late_ms;      // how far behind the last fired tick was
    uint32_t runs;
    uint32_t overruns;
    uint32_t worst_late_ms;
    const char* name;
    bool used;
};

class Scheduler {
public:
    int add(const char* name, uint32_t period_ms) {
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) {
            if (jobs_[i].used) continue;
            jobs_[i] = Job{period_ms, 0, 0, 0, 0, 0, name, true};
            ++count_;
            return i;
        }
        return -1;
    }

    // Returns true when the job is due. dt_ms is the wall time since the last
    // call for this job, so a job that is called twice as often as its rate
    // still fires on schedule.
    bool due(int id, uint32_t dt_ms) {
        if (id < 0 || id >= SCHED_MAX_JOBS || !jobs_[id].used) return false;
        Job& j = jobs_[id];
        j.acc_ms += dt_ms;
        if (j.acc_ms < j.period_ms) return false;
        const uint32_t late = j.acc_ms - j.period_ms;
        j.late_ms = late;
        if (late > j.worst_late_ms) j.worst_late_ms = late;
        if (late > j.period_ms) j.overruns++;
        j.acc_ms = 0;
        j.runs++;
        return true;
    }

    // Wall time covered by the tick that just fired. A job that runs its state
    // machine on period_ms instead of this number loses time whenever the loop
    // is late, and the clock it maintains quietly falls behind reality.
    uint32_t elapsed(int id) const {
        if (id < 0 || id >= SCHED_MAX_JOBS || !jobs_[id].used) return 0;
        return jobs_[id].period_ms + jobs_[id].late_ms;
    }

    // Start over. The host test runs a whole mission twice in one process, and
    // a second setup() must not find the job table full of the first one.
    void clear() {
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) jobs_[i] = Job{};
        count_ = 0;
    }

    uint8_t count() const { return count_; }
    const Job& job(int id) const { return jobs_[id]; }

    // Total CPU spent inside the loops, for the health frame.
    uint32_t totalRuns() const {
        uint32_t n = 0;
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) if (jobs_[i].used) n += jobs_[i].runs;
        return n;
    }

    uint32_t totalOverruns() const {
        uint32_t n = 0;
        for (uint8_t i = 0; i < SCHED_MAX_JOBS; ++i) if (jobs_[i].used) n += jobs_[i].overruns;
        return n;
    }

private:
    Job jobs_[SCHED_MAX_JOBS]{};
    uint8_t count_ = 0;
};

}  // namespace ranch

/* ==================== ../lib/show_core.h ==================== */

// Drone light-show core: shape sampling, aircraft-to-point assignment, baked
// trajectories, the checks that decide whether a plan may fly at all, and the
// per-aircraft return slots that keep a hundred simultaneous RTLs from converging
// on one coordinate.
//
// Platform neutral and side-effect free like the rest of firmware/lib: geometry and
// a clock go in, positions and verdicts come out. The ground station decides whether
// to upload, the flight controller decides how to hold position, and neither is this
// file's business.
//
// What is deliberately copied from how the real shows are built (Intel Shooting Star,
// DJI/DXL, 高巨/一飞 and the rest all converge on the same shape):
//
//  - The choreography is compiled **offline** into one time-indexed path per
//    aircraft and uploaded before the show. In flight nobody pilots: each aircraft
//    follows its own baked path and holds position with RTK, and the ground station
//    supervises. That is why `showPath()` is a pure function of (shape A, shape B,
//    progress) and there is no "joystick" anywhere in here.
//  - A shape is a set of points; the show is a sequence of shapes. Which aircraft
//    gets which point is its own solved step (`showAssign`) -- a naive index-to-index
//    mapping makes aircraft fly across the field through each other at every change.
//  - Separation is checked on the **baked paths**, not on the shape outlines: two
//    formations that are each perfectly spaced can still cross in the middle of a
//    transition. `showValidate` samples the transitions for exactly that.
//  - Every aircraft has its own return slot, offset from the launch origin by its
//    own station number. A mass failsafe that sends 100 aircraft to one point is a
//    collision, not a recovery.

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ranch {

constexpr int SHOW_MAX_DRONES = 128;      // one ground station, this build
constexpr int SHOW_MAX_ACTS = 16;
constexpr float SHOW_PI = 3.14159265358979f;

// Local NED metres, origin at the surveyed launch point, `d` positive downwards --
// the same convention MAVLink's LOCAL_POSITION_NED uses, so a baked point can be
// uploaded without a unit conversion anywhere.
struct ShowPoint {
    float n = 0.0f;
    float e = 0.0f;
    float d = 0.0f;
};

enum ShowShape : uint8_t {
    SHAPE_RING = 0,     // a circle, seen edge-on or from below
    SHAPE_GRID,         // a filled lattice: the one that reads as a wall of light
    SHAPE_HEART,        // the classic parametric heart
    SHAPE_WAVE,         // a travelling sine across the field
    SHAPE_COLUMN,       // vertical threads, one per station column
    SHAPE_ARC,          // a bow: cheap, and the one that scales to any count
    SHOW_SHAPE_COUNT
};

struct ShowAct {
    uint8_t shape = SHAPE_RING;
    float scale_m = 18.0f;        // the shape's half-extent
    float alt_m = 40.0f;          // height above the launch origin, positive up
    float hold_s = 12.0f;         // time held as a formation
    float move_s = 6.0f;          // time spent getting there from the previous act
    uint32_t colour = 0x00e0a0;   // 0xRRGGBB, the WS2812 state for the whole act
};

struct ShowPlan {
    int drones = 24;
    ShowAct acts[SHOW_MAX_ACTS];
    int act_count = 0;
    // The three numbers a show is actually judged on. All of them are the values a
    // real operator writes in a risk assessment, not tuning knobs.
    float separation_m = 2.0f;    // minimum aircraft-to-aircraft distance
    float max_speed_ms = 6.0f;    // what the airframe can hold in a 5 m/s wind
    float geofence_m = 120.0f;    // lateral limit from the surveyed origin
    float rtl_alt_m = 35.0f;      // climb/cruise height on return
};

// Why a plan may not fly. A bitmask: an operator needs every reason at once, and a
// dialog that shows one reason per press is a dialog that gets closed without the
// last one being read.
enum ShowRefusal : uint32_t {
    SHOW_OK = 0,
    SHOW_BAD_COUNT = 1u << 0,       // zero or more aircraft than this build supports
    SHOW_BAD_ACTS = 1u << 1,        // no acts, or a transition/hold that is not a time
    SHOW_SEPARATION = 1u << 2,      // two paths come closer than separation_m
    SHOW_TOO_FAST = 1u << 3,        // a baked path needs more than max_speed_ms
    SHOW_OUTSIDE = 1u << 4,         // a point or a path leaves the geofence
    SHOW_ALTITUDE = 1u << 5,        // below the grid or above the ceiling for this site
    SHOW_BATTERY = 1u << 6,         // the show plus the return does not fit the pack
};

// --- shape sampling ------------------------------------------------------------
// Fills `out` with exactly `n` points for the shape, spread so the formation does
// not bunch up when the count changes. Deterministic and order-stable: the same
// (shape, n) always produces the same cloud, which is what makes an uploaded show
// reproducible between the rehearsal and the night.
inline int showSampleShape(ShowShape s, int n, float scale_m, float alt_m,
                           ShowPoint* out, int cap) {
    if (n <= 0 || out == nullptr || cap <= 0) return 0;
    const int count = n < cap ? n : cap;
    const float d = -alt_m;                       // NED: up is negative
    switch (s) {
        case SHAPE_RING: {
            // Concentric rings, with the aircraft shared out by circumference -- the same
            // rule as `showSampleShape()`'s ring case in `js/show/core.js`, and the two
            // have to agree to the printed digit because `.probe/cross.sh` diffs them.
            // Giving every ring the same number packed the inner circle 2.35x tighter than
            // the outer (2.81 m against 6.65 m at 49 aircraft on an 18 m ring), which is a
            // ring formation with no single pitch. Ring j carries weight j, so the arc gap
            // is the same on every ring; the ring count comes from the aircraft count so
            // the radial gap stays near that arc gap. One ring is already even and stays.
            int rings = 1;
            if (count > 28) {
                rings = static_cast<int>(std::lround(
                    (std::sqrt(1.0f + 4.0f * static_cast<float>(count) / SHOW_PI) - 1.0f) / 2.0f));
                if (rings < 2) rings = 2;
                if (rings > count) rings = count;
            }
            const long t = static_cast<long>(rings) * (rings + 1);
            int made = 0;
            int prev = 0;
            for (int j = 1; j <= rings; ++j) {
                const int upto = static_cast<int>(static_cast<long>(count) * j * (j + 1) / t);
                const int take = upto - prev;
                const float radius = scale_m * static_cast<float>(j) / static_cast<float>(rings);
                for (int i = 0; i < take; ++i) {
                    const float a = 2.0f * SHOW_PI * static_cast<float>(i) / static_cast<float>(take);
                    out[made].n = radius * std::sin(a);
                    out[made].e = radius * std::cos(a);
                    out[made].d = d;
                    ++made;
                }
                prev = upto;
            }
            return made;
        }
        case SHAPE_GRID: {
            int side = static_cast<int>(std::sqrt(static_cast<double>(count)) + 0.9999);
            if (side < 1) side = 1;
            for (int i = 0; i < count; ++i) {
                const int row = i / side;
                const int col = i % side;
                const float span = static_cast<float>(side - 1);
                out[i].n = span > 0.0f ? (-scale_m + 2.0f * scale_m * col / span) : 0.0f;
                out[i].e = span > 0.0f ? (-scale_m + 2.0f * scale_m * row / span) : 0.0f;
                out[i].d = d;
            }
            return count;
        }
        case SHAPE_HEART: {
            // Sampled by equal **arc length**, not equal parameter. The parametric
            // heart's speed collapses at the bottom tip and at the dimple on top, so
            // equal-parameter steps packed two aircraft 0.59 m apart inside a 2 m rule
            // (24 aircraft, 18 m shape) -- the show was refused and the reason looked
            // like a transition problem when it was the outline itself. Walking the
            // curve by length gives the same show 3.52 m.
            //
            // The 720-step table and the order of the arithmetic are the same on the
            // ground-station side (js/show/core.js), because the cross-check compares
            // the two implementations to the centimetre; do not "optimise" one side.
            constexpr int STEPS = 720;
            auto at = [](float t, float& x, float& y) {
                const float s = std::sin(t);
                x = 16.0f * s * s * s;
                y = 13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t)
                        - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t);
            };
            // Pass one: how long the outline is. Pass two: emit `count` points spaced
            // that many metres apart along it. Two passes over 720 cheap evaluations,
            // once per act at plan load -- no table on the stack, which matters on a
            // 4 KB task.
            float total = 0.0f;
            float px = 0.0f, py = 0.0f, cx = 0.0f, cy = 0.0f;
            at(0.0f, px, py);
            for (int i = 1; i <= STEPS; ++i) {
                at(2.0f * SHOW_PI * static_cast<float>(i) / static_cast<float>(STEPS), cx, cy);
                const float dx = cx - px, dy = cy - py;
                total += std::sqrt(dx * dx + dy * dy);
                px = cx;
                py = cy;
            }
            const float step = total / static_cast<float>(count);
            float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f, walked = 0.0f;
            at(0.0f, ax, ay);
            int seg = 0;
            for (int i = 0; i < count; ++i) {
                const float want = static_cast<float>(i) * step;
                for (;;) {
                    at(2.0f * SHOW_PI * static_cast<float>(seg + 1) / static_cast<float>(STEPS), bx, by);
                    const float dx = bx - ax, dy = by - ay;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (walked + len > want || seg + 1 >= STEPS) break;
                    walked += len;
                    ax = bx;
                    ay = by;
                    ++seg;
                }
                const float dx = bx - ax, dy = by - ay;
                const float len = std::sqrt(dx * dx + dy * dy);
                const float f = len > 0.0f ? (want - walked) / len : 0.0f;
                const float x = ax + dx * f, y = ay + dy * f;
                // Divided by 17, not 16: this parametrisation reaches -17 below the
                // origin and +12 above it, so the naive normalisation put a 19 m
                // point on an 18 m shape and the geofence check caught it.
                out[i].e = x * (scale_m / 17.0f);
                out[i].n = y * (scale_m / 17.0f);
                out[i].d = d;
            }
            return count;
        }
        case SHAPE_WAVE: {
            for (int i = 0; i < count; ++i) {
                const float u = count > 1 ? static_cast<float>(i) / (count - 1) : 0.5f;
                out[i].e = -scale_m + 2.0f * scale_m * u;
                out[i].n = 0.0f;
                // A wave only reads as one from the side, so the ripple is put in the
                // vertical axis rather than the horizontal one -- and the amplitude is
                // a fraction of the shape, not of the field: 0.35 put 10 m of drop
                // between neighbours 2.6 m apart, which is a vertical wall and fails
                // the separation rule on its own formation.
                out[i].d = d - 0.06f * scale_m * std::sin(6.0f * SHOW_PI * u);
            }
            return count;
        }
        case SHAPE_COLUMN: {
            const int cols = count / 4 > 1 ? count / 4 : 1;
            int made = 0;
            for (int c = 0; c < cols && made < count; ++c) {
                const float u = cols > 1 ? static_cast<float>(c) / (cols - 1) : 0.5f;
                const int per = (made == 0) ? count / cols : count / cols;
                for (int i = 0; i < per && made < count; ++i) {
                    const float v = per > 1 ? static_cast<float>(i) / (per - 1) : 0.5f;
                    out[made].e = -scale_m + 2.0f * scale_m * u;
                    out[made].n = 0.0f;
                    out[made].d = d + 0.45f * scale_m * (2.0f * v - 1.0f);
                    ++made;
                }
            }
            return made;
        }
        case SHAPE_ARC: {
            for (int i = 0; i < count; ++i) {
                const float u = count > 1 ? static_cast<float>(i) / (count - 1) : 0.5f;
                const float a = SHOW_PI * (0.15f + 0.7f * u);
                out[i].e = scale_m * std::cos(a);
                out[i].n = scale_m * std::sin(a);
                out[i].d = d;
            }
            return count;
        }
        default:
            return 0;
    }
}

// --- assignment ----------------------------------------------------------------
// Greedy nearest-neighbour matching from the previous formation to the next, in
// station order. Not optimal -- the Hungarian assignment is, and is what the real
// tools use -- but it is stable, runs in n^2 with no allocation, and removes the
// failure that actually matters: aircraft swapping sides of the field through each
// other because point 7 of shape A and point 7 of shape B are 90 m apart.
// `order[i]` is the index into `to` that station `i` should fly to.
inline void showAssign(const ShowPoint* from, const ShowPoint* to, int n, int* order) {
    if (from == nullptr || to == nullptr || order == nullptr || n <= 0) return;
    bool taken[SHOW_MAX_DRONES] = {};
    for (int i = 0; i < n; ++i) {
        float best = 1e30f;
        int at = -1;
        for (int j = 0; j < n; ++j) {
            if (taken[j]) continue;
            const float dn = to[j].n - from[i].n;
            const float de = to[j].e - from[i].e;
            const float dd = to[j].d - from[i].d;
            const float d2 = dn * dn + de * de + dd * dd;
            if (d2 < best) { best = d2; at = j; }
        }
        if (at < 0) at = i;                       // nothing left; stay put
        order[i] = at;
        taken[at] = true;
    }
}

// --- the baked path -------------------------------------------------------------
// Position along one transition at progress `u` in [0,1]. Smoothstep, because the
// aircraft has to leave and arrive at zero horizontal speed: a linear ramp would
// start the move at full speed and the first second of every transition would be a
// lurch that the position controller spends the rest of it recovering from.
inline ShowPoint showPath(const ShowPoint& a, const ShowPoint& b, float u) {
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    const float s = u * u * (3.0f - 2.0f * u);
    ShowPoint p;
    p.n = a.n + (b.n - a.n) * s;
    p.e = a.e + (b.e - a.e) * s;
    p.d = a.d + (b.d - a.d) * s;
    return p;
}

// The altitude lane one station uses while it is moving. Greedy assignment stops
// aircraft crossing the field through each other's formations, but two stations can
// still meet head-on in the middle of a transition -- measured at 0.03 m apart for a
// 24-aircraft ring-to-grid. Real shows resolve this by flying the transitions in
// stacked layers: each aircraft bulges up or down by its lane for the length of the
// move and returns to the formation height as it arrives, so `showPath` is unchanged
// at both ends.
inline float showLane(const ShowPoint& dest, float separation_m) {
    constexpr int lanes = 4;
    // A checkerboard over the destination, in cells the size of the separation rule:
    // aircraft headed for nearby points land in different lanes, which is the whole
    // point. Indexing by station number instead -- the first version -- left stations
    // 16 and 18 (adjacent cells, same-ish lane) meeting 0.8 m apart on a wave crest,
    // because a station number says nothing about where an aircraft is going.
    const int cx = static_cast<int>(std::floor(dest.n / separation_m));
    const int cz = static_cast<int>(std::floor(dest.e / separation_m));
    const int k = ((cx + cz) % lanes + lanes) % lanes;
    return (static_cast<float>(k) - (lanes - 1) * 0.5f) * separation_m * 2.0f;
}

// A transition with its lane applied: the same endpoints, a bulge in between.
inline ShowPoint showPathLane(const ShowPoint& a, const ShowPoint& b, float u, float lane_m) {
    ShowPoint p = showPath(a, b, u);
    p.d -= lane_m * std::sin(SHOW_PI * u);       // NED: up is the negative direction
    return p;
}

// Where station `i` returns to. Spread over a grid around the origin on purpose:
// with one shared RTL point, a hundred-aircraft failsafe puts a hundred airframes
// into the same column of air at the same time.
inline ShowPoint showReturnSlot(const ShowPlan& plan, int i) {
    const int side = static_cast<int>(std::sqrt(static_cast<double>(plan.drones > 0 ? plan.drones : 1)) + 0.9999);
    const int row = i / side;
    const int col = i % side;
    const float step = 8.0f;                      // wider than separation_m
    ShowPoint p;
    p.n = (static_cast<float>(col) - (side - 1) * 0.5f) * step;
    p.e = (static_cast<float>(row) - (side - 1) * 0.5f) * step;
    p.d = -plan.rtl_alt_m;
    return p;
}

// Total show time, transitions included: the number the battery check is against.
inline float showDuration(const ShowPlan& plan) {
    float t = 0.0f;
    for (int i = 0; i < plan.act_count && i < SHOW_MAX_ACTS; ++i) {
        t += plan.acts[i].move_s + plan.acts[i].hold_s;
    }
    return t;
}

// --- deconfliction ------------------------------------------------------------
// Greedy assignment plus altitude lanes still leaves a pair of stations meeting
// head-on inside a transition: measured, stations 16 and 18 passed 0.03 m apart on a
// 24-aircraft wave. Real show compilers resolve that with a repair pass -- swap the
// two aircraft's destinations and keep the swap if the show gets less tight. The
// formations are unchanged by a swap (each cloud is a permutation of the same set of
// points), so the only thing that moves is who flies where.
constexpr int SHOW_SAMPLES = 32;

// Clearance between two stations across one transition, sampled.
inline float showPairClearance(const ShowPoint& aFrom, const ShowPoint& aTo,
                               const ShowPoint& bFrom, const ShowPoint& bTo,
                               float aLane, float bLane) {
    float best = 1e30f;
    for (int s = 0; s < SHOW_SAMPLES; ++s) {
        const float u = static_cast<float>(s) / (SHOW_SAMPLES - 1);
        const ShowPoint pa = showPathLane(aFrom, aTo, u, aLane);
        const ShowPoint pb = showPathLane(bFrom, bTo, u, bLane);
        const float dn = pa.n - pb.n, de = pa.e - pb.e, dd = pa.d - pb.d;
        const float d = std::sqrt(dn * dn + de * de + dd * dd);
        if (d < best) best = d;
    }
    return best;
}

// The clearance of the tightest other aircraft against one station, over every
// transition. Recomputing only this -- instead of the whole show's worst pair -- is
// what keeps the repair pass usable at 128 aircraft.
inline float showStationWorst(const ShowPoint* pos, int actCount, int n, int k, float separation) {
    float worst = 1e30f;
    for (int i = 0; i + 1 < actCount; ++i) {
        for (int o = 0; o < n; ++o) {
            if (o == k) continue;
            const float d = showPairClearance(pos[i * n + k], pos[(i + 1) * n + k],
                                              pos[i * n + o], pos[(i + 1) * n + o],
                                              showLane(pos[(i + 1) * n + k], separation),
                                              showLane(pos[(i + 1) * n + o], separation));
            if (d < worst) worst = d;
        }
    }
    return worst;
}

// The show's single tightest pair.
//
// The pair is named with a one-millimetre dead band: two candidates that differ by
// less than that are the same problem, and the rule picks the one that comes first in
// station order so that this file and the JavaScript mirror name the *same* pair. A
// bare `d < worst` let float rounding (ulp 3.8e-6 at 40 m) pick a different winner than
// the double arithmetic does, and the repair pass then made different swaps on the two
// sides. The reported clearance is still the true minimum.
inline float showWorstPair(const ShowPoint* pos, int actCount, int n, float separation,
                           int& atAct, int& outA, int& outB) {
    constexpr float TIE_BAND_M = 0.001f;
    float worst = 1e30f;
    atAct = -1; outA = -1; outB = -1;
    for (int i = 0; i + 1 < actCount; ++i) {
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float d = showPairClearance(pos[i * n + a], pos[(i + 1) * n + a],
                                                  pos[i * n + b], pos[(i + 1) * n + b],
                                                  showLane(pos[(i + 1) * n + a], separation),
                                                  showLane(pos[(i + 1) * n + b], separation));
                if (d < worst - TIE_BAND_M) {
                    worst = d; atAct = i; outA = a; outB = b;
                } else if (d < worst) {
                    worst = d;
                }
            }
        }
    }
    return worst;
}

// Repair the compiled show in place; returns how many swaps it took. Stops when the
// show is clear, when a pass cannot improve it, or after `maxPasses`.
inline int showDeconflict(ShowPoint* pos, int actCount, int n, float separation, int maxPasses) {
    // No swap can get under the spacing inside a formation itself: an act is a
    // permutation of the same cloud whoever flies it, so its tightest pair is a
    // property of the shape and the count, not of the routing. When that floor is
    // already under the rule the show is going to be refused for density whatever the
    // repair does -- measured on a 100-aircraft plan that is simply too dense, running
    // the pass anyway burned the whole budget (400 passes, ~0.8 s on the ground station,
    // on every keystroke) and refused all the same.
    float staticWorst = 1e30f;
    for (int i = 0; i < actCount; ++i) {
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float dn = pos[i * n + a].n - pos[i * n + b].n;
                const float de = pos[i * n + a].e - pos[i * n + b].e;
                const float dd = pos[i * n + a].d - pos[i * n + b].d;
                const float d = std::sqrt(dn * dn + de * de + dd * dd);
                if (d < staticWorst) staticWorst = d;
            }
        }
    }
    if (staticWorst < separation) return 0;

    int swaps = 0;
    for (int pass = 0; pass < maxPasses; ++pass) {
        int i = -1, a = -1, b = -1;
        const float worst = showWorstPair(pos, actCount, n, separation, i, a, b);
        if (i < 0 || worst >= separation) break;

        const float beforeB = showStationWorst(pos, actCount, n, b, separation);
        bool repaired = false;
        // Try handing b's destination to someone else. a is tried first (the direct
        // swap), then every other station, and a swap is kept only if the tightest
        // thing about *both* aircraft involved gets better -- otherwise the repair
        // just moves the collision somewhere else and the next pass undoes it.
        for (int c = 0; c < n && !repaired; ++c) {
            if (c == b) continue;
            const float beforeC = showStationWorst(pos, actCount, n, c, separation);
            const ShowPoint tmp = pos[(i + 1) * n + b];
            pos[(i + 1) * n + b] = pos[(i + 1) * n + c];
            pos[(i + 1) * n + c] = tmp;
            const float afterB = showStationWorst(pos, actCount, n, b, separation);
            const float afterC = showStationWorst(pos, actCount, n, c, separation);
            if (afterB > worst + 0.001f && afterC > worst + 0.001f
                && afterB > beforeB - 0.001f && afterC >= beforeC - 0.001f) {
                ++swaps;
                repaired = true;
                break;
            }
            pos[(i + 1) * n + c] = pos[(i + 1) * n + b];
            pos[(i + 1) * n + b] = tmp;
        }
        if (!repaired) break;
    }
    return swaps;
}

// Sample, assign, repair: the compiled station-ordered positions of every act, laid
// out act-major in `pos` (which must hold actCount * drones points), plus the number
// of swaps the repair pass needed. This is the function both the go/no-go decision
// and the aircraft read from, so a show is judged on exactly the paths it will fly.
inline int showCompilePlan(const ShowPlan& plan, ShowPoint* pos, int cap, int* swaps) {
    const int n = plan.drones;
    if (n <= 0 || n > SHOW_MAX_DRONES || plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) return -1;
    if (cap < plan.act_count * n) return -1;
    ShowPoint cloud[SHOW_MAX_DRONES];
    for (int i = 0; i < plan.act_count; ++i) {
        const ShowAct& a = plan.acts[i];
        if (showSampleShape(static_cast<ShowShape>(a.shape), n, a.scale_m, a.alt_m, cloud, SHOW_MAX_DRONES) != n)
            return -1;
        for (int k = 0; k < n; ++k) pos[i * n + k] = cloud[k];
        if (i > 0) {
            int order[SHOW_MAX_DRONES];
            showAssign(pos + (i - 1) * n, pos + i * n, n, order);
            ShowPoint held[SHOW_MAX_DRONES];
            for (int k = 0; k < n; ++k) held[k] = pos[i * n + order[k]];
            for (int k = 0; k < n; ++k) pos[i * n + k] = held[k];
        }
    }
    const int made = showDeconflict(pos, plan.act_count, n, plan.separation_m, 4 * n);
    if (swaps) *swaps = made;
    return plan.act_count;
}

// Everything that has to be true before an upload is worth pressing.
inline uint32_t showValidate(const ShowPlan& plan, float batt_pct, float wind_ms) {
    uint32_t why = SHOW_OK;
    if (plan.drones <= 0 || plan.drones > SHOW_MAX_DRONES) why |= SHOW_BAD_COUNT;
    if (plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) why |= SHOW_BAD_ACTS;
    for (int i = 0; i < plan.act_count; ++i) {
        const ShowAct& a = plan.acts[i];
        if (a.hold_s <= 0.0f || a.move_s <= 0.0f || a.scale_m <= 0.0f) why |= SHOW_BAD_ACTS;
        if (a.alt_m < 10.0f || a.alt_m > 120.0f) why |= SHOW_ALTITUDE;
    }
    if (why != SHOW_OK) return why;

    // Sample, assign, repair -- all of it inside showCompilePlan, so the verdict and
    // the aircraft read the same paths. The buffer is 128 points per act and lives on
    // the caller's stack: this runs on the ground station, and an aircraft is only
    // ever told which point is its own.
    //
    // Not static, and that is load-bearing: with these arrays static, a shape that
    // forgot to write one field inherited the previous plan's value and the altitude
    // check passed on a wave sitting at ground level.
    ShowPoint pos[SHOW_MAX_ACTS * SHOW_MAX_DRONES];
    int swaps = 0;
    if (showCompilePlan(plan, pos, SHOW_MAX_ACTS * SHOW_MAX_DRONES, &swaps) < 0) {
        why |= SHOW_BAD_COUNT;
        return why;
    }
    const int n = plan.drones;

    // The geofence and the altitude ceiling are checked on the paths, not just the
    // endpoints: a straight line between two points that are both inside can still
    // bulge outside, and a lane bulge can lift an aircraft over the ceiling.
    for (int i = 0; i < plan.act_count; ++i) {
        for (int sm = 0; sm < SHOW_SAMPLES; ++sm) {
            for (int k = 0; k < n; ++k) {
                ShowPoint q = pos[i * n + k];
                if (i + 1 < plan.act_count) {
                    q = showPathLane(pos[i * n + k], pos[(i + 1) * n + k],
                                     static_cast<float>(sm) / (SHOW_SAMPLES - 1),
                                     showLane(pos[(i + 1) * n + k], plan.separation_m));
                }
                if (std::sqrt(q.n * q.n + q.e * q.e) > plan.geofence_m) why |= SHOW_OUTSIDE;
                if (-q.d < 5.0f || -q.d > 120.0f) why |= SHOW_ALTITUDE;
            }
        }
    }

    // Speed: the distance a station covers during its transition divided by the
    // transition time, at the steepest part of the smoothstep (1.5x the average, the
    // derivative of u^2(3-2u) at u = 0.5). Wind is added rather than multiplied: the
    // airframe has to hold a ground point, so the worst-case airspeed is the path
    // speed plus the wind.
    for (int i = 0; i + 1 < plan.act_count; ++i) {
        const float dt = plan.acts[i + 1].move_s;
        for (int k = 0; k < n; ++k) {
            const float dn = pos[(i + 1) * n + k].n - pos[i * n + k].n;
            const float de = pos[(i + 1) * n + k].e - pos[i * n + k].e;
            const float dd = pos[(i + 1) * n + k].d - pos[i * n + k].d;
            const float v = 1.5f * std::sqrt(dn * dn + de * de + dd * dd) / dt;
            if (v + wind_ms > plan.max_speed_ms) why |= SHOW_TOO_FAST;
        }
    }

    // Separation inside each formation: a shape that is too dense for the count is
    // unsafe while it is simply sitting there, and no swap can fix that -- the cloud
    // is the same set of points whoever flies it.
    for (int i = 0; i < plan.act_count; ++i) {
        for (int a = 0; a < n; ++a) {
            for (int b = a + 1; b < n; ++b) {
                const float dn = pos[i * n + a].n - pos[i * n + b].n;
                const float de = pos[i * n + a].e - pos[i * n + b].e;
                const float dd = pos[i * n + a].d - pos[i * n + b].d;
                if (std::sqrt(dn * dn + de * de + dd * dd) < plan.separation_m) why |= SHOW_SEPARATION;
            }
        }
    }

    // Separation on the repaired paths. This is the number the editor shows next to
    // the verdict, because "it passes" is less useful than "it passes with 2.4 m to
    // spare on a 2 m rule".
    int wi = 0, wa = 0, wb = 0;
    const float tightest = showWorstPair(pos, plan.act_count, n, plan.separation_m, wi, wa, wb);
    if (tightest < plan.separation_m) why |= SHOW_SEPARATION;

    // Endurance: the show, plus the return at cruise speed, plus a two-minute hold
    // on the pad for the crew. Packs are sized in minutes of hover; anything that
    // lands with less than that reserve is a show that ends in a swim.
    const float need_s = showDuration(plan) + 90.0f + 120.0f;
    constexpr float PACK_HOVER_S = 22.0f * 60.0f;      // the pack on this airframe
    if (batt_pct * PACK_HOVER_S < need_s * 100.0f) why |= SHOW_BATTERY;
    return why;
}

}  // namespace ranch

/* ==================== src/rtk.h ==================== */

// The RTK receiver: a UBX stream parser for the one message that matters, and the
// geodetic-to-local conversion that turns it into the metres show_core bakes its
// paths in.
//
// Position priority, as the show needs it: an RTK **fixed** solution is the one a
// formation is flown on; a **single** solution is good enough to keep flying and
// hold a point, but not good enough to be silent about, so it is reported; no
// solution at all is the trigger for the aircraft to stop advancing the programme
// and go to its own return slot. That ranking lives here, in one function, because
// three separate places guessing at "is the GPS okay" is how a show ends with some
// aircraft having decided differently.

#include <cstddef>
#include <cstdint>

namespace ranch {

// The three states the show cares about, ordered by how much of the programme they
// let the aircraft fly.
enum PositionSource : uint8_t {
    POS_NONE = 0,       // nothing locked: the aircraft stops and goes home
    POS_SINGLE = 1,     // a 3D fix without correction data: fly, and say so
    POS_RTK = 2,        // carrier-phase fixed: this is what the formation needs
};

struct RtkState {
    bool fresh = false;         // a NAV-PVT has arrived within SHOW_RTK_STALE_MS
    uint8_t fix_type = 0;       // the receiver's own: 0 none, 2 2D, 3 3D, 4 GNSS+DR, 5 time
    uint8_t carr_soln = 0;      // 0 none, 1 float, 2 fixed
    bool diff_soln = false;     // correction data was in use
    uint8_t sats = 0;
    uint16_t hacc_cm = 0xFFFF;  // the receiver's own horizontal accuracy claim
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    int32_t alt_mm = 0;         // height above the ellipsoid
    float n = 0.0f, e = 0.0f, d = 0.0f;   // local NED metres from the surveyed origin
    float vn = 0.0f, ve = 0.0f, vd = 0.0f;
    uint32_t age_ms = 0;
    uint32_t frames = 0;
    uint32_t crc_errors = 0;    // UBX frames whose 8-bit Fletcher failed
    uint32_t rejected = 0;      // frames that parsed and were the wrong message
};

void rtkInit();

// Feed one byte. Returns true when a complete NAV-PVT has just been taken in,
// which is also when the local position is recomputed. Every other byte either
// moves the state machine or is discarded with a count.
bool rtkFeed(uint8_t b);

// Age the reading. Called with the current monotonic clock on every loop.
void rtkUpdate(uint32_t now_ms);

const RtkState& rtkState();

// The surveyed launch point. Everything show_core calls "the origin" is this, and
// without it there is no local frame to fly the programme in, so no upload can be
// flown and the aircraft says why.
bool rtkOriginSet();
void rtkSetOrigin(int32_t lat_e7, int32_t lon_e7, int32_t alt_mm);
bool rtkOrigin(int32_t& lat_e7, int32_t& lon_e7, int32_t& alt_mm);

// The one ranking the rest of the firmware is allowed to ask about.
PositionSource rtkSource();

// Centimetres, as claimed by the receiver, or a number big enough to fail any
// tolerance when the reading is not fresh.
uint16_t rtkHaccCm();

// UBX configuration: which message at which rate. Emitted at boot and every frame
// the receiver answers is evidence it took it.
size_t rtkCfgNavPvt(uint8_t* buf, size_t cap, uint8_t rate_hz);
size_t rtkCfgNavRate(uint8_t* buf, size_t cap, uint16_t measure_period_ms, uint16_t nav_rate);

}  // namespace ranch

/* ==================== src/rtk.cpp ==================== */


#include <cmath>
#include <cstring>

namespace ranch {
namespace {

// UBX sync and the one message this firmware reads.
constexpr uint8_t UBX_SYNC1 = 0xB5;
constexpr uint8_t UBX_SYNC2 = 0x62;
constexpr uint8_t UBX_CLASS_NAV = 0x01;
constexpr uint8_t UBX_ID_PVT = 0x07;
constexpr uint8_t UBX_CLASS_CFG = 0x06;
constexpr uint8_t UBX_ID_CFG_MSG = 0x01;
constexpr uint8_t UBX_ID_CFG_RATE = 0x08;
// NAV-PVT is 92 bytes on an M9N. Only the first 64 are read, and a frame shorter
// than that is refused rather than guessed at: the fields past it would decode as
// zero, which is a fix at the Gulf of Guinea.
constexpr uint16_t PVT_MIN_LEN = 64;

// NAV-PVT payload offsets, from the u-blox interface specification.
enum PvtField : size_t {
    PVT_ITOW = 0,       // u32 ms within the GNSS week
    PVT_YEAR = 4,       // u16, then month day hour min sec at 6..10
    PVT_VALID = 11,     // u8 bit field
    PVT_TIMEACC = 12,   // u32 ns
    PVT_NANO = 16,      // i32 ns
    PVT_FIXTYPE = 20,   // u8
    PVT_FLAGS = 21,     // u8 bit0 gnssFixOk, bit1 diffSoln
    PVT_FLAGS2 = 22,    // u8
    PVT_NUMSV = 23,     // u8
    PVT_LON = 24,       // i32 1e-7 deg
    PVT_LAT = 28,       // i32 1e-7 deg
    PVT_HEIGHT = 32,    // i32 mm above the ellipsoid
    PVT_HMSL = 36,      // i32 mm above mean sea level
    PVT_HACC = 40,      // u32 mm
    PVT_VACC = 44,      // u32 mm
    PVT_VELN = 48,      // i32 mm/s
    PVT_VELE = 52,
    PVT_VELD = 56,
    PVT_GSPEED = 60,
};

constexpr uint8_t PVT_FLAG_FIXOK = 1u << 0;
constexpr uint8_t PVT_FLAG_DIFF = 1u << 1;

// The carrier-phase solution range, which is the difference between an RTK fix and
// a very confident single point. u-blox moved it between two registers when the
// protocol went past 27.00 -- 23.01 parts put carrSoln in flags bits 6..7, later
// parts (the M8N, the M9N and the F9 family) put the same two bits in flags2 -- and
// a receiver that is read out of the wrong one reports "no RTK" forever while the
// LED on the module says fixed. So both are read, and either one is believed.
uint8_t carrSolnOf(uint8_t flags, uint8_t flags2) {
    const uint8_t modern = static_cast<uint8_t>((flags2 >> 6) & 0x03u);
    if (modern) return modern;
    return static_cast<uint8_t>((flags >> 6) & 0x03u);
}

RtkState gnss;
uint32_t last_pvt_ms = 0;

int32_t origin_lat_e7 = 0;
int32_t origin_lon_e7 = 0;
int32_t origin_height_mm = 0;
bool origin_set = false;

// The equirectangular step, in double: one unit of the 1e-7 degree integers is
// about 1.1 cm, and a float cannot hold 30.5e7 and a centimetre at the same time.
constexpr double M_PER_DEG_LAT = 111320.0;

double metresPerDegLon() {
    const double lat0 = static_cast<double>(origin_lat_e7) * 1e-7;
    return M_PER_DEG_LAT * std::cos(lat0 * 0.0174532925199433);
}

// --- the byte-level parser --------------------------------------------------
enum class UState : uint8_t { Sync1, Sync2, Class, Id, Len1, Len2, Payload, CkA, CkB };
UState u_st = UState::Sync1;
uint8_t u_class = 0, u_id = 0, u_cka = 0, u_ckb = 0;
uint16_t u_len = 0, u_have = 0;
uint8_t u_buf[128];

// The byte order here is the receiver's: little-endian, unaligned.
uint32_t leU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
int32_t leI32(const uint8_t* p) { return static_cast<int32_t>(leU32(p)); }
// UBX carries a Fletcher-8 checksum, not a CRC: 8-bit accumulators mod 256 over
// class, id, length and payload.
void ckAdd(uint8_t b) {
    u_cka = static_cast<uint8_t>(u_cka + b);
    u_ckb = static_cast<uint8_t>(u_ckb + u_cka);
}

void takePvt() {
    if (u_len < PVT_MIN_LEN) { gnss.rejected++; return; }
    const uint8_t flags = u_buf[PVT_FLAGS];
    gnss.fix_type = u_buf[PVT_FIXTYPE];
    gnss.diff_soln = (flags & PVT_FLAG_DIFF) != 0u;
    gnss.carr_soln = carrSolnOf(flags, u_buf[PVT_FLAGS2]);
    gnss.sats = u_buf[PVT_NUMSV];
    // Millimetres to centimetres, capped below the 0xFFFF that means "no reading"
    // so a very bad fix cannot be mistaken for an absent one.
    const uint32_t hacc_cm = leU32(u_buf + PVT_HACC) / 10u;
    gnss.hacc_cm = hacc_cm > 65534u ? 65534u : static_cast<uint16_t>(hacc_cm);
    gnss.lat_e7 = leI32(u_buf + PVT_LAT);
    gnss.lon_e7 = leI32(u_buf + PVT_LON);
    gnss.alt_mm = leI32(u_buf + PVT_HEIGHT);
    gnss.vn = static_cast<float>(leI32(u_buf + PVT_VELN)) / 1000.0f;
    gnss.ve = static_cast<float>(leI32(u_buf + PVT_VELE)) / 1000.0f;
    gnss.vd = static_cast<float>(leI32(u_buf + PVT_VELD)) / 1000.0f;

    // gnssFixOk is the receiver's own "this is a position" flag. Reading fixType
    // alone is not enough: a 3D-typed fix with the flag clear is the receiver
    // repeating itself after the satellites went away.
    gnss.fresh = (flags & PVT_FLAG_FIXOK) != 0u && gnss.fix_type >= 2;
    if (!gnss.fresh) {
        // Keep the last coordinates for the log but do not let anyone fly on them:
        // the local position is only recomputed while the reading is good.
        gnss.frames++;
        last_pvt_ms = 0;                 // rtkUpdate() will age it out immediately
        return;
    }
    if (origin_set) {
        gnss.n = static_cast<float>((static_cast<double>(gnss.lat_e7 - origin_lat_e7) * 1e-7) * M_PER_DEG_LAT);
        gnss.e = static_cast<float>((static_cast<double>(gnss.lon_e7 - origin_lon_e7) * 1e-7) * metresPerDegLon());
        gnss.d = -static_cast<float>(static_cast<double>(gnss.alt_mm - origin_height_mm) / 1000.0);
    }
    last_pvt_ms = halMillis();
    gnss.frames++;
}

}  // namespace

void rtkInit() {
    gnss = RtkState{};
    u_st = UState::Sync1;
    u_have = 0;
    u_len = 0;
    last_pvt_ms = 0;
}

bool rtkFeed(uint8_t b) {
    switch (u_st) {
        case UState::Sync1:
            if (b == UBX_SYNC1) u_st = UState::Sync2;
            return false;
        case UState::Sync2:
            u_st = (b == UBX_SYNC2) ? UState::Class : UState::Sync1;
            return false;
        case UState::Class:
            u_class = b;
            u_cka = b; u_ckb = b;
            u_st = UState::Id;
            return false;
        case UState::Id:
            u_id = b;
            ckAdd(b);
            u_st = UState::Len1;
            return false;
        case UState::Len1:
            u_len = b;
            ckAdd(b);
            u_st = UState::Len2;
            return false;
        case UState::Len2:
            u_len = static_cast<uint16_t>(u_len | (b << 8));
            ckAdd(b);
            u_have = 0;
            if (u_len > sizeof(u_buf)) { u_st = UState::Sync1; gnss.rejected++; return false; }
            u_st = u_len ? UState::Payload : UState::CkA;
            return false;
        case UState::Payload:
            u_buf[u_have] = b;
            ckAdd(b);
            if (++u_have >= u_len) u_st = UState::CkA;
            return false;
        case UState::CkA:
            u_st = (b == u_cka) ? UState::CkB : UState::Sync1;
            if (u_st == UState::Sync1) gnss.crc_errors++;
            return false;
        case UState::CkB: {
            u_st = UState::Sync1;
            if (b != u_ckb) { gnss.crc_errors++; return false; }
            if (u_class != UBX_CLASS_NAV || u_id != UBX_ID_PVT) { gnss.rejected++; return false; }
            takePvt();
            return true;
        }
    }
    u_st = UState::Sync1;
    return false;
}

void rtkUpdate(uint32_t now_ms) {
    gnss.age_ms = now_ms - last_pvt_ms;
    if (last_pvt_ms == 0) {
        gnss.fresh = false;
        gnss.age_ms = 0xFFFFFFFFu;
    } else if (gnss.age_ms > SHOW_RTK_STALE_MS) {
        gnss.fresh = false;
    }
}

const RtkState& rtkState() { return gnss; }

bool rtkOriginSet() { return origin_set; }

void rtkSetOrigin(int32_t lat_e7, int32_t lon_e7, int32_t alt_mm) {
    origin_lat_e7 = lat_e7;
    origin_lon_e7 = lon_e7;
    origin_height_mm = alt_mm;
    origin_set = lat_e7 != 0 || lon_e7 != 0;
}

bool rtkOrigin(int32_t& lat_e7, int32_t& lon_e7, int32_t& alt_mm) {
    if (!origin_set) return false;
    lat_e7 = origin_lat_e7;
    lon_e7 = origin_lon_e7;
    alt_mm = origin_height_mm;
    return true;
}

PositionSource rtkSource() {
    if (!gnss.fresh) return POS_NONE;
    // Fixed, or nothing. A float solution carries decimetres of error along the
    // baseline, which is the difference between two aircraft 2 m apart and two
    // aircraft 0.5 m apart, so it is reported as what it is and flown as a single.
    if (gnss.carr_soln == 2 && gnss.diff_soln && gnss.fix_type >= 2) return POS_RTK;
    if (gnss.fix_type >= 3) return POS_SINGLE;
    return POS_NONE;
}

uint16_t rtkHaccCm() { return gnss.fresh ? gnss.hacc_cm : 9999u; }

namespace {

// Wraps a payload in UBX framing with the checksum already appended.
size_t ubxFrame(uint8_t cls, uint8_t id, const uint8_t* payload, size_t n, uint8_t* out, size_t cap) {
    const size_t total = 8 + n;
    if (cap < total) return 0;
    size_t i = 0;
    out[i++] = UBX_SYNC1;
    out[i++] = UBX_SYNC2;
    out[i++] = cls;
    out[i++] = id;
    out[i++] = static_cast<uint8_t>(n & 0xFF);
    out[i++] = static_cast<uint8_t>(n >> 8);
    std::memcpy(out + i, payload, n);
    i += n;
    uint8_t a = 0, b = 0;
    for (size_t k = 2; k < 6 + n; ++k) {           // class .. payload
        a = static_cast<uint8_t>(a + out[k]);
        b = static_cast<uint8_t>(b + a);
    }
    out[i++] = a;
    out[i++] = b;
    return i;
}

}  // namespace

size_t rtkCfgNavPvt(uint8_t* buf, size_t cap, uint8_t rate_hz) {
    const uint8_t pl[3] = { UBX_CLASS_NAV, UBX_ID_PVT, rate_hz };
    return ubxFrame(UBX_CLASS_CFG, UBX_ID_CFG_MSG, pl, sizeof(pl), buf, cap);
}

size_t rtkCfgNavRate(uint8_t* buf, size_t cap, uint16_t meas_ms, uint16_t nav_rate) {
    uint8_t pl[6];
    pl[0] = static_cast<uint8_t>(meas_ms & 0xFF);
    pl[1] = static_cast<uint8_t>(meas_ms >> 8);
    pl[2] = static_cast<uint8_t>(nav_rate & 0xFF);
    pl[3] = static_cast<uint8_t>(nav_rate >> 8);
    pl[4] = 0;                                       // timeRef: UTC
    pl[5] = 0;
    return ubxFrame(UBX_CLASS_CFG, UBX_ID_CFG_RATE, pl, sizeof(pl), buf, cap);
}

}  // namespace ranch

/* ==================== src/plan.h ==================== */

// The programme as it crosses the radio, and the one lane in it that belongs to
// this airframe.
//
// The upload is a compact line, not a file: a show is small enough to read out
// over a console and it has to survive a technician typing it by hand.
//
//   SHOWPLAN,<drones>,<separation>,<max speed>,<geofence>,<rtl alt>,<act count>
//     ;A,<shape>,<scale>,<alt>,<hold s>,<move s>,<colour>     once per act
//
// The round trip is asserted in test_sandbox.cpp: format a plan, parse it back,
// and every number has to come out where it went in, including the colours, which
// are the one field that is not a number.
//
// The aircraft never stores the whole plan: planLane() runs show_core's sampling
// and assignment for all the stations and keeps only this station's point per act,
// which is the trajectory it was uploaded to fly. Everything else is the ground
// station's business.

#include <cstddef>

namespace ranch {

constexpr size_t SHOW_PLAN_MAX_TEXT = 1024;    // the longest upload this build takes
constexpr const char* SHOW_PLAN_TAG = "SHOWPLAN";

// Writes the plan and returns the number of bytes used, or 0 when the buffer was
// too small or the plan is not representable. Never truncates silently: either
// the whole plan is there or nothing is.
size_t planFormat(const ShowPlan& plan, char* buf, size_t cap);

// Parses in place with strtod, so a corrupt record cannot allocate and cannot
// walk off the end of the act array. Returns false on the first field that is
// not a number, on a missing act, and on an act count that does not match the
// records that follow it -- a half-parsed programme is a programme that flies
// somewhere nobody approved.
bool planParse(const char* text, ShowPlan& out);

// The one lane this airframe flies. `at[i]` is the point station `station` ends up
// with in act i, taken out of show_core's sample + assign + **repair**, so it is the
// same point the ground station certified -- not the point a re-derivation without the
// repair pass would have produced.
struct ShowLane {
    int station = 0;
    int act_count = 0;
    ShowPoint at[SHOW_MAX_ACTS] = {};
    uint32_t colour[SHOW_MAX_ACTS] = {};
    // Where the aircraft lifts off from: the surveyed origin, which is also
    // station 0's neighbourhood and, in practice, the middle of the pad.
    ShowPoint pad = {};
};

// False when the station is outside the fleet or the plan has no acts.
//
// `table` is the caller's workspace and has to hold `plan.act_count * plan.drones`
// points: the repair pass decides which destination belongs to which airframe by
// looking at every airframe's destination, so it cannot run out of one station's row.
// The caller owns it for exactly this reason -- the ground station can put it on a
// heap and the aircraft can put it in the one task that unpacks an upload, instead of
// both paying for it in .bss forever. It is not touched in flight.
bool planBuildLane(const ShowPlan& plan, int station, ShowLane& out, ShowPoint* table);

// The height layer this station bulges through while it moves between two
// formation points, straight from show_core.
float planLaneOffset(const ShowLane& lane, int act, float separation_m);

// The ack number for a record: a 16-bit rolling sum over its exact bytes. The ground
// station computes the same number in JavaScript (ranch/js/show/plan.js) and compares
// it against the `ck=` field of the aircraft's own telemetry frame, so "it has a
// two-act show" and "it has *this* show" are different claims and only the second one
// is checked. `.probe/cross.sh` prints both implementations' numbers for the same
// records.
uint16_t planChecksum(const char* text, size_t len);

}  // namespace ranch

/* ==================== src/plan.cpp ==================== */


#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ranch {
namespace {

// Reads one number out of the upload text, refusing a field that is not there.
// `p` is left pointing at the separator that stopped the scan.
bool readNum(const char*& p, double& out) {
    if (p == nullptr || *p == '\0') return false;
    char* end = nullptr;
    out = std::strtod(p, &end);
    if (end == p) return false;
    p = end;
    return true;
}

bool eat(const char*& p, char c) {
    if (p != nullptr && *p == c) { ++p; return true; }
    return false;
}

}  // namespace

size_t planFormat(const ShowPlan& plan, char* buf, size_t cap) {
    if (buf == nullptr || cap == 0) return 0;
    if (plan.drones <= 0 || plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) return 0;
    char tmp[SHOW_PLAN_MAX_TEXT];
    size_t n = static_cast<size_t>(std::snprintf(tmp, sizeof(tmp),
                 "%s,%d,%.2f,%.2f,%.2f,%.2f,%d", SHOW_PLAN_TAG, plan.drones,
                 static_cast<double>(plan.separation_m), static_cast<double>(plan.max_speed_ms),
                 static_cast<double>(plan.geofence_m), static_cast<double>(plan.rtl_alt_m),
                 plan.act_count));
    if (n >= sizeof(tmp)) return 0;
    for (int i = 0; i < plan.act_count; ++i) {
        const ShowAct& a = plan.acts[i];
        const size_t used = static_cast<size_t>(std::snprintf(
            tmp + n, sizeof(tmp) - n, ";A,%d,%.2f,%.2f,%.2f,%.2f,%06lx",
            static_cast<int>(a.shape), static_cast<double>(a.scale_m),
            static_cast<double>(a.alt_m), static_cast<double>(a.hold_s),
            static_cast<double>(a.move_s), static_cast<unsigned long>(a.colour)));
        if (used == 0 || used >= sizeof(tmp) - n) return 0;
        n += used;
    }
    if (n + 1 > cap) return 0;                 // never hand back half a programme
    std::memcpy(buf, tmp, n);
    buf[n] = '\0';
    return n;
}

bool planParse(const char* text, ShowPlan& out) {
    if (text == nullptr) return false;
    const char* p = text;
    const size_t tag_len = std::strlen(SHOW_PLAN_TAG);
    if (std::strncmp(p, SHOW_PLAN_TAG, tag_len) != 0) return false;
    p += tag_len;
    if (!eat(p, ',')) return false;

    double v = 0.0;
    if (!readNum(p, v) || v <= 0.0 || v > SHOW_MAX_DRONES) return false;
    out.drones = static_cast<int>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.separation_m = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.max_speed_ms = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.geofence_m = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v)) return false;
    out.rtl_alt_m = static_cast<float>(v);
    if (!eat(p, ',')) return false;
    if (!readNum(p, v) || v <= 0.0 || v > SHOW_MAX_ACTS) return false;
    const int acts = static_cast<int>(v);

    ShowPlan parsed{};
    parsed.drones = out.drones;
    parsed.separation_m = out.separation_m;
    parsed.max_speed_ms = out.max_speed_ms;
    parsed.geofence_m = out.geofence_m;
    parsed.rtl_alt_m = out.rtl_alt_m;
    parsed.act_count = 0;
    for (int i = 0; i < acts; ++i) {
        if (!eat(p, ';')) return false;         // a missing act record
        if (!eat(p, 'A')) return false;         // a record type this build does not know
        if (!eat(p, ',')) return false;
        double s = 0.0, scale = 0.0, alt = 0.0, hold = 0.0, move = 0.0, colour = -1.0;
        if (!readNum(p, s)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, scale)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, alt)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, hold)) return false;
        if (!eat(p, ',')) return false;
        if (!readNum(p, move)) return false;
        if (!eat(p, ',')) return false;
        // The colour is six hex digits, which strtod would happily read "00e0a0"
        // as zero times ten to the something, so it gets its own base.
        {
            char* end = nullptr;
            colour = static_cast<double>(std::strtoul(p, &end, 16));
            if (end == p) return false;
            p = end;
        }
        if (s < 0.0 || s >= SHOW_SHAPE_COUNT) return false;
        ShowAct a{};
        a.shape = static_cast<uint8_t>(s);
        a.scale_m = static_cast<float>(scale);
        a.alt_m = static_cast<float>(alt);
        a.hold_s = static_cast<float>(hold);
        a.move_s = static_cast<float>(move);
        a.colour = static_cast<uint32_t>(colour) & 0xFFFFFFu;
        parsed.acts[parsed.act_count++] = a;
    }
    // Trailing blanks are a fact of radio life; anything else means the record was
    // cut short, and an act that did not arrive is an aircraft that flies the
    // wrong shape and never learns it did.
    while (*p == ' ' || *p == '\r' || *p == '\n') ++p;
    if (*p != '\0') return false;
    if (parsed.act_count != acts) return false;
    out = parsed;
    return true;
}

bool planBuildLane(const ShowPlan& plan, int station, ShowLane& out, ShowPoint* table) {
    if (plan.drones <= 0 || plan.drones > SHOW_MAX_DRONES) return false;
    if (plan.act_count <= 0 || plan.act_count > SHOW_MAX_ACTS) return false;
    if (station < 0 || station >= plan.drones) return false;
    if (table == nullptr) return false;

    // The whole compiler, not a re-derivation of part of it. Sampling and greedy
    // assignment alone reproduce the ground station's routing; the repair pass that
    // follows them is what makes the routing *certified*, and it decides per pair, so
    // an aircraft that ran the first two steps and skipped the third would fly a
    // meeting the show was cleared for. Measured on a 24-ship three-act programme: the
    // ground station signs 2.64 m, sample+assign alone gives stations 17 and 18 0.39 m.
    int swaps = 0;
    if (showCompilePlan(plan, table, plan.act_count * plan.drones, &swaps) != plan.act_count)
        return false;

    out.station = station;
    out.pad = ShowPoint{};
    out.act_count = 0;
    for (int i = 0; i < plan.act_count; ++i) {
        out.at[i] = table[i * plan.drones + station];
        out.colour[i] = plan.acts[i].colour;
        out.act_count = i + 1;
    }
    return true;
}

float planLaneOffset(const ShowLane& lane, int act, float separation_m) {
    if (act < 0 || act >= lane.act_count) return 0.0f;
    // The layer is chosen from where the aircraft is going, not from who it is:
    // two airframes headed for neighbouring cells end up in different layers, and
    // that is the only reason a head-on meeting in the middle of a transition has
    // a chance of not happening.
    return showLane(lane.at[act], separation_m);
}

uint16_t planChecksum(const char* text, size_t len) {
    uint16_t ck = 0;
    for (size_t i = 0; i < len; ++i) {
        ck = static_cast<uint16_t>(ck * 31u + static_cast<unsigned char>(text[i]));
    }
    return ck;
}

}  // namespace ranch

/* ==================== src/show.h ==================== */

// The show, as one aircraft flies it.
//
// The programme arrives as a plan; this module turns it into the one lane that
// belongs to this station and then follows it: a position target per tick along
// the baked transition, a hold on the formation point, the act's colour on the
// pixels while it is there, and the next act when the hold is spent. Nothing here
// decides the choreography, and nothing here steers: the flight controller closes
// the loop, and this file tells it where the point is.
//
// The three ways it stops are the interesting part, and each one has its own
// reason and its own telemetry field:
//   - the RTK solution degrades to a single fix: carry on, and say so.
//   - the position goes away altogether: stop advancing, hold for three seconds
//     in case it was a shadow, then go to this station's own return slot.
//   - the ground station stops being heard: go to the slot immediately, and do not
//     enter the next act on the way.
// Nobody up here is flying the aircraft when that happens, so the aircraft has to
// know its own square.

#include <cstddef>
#include <cstdint>

namespace ranch {

enum ShowPhase : uint8_t {
    SHOW_IDLE = 0,     // no programme on board
    SHOW_READY,        // uploaded and checked; on the pad, waiting for permission
    SHOW_REFUSED,      // asked to launch and told no, with the reason
    SHOW_CLIMB,        // straight up off the pad, to the height act one starts at
    SHOW_MOVE,         // flying a baked transition, lane and all
    SHOW_HOLD,         // on the formation point, holding it
    SHOW_HOVER,        // no position at all: the three seconds before committing
    SHOW_RETURN,       // on the way to this station's own slot
    SHOW_AT_SLOT,      // in the slot, holding, programme over or failed
    SHOW_LANDED,       // the FC has the landing; the programme is done
};

const char* showPhaseName(ShowPhase p);

struct ShowStatus {
    ShowPhase phase = SHOW_IDLE;
    int station = 0;
    int act = 0;              // 1-based, what the frame publishes; 0 before act 1
    int act_count = 0;
    // A checksum of the programme record this airframe accepted, so the ground
    // station can tell "it has a three-act show" from "it has *this* three-act show".
    // Zero means nothing has been uploaded since boot.
    uint16_t plan_ck = 0;
    int acts_done = 0;        // acts flown to the end of their hold
    float u = 0.0f;           // 0..1 through the whole act: approach then hold
    uint32_t colour = 0;      // what the programme wants on the pixels
    ShowPoint target = {};    // the point being commanded, local NED
    ShowPoint rtk = {};       // where the receiver says this aircraft is
    ShowPoint fc = {};        // where the flight controller says it is
    float path_error_m = 0.0f;    // |rtk - target|, the number a show is judged on
    uint8_t fix_type = 0;
    uint8_t carr_soln = 0;
    uint8_t sats = 0;
    uint16_t hacc_cm = 0xFFFF;
    PositionSource source = POS_NONE;
    float batt_pct = 0.0f;
    float batt_v = 0.0f;
    uint32_t uptime_ms = 0;
    uint32_t setpoints = 0;   // position targets put on the wire
    uint32_t phase_ms = 0;
    bool armed = false;
    bool guided = false;
    bool fc_alive = false;
    bool link_up = false;
    bool safe_closed = false;
    bool has_plan = false;
    // True from the moment this airframe decides to come home by itself. It is not
    // the same as `why` being set: a programme that runs to its end also returns to
    // its slot, and that is not something a person on the ground should hear.
    bool failsafe = false;
    char why[20] = "";
};

void showInit();

// The upload. Parses the compact plan, refuses it outright if the aircraft cannot
// fly it, and keeps only this station's lane. `why` says which of the two happened.
bool showUpload(const char* text, size_t len, char* why, size_t cap);

// The station this airframe is. Set from NVS at boot or from the ground, and it
// re-derives the lane from the plan already on board.
void showSetStation(int station);
int showStation();

void showRequestLaunch();
void showLand();
void showNoteGcsContact();
bool showLinkIsUp();
const char* showReason();

// A MAVLink frame that came off the flight controller's UART. The heartbeats and
// the FC's own position estimate both arrive here; the show state machine is the
// only consumer, so the link is not filtered twice.
void showOnMavlink(const MavMessage& m);

// 0..1 through one act: the approach and the hold together, which is what a person
// watching the timeline on the dashboard means by "how far into act two are we".
float actProgress(uint32_t act_ms, float move_s, float hold_s);

// One tick of the programme, at SHOW_SETPOINT_HZ.
void showTick(uint32_t dt_ms);

const ShowStatus& showStatus();
bool showCanLaunch(char* why, size_t cap);

// The fuel gate, and the state of charge the board's own divider is reading.
float showBatteryPct();
float showBatteryVolts();
// True when the pack holds enough to fly the programme, get home, and leave the
// crew two minutes of reserve. Deliberately the same arithmetic as the ground
// station's SHOW_BATTERY check; test_sandbox.cpp walks every percentage and
// asserts the two verdicts never differ.
bool showFuelMayFly(const ShowPlan& plan, float pct);

// The leg from the pad to the first formation point, which show_core's own speed
// check does not cover because the ground station never has to leave the ground.
bool showLaunchLegFlyable(const ShowPlan& plan, const ShowLane& lane);

}  // namespace ranch

/* ==================== src/show.cpp ==================== */


#include <cmath>
#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

constexpr size_t SHOW_WHY_LEN = 20;

ShowStatus st;
ShowPlan plan;
ShowLane lane;
bool have_plan = false;

// The workspace an upload needs to find this station's point in a fleet-wide
// programme: one row per station per act, because the routing repair only knows which
// destination belongs to which airframe after it has seen all of them. It costs
// 24,576 B of .bss (`nm -S` on the host build: lane_table = 0x6000), 19,456 B more than
// the three rows plus order vector it replaces -- paid because the alternative is the
// aircraft flying a routing nobody certified. None of it is touched in flight: the lane
// the airframe actually follows is SHOW_MAX_ACTS points.
ShowPoint lane_table[SHOW_MAX_ACTS * SHOW_MAX_DRONES];

bool buildLane(const ShowPlan& p, int station, ShowLane& out) {
    return planBuildLane(p, station, out, lane_table);
}

// --- the ground station, as far as this aircraft knows ----------------------
uint32_t gcs_ms = 0;
bool gcs_seen = false;

// --- the flight controller's own reports ------------------------------------
uint32_t fc_hb_ms = 0;
bool fc_heartbeat = false;
bool fc_armed = false;
uint32_t fc_mode = 0xFFFFFFFFu;
LocalNed fc_pos{};

// --- what the programme is doing -------------------------------------------
bool launch_pending = false;      // cleared on the next GNSS second edge
uint32_t act_ms = 0;              // time inside the current act
ShowPhase hover_from = SHOW_IDLE; // where a hover was interrupted
ShowPhase hover_back = SHOW_IDLE; // and what a resume has to go back to
uint32_t cmd_ms = 0;              // retry timer for the arm/mode commands
uint32_t boot_ms = 0;

void clearReason() { st.why[0] = '\0'; }

void setReason(const char* r) { std::snprintf(st.why, sizeof(st.why), "%s", r); }

bool inFlight(ShowPhase p) {
    return p == SHOW_CLIMB || p == SHOW_MOVE || p == SHOW_HOLD || p == SHOW_HOVER
        || p == SHOW_RETURN || p == SHOW_AT_SLOT;
}

// The height every act in this programme starts at. The aircraft gets there
// straight up, on its own axis, before the baked timeline starts: a copter cannot
// climb at the speed it can fly sideways, and a launch leg that asks it to fly 44 m
// of diagonal in six seconds is a leg it will not make.
float standoffD(const ShowLane& l) {
    return l.act_count > 0 ? l.at[0].d : 0.0f;
}

// One shared headroom figure for every pixel this aircraft lights, so the strip
// budget in board.h and what the crew dial in are the same number and not two.
//
// Read from NVS on every paint rather than cached: an nvs_get_i32 on a namespace
// this small is tens of microseconds, ten times a second is nothing next to the
// 800 us the strip itself steals with interrupts off, and the alternative is a
// cache that has to be invalidated from the command handler -- two places that
// have to agree about when the number changed.
uint32_t ledBrightness() {
    int32_t v = SHOW_LED_FULL_PCT;
    if (!nvGetI32("led_pct", v) || v < 20 || v > 100) v = SHOW_LED_FULL_PCT;
    return static_cast<uint32_t>(v);
}

uint32_t dimColour(uint32_t rgb, unsigned n) {
    if (n == 0) return rgb;
    const uint32_t k = ledBrightness() / static_cast<uint32_t>(n);
    const uint32_t r = ((rgb >> 16) & 0xFFu) * k / 100u;
    const uint32_t g = ((rgb >> 8) & 0xFFu) * k / 100u;
    const uint32_t b = (rgb & 0xFFu) * k / 100u;
    return (r << 16) | (g << 8) | b;
}

// The coarse charge curve the on-board gauge reads the pack off. Deliberately
// coarser than the model in hal_sim.cpp: a firmware carrying a 1 % resolution table
// would only be pretending to know, and the two disagreeing by a few points is what
// the sandbox measures, because a reversed or transposed table has to show up as a
// fuel figure that does not follow the pack.
struct OcvPoint { float volts; float pct; };
const OcvPoint kOcv[] = {
    {3.30f, 0.0f}, {3.55f, 15.0f}, {3.68f, 35.0f}, {3.78f, 55.0f},
    {3.90f, 72.0f}, {4.03f, 87.0f}, {4.20f, 100.0f},
};

float packPct(float volts) {
    const float cell = volts / static_cast<float>(BATT_CELLS);
    const size_t n = sizeof(kOcv) / sizeof(kOcv[0]);
    if (cell <= kOcv[0].volts) return kOcv[0].pct;
    if (cell >= kOcv[n - 1].volts) return kOcv[n - 1].pct;
    for (size_t i = 1; i < n; ++i) {
        if (cell <= kOcv[i].volts) {
            const float span = kOcv[i].volts - kOcv[i - 1].volts;
            const float at = (cell - kOcv[i - 1].volts) / (span > 0.001f ? span : 0.001f);
            return kOcv[i - 1].pct + at * (kOcv[i].pct - kOcv[i - 1].pct);
        }
    }
    return kOcv[n - 1].pct;
}

float distanceTo(const ShowPoint& a, const ShowPoint& b) {
    const float dn = a.n - b.n, de = a.e - b.e, dd = a.d - b.d;
    return std::sqrt(dn * dn + de * de + dd * dd);
}

void sendTarget(const ShowPoint& p) {
    PositionTargetTx t{};
    t.x = p.n;
    t.y = p.e;
    t.z = p.d;
    t.yaw_rad = SHOW_HEADING_DEG * 0.0174532925f;
    t.type_mask = TYPEMASK_POSITION_YAW;
    t.coordinate_frame = FRAME_LOCAL_NED;

    uint8_t pl[64];
    MavWriter w(pl, sizeof(pl));
    t.pack(w, halMillis() - boot_ms);
    uint8_t frame[MAV_FRAME_MAX];
    MavFrameBuilder fb(1, 190);           // this board is a companion computer
    const size_t n = fb.build(MSG_SET_POSITION_TARGET_LOCAL_NED, pl, w.size(), frame, sizeof(frame));
    if (n) {
        fcWrite(frame, n);
        st.setpoints++;
    }
}

void sendCommand(uint16_t command, float p1, float p2) {
    CommandLongTx c{};
    c.p[0] = p1;
    c.p[1] = p2;
    for (int i = 2; i < 7; ++i) c.p[i] = 0.0f;
    c.command = command;
    c.target_sys = 1;
    c.target_comp = 1;                    // ArduPilot's autopilot component id
    uint8_t pl[64];
    MavWriter w(pl, sizeof(pl));
    c.pack(w);
    uint8_t frame[MAV_FRAME_MAX];
    MavFrameBuilder fb(1, 190);
    const size_t n = fb.build(MSG_COMMAND_LONG, pl, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void requestArmAndGuided() {
    sendCommand(CMD_DO_SET_MODE, 1.0f, static_cast<float>(FC_MODE_GUIDED));
    sendCommand(CMD_COMPONENT_ARM_DISARM, 1.0f, 0.0f);
}

void paint(uint32_t arm, uint32_t beacon) {
    ledSetPixel(LED_PIXEL_ARM, arm);
    ledSetPixel(LED_PIXEL_BEACON, beacon);
    ledCommit();
}

// The failsafe light: the arm pixel goes flat red and the beacon strobes. This is
// the one thing that tells a person on the ground which of the twenty-four came
// home early, so it is deliberately not the programme's colour.
void paintFailsafe(uint32_t now_ms) {
    const bool blink = ((now_ms / 250u) & 1u) != 0u;
    paint(dimColour(0x800000u, 1), blink ? dimColour(0xff0000u, 1) : 0x000000u);
}

void beginHover() {
    hover_from = st.phase;
    hover_back = st.phase;
    st.phase = SHOW_HOVER;
    st.phase_ms = 0;
    st.failsafe = true;
    setReason("RTK LOST");
}

void beginReturn(const char* why) {
    hover_from = SHOW_IDLE;
    st.phase = SHOW_RETURN;
    st.phase_ms = 0;
    if (why != nullptr && *why != '\0') setReason(why);
}

void enterAct(int one_based) {
    st.act = one_based;
    st.phase = SHOW_MOVE;
    st.phase_ms = 0;
    act_ms = 0;
    st.colour = lane.colour[one_based - 1];
}

// The point this act is built between. Acts are 1-based on the wire and 0-based in
// the lane, and the first act starts from the pad rather than from a predecessor.
void actEnds(int one_based, ShowPoint& from, ShowPoint& to, float& move_s, float& hold_s) {
    const int i = (one_based > 0 ? one_based : 1) - 1;
    // Act one starts where the climb ended: above the origin, at the height the
    // formation is drawn at, with no horizontal distance left to cover.
    from = (i <= 0) ? ShowPoint{0.0f, 0.0f, lane.at[0].d} : lane.at[i - 1];
    to = lane.at[i];
    move_s = plan.acts[i].move_s;
    hold_s = plan.acts[i].hold_s;
}

// Which position this aircraft believes in right now. The receiver is the
// authority; when it has gone quiet the flight controller's own estimate is used,
// because the only thing it is good for is noticing that we arrived somewhere.
ShowPoint whereWeAre() {
    if (st.source != POS_NONE) return st.rtk;
    if (fc_pos.valid) return st.fc;
    return st.rtk;
}

void tickGround() {
    // On the pad nothing moves until the permission is given, and then it does not
    // move *when* the permission is given either: it moves on a GNSS second, so
    // that every other airframe in the show starts its baked timeline on the same
    // one. A fleet that starts on its own boot time is a fleet of near-misses.
    st.phase_ms += SHOW_LOOP_MS;
    if (!launch_pending) return;
    if (!rtkPpsEdge()) return;
    launch_pending = false;
    st.armed = false;
    st.guided = false;
    cmd_ms = 0;
    st.failsafe = false;
    requestArmAndGuided();
    // NAV_TAKEOFF is what arms the climb on ArduCopter: it is the one command that
    // says "leave the ground" without also saying "and then fly somewhere", which is
    // what the baked path is for.
    sendCommand(CMD_NAV_TAKEOFF, -standoffD(lane), 0.0f);
    st.phase = SHOW_CLIMB;
    st.phase_ms = 0;
    act_ms = 0;
    st.acts_done = 0;
    st.target = ShowPoint{0.0f, 0.0f, standoffD(lane)};
    clearReason();
}

void tickFlight(uint32_t now_ms, uint32_t dt) {
    st.phase_ms += dt;
    const bool link_up = showLinkIsUp();
    const PositionSource src = rtkSource();

    // 1. The two ways the programme is abandoned, looked at before anything can
    //    advance it, so no ordering of the switch below can step into the next act
    //    after a loss has been seen in this same tick.
    if (src == POS_NONE && (st.phase == SHOW_CLIMB || st.phase == SHOW_MOVE || st.phase == SHOW_HOLD)) {
        beginHover();
    } else if (!link_up && (st.phase == SHOW_CLIMB || st.phase == SHOW_MOVE || st.phase == SHOW_HOLD
                            || st.phase == SHOW_HOVER)) {
        // No supervising station means nobody is watching this airframe, and the
        // next act is exactly the moment at which it would like to be watched.
        st.failsafe = true;
        setReason("LINK LOST");
        beginReturn("LINK LOST");
    }

    // 2. The phase machine.
    switch (st.phase) {
        case SHOW_HOVER: {
            if (src != POS_NONE) {
                // The shadow passed. Pick the same act back up at the same place in
                // it: the aircraft held position throughout, so the path it was on
                // is still the path it is on.
                st.phase = hover_back;
                hover_from = SHOW_IDLE;
                st.failsafe = false;
                clearReason();
            } else if (st.phase_ms >= SHOW_HOVER_MS) {
                beginReturn("RTK LOST");
            }
            break;
        }
        case SHOW_CLIMB: {
            st.target = ShowPoint{0.0f, 0.0f, standoffD(lane)};
            st.u = 0.0f;
            // "High enough to start the programme." The FC's own tolerance on a
            // takeoff, not the show's arrival radius: what matters here is that the
            // aircraft is at the height the first act is drawn at, not that it is
            // within 30 cm of a point it has not started flying to yet.
            if (std::fabs(whereWeAre().d - standoffD(lane)) < 1.0f) enterAct(1);
            break;
        }
        case SHOW_MOVE: {
            ShowPoint from{}, to{};
            float move_s = 0.0f, hold_s = 0.0f;
            actEnds(st.act, from, to, move_s, hold_s);
            act_ms += dt;
            const float u = move_s > 0.0f
                ? static_cast<float>(act_ms) / (move_s * 1000.0f) : 1.0f;
            st.target = showPathLane(from, to, u, planLaneOffset(lane, st.act - 1, plan.separation_m));
            if (st.phase == SHOW_MOVE) st.u = actProgress(act_ms, move_s, hold_s);
            if (u >= 1.0f) {
                st.phase = SHOW_HOLD;
                st.phase_ms = 0;
                st.target = to;
            }
            break;
        }
        case SHOW_HOLD: {
            ShowPoint from{}, to{};
            float move_s = 0.0f, hold_s = 0.0f;
            actEnds(st.act, from, to, move_s, hold_s);
            st.target = to;
            act_ms += dt;
            st.u = actProgress(act_ms, move_s, hold_s);
            if (st.phase_ms >= static_cast<uint32_t>(hold_s * 1000.0f)) {
                if (st.act < lane.act_count) {
                    st.acts_done = st.act;
                    enterAct(st.act + 1);
                } else {
                    // The programme is over. Everybody goes to their own square: a
                    // fleet that finishes by sitting 24 aircraft in one formation is
                    // a fleet waiting for the first gust.
                    st.acts_done = st.act;
                    // The programme ended; nothing went wrong. The reason is still
                    // published, because a ground app that sees an aircraft sitting
                    // in a return grid should not have to guess why.
                    beginReturn("SHOW OVER");
                }
            }
            break;
        }
        case SHOW_RETURN:
        case SHOW_AT_SLOT: {
            st.target = showReturnSlot(plan, lane.station);
            st.u = 1.0f;
            if (distanceTo(whereWeAre(), st.target) < SHOW_ARRIVAL_M) {
                if (st.phase != SHOW_AT_SLOT) {
                    st.phase = SHOW_AT_SLOT;
                    st.phase_ms = 0;
                }
            } else if (st.phase == SHOW_AT_SLOT) {
                // Wind took it off the slot. It is not "in" any more.
                st.phase = SHOW_RETURN;
            }
            break;
        }
        default:
            break;
    }

    // 3. Arm and mode are re-asked until the FC agrees. A command sent once at the
    //    wrong moment is a command that never happened, and the only evidence is
    //    that nothing moves.
    if (!fc_armed || fc_mode != static_cast<uint32_t>(FC_MODE_GUIDED)) {
        cmd_ms += dt;
        if (cmd_ms >= 500u) { cmd_ms = 0; requestArmAndGuided(); }
    }
    st.armed = fc_armed;
    st.guided = (fc_mode == static_cast<uint32_t>(FC_MODE_GUIDED));

    // 4. The lights, then the setpoint. The strip is written first because
    //    committing it is the one thing on this path that runs with interrupts
    //    off, and it must not delay a position target.
    const bool failed = st.why[0] != '\0' && st.phase != SHOW_MOVE && st.phase != SHOW_HOLD;
    if (failed) paintFailsafe(now_ms);
    else paint(dimColour(st.colour, 1), dimColour(st.colour, 1));

    sendTarget(st.target);
}

}  // namespace

const char* showPhaseName(ShowPhase p) {
    switch (p) {
        case SHOW_IDLE:     return "IDLE";
        case SHOW_READY:    return "READY";
        case SHOW_REFUSED:  return "REFUSED";
        case SHOW_CLIMB:    return "CLIMB";
        case SHOW_MOVE:     return "MOVE";
        case SHOW_HOLD:     return "HOLD";
        case SHOW_HOVER:    return "HOVER";
        case SHOW_RETURN:   return "RETURN";
        case SHOW_AT_SLOT:  return "SLOT";
        case SHOW_LANDED:   return "LANDED";
    }
    return "?";
}

float actProgress(uint32_t act_ms, float move_s, float hold_s) {
    const float total = (move_s + hold_s) * 1000.0f;
    if (total <= 0.0f) return 1.0f;
    const float u = static_cast<float>(act_ms) / total;
    return u > 1.0f ? 1.0f : u;
}

void showInit() {
    st = ShowStatus{};
    plan = ShowPlan{};
    lane = ShowLane{};
    have_plan = false;
    gcs_ms = 0;
    gcs_seen = false;
    fc_hb_ms = 0;
    fc_heartbeat = false;
    fc_armed = false;
    fc_mode = 0xFFFFFFFFu;
    fc_pos = LocalNed{};
    launch_pending = false;
    act_ms = 0;
    hover_from = SHOW_IDLE;
    hover_back = SHOW_IDLE;
    cmd_ms = 0;
    boot_ms = halMillis();
    st.station = 0;
    st.phase = SHOW_IDLE;
    st.act_count = 0;
    clearReason();
}

bool showFuelMayFly(const ShowPlan& p, float pct) {
    const float need_s = showDuration(p) + SHOW_RTL_RESERVE_S + SHOW_PAD_RESERVE_S;
    return (pct / 100.0f) * SHOW_PACK_HOVER_S >= need_s;
}

bool showLaunchLegFlyable(const ShowPlan& p, const ShowLane& l) {
    if (l.act_count <= 0) return false;
    const float dt = p.acts[0].move_s;
    if (dt <= 0.0f) return false;
    // The horizontal part only: the vertical part of getting to the first act is
    // the climb's business, and it is bounded by the FC's own climb rate rather
    // than by the programme's clock. The same 1.5x the smoothstep costs at its
    // steepest point that the ground station's SHOW_TOO_FAST check applies to every
    // leg between acts -- and the launch leg is missing there because the ground
    // station never has to leave the pad.
    const float dn = l.at[0].n - l.pad.n;
    const float de = l.at[0].e - l.pad.e;
    const float v = 1.5f * std::sqrt(dn * dn + de * de) / dt;
    return v <= p.max_speed_ms;
}

bool showCanLaunch(char* why, size_t cap) {
    const char* reason = nullptr;
    const uint32_t now = halMillis();
    if (!have_plan) reason = "NO PROGRAMME";
    else if (!safeSwitchIsClosed()) reason = "SAFE SWITCH";
    else if (!rtkOriginSet()) reason = "NO ORIGIN";
    else if (!showLaunchLegFlyable(plan, lane)) reason = "LEG TOO FAST";
    else if (rtkSource() == POS_NONE) reason = "NO FIX";
    // The fleet launches on a fixed solution. It can finish on a single one, which
    // is why the two rules differ: on the pad there is no reason at all to accept
    // decimetres, and in the air there is nothing better to be had.
    else if (rtkSource() != POS_RTK) reason = "NO RTK FIX";
    else if (!fc_heartbeat || now - fc_hb_ms > SHOW_FC_STALE_MS) reason = "NO FC LINK";
    else if (!showLinkIsUp()) reason = "NO GCS LINK";
    else if (!showFuelMayFly(plan, packPct(batteryVolts()))) reason = "LOW BATTERY";
    else if (st.phase != SHOW_READY) reason = "NOT READY";

    if (reason == nullptr) return true;
    if (why && cap) std::snprintf(why, cap, "%s", reason);
    return false;
}

bool showUpload(const char* text, size_t len, char* why, size_t cap) {
    if (why && cap) why[0] = '\0';
    if (text == nullptr || len == 0) {
        if (why && cap) std::snprintf(why, cap, "EMPTY PLAN");
        return false;
    }
    char buf[SHOW_PLAN_MAX_TEXT];
    if (len >= sizeof(buf)) {
        if (why && cap) std::snprintf(why, cap, "PLAN TOO LONG");
        return false;
    }
    std::memcpy(buf, text, len);
    buf[len] = '\0';

    ShowPlan parsed{};
    if (!planParse(buf, parsed)) {
        if (why && cap) std::snprintf(why, cap, "PLAN CORRUPT");
        return false;
    }
    // The go/no-go table the ground station ran is run again here. It is not trust
    // in the operator that is the problem: a plan that reached this UART through a
    // repeater is a plan somebody else may have typed.
    const uint32_t why_bits = showValidate(parsed, packPct(batteryVolts()), 0.0f);
    if (why_bits != SHOW_OK) {
        if (why && cap) std::snprintf(why, cap, "PLAN REJECTED %lu", static_cast<unsigned long>(why_bits));
        return false;
    }
    ShowLane built{};
    if (!buildLane(parsed, st.station, built)) {
        if (why && cap) std::snprintf(why, cap, "NO LANE FOR STATION");
        return false;
    }
    if (!showLaunchLegFlyable(parsed, built)) {
        if (why && cap) std::snprintf(why, cap, "LEG TOO FAST");
        return false;
    }

    // The ack the ground station reads back, over the exact bytes it sent. Computed
    // here and assigned only on success: a refused upload leaves the previous
    // programme on board, and reporting ck=0 then would say "nothing loaded", which
    // is not what happened.
    const uint16_t ck = planChecksum(buf, len);

    plan = parsed;
    lane = built;
    have_plan = true;
    st.has_plan = true;
    st.act_count = lane.act_count;
    st.plan_ck = ck;
    st.act = 0;
    st.acts_done = 0;
    st.u = 0.0f;
    st.colour = lane.colour[0];
    st.phase = SHOW_READY;
    st.phase_ms = 0;
    launch_pending = false;
    st.failsafe = false;
    clearReason();
    showNoteGcsContact();
    return true;
}

void showSetStation(int station) {
    if (station < 0) station = 0;
    if (station >= SHOW_MAX_DRONES) station = SHOW_MAX_DRONES - 1;
    st.station = station;
    lane.station = station;
    if (have_plan) {
        ShowLane rebuilt{};
        if (buildLane(plan, station, rebuilt)) {
            lane = rebuilt;
            st.act_count = lane.act_count;
            st.colour = lane.colour[0];
        }
    }
    nvSetI32("station", station);
}

int showStation() { return st.station; }

void showRequestLaunch() {
    char why[SHOW_WHY_LEN];
    if (!showCanLaunch(why, sizeof(why))) {
        if (have_plan) st.phase = SHOW_REFUSED;
        setReason(why);
        return;
    }
    // Throw away the second we are standing in. The receiver's TIMEPULSE is latched
    // by an interrupt until somebody reads it, and an edge from before the launch
    // was asked for is not the boundary the rest of the fleet is about to start on.
    while (rtkPpsEdge()) {}
    launch_pending = true;
    setReason("WAIT PPS");
}

void showLand() {
    if (!inFlight(st.phase)) return;
    sendCommand(CMD_NAV_LAND, 0.0f, 0.0f);
    st.phase = SHOW_LANDED;
    st.phase_ms = 0;
    st.u = 1.0f;
    setReason("LANDED ON REQUEST");
}

void showNoteGcsContact() {
    gcs_ms = halMillis();
    gcs_seen = true;
}

bool showLinkIsUp() {
    if (!gcs_seen || !wifiUp()) return false;
    return (halMillis() - gcs_ms) < SHOW_LINK_TIMEOUT_MS;
}

const char* showReason() { return st.why; }

void showOnMavlink(const MavMessage& m) {
    if (m.id == MSG_HEARTBEAT) {
        const Heartbeat h = decodeHeartbeat(m);
        if (!h.alive) return;
        fc_heartbeat = true;
        fc_hb_ms = halMillis();
        fc_armed = h.armed;
        fc_mode = h.custom_mode;
        return;
    }
    if (m.id == MSG_LOCAL_POSITION_NED) {
        const LocalNed l = decodeLocalPosition(m);
        if (l.valid) fc_pos = l;
    }
}

void showTick(uint32_t dt_ms) {
    const uint32_t now = halMillis();
    const RtkState& r = rtkState();

    st.source = rtkSource();
    st.fix_type = r.fix_type;
    st.carr_soln = r.carr_soln;
    st.sats = r.sats;
    st.hacc_cm = rtkHaccCm();
    st.rtk.n = r.n;
    st.rtk.e = r.e;
    st.rtk.d = r.d;
    if (fc_pos.valid) {
        st.fc.n = fc_pos.n;
        st.fc.e = fc_pos.e;
        st.fc.d = fc_pos.d;
    }
    st.batt_v = batteryVolts();
    st.batt_pct = packPct(st.batt_v);
    st.link_up = showLinkIsUp();
    st.fc_alive = fc_heartbeat && (now - fc_hb_ms) <= SHOW_FC_STALE_MS;
    st.safe_closed = safeSwitchIsClosed();
    st.uptime_ms = now - boot_ms;

    if (inFlight(st.phase)) {
        st.path_error_m = distanceTo(whereWeAre(), st.target);
        tickFlight(now, dt_ms);
    } else if (st.phase == SHOW_READY) {
        st.path_error_m = 0.0f;
        tickGround();
        // The strip shows the first act's colour at a fifth of its current while it
        // waits: a crew checking a line-up needs to see which aircraft is which
        // without two hundred pixels loading the BEC on the pad.
        paint(dimColour(st.colour, 5), dimColour(st.colour, 5));
    } else {
        st.path_error_m = 0.0f;
        paint(0x000000u, 0x000000u);
    }
}

const ShowStatus& showStatus() { return st; }

float showBatteryPct() { return st.batt_pct; }
float showBatteryVolts() { return st.batt_v; }

}  // namespace ranch

/* ==================== src/telemetry.h ==================== */

// State uplink for one show aircraft: one key=value frame, three sinks (console,
// MQTT, the log ring). The frame is composed once so all three carry byte-identical
// data, and the field order below is the contract the dashboard parses -- it is the
// same frame the ground station's own mirror of this airframe reads, so a reordering
// here is a silent misfeed there.

#include <cstddef>

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;    // broker down, or a frame that did not fit
    uint16_t last_len;
    bool broker_connected;
};

void telemetryInit();
// Broker identity comes from NVS, so a fleet of aircraft can share firmware.
// Call before the first publish; the buffers are copied, not borrowed.
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

// Ground-to-aircraft commands. The payload is not NUL-terminated: it is a view of
// the receive buffer that is only valid for the duration of the call, and a show
// upload is long enough that a handler which wants to keep it must copy it first.
using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const ShowStatus& st);
void telemetryService();               // broker keepalive + log drain
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch

/* ==================== src/telemetry.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

// Sized for the longest frame below with every optional field present. A frame
// that outgrows it is dropped and counted, never truncated.
constexpr size_t FRAME_CAP = 160;

UplinkStats up{};
char frame[FRAME_CAP];

char broker_host[64] = "";
char broker_client[24] = "show";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

}  // namespace

#if defined(RANCH_SIM)
namespace {
// The browser build has no broker. Frames still go to the console, which is what
// the ranch dashboard reads back, so the publish path stays exercised end to end
// and the counters below stay honest about what was not sent.
void uplinkMqtt(const char*, size_t) {}
void brokerService() {}
bool brokerUp() { return false; }
}  // namespace
#else
namespace {
#include <WiFi.h>
#include <PubSubClient.h>

WiFiClient wifi_client;
PubSubClient broker(wifi_client);
uint32_t next_connect = 0;
bool callback_wired = false;

// The only topic this firmware listens to is the command one, and the payload
// arrives as a length-delimited view of the receive buffer.
void onMqttPayload(char* topic, uint8_t* payload, unsigned int len) {
    if (!cmd_fn) return;
    if (std::strcmp(topic, MQTT_TOPIC_CMD) != 0) return;
    cmd_fn(reinterpret_cast<const char*>(payload), len);
}

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

void brokerService() {
    const uint32_t now = halMillis();
    if (*broker_host == '\0') return;
    if (!broker.connected()) {
        if (static_cast<int32_t>(now - next_connect) < 0) return;
        next_connect = now + 5000;              // one attempt per 5 s, forever
        broker.setServer(broker_host, broker_port);
        broker.setBufferSize(1024);             // an uploaded programme is not short
        if (!callback_wired) {
            broker.setCallback(onMqttPayload);
            callback_wired = true;
        }
        if (broker.connect(broker_client)) broker.subscribe(MQTT_TOPIC_CMD, 0);
    } else {
        broker.loop();
    }
}

bool brokerUp() { return broker.connected(); }
}  // namespace
#endif

void telemetryInit() {
    up = UplinkStats{};
    frame[0] = '\0';
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "show");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const ShowStatus& s) {
    char rgb[10];
    std::snprintf(rgb, sizeof(rgb), "%06lx", static_cast<unsigned long>(s.colour) & 0xFFFFFFul);

    FrameWriter w(frame, sizeof(frame));
    w.begin("SHOW");
    // The station number, not a serial: two aircraft whose frames cannot be told
    // apart are one aircraft whose telemetry is a lie, and the ground station keys
    // its whole picture on this field.
    w.add("id", s.station);
    // Which act is being flown, 1-based, 0 for "no programme on board". The phase
    // is not published separately because the three fields below it already say it:
    // a returning aircraft reports the slot in n/e/d and the reason in why.
    w.add("act", s.act);
    // What programme this airframe is holding, so an upload can be acked from the
    // aircraft's own words rather than from the ground station hoping the packet
    // arrived: the act count plus a checksum of the record it parsed. A board that
    // refused an upload reports acts=0/ck=0, and a board still flying last night's
    // show reports that show's numbers.
    w.add("acts", s.act_count);
    w.add("ck", static_cast<int>(s.plan_ck));
    w.add("u", s.u, 2);
    // Where this airframe actually is, in local NED metres from the surveyed
    // origin -- the same frame show_core bakes its paths in, so a viewer can
    // compare a reported position against the planned one without a conversion.
    w.add("n", s.rtk.n, 2);
    w.add("e", s.rtk.e, 2);
    w.add("d", s.rtk.d, 2);
    // The colour the programme asked for, not the current going into the pixels:
    // the mirror of this airframe has to be able to compare it against the act it
    // planned. The strip's own headroom, and the red a failsafe return puts on it,
    // are the aircraft's business and are measured, in the sandbox, off the pixels.
    w.add("led", rgb);
    // Two words for "how good is this position", because they are two different
    // devices' opinions: the receiver's own fix type, and whether the carrier
    // phase resolved. A show flown on fix_type=3 with carr_soln=0 is a show flown
    // on decimetres, and the frame has to be able to say that.
    w.add("fix", static_cast<int>(s.fix_type));
    w.add("rtk", static_cast<int>(s.carr_soln));
    w.add("batt", static_cast<int>(s.batt_pct + 0.5f));
    w.add("link", s.link_up ? "up" : "down");
    w.add("up", static_cast<int>(s.uptime_ms / 1000u));
    if (*s.why) w.add("why", s.why);
    w.endLine();

    if (w.overflow()) {
        // A truncated telemetry frame is worse than a missing one: the ground app
        // would read a stale field as fresh. Drop and count it.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

// Nothing to drain here: the flight log belongs to the flight controller, which
// has the card and the airframe's own sensors on it. What is left for this service
// is the broker keepalive, and it is on its own job because a socket that blocks
// must not delay a position target.
void telemetryService() {
    brokerService();
}

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch

/* ==================== src/hal_sim.cpp ==================== */

// Synthetic airframe, flight controller, RTK receiver, light strip and battery for
// the browser (Velxio) and host test builds.
//
// Why it is written this way:
//  - fcWrite() feeds the firmware's own bytes through the same MavParser the
//    firmware uses, and the simulated FC answers with real MAVLink frames. A wrong
//    CRC_EXTRA, a wrong field offset, or a type_mask that ignores the very
//    dimensions the show needs therefore surfaces as "the FC never moved" or
//    "every frame was a CRC error", instead of passing silently the way a stubbed
//    HAL would.
//  - the receiver answers with a UBX-NAV-PVT built out of where the airframe
//    actually is. The firmware's position arrives through a serial frame that has
//    to be framed, checksummed, decoded and converted from geodetic metres, so a
//    byte-level mistake in that chain is measurable rather than hidden.
//  - the position hold is a bounded pursuit with a deadband and a wind. It stops
//    correcting inside its own band, so the aircraft is never exactly on the
//    point, which is what the arrival tolerance in the sandbox is really judging.
//  - no allocation, no threads, and time only moves when the firmware asks for it,
//    which lets the host test fly a whole show in milliseconds.
#if defined(RANCH_SIM)

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace ranch {
namespace {

// --- the pad ----------------------------------------------------------------
// Anywhere will do, but it must be a real place: the receiver turns the airframe's
// local metres into lat/lon and the firmware turns them back, so a geodetic bug
// moves the aircraft by kilometres and the show notices.
constexpr int32_t SIM_LAT_E7 = 305000000;        // 30.5000000 N
constexpr int32_t SIM_LON_E7 = 1143000000;       // 114.3000000 E
constexpr int32_t SIM_ALT_MM = 42000;            // 42 m above the ellipsoid
constexpr double SIM_M_PER_DEG_LAT = 111320.0;
constexpr double SIM_LAT_DEG = 30.5;

double simMetresPerDegLon() {
    return SIM_M_PER_DEG_LAT * std::cos(SIM_LAT_DEG * 0.0174532925199433);
}

// --- the wind and the airframe's limits -------------------------------------
// A steady 1.2 m/s from the south. Small enough that a show flies in it, large
// enough that a controller which was assumed perfect stops looking perfect.
constexpr float SIM_WIND_N = 0.0f;
constexpr float SIM_WIND_E = 1.2f;
// Position hold: it stops inside its own deadband and drifts in the wind until it
// does not, which is the limit cycle every real copter sits in over a field.
constexpr float SIM_HOLD_DEADBAND_M = 0.08f;
constexpr float SIM_WOBBLE_M = 0.03f;            // the EKF's own wander

// --- clock ------------------------------------------------------------------
uint32_t g_now = 0;
uint32_t g_wdt_feeds = 0;

#if defined(RANCH_HOST)
uint32_t g_virt = 0;                 // advanced only by halDelayMs
inline uint32_t rawMillis() { return g_virt; }
inline void rawSleep(uint32_t ms) { g_virt += ms; }
#else
inline uint32_t rawMillis() { return ::millis(); }
inline void rawSleep(uint32_t ms) { ::delay(ms); }
#endif

// --- the airframe, as the flight controller flies it ------------------------
struct Airframe {
    float n = 0.0f, e = 0.0f, d = 0.0f;      // local NED metres; d = 0 is on the pad
    float tn = 0.0f, te = 0.0f, td = 0.0f;   // the last position target it latched
    bool have_target = false;
    bool armed = false;
    bool landing = false;
    uint32_t mode = 0;                       // custom_mode, echoed in HEARTBEAT
    uint32_t setpoints = 0;
    uint32_t commands = 0;
    uint32_t mask_refused = 0;               // targets whose mask ignored position
    uint16_t last_mask = 0;
};
Airframe af;

// --- the receiver ------------------------------------------------------------
struct Receiver {
    SimFix fix = SIM_FIX_RTK_FIXED;
    uint32_t frames = 0;
    uint32_t t_pvt = 0;                      // ms since the last NAV-PVT
    uint32_t t_pps = 0;
    bool pps = false;                        // latched until rtkPpsEdge() takes it
    uint32_t cfg_frames = 0;                 // UBX the firmware asked for
};
Receiver rx;

// --- the pack ----------------------------------------------------------------
// 3300 mAh on 2S, and a hover current chosen so that the endurance the firmware is
// given (SHOW_PACK_HOVER_S) is the endurance this model actually has:
// 3300 mAh / 9.0 A = 22.0 min.
constexpr float PACK_MAHP = 3300.0f;
constexpr float I_PAD_A = 0.35f;
constexpr float I_HOVER_A = 9.0f;
constexpr float BEC_EFF = 0.90f;             // the 5 V rail is not lossless
float pack_mah = PACK_MAHP;

// The resting open-circuit curve of a lithium cell, at the resolution the *model*
// needs. The firmware reads a coarser one, and the difference between the two is
// what the sandbox measures as gauge error -- a firmware with a transposed or
// reversed table cannot survive that.
float cellOcv(float pct) {
    static const float kPct[] = {0.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f,
                                 60.0f, 70.0f, 80.0f, 90.0f, 100.0f};
    static const float kV[]   = {3.20f, 3.45f, 3.58f, 3.64f, 3.69f, 3.74f,
                                 3.80f, 3.87f, 3.97f, 4.08f, 4.20f};
    if (pct <= kPct[0]) return kV[0];
    if (pct >= kPct[10]) return kV[10];
    for (int i = 1; i <= 10; ++i) {
        if (pct <= kPct[i]) {
            const float at = (pct - kPct[i - 1]) / (kPct[i] - kPct[i - 1]);
            return kV[i - 1] + at * (kV[i] - kV[i - 1]);
        }
    }
    return kV[10];
}

// --- outputs, as observed ---------------------------------------------------
bool sim_safe_closed = true;      // the shorting connector is in: rigged for the show
bool buzzer_on = false;
uint32_t buzzer_ms = 0;
uint32_t led_px[2] = {0, 0};
uint32_t led_commits = 0;
uint32_t status_led_ms = 0;
bool wifi_up = true;

void simService(uint32_t dt_ms);

// --- the byte pipes ---------------------------------------------------------
// Two rings: what the receiver puts on its UART, and what the FC puts on its own.
constexpr size_t RX_CAP = 2048;
uint8_t rtk_rx[RX_CAP];
size_t rtk_head = 0, rtk_tail = 0;
uint8_t fc_rx[RX_CAP];
size_t fc_head = 0, fc_tail = 0;
uint8_t console_rx[RX_CAP];
size_t console_head = 0, console_tail = 0;
uint32_t rx_overflow = 0;

void push(uint8_t* buf, size_t& head, size_t cap, size_t& tail, const uint8_t* p, size_t n) {
    while (n--) {
        const size_t next = (head + 1) % cap;
        if (next == tail) { rx_overflow++; continue; }
        buf[head++] = *p++;
        if (head == cap) head = 0;
    }
}

// --- UBX out of the receiver and into the firmware --------------------------
void emitPvt() {
    uint8_t pl[92];
    std::memset(pl, 0, sizeof(pl));
    // The airframe's true local metres, back into geodetic, with the noise the
    // solution class actually has: centimetres when the carrier phase is fixed,
    // a couple of decimetres when it is not.
    const float noise = (rx.fix == SIM_FIX_RTK_FIXED) ? 0.02f
                      : (rx.fix == SIM_FIX_RTK_FLOAT ? 0.08f
                      : (rx.fix == SIM_FIX_SINGLE ? 0.25f : 0.0f));
    const float wob = static_cast<float>(std::sin(static_cast<double>(g_now) / 640.0)) * SIM_WOBBLE_M;
    const float wnc = noise > 0.0f ? noise : 0.0f;
    const float dn = (noise > 0.0f ? static_cast<float>(std::sin(static_cast<double>(g_now) / 210.0)) * wnc : 0.0f) + wob;
    const float de = (noise > 0.0f ? static_cast<float>(std::cos(static_cast<double>(g_now) / 173.0)) * wnc : 0.0f);

    const double lat = SIM_LAT_E7 * 1e-7 + (af.n + dn) / SIM_M_PER_DEG_LAT;
    const double lon = SIM_LON_E7 * 1e-7 + (af.e + de) / simMetresPerDegLon();
    const int32_t lat_e7 = static_cast<int32_t>(lat * 1e7);
    const int32_t lon_e7 = static_cast<int32_t>(lon * 1e7);
    const int32_t h_mm = SIM_ALT_MM - static_cast<int32_t>(af.d * 1000.0f);

    const uint32_t ms = g_now;
    auto put32 = [&](size_t at, int32_t v) {
        pl[at] = static_cast<uint8_t>(v & 0xFF);
        pl[at + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        pl[at + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        pl[at + 3] = static_cast<uint8_t>((static_cast<uint32_t>(v) >> 24) & 0xFF);
    };
    auto put16 = [&](size_t at, uint16_t v) {
        pl[at] = static_cast<uint8_t>(v & 0xFF);
        pl[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    put32(0, ms % 1000u);                             // iTOW
    put16(4, 2026);
    pl[6] = 9; pl[7] = 30; pl[8] = 21; pl[9] = 0; pl[10] = 0;
    const bool locked = rx.fix != SIM_FIX_NONE;
    pl[11] = locked ? 0x07 : 0x00;                    // valid: date, time, full resolution
    put32(12, locked ? 20000u : 30000000u);           // time accuracy, ns
    put32(16, 0);                                     // nano
    pl[20] = locked ? 3 : 0;                          // fixType: 3 = 3D
    uint8_t flags = 0;
    if (locked) flags |= 0x01;                        // gnssFixOk
    if (rx.fix == SIM_FIX_RTK_FIXED || rx.fix == SIM_FIX_RTK_FLOAT) flags |= 0x02;  // diffSoln
    pl[21] = flags;
    // Protocol 27.00 and later -- every current M-series and F-series module --
    // carries the carrier solution in flags2 bits 6..7 (1 float, 2 fixed).
    uint8_t flags2 = 0;
    if (rx.fix == SIM_FIX_RTK_FIXED) flags2 = 2u << 6;
    else if (rx.fix == SIM_FIX_RTK_FLOAT) flags2 = 1u << 6;
    pl[22] = flags2;
    pl[23] = locked ? 14 : 0;                         // numSV
    put32(24, lon_e7);
    put32(28, lat_e7);
    put32(32, h_mm);                                  // height above the ellipsoid
    put32(36, h_mm);                                  // hMSL, same datum here
    const uint32_t hacc_mm = noise > 0.0f ? static_cast<uint32_t>(noise * 1000.0f) : 99990u;
    put32(40, static_cast<int32_t>(hacc_mm));
    put32(44, static_cast<int32_t>(hacc_mm * 1.3f));
    put32(48, 0); put32(52, 0); put32(56, 0);         // velN/E/D, reported as still
    put32(60, 0);                                     // gSpeed
    rx.frames++;

    uint8_t frame[8 + sizeof(pl)];
    frame[0] = 0xB5; frame[1] = 0x62; frame[2] = 0x01; frame[3] = 0x07;
    frame[4] = static_cast<uint8_t>(sizeof(pl) & 0xFF);
    frame[5] = static_cast<uint8_t>(sizeof(pl) >> 8);
    std::memcpy(frame + 6, pl, sizeof(pl));
    uint8_t a = 0, b = 0;
    for (size_t i = 2; i < 6 + sizeof(pl); ++i) { a = static_cast<uint8_t>(a + frame[i]); b = static_cast<uint8_t>(b + a); }
    frame[6 + sizeof(pl)] = a;
    frame[7 + sizeof(pl)] = b;
    push(rtk_rx, rtk_head, RX_CAP, rtk_tail, frame, sizeof(frame));
}

// --- MAVLink out of the simulated FC ---------------------------------------
MavFrameBuilder sim_tx(1, 1);                 // sysid 1, the autopilot itself

void emit(uint32_t id, const uint8_t* payload, size_t len) {
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = sim_tx.build(id, payload, len, frame, sizeof(frame));
    if (!n) return;
    push(fc_rx, fc_head, RX_CAP, fc_tail, frame, n);
}

void emitHeartbeat() {
    uint8_t pl[9];
    MavWriter w(pl, sizeof(pl));
    HeartbeatTx ht{};
    ht.custom_mode = af.mode;
    ht.type = 2;                      // MAV_TYPE_QUADROTOR
    ht.autopilot = 3;                 // MAV_AUTOPILOT_ARDUPILOTMEGA
    ht.base_mode = static_cast<uint8_t>(1 | (af.armed ? 0x80 : 0x00));
    ht.system_status = static_cast<uint8_t>(af.armed ? (af.d < -0.5f ? 4 : 2) : 1);
    ht.pack(w);
    emit(MSG_HEARTBEAT, pl, w.size());
}

void emitLocalPosition() {
    uint8_t pl[28];
    MavWriter w(pl, sizeof(pl));
    w.u32(g_now);
    w.f32(af.n); w.f32(af.e); w.f32(af.d);
    w.f32(0.0f); w.f32(0.0f); w.f32(0.0f);
    emit(MSG_LOCAL_POSITION_NED, pl, w.size());
}

// --- the FC's own idea of how an airframe moves ----------------------------
void airframeStep(float dt) {
    if (!sim_safe_closed) {
        // SAF_SAFE is not advisory on this airframe: the motors cannot start, and
        // anything already flying lands where it is.
        af.armed = false;
        af.landing = true;
        af.mode = FC_MODE_LAND;
    }

    if (af.landing) {
        af.d += SHOW_CLIMB_MS * dt;            // a copter descends at its own limit
        if (af.d >= 0.0f) {
            af.d = 0.0f;
            af.armed = false;
            af.landing = false;
            af.have_target = false;
        }
        return;
    }
    if (!af.armed) { af.d = 0.0f; return; }
    if (!af.have_target) return;

    float dn = af.tn - af.n, de = af.te - af.e, dd = af.td - af.d;
    const float err = std::sqrt(dn * dn + de * de + dd * dd);
    if (err <= SIM_HOLD_DEADBAND_M) {
        // Inside the hold's own band the loop does nothing, so the wind is what
        // moves the aircraft. This is where "it holds 40 m up within 20 cm" comes
        // from, and it is not a number anybody gets by pretending the loop is good.
        af.n += SIM_WIND_N * dt;
        af.e += SIM_WIND_E * dt;
        return;
    }
    const float step = SHOW_CRUISE_MS * dt;
    const float hdist = std::sqrt(dn * dn + de * de);
    const float hstep = hdist < step ? hdist : step;
    if (hdist > 0.001f) {
        af.n += dn / hdist * hstep;
        af.e += de / hdist * hstep;
    }
    const float vstep = SHOW_CLIMB_MS * dt;
    af.d += dd > vstep ? vstep : (dd < -vstep ? -vstep : dd);
}

void plantStep(float dt) {
    airframeStep(dt);

    // The pack, drained by what the aircraft is actually doing. Hover current on
    // an airframe this size, the electronics' own draw on the pad, and the light
    // strip's real current -- which is why the pixels' brightness is a flight-time
    // decision and not a cosmetic one.
    const bool flying = af.armed && af.d < -0.5f;
    float amps = flying ? I_HOVER_A : I_PAD_A;
    if (!flying) {
        // On the pad the strip is what the pack is actually paying for. A WS2812B
        // at full white takes about LED_INSHINE_MA on the 5 V rail, the BEC is not
        // lossless, and the pack sees the sum: which is why a show's brightness is
        // a flight-time decision and not a cosmetic one.
        float led_ma = 0.0f;
        for (int i = 0; i < SHOW_LED_PIXELS; ++i) {
            const uint32_t c = led_px[i < 2 ? i : 1];
            led_ma += static_cast<float>(((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 255.0f
                    * (LED_INSHINE_MA / 3.0f);
        }
        const float pack_v = static_cast<float>(BATT_CELLS) * 3.7f;
        amps += (5.0f * led_ma / 1000.0f) / pack_v / BEC_EFF;
    }
    pack_mah -= amps * 1000.0f / 3600.0f * dt;
    if (pack_mah < 0.0f) pack_mah = 0.0f;
    if (pack_mah > PACK_MAHP) pack_mah = PACK_MAHP;
}

// --- what the firmware sent, and what the FC makes of it -------------------
MavParser sim_rx;

void fcOnMessage(const MavMessage& m) {
    if (m.id == MSG_SET_POSITION_TARGET_LOCAL_NED) {
        if (m.len < 53) return;
        const uint16_t mask = m.u16At(48);
        af.last_mask = mask;
        // The mask is honoured, not read and ignored: a firmware that forgot to
        // un-set the position bits commands nothing at all, and an FC model that
        // did not check would fly it anyway and hide the bug.
        if ((mask & 0x0007) != 0) { af.mask_refused++; return; }
        af.tn = m.f32At(4);
        af.te = m.f32At(8);
        af.td = m.f32At(12);
        af.have_target = true;
        af.setpoints++;
        return;
    }
    if (m.id == MSG_COMMAND_LONG) {
        const uint16_t cmd = m.u16At(28);
        const float p1 = m.f32At(0);
        const float p2 = m.f32At(4);
        switch (cmd) {
            case CMD_DO_SET_MODE:
                af.mode = static_cast<uint32_t>(p2);
                af.commands++;
                break;
            case CMD_COMPONENT_ARM_DISARM:
                if (p1 != 0.0f && sim_safe_closed) af.armed = true;
                else if (p1 != 0.0f) af.commands++;       // refused, and counted as seen
                else { af.armed = false; af.have_target = false; }
                if (p1 == 0.0f) af.landing = false;
                break;
            case CMD_NAV_TAKEOFF:
                if (!sim_safe_closed) break;
                af.armed = true;
                af.mode = FC_MODE_GUIDED;
                af.tn = af.n; af.te = af.e; af.td = -p1;
                af.have_target = true;
                af.commands++;
                break;
            case CMD_NAV_LAND:
                af.landing = true;
                af.mode = FC_MODE_LAND;
                af.commands++;
                break;
            default:
                break;
        }
    }
}

void simService(uint32_t dt_ms) {
    const float dt = dt_ms / 1000.0f;
    plantStep(dt);
    if (buzzer_on) buzzer_ms += dt_ms;

    rx.t_pvt += dt_ms;
    if (rx.t_pvt >= 200) { rx.t_pvt = 0; emitPvt(); }       // NAV-PVT at 5 Hz
    rx.t_pps += dt_ms;
    if (rx.t_pps >= 1000) {
        rx.t_pps = 0;
        // TIMEPULSE only exists when the module has a time solution. A receiver
        // with nothing locked does not tick, and an aircraft that waits for a tick
        // it never gets stays on the pad -- which is the right answer.
        if (rx.fix != SIM_FIX_NONE) rx.pps = true;
    }

    static uint32_t t_hb = 0, t_pos = 0;
    t_hb += dt_ms;
    if (t_hb >= 1000) { t_hb = 0; emitHeartbeat(); }
    t_pos += dt_ms;
    if (t_pos >= 200) { t_pos = 0; emitLocalPosition(); }
}

void pump() {
    const uint32_t t = rawMillis();
    if (t == g_now) return;
    uint32_t dt = t - g_now;
    g_now = t;
    if (dt > 100) dt = 100;           // a stalled UI thread must not teleport
    simService(dt);
}

// --- non-volatile parameters ------------------------------------------------
struct Kv {
    char key[16];
    uint8_t kind;                       // 0 free, 1 i32, 2 f32, 3 str
    int32_t i;
    float f;
    char s[64];
};
Kv nvs_[16];

Kv* findKv(const char* key, bool create) {
    const size_t klen = std::strlen(key);
    Kv* free_slot = nullptr;
    for (auto& k : nvs_) {
        if (k.kind && std::strlen(k.key) == klen && std::memcmp(k.key, key, klen) == 0) return &k;
        if (!k.kind && !free_slot) free_slot = &k;
    }
    if (!create || !free_slot) return nullptr;
    std::snprintf(free_slot->key, sizeof(free_slot->key), "%s", key);
    free_slot->i = 0;
    free_slot->f = 0.0f;
    free_slot->s[0] = '\0';
    return free_slot;
}

}  // namespace

// --- HAL --------------------------------------------------------------------
void halInit() {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    // The console is the only sink in the simulation, and on this core a
    // HardwareSerial that was never begun silently discards every write.
    Serial.begin(DBG_BAUD);
#endif
    g_now = rawMillis();
    rtk_head = rtk_tail = 0;
    fc_head = fc_tail = 0;
    af = Airframe{};
    rx = Receiver{};
}

uint32_t halMillis() { return g_now; }

void halDelayMs(uint32_t ms) {
    rawSleep(ms);
    pump();
}

void halSimPump() { pump(); }

void consoleWrite(const char* data, size_t len) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#else
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
#endif
}

int consoleRead(char* buf, size_t cap) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    // In the browser this file *is* the hardware layer, so the console is the
    // emulator's own USB serial: whatever the monitor's send box delivers arrives here.
    // The port has to be opened first, and in this build hal_esp32.cpp -- where the
    // aircraft's own setup() does it -- is not compiled in.
    static bool opened = [] { Serial.begin(DBG_BAUD); return true; }();
    (void)opened;
    int n = 0;
    while (n < static_cast<int>(cap) && Serial.available() > 0) {
        buf[n++] = static_cast<char>(Serial.read());
    }
    return n;
#else
    size_t n = 0;
    while (n < cap && console_tail != console_head) {
        buf[n++] = static_cast<char>(console_rx[console_tail]);
        console_tail = (console_tail + 1) % RX_CAP;
    }
    return static_cast<int>(n);
#endif
}

// The serial monitor's "type to send" box, for the sandbox: bytes go in one end and
// come out of consoleRead() the other, through the same line parser the aircraft's
// UART goes through. A test that uploads this way cannot pass by calling the handler
// directly, which is the point.
void simConsoleInput(const char* data, size_t len) {
    push(console_rx, console_head, RX_CAP, console_tail,
         reinterpret_cast<const uint8_t*>(data), len);
}

int rtkAvailable() {
    pump();
    return static_cast<int>((RX_CAP + rtk_head - rtk_tail) % RX_CAP);
}

int rtkRead(uint8_t* buf, size_t cap) {
    size_t n = 0;
    while (n < cap && rtk_tail != rtk_head) {
        buf[n++] = rtk_rx[rtk_tail];
        rtk_tail = (rtk_tail + 1) % RX_CAP;
    }
    return static_cast<int>(n);
}

void rtkWrite(const uint8_t* buf, size_t len) {
    // A receiver that is configured answers: count the frames and let the test see
    // that the configuration was written at all.
    for (size_t i = 0; i + 1 < len; ++i) {
        if (buf[i] == 0xB5 && buf[i + 1] == 0x62) rx.cfg_frames++;
    }
}

bool rtkPpsEdge() {
    pump();
    if (!rx.pps) return false;
    rx.pps = false;
    return true;
}

int fcAvailable() {
    pump();
    return static_cast<int>((RX_CAP + fc_head - fc_tail) % RX_CAP);
}

int fcRead(uint8_t* buf, size_t cap) {
    size_t n = 0;
    while (n < cap && fc_tail != fc_head) {
        buf[n++] = fc_rx[fc_tail];
        fc_tail = (fc_tail + 1) % RX_CAP;
    }
    return static_cast<int>(n);
}

void fcWrite(const uint8_t* buf, size_t len) {
    MavMessage m{};
    for (size_t i = 0; i < len; ++i) {
        if (sim_rx.push(buf[i], m)) fcOnMessage(m);
    }
}

void ledSetPixel(uint8_t index, uint32_t rgb) {
    if (index < 2) led_px[index] = rgb;
}

void ledCommit() { led_commits++; }

bool safeSwitchClosed() { return sim_safe_closed; }
bool safeSwitchIsClosed() { return sim_safe_closed; }

void setBuzzer(bool on) { buzzer_on = on; }
bool buzzerIsOn() { return buzzer_on; }
void feedWatchdog() { g_wdt_feeds++; }
void setStatusLed(bool on) { if (on) status_led_ms++; }

float batteryVolts() {
    const float pct = 100.0f * pack_mah / PACK_MAHP;
    return cellOcv(pct) * static_cast<float>(BATT_CELLS);
}

bool nvGetI32(const char* key, int32_t& out) {
    Kv* k = findKv(key, false);
    if (!k || k->kind != 1) return false;
    out = k->i;
    return true;
}

bool nvSetI32(const char* key, int32_t value) {
    Kv* k = findKv(key, true);
    if (!k) return false;
    k->kind = 1;
    k->i = value;
    return true;
}

bool nvGetF32(const char* key, float& out) {
    Kv* k = findKv(key, false);
    if (!k || k->kind != 2) return false;
    out = k->f;
    return true;
}

bool nvSetF32(const char* key, float value) {
    Kv* k = findKv(key, true);
    if (!k) return false;
    k->kind = 2;
    k->f = value;
    return true;
}

bool nvGetStr(const char* key, char* buf, size_t cap) {
    Kv* k = findKv(key, false);
    if (!k || k->kind != 3) return false;
    std::snprintf(buf, cap, "%s", k->s);
    return true;
}

bool nvSetStr(const char* key, const char* value) {
    Kv* k = findKv(key, true);
    if (!k) return false;
    k->kind = 3;
    std::snprintf(k->s, sizeof(k->s), "%s", value);
    return true;
}

// The dashboard radio is associated in the simulation, and its strength falls off
// with distance from the pad, so the RSSI a test sees is a measurement and not a
// constant.
bool wifiConnect(const char*, const char*) { wifi_up = true; return true; }
bool wifiUp() { return wifi_up; }

int wifiRssi() {
    const float d = std::sqrt(af.n * af.n + af.e * af.e);
    float dbm = -40.0f - 0.28f * d;
    if (dbm < -95.0f) dbm = -95.0f;
    return static_cast<int>(dbm);
}

void wifiReconnect() { wifi_up = true; }

const char* resetReason() { return "sim"; }

// --- introspection and injection for the host test --------------------------
void halSimCounters(SimCounters& out) {
    out.rtk_frames = rx.frames;
    out.fc_frames_in = sim_rx.parsed();
    out.setpoints_latched = af.setpoints;
    out.commands_taken = af.commands;
    out.led_commits = led_commits;
    out.wdt_feeds = g_wdt_feeds;
    out.buzzer_ms = buzzer_ms;
    out.last_type_mask = af.last_mask;
    out.last_n = af.tn;
    out.last_e = af.te;
    out.last_d = af.td;
    out.cfg_frames = rx.cfg_frames;
    out.pack_pct = 100.0f * pack_mah / PACK_MAHP;
    out.led_pixel[0] = led_px[0];
    out.led_pixel[1] = led_px[1];
}

void halSimFix(SimFix f) { rx.fix = f; }

void halSimSafeSwitch(bool closed) { sim_safe_closed = closed; }

void halSimBatteryPct(float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    pack_mah = PACK_MAHP * pct / 100.0f;
}

void halSimClear() {
    // Each phase of the test is a different aircraft on the same pad. A reboot does
    // not un-swap a pack or un-blinding a receiver, so the world is put back here
    // and not by whoever set it the last time.
    af = Airframe{};
    rx = Receiver{};
    pack_mah = PACK_MAHP;
    buzzer_on = false;
    buzzer_ms = 0;
    led_px[0] = led_px[1] = 0;
    led_commits = 0;
    g_wdt_feeds = 0;
    status_led_ms = 0;
    sim_safe_closed = true;
    wifi_up = true;
    rtk_head = rtk_tail = 0;
    fc_head = fc_tail = 0;
    sim_rx = MavParser{};
}

}  // namespace ranch

#endif  // RANCH_SIM

/* ==================== src/main.cpp ==================== */

// Entry point: parameters, the job table, and the task layout for one aircraft of
// the show.
//
// Shape of the concurrency, and why:
//  - one cooperative task at PRIO_SHOW runs the programme, the two serial drains
//    and the uplink. They share the aircraft's one snapshot of itself, and running
//    them in a known order is a smaller correctness risk on an airframe than a
//    mutex around it.
//  - the safety job runs every iteration as well as on its own rate: the external
//    watchdog wants an edge at least ten times a second, and the SAFE switch is
//    the one input that must never be sampled late.
//  - the position target is the only thing on this board with a deadline. The
//    console, the broker and the strip all have to be able to wait for it, which
//    is why the strip is latched inside the show job and the log is not written
//    there at all.
//  - the Arduino loop task is deleted rather than used, so the show loop gets a
//    real priority instead of the core's default.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(RANCH_SIM)
#include <Arduino.h>
#endif

#ifndef RANCH_FW_VERSION
#define RANCH_FW_VERSION "dev"
#endif

namespace ranch {
namespace {

Scheduler sched;
int job_safety = -1, job_show = -1, job_telem = -1, job_uplink = -1, job_health = -1;
int job_console = -1;
MavParser fc_parser;
uint32_t last_loop_ms = 0;
bool wifi_down = false;
uint32_t wifi_down_ms = 0;
bool safe_last = false;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char craft_tag[16] = "show-0";
uint16_t mqtt_port = MQTT_PORT;

void paramsLoad() {
    int32_t v = 0;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    // The station number is this aircraft's identity in the programme. It is a
    // parameter and not a pin strapping because the fleet is numbered by the
    // ground station, which has to be able to tell an airframe to change its
    // number after a swap on the pad.
    int32_t station = 0;
    if (nvGetI32("station", station)) showSetStation(static_cast<int>(station));

    // The surveyed launch point: the origin every baked path is measured from.
    int32_t lat = 0, lon = 0, alt = 0;
    if (nvGetI32("lat_e7", lat) && nvGetI32("lon_e7", lon)) {
        if (!nvGetI32("alt_mm", alt)) alt = 0;
        rtkSetOrigin(lat, lon, alt);
    }

    if (!nvGetStr("tag", craft_tag, sizeof(craft_tag)) || !craft_tag[0]) {
        std::snprintf(craft_tag, sizeof(craft_tag), "show-%d", showStation());
    }
}

void banner() {
    char line[112];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", craft_tag);
    w.add("id", showStation());
    w.add("pps", 1);
    w.endLine();
    consoleWrite(line, w.size());
}

// An event line, on the same source token as the boot banner and never on SHOW,
// so the dashboard cannot mistake an occurrence for a state: a frame that is
// parsed as telemetry would report the upload's position for the next five frames.
void event(const char* what, const char* detail) {
    char line[128];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", what);
    if (detail && *detail) w.add("why", detail);
    w.add("id", showStation());
    w.endLine();
    consoleWrite(line, w.size());
}

void health() {
    UplinkStats u{};
    telemetryStats(u);
    const ShowStatus& s = showStatus();
    char line[128];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "health");
    w.add("over", static_cast<int>(sched.totalOverruns()));
    w.add("runs", static_cast<int>(sched.totalRuns()));
    w.add("rxcrc", static_cast<int>(fc_parser.crcErrors()));
    w.add("rxdrop", static_cast<int>(fc_parser.dropped()));
    w.add("pub", static_cast<int>(u.published));
    w.add("mqtt", u.broker_connected ? 1 : 0);
    w.add("failed", static_cast<int>(u.mqtt_failed));
    w.add("set", static_cast<int>(s.setpoints));
    w.add("src", static_cast<int>(s.source));
    w.add("hacc", static_cast<int>(s.hacc_cm));
    w.add("ph", showPhaseName(s.phase));
    w.endLine();
    consoleWrite(line, w.size());
}

// The RTK receiver's UART. Bounded per call for the same reason the flight
// controller's is: a receiver that has been left talking at 5 Hz must not be able
// to starve the job that puts position targets on the wire.
void drainRtk() {
    uint8_t buf[128];
    for (int batch = 0; batch < 4; ++batch) {
        const int n = rtkRead(buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) rtkFeed(buf[i]);
    }
    rtkUpdate(halMillis());
}

void drainFc() {
    uint8_t buf[128];
    for (int batch = 0; batch < 4; ++batch) {
        const int n = fcRead(buf, sizeof(buf));
        if (n <= 0) break;
        MavMessage m{};
        for (int i = 0; i < n; ++i) {
            if (fc_parser.push(buf[i], m)) showOnMavlink(m);
        }
    }
}

// The ground station's grammar: a verb, optionally "verb=value". Anything else is
// ignored rather than guessed at, because a mis-parsed upload is an aircraft that
// flies a shape nobody drew.
//
// There is no "arm" verb here. The permission that lets the motors turn on this
// airframe is a pin a person shorts by hand, and a packet that could close it
// would make that pin decorative.
void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[16];
    size_t i = 0;
    while (i < len && i < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[i] = payload[i];
        ++i;
    }
    verb[i] = '\0';
    const char* value = nullptr;
    size_t vlen = 0;
    if (i < len && payload[i] == '=') {
        value = payload + i + 1;
        vlen = len - (i + 1);
    }

    if (std::strcmp(verb, "show") == 0 && value) {
        char why[24];
        const bool ok = showUpload(value, vlen, why, sizeof(why));
        event(ok ? "upload" : "upload-refused", ok ? "" : why);
    } else if (std::strcmp(verb, "station") == 0 && value) {
        char num[8];
        size_t n = 0;
        while (n < vlen && n < sizeof(num) - 1) { num[n] = value[n]; ++n; }
        num[n] = '\0';
        showSetStation(static_cast<int>(std::strtol(num, nullptr, 10)));
        event("station", "");
    } else if (std::strcmp(verb, "origin") == 0 && value) {
        char buf[64];
        size_t n = 0;
        while (n < vlen && n < sizeof(buf) - 1) { buf[n] = value[n]; ++n; }
        buf[n] = '\0';
        char* end = nullptr;
        const double lat = std::strtod(buf, &end);
        if (end != buf && *end == ',') {
            const char* p = end + 1;
            const double lon = std::strtod(p, &end);
            double alt = 0.0;
            if (end != p && *end == ',') alt = std::strtod(end + 1, nullptr);
            rtkSetOrigin(static_cast<int32_t>(lat * 1e7), static_cast<int32_t>(lon * 1e7),
                         static_cast<int32_t>(alt * 1000.0));
            nvSetI32("lat_e7", static_cast<int32_t>(lat * 1e7));
            nvSetI32("lon_e7", static_cast<int32_t>(lon * 1e7));
            nvSetI32("alt_mm", static_cast<int32_t>(alt * 1000.0));
            event("origin", "");
        } else {
            event("origin-refused", "WANT LAT,LON,ALT");
        }
    } else if (std::strcmp(verb, "start") == 0) {
        showRequestLaunch();
        event("launch", showReason());
    } else if (std::strcmp(verb, "land") == 0) {
        showLand();
        event("land", "");
    } else if (std::strcmp(verb, "led") == 0 && value) {
        char num[8];
        size_t n = 0;
        while (n < vlen && n < sizeof(num) - 1) { num[n] = value[n]; ++n; }
        num[n] = '\0';
        const long pct = std::strtol(num, nullptr, 10);
        if (pct >= 20 && pct <= 100) {
            nvSetI32("led_pct", static_cast<int32_t>(pct));
            event("led", "");
        } else {
            event("led-refused", "WANT 20..100");
        }
    }
    // Any command at all is evidence the ground station is awake, which is what
    // the link timeout counts on. A heartbeat is a command with no arguments.
    showNoteGcsContact();
}

// The buzzer has two jobs and they sound different on purpose: a chirp on the pad
// for a pack that will not fly, and a steady tone in the air for an aircraft that
// is coming home by itself. A person on the ground cannot see which of twenty-four
// airframes has a low pack, and they can always hear one.
void sounds() {
    const ShowStatus& s = showStatus();
    const bool airborne = s.phase == SHOW_CLIMB || s.phase == SHOW_MOVE || s.phase == SHOW_HOLD
                       || s.phase == SHOW_HOVER
                       || s.phase == SHOW_RETURN || s.phase == SHOW_AT_SLOT;
    const bool lost = !s.link_up || s.source == POS_NONE;
    bool on = false;
    if (airborne && lost) on = true;
    else if (!airborne && (s.batt_pct < static_cast<float>(SHOW_BATT_WARN_PCT)
                           || (s.phase == SHOW_REFUSED && lost))) {
        on = ((halMillis() / 200u) & 1u) != 0u;
    }
    setBuzzer(on);
}

void supervision() {
    // A link that has been down for half a minute is not a fade: ask the stack to
    // re-associate instead of waiting for its own backoff.
    const bool up = wifiUp();
    if (up) {
        wifi_down = false;
        return;
    }
    if (!wifi_down) {
        wifi_down = true;
        wifi_down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (wifi_down_ms + 30000u)) >= 0) {
        wifi_down_ms = halMillis();
        wifiReconnect();
    }
}

// The USB console: how a programme gets into an aircraft that is not on the radio
// yet. plan.h promises the upload "has to survive a technician typing it by hand",
// and the simulator's serial monitor delivers exactly that -- bytes into UART0 RX.
// The line is assembled here rather than in the HAL so the aircraft's UART and the
// sandbox's queue are read by one parser.
constexpr size_t CONSOLE_LINE_MAX = SHOW_PLAN_MAX_TEXT + 32;
char console_line[CONSOLE_LINE_MAX];
size_t console_len = 0;
bool console_overflow = false;

void consoleJob() {
    char buf[64];
    const int n = consoleRead(buf, sizeof(buf));
    for (int i = 0; i < n; ++i) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            if (console_len && !console_overflow) onGroundCommand(console_line, console_len);
            console_len = 0;
            console_overflow = false;
            continue;
        }
        if (console_len < sizeof(console_line) - 1) {
            console_line[console_len++] = buf[i];
        } else {
            // Longer than anything this build takes: keep reading to the end of the
            // line so the next one starts clean, but never run half of this one.
            console_overflow = true;
        }
    }
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;              // a stalled loop must not teleport

    drainRtk();
    drainFc();

    if (sched.due(job_safety, dt)) {
        const bool closed = safeSwitchClosed();
        if (closed != safe_last) {
            safe_last = closed;
            event(closed ? "safe-released" : "safe-asserted", "SAFE SWITCH");
        }
        feedWatchdog();
        sounds();
        // The bench indicator: one flash per second while it waits, two while it
        // flies, none while it is safe. None of that is on the show strip: the
        // crew's lamp must stay readable with the programme's pixels dark.
        const ShowStatus& s = showStatus();
        const uint32_t period = (s.phase == SHOW_MOVE || s.phase == SHOW_HOLD) ? 250u : 1000u;
        setStatusLed(!s.safe_closed ? false : ((now / period) & 1u) != 0u);
    }
    if (sched.due(job_show, dt)) {
        showTick(sched.elapsed(job_show));
    }
    if (sched.due(job_telem, dt)) {
        telemetryPublish(showStatus());
    }
    if (sched.due(job_uplink, dt)) telemetryService();
    if (sched.due(job_health, dt)) health();
    if (sched.due(job_console, dt)) consoleJob();
    supervision();
}

#if !defined(RANCH_SIM)
void loopTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(SHOW_LOOP_MS);
    }
}
#endif

}  // namespace

// The host sandbox drives the command channel the way the ground station does, so
// "upload, then start" is exercised as traffic rather than as a timer nobody had to
// press. Outside the anonymous namespace because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }
#endif

void appSetup() {
    halInit();
    last_loop_ms = halMillis();
    wifi_down = false;
    wifi_down_ms = 0;
    fc_parser = MavParser{};
    sched.clear();

    rtkInit();
    showInit();
    telemetryInit();
    paramsLoad();

    // Ask the receiver for the one message this aircraft flies on, at the rate the
    // path was baked at. If it never answers, the position stays absent and the
    // programme never starts, which is the only safe reading of a silent UART.
    uint8_t cfg[32];
    const size_t n1 = rtkCfgNavPvt(cfg, sizeof(cfg), 5);
    if (n1) rtkWrite(cfg, n1);
    const size_t n2 = rtkCfgNavRate(cfg, sizeof(cfg), 200, 1);
    if (n2) rtkWrite(cfg, n2);

    safe_last = safeSwitchIsClosed();
    banner();
    telemetrySetCommandHandler(onGroundCommand);

    job_safety = sched.add("safety", 20);
    job_show = sched.add("show", 1000 / SHOW_SETPOINT_HZ);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_uplink = sched.add("uplink", 100);
    job_health = sched.add("health", 5000);
    job_console = sched.add("console", 20);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, craft_tag);

#if !defined(RANCH_SIM)
    xTaskCreatePinnedToCore(loopTask, "ranch", STACK_SHOW, nullptr, PRIO_SHOW, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
    halSimPump();
    runJobs();
    halDelayMs(SHOW_LOOP_MS);
#endif
}

}  // namespace ranch

#if !defined(RANCH_SIM)
void setup() { ranch::appSetup(); }
void loop() { vTaskDelete(NULL); }
#endif

/* ==================== simulator entry point ==================== */

void setup() { ranch::appSetup(); }
void loop() { ranch::appLoop(); }
