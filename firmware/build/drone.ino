// GENERATED FILE - do not edit, edit the project and re-run:
//   node tools/bundle.mjs
// Source: firmware/drone + firmware/lib  (18 files, 130.3 KB before bundling)
// The simulated airframe in hal_sim.cpp stands in for the flight controller and the camera.
//
// Build the same code for hardware with:  pio run -d firmware/drone

#define RANCH_SIM 1

/* ==================== src/board.h ==================== */

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

/* ==================== src/hal.h ==================== */

// Hardware abstraction. Two implementations: hal_esp32.cpp for the aircraft and
// hal_sim.cpp for the browser build, which compiles the same mission,
// failsafe and telemetry logic against a synthetic airframe.
//
// Nothing in this header allocates, and every call is safe to make from any
// task.

#include <cstdint>
#include <cstddef>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);

// Console sink for telemetry and the boot banner: USB-CDC on the aircraft,
// stdout in the browser and host builds. Never blocks on a reader.
void consoleWrite(const char* data, size_t len);

// Flight controller serial link (MAVLink).
int  fcAvailable();
int  fcRead(uint8_t* buf, size_t cap);
void fcWrite(const uint8_t* buf, size_t len);

// Safety outputs. setSafe(true) drives the FC kill input so the motors cannot
// start; the external watchdog pin must keep toggling while armed.
void setSafe(bool safe);
bool safeRequested();
void armSwitchClosed();          // sample the physical arm switch now
bool armSwitchIsClosed();        // the most recent sample
void feedWatchdog();
void setStatusLed(uint8_t pattern);

// Analogy.
float batteryVolts();            // pack voltage, divider corrected
float batteryCellVolts();        // pack / cell count
float busCurrentAmps();

// Non-volatile parameters. Return false when the key is absent or the stored
// type does not match.
bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

// Network. The sim back-end reports "connected" with a fixed RSSI so the
// telemetry path can be exercised without a radio.
bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

// Mass storage for the flight log. Returns false when no card is present, in
// which case the logger keeps a ring buffer in RAM instead.
bool sdOpen(const char* path);
bool sdAppend(const char* data, size_t len);
void sdClose();

// Reset reason string, for the boot banner and the first telemetry frame.
const char* resetReason();

#if defined(RANCH_SIM)
// Advance the synthetic airframe by the real (browser) or virtual (host test)
// clock. The aircraft build has no equivalent: the FC is a separate box there.
void halSimPump();

// What the simulated flight controller actually did, so a host test can assert
// on "the FC acknowledged 8 items" instead of only on "no crash".
struct SimCounters {
    uint32_t rx_overflow;
    uint32_t frames_emitted;
    uint32_t mavlink_crc_errors;
    uint32_t mavlink_dropped;
    uint32_t fc_items;
    uint32_t fc_captures;
    uint32_t wdt_feeds;
    uint32_t card_bytes;
    uint32_t led_pattern;
    bool fc_acked_mission;
};
void halSimCounters(SimCounters& out);
#endif

}  // namespace ranch

/* ==================== src/mavlink.h ==================== */

// MAVLink v2 codec: framing, CRC, and the subset of messages the companion
// computer needs. Written by hand rather than generated so the build has no
// codegen step; the field offsets below are the wire order after the v2
// size-descending reordering rule, and each one is commented with the XML
// order it came from.
//
// No dynamic allocation, no exceptions, and a corrupt frame can never advance
// the parser past the buffer.

#include <cstddef>
#include <cstdint>

namespace ranch {

constexpr uint8_t MAV_STX = 0xFD;
constexpr size_t  MAV_MAX_PAYLOAD = 64;
constexpr size_t  MAV_FRAME_MAX = 6 + MAV_MAX_PAYLOAD + 2;

enum MavMsg : uint32_t {
    MSG_HEARTBEAT              = 0,
    MSG_SYS_STATUS             = 1,
    MSG_GPS_RAW_INT            = 24,
    MSG_ATTITUDE               = 30,
    MSG_COMMAND_LONG           = 17,
    MSG_COMMAND_ACK            = 77,
    MSG_MISSION_COUNT          = 49,
    MSG_MISSION_REQUEST_INT    = 51,
    MSG_MISSION_ITEM_INT       = 73,
    MSG_MISSION_ACK            = 47,
    MSG_MISSION_CURRENT        = 42,
    MSG_MISSION_START          = 23,
    MSG_BATTERY_STATUS         = 147,
    MSG_RADIO_STATUS           = 185,
    MSG_STATUSTEXT             = 253,
    MSG_GLOBAL_POSITION_INT    = 33,
    MSG_VFR_HUD                = 74,
};

// CRC_EXTRA values from the common dialect.
uint8_t mavCrcExtra(uint32_t msgId);

// MAV_CMD subset used by the mission service.
enum MavCmd : uint16_t {
    CMD_NAV_WAYPOINT       = 16,
    CMD_NAV_LOITER_UNLIM   = 17,
    CMD_NAV_RETURN_TO_LAUNCH = 20,
    CMD_NAV_LAND           = 21,
    CMD_NAV_TAKEOFF        = 22,
    CMD_MISSION_START      = 23,
    CMD_DO_SET_MODE        = 176,
    CMD_DO_SET_RELAY       = 181,
    CMD_DO_DIGICAM_CONTROL = 203,
    CMD_COMPONENT_ARM_DISARM = 400,
    CMD_NAV_VTOL_TAKEOFF   = 84,
};

// ArduCopter custom_mode values carried by DO_SET_MODE. These belong to the FC
// firmware, not to MAVLink, so they are build-time configuration here: change
// them from the FC's own Mode page if the build differs. The simulator honours
// the same numbers, so a wrong one fails on the bench as "mode never changed"
// rather than in the air.
#ifndef FC_MODE_AUTO
#define FC_MODE_AUTO     3
#endif
#ifndef FC_MODE_LOITER
#define FC_MODE_LOITER   5
#endif
#ifndef FC_MODE_RTL
#define FC_MODE_RTL      6
#endif
#ifndef FC_MODE_LAND
#define FC_MODE_LAND     9
#endif

// HEARTBEAT accessors, so the bit twiddling lives once.
inline bool hbArmed(uint8_t base_mode) { return (base_mode & 0x80) != 0; }   // MAV_MODE_FLAG_SAFETY_ARMED
inline bool hbCustomEnabled(uint8_t base_mode) { return (base_mode & 1) != 0; }

enum MavFrame : uint8_t {
    FRAME_GLOBAL_INT = 0,   // absolute WGS84, int32 1e7
    FRAME_GLOBAL_RELATIVE_ALT_INT = 6,
    FRAME_MISSION   = 2,
};

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
    void f32(float v)    { uint32_t u; static_assert(sizeof(u) == sizeof(v), "float size"); __builtin_memcpy(&u, &v, 4); u32(u); }

    size_t size() const { return n_; }
    bool ok() const { return n_ <= cap_; }

private:
    void put(uint8_t b) { if (n_ < cap_) p_[n_++] = b; else n_ = cap_ + 1; }
    uint8_t* p_;
    size_t cap_;
    size_t n_ = 0;
};

// Assembles a complete v2 frame into out_ and returns its length, or 0 when the
// message table does not know the id (a silently mis-CRCed frame is worse than
// a refused one).
class MavFrameBuilder {
public:
    MavFrameBuilder(uint8_t sysid, uint8_t compid) : sys_(sysid), comp_(compid) {}

    size_t build(uint32_t msgId, const uint8_t* payload, size_t len, uint8_t* out, size_t cap);
    uint8_t nextSeq() { return seq_++; }

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

// --- typed helpers for the messages this firmware actually uses -------------

struct HeartbeatTx {
    uint32_t custom_mode;
    uint8_t type, autopilot, base_mode, system_status;
    void pack(MavWriter& w) const;
};

struct CommandLongTx {
    float p[7];
    uint16_t command;
    uint8_t target_sys, target_comp, confirmation;
    void pack(MavWriter& w) const;
};

struct MissionItemIntTx {
    int32_t x, y;
    float z, param1, param2, param3, param4;
    uint16_t seq, command;
    uint8_t frame, current, autocontinue, mission_type;
    uint8_t target_sys, target_comp;
    void pack(MavWriter& w) const;
};

// Decoded view of an incoming GPS_RAW_INT.
struct GnssFix {
    uint8_t fix_type;
    uint8_t satellites;
    int32_t lat_e7, lon_e7;
    int32_t alt_mm;
    uint16_t hdop_cm;
    bool valid;
};

GnssFix decodeGpsRaw(const MavMessage& m);

struct Attitude {
    float roll_deg, pitch_deg, yaw_deg;
    uint32_t time_boot_ms;
};
Attitude decodeAttitude(const MavMessage& m);

struct Battery {
    uint16_t mv_per_cell[10];
    uint8_t cells;
    int16_t remaining_pct;
    int16_t current_centi;
};
Battery decodeBattery(const MavMessage& m);

}  // namespace ranch

/* ==================== src/mavlink.cpp ==================== */


namespace ranch {

uint8_t mavCrcExtra(uint32_t id) {
    switch (id) {
        case MSG_HEARTBEAT:           return 50;
        case MSG_SYS_STATUS:          return 124;
        case MSG_GPS_RAW_INT:         return 24;
        case MSG_ATTITUDE:            return 39;
        case MSG_COMMAND_LONG:        return 132;
        case MSG_COMMAND_ACK:         return 101;
        case MSG_MISSION_COUNT:       return 142;
        case MSG_MISSION_REQUEST_INT: return 152;
        case MSG_MISSION_ITEM_INT:    return 15;
        case MSG_MISSION_ACK:         return 153;
        case MSG_MISSION_CURRENT:     return 28;
        case MSG_MISSION_START:       return 101;
        case MSG_BATTERY_STATUS:      return 208;
        case MSG_RADIO_STATUS:        return 130;
        case MSG_STATUSTEXT:          return 83;
        case MSG_GLOBAL_POSITION_INT: return 104;
        case MSG_VFR_HUD:             return 20;
        default:                      return 0xFF;   // unknown, refuse to send
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
        case Crc1: {
            buf_[9 + need_] = b;
            st_ = Crc2;
            return false;
        }
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
    w.u8(3);                            // mavlink_version
}

void CommandLongTx::pack(MavWriter& w) const {
    for (int i = 0; i < 7; ++i) w.f32(p[i]);
    w.u16(command);
    w.u8(target_sys);
    w.u8(target_comp);
    w.u8(confirmation);
}

// Wire order after the size-descending rule:
//   4-byte: param1 param2 param3 param4 x y z
//   2-byte: seq command
//   1-byte: target_system target_component frame current autocontinue mission_type
void MissionItemIntTx::pack(MavWriter& w) const {
    w.f32(param1); w.f32(param2); w.f32(param3); w.f32(param4);
    w.i32(x); w.i32(y); w.f32(z);
    w.u16(seq); w.u16(command);
    w.u8(target_sys); w.u8(target_comp);
    w.u8(frame); w.u8(current); w.u8(autocontinue); w.u8(mission_type);
}

GnssFix decodeGpsRaw(const MavMessage& m) {
    GnssFix g{};
    // 8-byte time_usec first, then 4-byte lat lon alt alt_msl, then 2-byte
    // eph epv vel cog, then 1-byte fix_type satellites_visible ...
    if (m.len < 8 + 16 + 8 + 2) return g;
    g.lat_e7 = m.i32At(8);
    g.lon_e7 = m.i32At(12);
    g.alt_mm = m.i32At(16);
    const int32_t alt_msl = m.i32At(20);
    g.hdop_cm = m.u16At(24);
    g.fix_type = m.u8At(32);
    g.satellites = m.u8At(33);
    g.valid = g.fix_type >= 2 && alt_msl != 0x7FFFFFFF;
    return g;
}

Attitude decodeAttitude(const MavMessage& m) {
    Attitude a{};
    if (m.len < 28) return a;
    a.time_boot_ms = static_cast<uint32_t>(m.i32At(0));
    a.roll_deg = m.f32At(4) * 57.29578f;
    a.pitch_deg = m.f32At(8) * 57.29578f;
    a.yaw_deg = m.f32At(12) * 57.29578f;
    return a;
}

// BATTERY_STATUS wire order after the size-descending rule:
//   4-byte: current_consumed(hB) energy_consumed(cWh)
//   2-byte: temperature(0.1degC) voltages[10](mV) [current(cA)]
//   1-byte: id battery_function type remaining(int8 %) [charge_state]
// The bracketed fields are extensions, and v2 truncates trailing absent
// fields, so a 34-byte base frame puts `remaining` at 33 while a 36/37-byte
// frame puts it at 35. Reading a fixed offset silently returns the battery id
// as a state of charge.
Battery decodeBattery(const MavMessage& m) {
    Battery b{};
    b.remaining_pct = -1;                     // MAV_UNKNOWN: never "empty"
    const bool has_ext = m.len >= 36;
    if (m.len < (has_ext ? 36u : 34u)) return b;
    for (int i = 0; i < 10; ++i) {
        b.mv_per_cell[i] = m.u16At(10 + i * 2);
        if (b.mv_per_cell[i] != 0xFFFF && b.mv_per_cell[i] != 0) b.cells++;
    }
    b.remaining_pct = static_cast<int16_t>(static_cast<int8_t>(m.u8At(has_ext ? 35 : 33)));
    if (has_ext) b.current_centi = static_cast<int16_t>(m.u16At(30));
    return b;
}

}  // namespace ranch

/* ==================== src/video.h ==================== */

// Video downlink (图传): camera capture task feeding an MJPEG stream plus a
// snapshot sink for the mission's photo triggers.
//
// Design notes that matter on a 2.4 GHz link shared with telemetry:
//  - the camera task owns the frame buffer and never blocks on a client; a
//    client that cannot drain the socket loses frames and is counted
//  - quality and frame size are stepped down when the encoder cannot keep the
//    target rate, stepped back up only after a stable window
//  - the payload trigger takes a frame from the same buffer, so a capture never
//    stalls the stream

#include <cstddef>
#include <cstdint>

namespace ranch {

struct VideoStats {
    uint32_t frames;
    uint32_t dropped;        // no consumer, or stale before send
    uint32_t bytes;
    uint32_t client_rejects; // back-pressured clients cut loose
    uint16_t fps;
    uint16_t quality;        // esp32-cam quality actually in use
    uint16_t width;
    uint16_t height;
    bool     streaming;
    bool     recording;
    int8_t   error;          // last camera driver status, <0 is a fault
};

struct Snapshot {
    const uint8_t* data;
    size_t len;
    uint32_t seq;
    uint32_t taken_ms;
};

bool videoInit();
void videoTaskLoop();                 // runs on its own FreeRTOS task
bool videoLatest(Snapshot& out);      // borrows the buffer; copy before returning
void videoRelease();                  // give the frame back to the encoder
bool videoSaveSnapshot(const char* path);

void videoSetStreaming(bool on);
void videoSetRecording(bool on);
void videoStats(VideoStats& out);

}  // namespace ranch

/* ==================== src/video.cpp ==================== */


#if defined(RANCH_SIM)

// --- simulated back-end ----------------------------------------------------
// The browser build has no camera. It still has to exercise the mission's photo
// triggers and the telemetry fields, so the encoder is replaced by a counter.
// The frame rate is measured from the calls that actually happen, not assumed
// from the target, so a starved mission loop shows up as a low fps number here
// exactly as it would on the aircraft.
namespace ranch {
namespace {
VideoStats vid{};
uint32_t g_window_start = 0;
uint32_t g_window_frames = 0;
uint32_t g_seq = 0;
}

bool videoInit() {
    vid.width = RANCH_VIDEO_W;
    vid.height = RANCH_VIDEO_H;
    vid.quality = RANCH_VIDEO_Q;
    vid.streaming = true;
    g_window_start = halMillis();
    return true;
}

void videoTaskLoop() {}

bool videoLatest(Snapshot& out) {
    if (!vid.streaming) return false;
    static uint8_t hdr[4] = {0xFF, 0xD8, 0xFF, 0xD9};
    out.data = hdr;
    out.len = sizeof(hdr);
    out.seq = ++g_seq;
    out.taken_ms = halMillis();
    vid.frames++;
    vid.bytes += out.len;
    g_window_frames++;
    const uint32_t span = out.taken_ms - g_window_start;
    if (span >= 1000) {
        vid.fps = static_cast<uint16_t>(g_window_frames * 1000u / span);
        g_window_start = out.taken_ms;
        g_window_frames = 0;
    }
    return true;
}

void videoRelease() {}

bool videoSaveSnapshot(const char*) { return false; }

void videoSetStreaming(bool on) { vid.streaming = on; }
void videoSetRecording(bool on) { vid.recording = on; }

void videoStats(VideoStats& out) {
    out = vid;
    if (!vid.streaming) out.fps = 0;
}
}  // namespace ranch

#else

#include <cstring>

namespace ranch {
namespace {

VideoStats vid{};
volatile bool g_streaming = true;
volatile bool g_recording = false;
uint32_t g_window_start = 0;
uint32_t g_window_frames = 0;
uint8_t g_bad_windows = 0;
uint8_t g_good_windows = 0;
camera_fb_t* g_last = nullptr;
uint32_t g_last_seq = 0;

bool applyProfile(uint16_t w, uint16_t h, uint8_t q) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    s->set_framesize(s, w <= 320 ? FRAMESIZE_QVGA : FRAMESIZE_VGA);
    s->set_quality(s, q);
    vid.width = w;
    vid.height = h;
    vid.quality = q;
    return true;
}

// Rate control: the encoder is the bottleneck, not the socket. Three bad
// windows degrades quality one step, ten good windows buys one step back. The
// hysteresis is deliberate so the picture does not pump on a marginal link.
void adaptRate(uint32_t now) {
    const uint32_t span = now - g_window_start;
    if (span < 2000) return;
    const uint32_t fps = (g_window_frames * 1000u) / (span ? span : 1);
    vid.fps = static_cast<uint16_t>(fps);
    if (fps + 2 < VIDEO_FPS_TARGET) {
        g_bad_windows++;
        g_good_windows = 0;
        if (g_bad_windows >= 3) {
            g_bad_windows = 0;
            const uint16_t q = vid.quality < 40 ? vid.quality + 4 : 40;
            applyProfile(vid.width, vid.height, q);
        }
    } else {
        g_good_windows++;
        g_bad_windows = 0;
        if (g_good_windows >= 10 && vid.quality > 10) {
            g_good_windows = 0;
            applyProfile(vid.width, vid.height, vid.quality - 2);
        }
    }
    g_window_start = now;
    g_window_frames = 0;
}

void onStream(uint32_t now, size_t frame_bytes) {
    vid.frames++;
    g_window_frames++;
    vid.bytes += static_cast<uint32_t>(frame_bytes);
    adaptRate(now);
}

}  // namespace

bool videoInit() {
    camera_config_t cfg{};
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_PIN_D0;
    cfg.pin_d1 = CAM_PIN_D1;
    cfg.pin_d2 = CAM_PIN_D2;
    cfg.pin_d3 = CAM_PIN_D3;
    cfg.pin_d4 = CAM_PIN_D4;
    cfg.pin_d5 = CAM_PIN_D5;
    cfg.pin_d6 = CAM_PIN_D6;
    cfg.pin_d7 = CAM_PIN_D7;
    cfg.pin_xclk = CAM_PIN_XCLK;
    cfg.pin_pclk = CAM_PIN_PCLK;
    cfg.pin_pwdn = CAM_PIN_PWDN;
    cfg.pin_reset = CAM_PIN_RESET;
    cfg.pin_sccb_sda = CAM_PIN_SIOD;
    cfg.pin_sccb_scl = CAM_PIN_SIOC;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAMESIZE_VGA;
    cfg.jpeg_quality = RANCH_VIDEO_Q;
    cfg.fb_count = 2;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        // PSRAM-less boards fall back to a single internal buffer at QVGA.
        cfg.fb_location = CAMERA_FB_IN_DRAM;
        cfg.fb_count = 1;
        cfg.frame_size = FRAMESIZE_QVGA;
        err = esp_camera_init(&cfg);
        vid.error = static_cast<int8_t>(err);
    }
    applyProfile(640, 480, RANCH_VIDEO_Q);
    vid.streaming = true;
    g_window_start = halMillis();
    return err == ESP_OK;
}

bool videoLatest(Snapshot& out) {
    if (!g_streaming) return false;
    if (g_last) {
        // A frame older than two periods means the consumer is not draining;
        // drop it rather than sending stale video.
        if (halMillis() - g_last->timestamp.tv_sec * 1000u > 250) {
            vid.dropped++;
            return false;
        }
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        vid.error = -1;
        vid.dropped++;
        return false;
    }
    if (fb->len > RANCH_VIDEO_MAX_FRAME_KB * 1024u) {
        esp_camera_fb_return(fb);
        vid.dropped++;
        vid.error = -2;
        return false;
    }
    g_last = fb;
    g_last_seq++;
    out.data = fb->buf;
    out.len = fb->len;
    out.seq = g_last_seq;
    out.taken_ms = halMillis();
    onStream(out.taken_ms, fb->len);
    return true;
}

void videoRelease() {
    if (g_last) {
        esp_camera_fb_return(g_last);
        g_last = nullptr;
    }
}

void videoSetStreaming(bool on) { g_streaming = on; if (!on) videoRelease(); }
void videoSetRecording(bool on) { g_recording = on; }

bool videoSaveSnapshot(const char* path) {
    if (!g_recording) return false;
    Snapshot s{};
    if (!videoLatest(s)) return false;
    const bool ok = sdOpen(path) && sdAppend(reinterpret_cast<const char*>(s.data), s.len);
    sdClose();
    videoRelease();
    return ok;
}

void videoStats(VideoStats& out) {
    out = vid;
    out.streaming = g_streaming;
    out.recording = g_recording;
}

// The stream task is a plain multipart writer: one client, non-blocking
// semantics through the socket send timeout, and a reject counter so the ground
// app can see it is being starved rather than silently getting a frozen image.
void videoTaskLoop() {
    for (;;) {
        if (!wifiUp()) {
            halDelayMs(500);
            continue;
        }
        static httpd_handle_t srv = nullptr;
        if (!srv) {
            httpd_config_t c = HTTPD_DEFAULT_CONFIG();
            c.server_port = VIDEO_PORT;
            c.task_priority = PRIO_VIDEO;
            c.stack_size = STACK_VIDEO;
            c.max_open_sockets = 2;
            if (httpd_start(&srv, &c) != ESP_OK) {
                vid.client_rejects++;
                halDelayMs(1000);
                continue;
            }
        }
        // esp_http_server runs its own task; this loop only supervises it and
        // keeps the watchdog fed while the camera is idle.
        feedWatchdog();
        halDelayMs(1000);
    }
}

}  // namespace ranch

#endif

/* ==================== ../lib/failsafe.h ==================== */

// Failsafe decision table. Platform neutral and deliberately side-effect free:
// it turns ages and sensor quality into one action plus one reason, and the
// caller decides what that action means on its own airframe.
//
// Priority is fixed, because a battery that is nearly empty must not be
// outranked by a glitched radio timer:
//   terminate > rtl > hover > continue

#include <cstdint>

namespace ranch {

enum class Action : uint8_t { Continue, Hover, Rtl, LandNow, Terminate };

inline const char* actionName(Action a) {
    switch (a) {
        case Action::Continue:  return "CONTINUE";
        case Action::Hover:     return "HOVER";
        case Action::Rtl:       return "RTL";
        case Action::LandNow:   return "LAND";
        case Action::Terminate: return "KILL";
    }
    return "?";
}

struct FailsafeConfig {
    uint32_t rc_timeout_ms;         // stick input staleness that counts as loss
    uint32_t gcs_timeout_ms;        // MAVLink heartbeat loss from the ground
    uint32_t link_debounce_ms;      // how long a loss must persist to act
    uint32_t geofence_debounce_ms;  // breaches are debounced: GPS can jump
    uint8_t  min_fix_type;          // 3 = 3D fix required to keep flying
    uint8_t  min_satellites;
    float    max_hdop;
    uint8_t  battery_rtl_pct;
    uint8_t  battery_land_pct;
    uint8_t  battery_kill_pct;
    float    cell_v_min;            // sag below this is a hard land, not RTL
};

constexpr FailsafeConfig FAILSAFE_DEFAULTS{
    /*rc_timeout*/       250,
    /*gcs_timeout*/      1500,
    /*link_debounce*/    400,
    /*geofence_debounce*/3000,
    /*min_fix*/          3,
    /*min_sats*/         8,
    /*max_hdop*/         2.2f,
    /*batt_rtl*/         25,
    /*batt_land*/        12,
    /*batt_kill*/        5,
    /*cell_v_min*/       3.30f,
};

struct FailsafeInput {
    uint32_t rc_age_ms;
    uint32_t gcs_age_ms;
    bool     in_flight;
    bool     geofence_breach;
    uint8_t  fix_type;
    uint8_t  satellites;
    float    hdop;
    uint8_t  battery_pct;
    float    cell_v_min;
    bool     baro_ok;
    bool     imu_ok;
    bool     gps_ok;
};

struct FailsafeDecision {
    Action action;
    char   reason[24];
    bool   armed_block;    // pre-flight: refuse to arm at all
};

// Per-source debounce state. Held by the caller so the module stays pure and a
// single instance can serve several vehicles in a test harness.
struct FailsafeState {
    uint32_t rc_bad_ms;
    uint32_t gcs_bad_ms;
    uint32_t fence_bad_ms;
    uint32_t gnss_bad_ms;
};

inline void failsafeReset(FailsafeState& s) {
    s.rc_bad_ms = s.gcs_bad_ms = s.fence_bad_ms = s.gnss_bad_ms = 0;
}

inline void copyReason(char (&dst)[24], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 23; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline FailsafeDecision failsafeEvaluate(const FailsafeConfig& cfg, FailsafeState& st,
                                         const FailsafeInput& in, uint32_t dt_ms) {
    FailsafeDecision out{Action::Continue, "", false};

    // Anything the sensors have not reported yet arrives as a sentinel: a
    // percentage above 100, or a cell voltage of exactly zero. Neither may be
    // read as "empty", or a bench rig with no battery would never arm and a
    // cold-start aircraft would declare a critical fault on the first tick.
    const bool batt_known = in.battery_pct <= 100;

    st.rc_bad_ms   = in.rc_age_ms   > cfg.rc_timeout_ms   ? st.rc_bad_ms + dt_ms   : 0;
    st.gcs_bad_ms  = in.gcs_age_ms  > cfg.gcs_timeout_ms  ? st.gcs_bad_ms + dt_ms  : 0;
    st.fence_bad_ms = in.geofence_breach ? st.fence_bad_ms + dt_ms : 0;
    const bool gnss_bad = !in.gps_ok || in.fix_type < cfg.min_fix_type ||
                          in.satellites < cfg.min_satellites || in.hdop > cfg.max_hdop;
    st.gnss_bad_ms = gnss_bad ? st.gnss_bad_ms + dt_ms : 0;

    // --- hard stops, in flight and on the ground -----------------------------
    if (!in.imu_ok) {
        out.action = Action::Terminate;
        copyReason(out.reason, "IMU FAULT");
        out.armed_block = true;
        return out;
    }
    if (in.cell_v_min > 0.0f && in.cell_v_min < cfg.cell_v_min) {
        out.action = in.in_flight ? Action::LandNow : Action::Continue;
        copyReason(out.reason, "CELL SAG");
        out.armed_block = !in.in_flight;
        return out;
    }
    if (batt_known && in.battery_pct <= cfg.battery_kill_pct) {
        out.action = in.in_flight ? Action::LandNow : Action::Continue;
        copyReason(out.reason, "BATT CRITICAL");
        out.armed_block = !in.in_flight;
        return out;
    }

    // --- pre-flight gates ----------------------------------------------------
    if (!in.in_flight) {
        if (!in.baro_ok) { out.armed_block = true; copyReason(out.reason, "BARO FAULT"); }
        else if (gnss_bad) { out.armed_block = true; copyReason(out.reason, "GNSS POOR"); }
        else if (st.rc_bad_ms > cfg.link_debounce_ms) {
            out.armed_block = true;
            copyReason(out.reason, "RC LOST");
        }
        if (out.armed_block) out.action = Action::Continue;
        return out;
    }

    // --- in flight, least severe last ---------------------------------------
    if (st.fence_bad_ms > cfg.geofence_debounce_ms) {
        out.action = Action::Rtl;
        copyReason(out.reason, "GEOFENCE");
        return out;
    }
    if (st.gnss_bad_ms > 2000) {
        out.action = Action::Rtl;
        copyReason(out.reason, "GNSS LOST");
        return out;
    }
    if (batt_known && in.battery_pct <= cfg.battery_rtl_pct) {
        out.action = Action::Rtl;
        copyReason(out.reason, "BATT LOW");
        return out;
    }
    if (st.rc_bad_ms > cfg.link_debounce_ms) {
        out.action = Action::Rtl;
        copyReason(out.reason, "RC LOST");
        return out;
    }
    if (st.gcs_bad_ms > cfg.gcs_timeout_ms * 4) {
        // The ground station is gone but the RC still flies the aircraft, so
        // hold position and let the pilot decide rather than auto-RTL.
        out.action = Action::Hover;
        copyReason(out.reason, "GCS LOST");
        return out;
    }

    return out;
}

}  // namespace ranch

/* ==================== ../lib/mission_planner.h ==================== */

// Waypoint mission planner and geofence. Platform neutral: no Arduino, no
// ESP-IDF, no floating-point library calls beyond sqrt/atan2.
//
// Coordinates are local planar metres in a north-east-up frame anchored at the
// home position, which is what a mission upload is reduced to after the GNSS
// fix is validated. Keeping the planner in local metres removes all the
// latitude/longitude rounding from the state machine.

#include <cmath>
#include <cstdint>

namespace ranch {

constexpr uint8_t MISSION_MAX_WAYPOINTS = 32;
constexpr float   MISSION_DEFAULT_ARRIVAL_M = 2.0f;
constexpr float   GEOFENCE_RADIUS_M = 180.0f;
constexpr float   GEOFENCE_ALT_CEIL_M = 120.0f;
constexpr float   GEOFENCE_ALT_FLOOR_M = 1.0f;

struct Waypoint {
    float north;
    float east;
    float alt;
    // Absolute WGS84 in 1e7 integer degrees, filled in by the uploader from the
    // latched home. The planner itself never reads these; keeping both in one
    // struct means a waypoint cannot be flown with a stale georeference.
    int32_t lat_e7;
    int32_t lon_e7;
    float dwell_s;      // time spent on arrival before advancing, 0 = fly-through
    uint8_t camera;     // 0 = none, 1 = still capture, 2 = start clip, 3 = stop clip
};

struct Mission {
    Waypoint items[MISSION_MAX_WAYPOINTS];
    uint8_t count;
    uint8_t cursor;
    float arrival_m;
    bool loop;
};

enum class Phase : uint8_t {
    Ground,     // motors off, on the pad
    Climbing,   // ascending to the cruise altitude
    Transit,    // flying the current leg
    Dwelling,   // on top of a waypoint, holding for the payload
    Rtl,        // returning to home
    Descending, // final approach
    Landed,     // touchdown, motors disarm pending
};

inline const char* phaseName(Phase p) {
    switch (p) {
        case Phase::Ground:     return "GROUND";
        case Phase::Climbing:   return "CLIMB";
        case Phase::Transit:    return "TRANSIT";
        case Phase::Dwelling:   return "DWELL";
        case Phase::Rtl:        return "RTL";
        case Phase::Descending: return "DESCEND";
        case Phase::Landed:     return "LANDED";
    }
    return "UNKNOWN";
}

struct PlanState {
    Phase phase;
    uint8_t target;        // waypoint index being flown
    float remaining_m;     // distance left on the current leg
    float dwell_left_s;    // countdown inside a waypoint
    uint32_t captures;     // payload triggers issued
    float cmd_alt;         // commanded altitude, owned by the planner
    bool mission_complete;
};

// Vertical and horizontal speed are commanded elsewhere (the FC does the
// closed loop); the planner only decides where the next setpoint is.
struct PlanInput {
    float north;
    float east;
    float alt;
    float cruise_ms;
    float climb_ms;
    // From the flight controller's heartbeat. The descent profile is planner
    // owned, but "down" is not a thing the planner can decide alone: a profile
    // that runs out before the aircraft does would otherwise report Landed with
    // the aircraft still in the air.
    bool in_flight;
};

struct Geofence {
    float radius_m;
    float ceil_m;
    float floor_m;
};

struct Breach {
    bool outside;
    bool too_high;
    bool too_low;
};

inline Breach checkGeofence(const Geofence& g, float home_n, float home_e,
                            float n, float e, float alt) {
    const float dn = n - home_n;
    const float de = e - home_e;
    Breach b{};
    b.outside = std::sqrt(dn * dn + de * de) > g.radius_m;
    b.too_high = alt > g.ceil_m;
    b.too_low = alt < g.floor_m;
    return b;
}

inline void missionReset(Mission& m) {
    m.count = 0;
    m.cursor = 0;
    m.arrival_m = MISSION_DEFAULT_ARRIVAL_M;
    m.loop = false;
}

// Returns false when the buffer is full, so a bad upload cannot walk off the
// end of the array.
inline bool missionAdd(Mission& m, const Waypoint& w) {
    if (m.count >= MISSION_MAX_WAYPOINTS) return false;
    m.items[m.count++] = w;
    return true;
}

// Starts the mission from the ground. Returns false when there is nothing to
// fly, so the caller can refuse the arm instead of entering an empty state.
inline bool missionStart(Mission& m, PlanState& s, float cruise_ms, float climb_ms) {
    if (m.count == 0) return false;
    m.cursor = 0;
    s.phase = Phase::Climbing;
    s.target = 0;
    s.remaining_m = 0.0f;
    s.dwell_left_s = 0.0f;
    s.mission_complete = false;
    (void)cruise_ms;
    (void)climb_ms;
    return true;
}

// Moves to the next waypoint, or ends/loops the mission. Kept separate so the
// transit and dwell branches cannot disagree about what "finished" means.
inline void advance(Mission& m, PlanState& s) {
    if (static_cast<uint8_t>(s.target + 1) >= m.count) {
        if (m.loop) {
            s.target = 0;
        } else {
            s.mission_complete = true;
            s.phase = Phase::Rtl;
        }
    } else {
        s.target = static_cast<uint8_t>(s.target + 1);
    }
    m.cursor = s.target;
}

// One planner tick. dt_s is the elapsed wall time since the previous tick.
// The horizontal setpoint is exposed through out_n/out_e so the caller can
// forward it to the FC as a position or velocity command.
inline void planStep(Mission& m, PlanState& s, const PlanInput& in,
                     float home_n, float home_e, float home_alt, float dt_s,
                     float& out_n, float& out_e, float& out_alt) {
    const Waypoint& wp = m.items[s.target < m.count ? s.target : 0];
    out_n = wp.north;
    out_e = wp.east;
    out_alt = wp.alt;

    const float dn = wp.north - in.north;
    const float de = wp.east - in.east;
    const float dist = std::sqrt(dn * dn + de * de);
    s.remaining_m = dist;

    switch (s.phase) {
        case Phase::Ground:
            out_alt = home_alt;
            break;

        case Phase::Climbing: {
            // Climb to the first waypoint's altitude before committing to the
            // leg, which is what keeps the aircraft clear of the shed roofline.
            out_alt = wp.alt;
            const float climb_rate = in.climb_ms > 0.0f ? in.climb_ms : 2.0f;
            if (in.alt >= wp.alt - 0.5f) {
                s.phase = Phase::Transit;
            } else {
                out_n = in.north;
                out_e = in.east;
                (void)climb_rate;
            }
            break;
        }

        case Phase::Transit:
            if (dist <= m.arrival_m) {
                if (wp.dwell_s > 0.0f) {
                    // Hold here first; the trigger fires on arrival and the
                    // cursor only moves when the dwell expires.
                    s.phase = Phase::Dwelling;
                    s.dwell_left_s = wp.dwell_s;
                    if (wp.camera == 1 || wp.camera == 2) s.captures++;
                } else {
                    if (wp.camera == 1 || wp.camera == 2) s.captures++;
                    // Fly-through: advance once per tick so a zero-dwell
                    // mission cannot consume the whole list in one frame.
                    advance(m, s);
                }
            }
            break;

        case Phase::Dwelling: {
            out_n = in.north;
            out_e = in.east;
            s.dwell_left_s -= dt_s;
            if (s.dwell_left_s <= 0.0f) {
                s.phase = Phase::Transit;
                advance(m, s);
            }
            break;
        }

        case Phase::Rtl: {
            out_n = home_n;
            out_e = home_e;
            s.cmd_alt = home_alt + 15.0f;   // RTL corridor above the obstacles
            out_alt = s.cmd_alt;
            // Measured to home, not to the last waypoint: a mission that ends
            // far from the pad must still come back to it.
            const float dn2 = in.north - home_n;
            const float de2 = in.east - home_e;
            if (std::sqrt(dn2 * dn2 + de2 * de2) <= m.arrival_m * 2.0f) s.phase = Phase::Descending;
            break;
        }

        case Phase::Descending: {
            // The descent profile is planner-owned, not feedback-derived: a laggy
            // altitude report must never stall the final approach.
            out_n = home_n;
            out_e = home_e;
            s.cmd_alt -= 1.5f * dt_s;                 // 1.5 m/s
            if (s.cmd_alt <= home_alt) {
                s.cmd_alt = home_alt;
                // The profile has reached the pad. Whether the aircraft has is
                // the flight controller's to say.
                if (!in.in_flight) s.phase = Phase::Landed;
            }
            out_alt = s.cmd_alt;
            break;
        }

        case Phase::Landed:
            out_n = home_n;
            out_e = home_e;
            out_alt = home_alt;
            s.cmd_alt = home_alt;
            break;
    }
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

/* ==================== src/safety.h ==================== */

// Safety supervisor: arming checks, the failsafe table from firmware/lib, and
// the external watchdog. This module is the only one allowed to drive the FC
// kill input, so "who stopped this aircraft" always has one answer.

namespace ranch {

struct AirState {
    bool armed;
    bool in_flight;
    bool fc_alive;            // MAVLink heartbeats are arriving
    uint32_t fc_age_ms;
    uint32_t rc_age_ms;
    uint32_t gcs_age_ms;
    uint32_t custom_mode;     // HEARTBEAT custom_mode: the FC's own mode number
    uint8_t system_status;
    uint8_t fix_type;
    uint8_t sats;
    float hdop;
    float lat, lon, alt_m;
    float north, east;        // local ENU from the latched home
    float heading_deg;
    float roll_deg, pitch_deg;    // the FC's own ATTITUDE, in degrees
    uint16_t batt_mv_cell;
    int16_t batt_pct;
    float batt_a;
    float rssi_dbm;
    uint32_t uptime_ms;
};

void safetyInit();
const AirState& safetyAir();
bool safetyAirHasFix();
void safetyOnMavlink(const MavMessage& m);
void safetyUpdate(uint32_t dt_ms);

// Home latch. The geodetic origin lives here and nowhere else, so the
// metres-per-degree conversion used by the mission planner and the one used by
// an uploader cannot drift apart.
bool safetyHomeGeodetic(int32_t& lat_e7, int32_t& lon_e7, float& alt_m);
void safetyGeodeticFromEnu(float north, float east, int32_t& lat_e7, int32_t& lon_e7);

// Ground-station liveness: the GCS timeout only counts down when something has
// actually arrived from the ground.
void safetyNoteGcsContact();

Action safetyAction();
const char* safetyReason();
bool safetyCanArm(char* why, size_t cap);

// Called by the main loop; toggles the watchdog pin and kills the aircraft when
// the supervisor itself stops running.
void safetyKick();

// Ground-station / RC override.
void safetyForceSafe(bool safe);
bool safetyIsSafe();

// The decision table is loadable so an unattended scheduled patrol can disable
// the dashboard-liveness rule without a different binary. Read it, change the
// field, write it back.
const FailsafeConfig& safetyFailsafeConfig();
void safetySetFailsafeConfig(const FailsafeConfig& c);

}  // namespace ranch

/* ==================== src/safety.cpp ==================== */


#include <cmath>
#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

AirState st{};
FailsafeState fs{};
FailsafeConfig cfg = FAILSAFE_DEFAULTS;
Action act = Action::Continue;
char reason[24] = "";

int32_t home_lat_e7 = 0, home_lon_e7 = 0;
float home_alt_m = 0.0f;
bool home_set = false;
uint32_t last_fc_ms = 0, last_rc_ms = 0, last_gcs_ms = 0;
bool force_safe = true;
uint32_t wdt_next = 0;

// WGS84 -> local ENU at metre scale. Accurate enough for a 180 m geofence and
// it keeps double-precision trig off the hot path: the longitude scale is
// derived from the latched home latitude once, then cached.
//
// The differencing is done on the 1e7 integers, never on the float degrees:
// at latitude 30 a float32 has a quantum of about 0.4 m, so (lat - home) in
// floats cannot resolve the last half metre at all, and an aircraft parked on a
// waypoint reads as permanently two-ish metres short of it.
double m_per_deg_lat = 111320.0;
double m_per_deg_lon = 111320.0 * 0.9848;

void enuFromFix(int32_t lat_e7, int32_t lon_e7, float& n, float& e) {
    n = static_cast<float>((lat_e7 - home_lat_e7) * 1e-7 * m_per_deg_lat);
    e = static_cast<float>((lon_e7 - home_lon_e7) * 1e-7 * m_per_deg_lon);
}

void latchHome(const GnssFix& fix) {
    home_lat_e7 = fix.lat_e7;
    home_lon_e7 = fix.lon_e7;
    home_alt_m = fix.alt_mm / 1000.0f;
    const double lat_rad = static_cast<double>(fix.lat_e7) / 1e7 * 0.0174532925;
    double c = std::cos(lat_rad);
    if (c < 0.01) c = 0.01;             // never let the longitude scale collapse
    m_per_deg_lat = 111320.0;
    m_per_deg_lon = 111320.0 * c;
    home_set = true;
}

void setReason(const char* r) {
    std::snprintf(reason, sizeof(reason), "%s", r ? r : "");
}

uint8_t cfg_min_sats() { return FAILSAFE_DEFAULTS.min_satellites; }

// >100 is the decision table's "not measured yet": a pack the FC has not
// reported must not read as empty, and a half-stale RSSI must not read as a
// dead receiver.
uint8_t battPctForTable() {
    if (st.batt_pct < 0 || st.batt_pct > 100) return 255;
    return static_cast<uint8_t>(st.batt_pct);
}

const Geofence kFence{RANCH_GEOFENCE_R_M, RANCH_GEOFENCE_CEIL_M, GEOFENCE_ALT_FLOOR_M};

}  // namespace

void safetyInit() {
    // Establish the whole state, not just the parts that are not already zero.
    // After a watchdog reboot the RAM image is whatever the last flight left
    // there, and a supervisor that inherits "armed" from it will report an
    // aircraft that is on the pad as one that is in the air.
    st = AirState{};
    st.batt_pct = -1;              // unknown, not empty
    fs = FailsafeState{};
    failsafeReset(fs);
    act = Action::Continue;
    force_safe = true;
    setSafe(true);
    setReason("DISARMED");
    home_set = false;
    home_lat_e7 = home_lon_e7 = 0;
    home_alt_m = 0.0f;
    m_per_deg_lon = 111320.0 * 0.9848;
    const uint32_t now = halMillis();
    last_fc_ms = last_rc_ms = last_gcs_ms = now;
    wdt_next = now;
}

const AirState& safetyAir() { return st; }

bool safetyAirHasFix() { return st.fix_type >= 3 && st.sats >= cfg_min_sats(); }

bool safetyHomeGeodetic(int32_t& lat_e7, int32_t& lon_e7, float& alt_m) {
    lat_e7 = home_lat_e7;
    lon_e7 = home_lon_e7;
    alt_m = home_alt_m;
    return home_set;
}

void safetyGeodeticFromEnu(float north, float east, int32_t& lat_e7, int32_t& lon_e7) {
    lat_e7 = home_lat_e7 + static_cast<int32_t>(north / m_per_deg_lat * 1e7);
    lon_e7 = home_lon_e7 + static_cast<int32_t>(east / m_per_deg_lon * 1e7);
}

void safetyNoteGcsContact() { last_gcs_ms = halMillis(); }

void safetyOnMavlink(const MavMessage& m) {
    const uint32_t now = halMillis();
    last_fc_ms = now;

    switch (m.id) {
        case MSG_GPS_RAW_INT: {
            const GnssFix fix = decodeGpsRaw(m);
            if (!fix.valid) break;
            st.fix_type = fix.fix_type;
            st.sats = fix.satellites;
            st.hdop = fix.hdop_cm / 100.0f;
            st.lat = fix.lat_e7 / 1e7f;
            st.lon = fix.lon_e7 / 1e7f;
            st.alt_m = fix.alt_mm / 1000.0f;
            if (!home_set && !st.armed && fix.fix_type >= 3) latchHome(fix);
            if (home_set) enuFromFix(fix.lat_e7, fix.lon_e7, st.north, st.east);
            break;
        }

        case MSG_BATTERY_STATUS: {
            const Battery b = decodeBattery(m);
            if (b.remaining_pct >= 0) st.batt_pct = b.remaining_pct;
            if (b.cells) {
                uint32_t sum = 0;
                for (uint8_t i = 0; i < b.cells; ++i) sum += b.mv_per_cell[i];
                st.batt_mv_cell = static_cast<uint16_t>(sum / b.cells);
            }
            st.batt_a = b.current_centi / 100.0f;
            break;
        }

        case MSG_ATTITUDE: {
            const Attitude a = decodeAttitude(m);
            st.heading_deg = a.yaw_deg;
            st.roll_deg = a.roll_deg;
            st.pitch_deg = a.pitch_deg;
            break;
        }

        // base_mode bit 0 says custom_mode is meaningful and bit 7 is the arm
        // flag; system_status MAV_STATE_ACTIVE (4) means airborne.
        case MSG_HEARTBEAT:
            st.custom_mode = static_cast<uint32_t>(m.i32At(0));
            st.armed = hbArmed(m.u8At(6));
            st.system_status = m.u8At(7);
            st.in_flight = st.system_status == 4;
            last_fc_ms = now;
            break;

        // RADIO_STATUS lays the 16-bit fields out first: txbuf, rssi, remrssi.
        // remrssi is the downlink (our transmitter as seen by the ground) and is
        // reported as dBm + 256 by every modem in the fleet.
        case MSG_RADIO_STATUS:
            if (m.len >= 6) st.rssi_dbm = static_cast<float>(m.u16At(4)) - 256.0f;
            last_rc_ms = now;
            break;

        default:
            break;
    }
}

void safetyUpdate(uint32_t dt_ms) {
    const uint32_t now = halMillis();
    st.uptime_ms += dt_ms;
    st.fc_alive = (now - last_fc_ms) < 3000;
    st.rc_age_ms = now - last_rc_ms;
    st.gcs_age_ms = now - last_gcs_ms;

    if (!st.fc_alive) {
        // Blind, not sick. The aircraft is still flying under the FC's own
        // failsafes and every sensor value we hold is stale, so commanding RTL
        // or kill here would act on the past. The supervisor reports it and
        // stops touching the mission; missionTick gates on the same flag.
        act = Action::Continue;
        setReason("NO FC LINK");
        return;
    }

    FailsafeInput in{};
    // RC loss belongs to the FC: it samples the receiver at hundreds of hertz
    // and has a throttle failsafe wired into the mixer. RADIO_STATUS arrives at
    // a fraction of that rate, so the companion would only ever be second, and a
    // modem that reports slowly would false an RTL. Reported, not acted on.
    in.rc_age_ms = 0;
    in.gcs_age_ms = st.gcs_age_ms;
    in.in_flight = st.in_flight;
    // Above the pad, not above sea level: the ceiling in board.h is an AGL
    // number and the GNSS altitude is AMSL.
    const Breach b = checkGeofence(kFence, 0.0f, 0.0f, st.north, st.east,
                                   st.alt_m - home_alt_m);
    in.geofence_breach = home_set && (b.outside || b.too_high);
    in.fix_type = st.fix_type;
    in.satellites = st.sats;
    in.hdop = st.hdop;
    in.battery_pct = battPctForTable();
    in.cell_v_min = st.batt_mv_cell / 1000.0f;
    in.baro_ok = true;
    // No IMU on this board, and the FC heartbeat is already covered by the
    // early-out above, so this table input is a pass.
    in.imu_ok = true;
    in.gps_ok = st.fix_type > 0;

    const FailsafeDecision d = failsafeEvaluate(cfg, fs, in, dt_ms);
    if (d.action != act || std::strcmp(d.reason, reason) != 0) {
        act = d.action;
        setReason(d.reason);
    }

    if (act == Action::Terminate && !force_safe) {
        force_safe = true;
        setSafe(true);
    }
}

Action safetyAction() { return act; }
const char* safetyReason() { return reason; }
bool safetyIsSafe() { return force_safe; }

const FailsafeConfig& safetyFailsafeConfig() { return cfg; }

void safetySetFailsafeConfig(const FailsafeConfig& c) {
    cfg = c;
    failsafeReset(fs);
}

void safetyForceSafe(bool safe) {
    force_safe = safe;
    setSafe(safe);
}

bool safetyCanArm(char* why, size_t cap) {
    if (force_safe) { std::snprintf(why, cap, "%s", "SAFE SWITCH"); return false; }
    if (!st.fc_alive) { std::snprintf(why, cap, "%s", "NO FC HEARTBEAT"); return false; }
    if (!home_set) { std::snprintf(why, cap, "%s", "HOME NOT SET"); return false; }

    FailsafeInput in{};
    in.rc_age_ms = 0;                 // see safetyUpdate: the FC owns RC loss
    in.gcs_age_ms = st.gcs_age_ms;
    in.fix_type = st.fix_type;
    in.satellites = st.sats;
    in.hdop = st.hdop;
    in.battery_pct = battPctForTable();
    in.cell_v_min = st.batt_mv_cell / 1000.0f;
    in.baro_ok = in.imu_ok = in.gps_ok = true;
    // A throwaway debounce instance: a pre-flight question must not disturb the
    // running supervisor's accumulated timers.
    FailsafeState scratch{};
    const FailsafeDecision d = failsafeEvaluate(cfg, scratch, in, 0);
    if (!d.armed_block) return true;
    std::snprintf(why, cap, "%s", d.reason);
    return false;
}

void safetyKick() {
    const uint32_t now = halMillis();
    if (static_cast<int32_t>(now - wdt_next) < 0) return;
    wdt_next = now + 40;              // 12.5 Hz, above the 10 Hz watchdog floor
    feedWatchdog();
}

}  // namespace ranch

/* ==================== src/mission.h ==================== */

// Mission service: accepts a waypoint list from the ground app, uploads it to
// the flight controller over MAVLink, supervises progress with the planner in
// firmware/lib, and enforces the geofence.

namespace ranch {

enum class MissionSource : uint8_t { None, Mqtt, Rtc, Manual };

struct MissionStatus {
    Phase phase;
    uint8_t target;
    uint8_t count;
    float remaining_m;
    uint32_t captures;
    bool complete;
    bool uploading;
    uint8_t arm_stage;       // ArmStep: how far the hand-over to AUTO has got
    uint8_t arm_retries;     // commands re-issued because the FC did not confirm
    Action failsafe;
    char failsafe_reason[24];
    char mode[12];            // FC mode string, e.g. AUTO, RTL, LOITER
};

void missionInit();
// Queue a mission for upload. Returns false when the list is empty or the FC is
// not reachable, so the caller can refuse the command with a reason.
bool missionUpload(const Mission& m, MissionSource src);
bool missionStartAuto();
void missionRtl();
void missionLand();
void missionHold();
void missionTick(uint32_t dt_ms);
void missionStatus(MissionStatus& out);

// Ground-station input.
void missionOnMavlink(const struct MavMessage& m);
void missionOnCommand(const char* cmd, const char* payload);

// Home position, latched from the first good GNSS fix while disarmed.
bool missionHomeSet();
void missionSetHome(float north, float east, float alt);
void missionHomeEnu(float& n, float& e, float& alt);

// The step of the arm/AUTO hand-over the aircraft is waiting on: "WAIT FIX",
// "SET MODE", "ARM", "TAKEOFF" or "AUTO".
const char* missionArmName();

}  // namespace ranch

/* ==================== src/mission.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

constexpr uint8_t SYSID = 1;        // ArduPilot FC is usually 1
constexpr uint8_t COMPID = 0;       // autopilot
constexpr uint8_t OUR_SYSID = 23;   // companion computer
constexpr uint8_t OUR_COMPID = 190; // MAV_COMP_ID_USER10

Mission mission{};
MissionSource source = MissionSource::None;
PlanState plan{};
MissionStatus status{};
bool uploading = false;
uint8_t upload_next = 0;
uint32_t upload_retry_ms = 0;
bool hold_requested = false;
// "Land now" and "coming home" both end with the aircraft descending, but only
// one of them should take the FC out of RTL: an explicit land goes down where it
// stands, a return flies over the pad first. Without the distinction a normal
// return touched down several metres short of home.
bool land_now = false;
uint32_t last_captures = 0;
uint32_t last_upload_ms = 0;
float g_home_n = 0, g_home_e = 0, g_home_alt = 0;
bool g_home_locked = false;

MavFrameBuilder tx(OUR_SYSID, OUR_COMPID);

void sendCommand(uint16_t cmd, float p1, float p2, uint8_t force) {
    uint8_t payload[40];
    MavWriter w(payload, sizeof(payload));
    CommandLongTx c{};
    c.p[0] = p1;
    c.p[1] = p2;
    c.command = cmd;
    c.target_sys = SYSID;
    c.target_comp = COMPID;
    c.confirmation = force ? 1 : 0;
    c.pack(w);
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_COMMAND_LONG, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void setMode(uint32_t mode) {
    // param1 = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, param2 = the FC's own number.
    sendCommand(CMD_DO_SET_MODE, 1, static_cast<float>(mode), 0);
}

// Hand-over sequence. Every step waits for the flight controller to confirm the
// previous one in its own heartbeat before issuing the next: a companion
// computer that only fires commands into a UART cannot tell "accepted" apart
// from "transmit wire broken", and on an aircraft that difference is the whole
// point of the redundancy.
enum class ArmStep : uint8_t { WaitFix, SetMode, Arm, Takeoff, Running, Count };
ArmStep arm = ArmStep::WaitFix;
uint32_t arm_deadline = 0;
uint8_t arm_retries = 0;

const char* armStepName(ArmStep s) {
    switch (s) {
        case ArmStep::WaitFix:   return "WAIT FIX";
        case ArmStep::SetMode:   return "SET MODE";
        case ArmStep::Arm:       return "ARM";
        case ArmStep::Takeoff:   return "TAKEOFF";
        case ArmStep::Running:   return "AUTO";
        case ArmStep::Count:     return "?";
    }
    return "?";
}

void armAdvance(const AirState& air, uint32_t now) {
    switch (arm) {
        case ArmStep::WaitFix:
            if (!safetyAirHasFix()) return;
            arm = ArmStep::SetMode;
            arm_deadline = now + 2000;
            setMode(FC_MODE_AUTO);
            return;

        case ArmStep::SetMode:
            if (air.custom_mode != FC_MODE_AUTO) break;
            arm = ArmStep::Arm;
            arm_deadline = now + 2000;
            sendCommand(CMD_COMPONENT_ARM_DISARM, 1, 0, 0);
            return;

        case ArmStep::Arm:
            if (!air.armed) break;
            arm = ArmStep::Takeoff;
            arm_deadline = now + 3000;
            sendCommand(CMD_NAV_TAKEOFF, RANCH_MISSION_ALT_M, 0, 0);
            return;

        case ArmStep::Takeoff:
            if (!air.in_flight) break;
            // Only now does the planner claim to be climbing: every phase above
            // Ground is a phase the flight controller has confirmed, so a safe
            // or unconnected aircraft cannot make the dashboard say "CLIMB".
            plan.phase = Phase::Climbing;
            arm = ArmStep::Running;
            sendCommand(CMD_MISSION_START, 0, 0, 0);
            return;

        case ArmStep::Running:
        case ArmStep::Count:
            return;
    }

    // Nothing to step on yet: re-issue the pending command rather than
    // restarting the sequence. The FC may already be in the right mode and have
    // simply missed the frame.
    if (static_cast<int32_t>(now - arm_deadline) < 0) return;
    arm_deadline = now + 2000;
    if (arm_retries < 255) arm_retries++;
    if (arm == ArmStep::SetMode) setMode(FC_MODE_AUTO);
    else if (arm == ArmStep::Arm) sendCommand(CMD_COMPONENT_ARM_DISARM, 1, 0, 0);
    else if (arm == ArmStep::Takeoff) sendCommand(CMD_NAV_TAKEOFF, RANCH_MISSION_ALT_M, 0, 0);
}

void sendMissionItem(uint8_t index) {
    const Waypoint& wp = mission.items[index];
    uint8_t payload[60];
    MavWriter w(payload, sizeof(payload));
    MissionItemIntTx it{};
    it.seq = index;
    it.command = CMD_NAV_WAYPOINT;
    it.frame = FRAME_GLOBAL_INT;
    it.current = index == 0 ? 1 : 0;
    it.autocontinue = 1;
    it.mission_type = 0;
    it.target_sys = SYSID;
    it.target_comp = COMPID;
    // Absolute WGS84 in 1e7 degrees, stamped by missionUpload from the latched
    // home. A zero here would send the aircraft to the Gulf of Guinea.
    it.x = wp.lat_e7;
    it.y = wp.lon_e7;
    it.z = wp.alt;
    it.param1 = 0;
    it.param2 = wp.dwell_s;      // ArduPilot uses param 2 as the waypoint delay
    it.param3 = 0;
    it.param4 = wp.camera == 1 ? 1.0f : 0.0f;
    it.pack(w);

    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_MISSION_ITEM_INT, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void sendMissionCount() {
    uint8_t payload[8];
    MavWriter w(payload, sizeof(payload));
    w.u16(mission.count);
    w.u8(SYSID);
    w.u8(COMPID);
    w.u8(0);   // mission_type MAV_MISSION_TYPE_MISSION
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_MISSION_COUNT, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

void sendAck(uint8_t type) {
    uint8_t payload[8];
    MavWriter w(payload, sizeof(payload));
    w.u8(SYSID);
    w.u8(COMPID);
    w.u8(type);   // MAV_MISSION_ACCEPTED = 0
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = tx.build(MSG_MISSION_ACK, payload, w.size(), frame, sizeof(frame));
    if (n) fcWrite(frame, n);
}

}  // namespace

void missionInit() {
    missionReset(mission);
    plan = PlanState{};
    status = MissionStatus{};
    source = MissionSource::None;
    uploading = false;
    hold_requested = false;
    arm = ArmStep::WaitFix;
    arm_retries = 0;
    land_now = false;
    last_captures = 0;
}

bool missionUpload(const Mission& m, MissionSource src) {
    if (m.count == 0) return false;
    int32_t lat_e7 = 0, lon_e7 = 0;
    float home_alt = 0.0f;
    // Refuse rather than fly: without a latched home there is no georeference,
    // and an unconverted waypoint list reads as a cluster of points on the
    // equator and Greenwich meridian.
    if (!safetyHomeGeodetic(lat_e7, lon_e7, home_alt)) return false;

    mission = m;
    for (uint8_t i = 0; i < mission.count; ++i) {
        const float n = mission.items[i].north;
        const float e = mission.items[i].east;
        safetyGeodeticFromEnu(n, e, mission.items[i].lat_e7, mission.items[i].lon_e7);
    }
    missionSetHome(0.0f, 0.0f, home_alt);

    source = src;
    uploading = true;
    upload_next = 0;
    // A new list starts a new flight: the supervisor's cursor, dwell timer and
    // capture count all belong to the list that is being replaced.
    plan = PlanState{};
    last_captures = 0;
    upload_retry_ms = halMillis() + 1500;
    last_upload_ms = halMillis();
    arm = ArmStep::WaitFix;
    arm_retries = 0;
    land_now = false;
    sendMissionCount();
    return true;
}

void missionOnMavlink(const MavMessage& m) {
    // The flight controller owns the cursor: it advances on arrival and it
    // skips a zero-dwell waypoint in the same tick it received it. Watching
    // MISSION_CURRENT is the only way the supervisor can know which leg is
    // really being flown, so it is handled even when nothing is uploading.
    if (m.id == MSG_MISSION_CURRENT) {
        const uint16_t seq = m.u16At(0);
        if (mission.count == 0 || seq >= mission.count) return;
        if (plan.phase == Phase::Rtl || plan.phase == Phase::Descending) return;
        const bool moved = seq != plan.target;
        plan.target = static_cast<uint8_t>(seq);
        mission.cursor = static_cast<uint8_t>(seq);
        // If the FC left a waypoint we were still dwelling on, its timer and
        // ours disagree; the aircraft is the one that is really flying.
        if (moved && plan.phase == Phase::Dwelling) {
            plan.phase = Phase::Transit;
            plan.dwell_left_s = 0.0f;
        }
        return;
    }

    if (!uploading) return;
    if (m.id == MSG_MISSION_REQUEST_INT) {
        const uint16_t seq = m.u16At(0);
        if (seq < mission.count) {
            sendMissionItem(static_cast<uint8_t>(seq));
            upload_next = static_cast<uint8_t>(seq + 1);
        } else {
            uploading = false;
            sendAck(0);
        }
    } else if (m.id == MSG_MISSION_ACK) {
        uploading = false;
    }
}

void missionTick(uint32_t dt_ms) {
    const uint32_t now = halMillis();
    // Re-send MISSION_COUNT if the FC never answered: a missed frame must not
    // leave the aircraft sitting on the pad with a queued mission. Signed
    // compare, because a wrap near millis()==0 would otherwise skip the retry.
    if (uploading && static_cast<int32_t>(now - upload_retry_ms) >= 0) {
        sendMissionCount();
        upload_retry_ms = now + 1500;
    }

    const AirState& air = safetyAir();

    PlanInput in{};
    in.north = air.north;
    in.east = air.east;
    in.alt = air.alt_m;
    in.cruise_ms = RANCH_CRUISE_MS;
    in.climb_ms = RANCH_CLIMB_MS;
    in.in_flight = air.in_flight;

    float hn, he, ha;
    missionHomeEnu(hn, he, ha);
    float n, e, alt;
    planStep(mission, plan, in, hn, he, ha, dt_ms / 1000.0f, n, e, alt);

    // One camera trigger per planned capture. The planner counts the capture
    // when it enters the dwell, so comparing the counter is what makes this a
    // single frame: testing the countdown window instead re-sent the trigger on
    // every tick for the last third of every dwell.
    if (plan.captures != last_captures) {
        last_captures = plan.captures;
        sendCommand(CMD_DO_DIGICAM_CONTROL, 1, 0, 0);
    }

    status.phase = plan.phase;
    status.target = plan.target;
    status.count = mission.count;
    status.remaining_m = plan.remaining_m;
    status.captures = plan.captures;
    status.complete = plan.mission_complete;
    status.uploading = uploading;
    status.arm_stage = static_cast<uint8_t>(arm);
    status.arm_retries = arm_retries;

    const Action a = safetyAction();
    status.failsafe = a;
    std::snprintf(status.failsafe_reason, sizeof(status.failsafe_reason), "%s", safetyReason());

    // The failsafe outranks the mission: once it says RTL, stop nudging the FC
    // with mission setpoints. And with no FC link there is nobody to nudge --
    // emitting arm or takeoff into a dead UART would still count as "sent".
    if (!air.fc_alive) return;

    if (a == Action::Terminate) { missionLand(); return; }
    if (a == Action::Rtl && plan.phase != Phase::Rtl && plan.phase != Phase::Descending) {
        missionRtl();
        return;
    }
    if (a == Action::Hover) { missionHold(); return; }

    // Mode watchdog: the planner's phase and the FC's mode have to agree. A lost
    // DO_SET_MODE would otherwise leave the aircraft flying the old plan while
    // the dashboard says RTL, which is the worst possible disagreement.
    uint32_t want = 0;
    if (plan.phase == Phase::Rtl) want = FC_MODE_RTL;
    else if (plan.phase == Phase::Descending) want = land_now ? FC_MODE_LAND : FC_MODE_RTL;
    else if (arm == ArmStep::Running) want = FC_MODE_AUTO;
    if (want != 0 && air.custom_mode != want) setMode(want);

    // The hand-over runs whenever a mission is loaded and the aircraft is not
    // yet flying it, whatever the planner's phase says: a resume from a loiter
    // has to be able to re-hand-over without pretending to be on the pad.
    if (!uploading && mission.count && arm != ArmStep::Running) armAdvance(air, now);
}

bool missionStartAuto() {
    if (!mission.count) return false;
    plan.phase = Phase::Climbing;
    plan.target = 0;
    arm = ArmStep::WaitFix;
    arm_retries = 0;
    return true;
}

// Setting the planner's phase is only half of a mode change: the aircraft is
// flying the FC's state machine, so the FC has to be told too. The mode
// watchdog in missionTick re-sends if the frame is lost.
void missionRtl() {
    plan.phase = Phase::Rtl;
    hold_requested = false;
    land_now = false;
    setMode(FC_MODE_RTL);
}

void missionLand() {
    plan.phase = Phase::Descending;
    land_now = true;
    setMode(FC_MODE_LAND);
}

void missionHold() {
    if (hold_requested) return;
    hold_requested = true;
    sendCommand(CMD_NAV_LOITER_UNLIM, 0, 0, 0);
}

void missionStatus(MissionStatus& out) {
    out = status;
    std::snprintf(out.mode, sizeof(out.mode), "%s", phaseName(plan.phase));
}

bool missionHomeSet() { return g_home_locked; }

// Which step of the hand-over we are stuck on. A mission that never leaves the
// pad looks identical to a dead upload unless the stage is reported.
const char* missionArmName() { return armStepName(arm); }

void missionSetHome(float north, float east, float alt) {
    g_home_n = north;
    g_home_e = east;
    g_home_alt = alt;
    g_home_locked = true;
}

void missionHomeEnu(float& n, float& e, float& alt) {
    n = g_home_n;
    e = g_home_e;
    alt = g_home_alt;
}

void missionOnCommand(const char* cmd, const char* payload) {
    if (!cmd) return;
    if (std::strcmp(cmd, "rtl") == 0) missionRtl();
    else if (std::strcmp(cmd, "land") == 0) missionLand();
    else if (std::strcmp(cmd, "hold") == 0) missionHold();
    else if (std::strcmp(cmd, "resume") == 0) { hold_requested = false; missionStartAuto(); }
    else if (std::strcmp(cmd, "camera") == 0 && payload && payload[0] == '1') {
        sendCommand(CMD_DO_DIGICAM_CONTROL, 1, 0, 0);
    }
}

}  // namespace ranch

/* ==================== src/telemetry.h ==================== */

// State uplink: one key=value frame, three sinks (console, MQTT, SD log).
// The frame is built once per publish so all three sinks carry byte-identical
// data and a lost MQTT message cannot make the log disagree with the dashboard.

#include <cstddef>

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint32_t logged;
    uint32_t log_dropped;   // ring buffer overflow while the card was busy
    uint16_t last_len;
    bool broker_connected;
};

void telemetryInit();
// Broker identity comes from NVS, so a fleet of aircraft can share firmware.
// Call before the first publish; the buffers are copied, not borrowed.
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

// Ground-to-aircraft commands. The payload is not NUL-terminated: it is a view
// of the receive buffer that is only valid for the duration of the call, so a
// handler that wants to keep it must copy it first.
using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const MissionStatus& ms, const AirState& air);
void telemetryService();               // broker keepalive + log drain
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch

/* ==================== src/telemetry.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

constexpr uint16_t LOG_SLOTS = 16;
constexpr size_t LOG_SLOT = 128;
// Sized for the longest frame below with every optional field present. A frame
// that outgrows it is dropped and counted, never truncated.
constexpr size_t FRAME_CAP = 208;

UplinkStats up{};
char frame[FRAME_CAP];
char log_[LOG_SLOTS][LOG_SLOT];
uint8_t log_head = 0, log_tail = 0, log_len = 0;

char broker_host[64] = "";
char broker_client[24] = "drone";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

void logPush(const char* s, size_t n) {
    if (log_len == LOG_SLOTS) {
        // Overwrite the oldest entry: a full ring must not stop the aircraft,
        // and the newest state is the one the ground app acts on.
        up.log_dropped++;
        log_tail = static_cast<uint8_t>((log_tail + 1) % LOG_SLOTS);
        log_len--;
    }
    std::snprintf(log_[log_head], LOG_SLOT, "%.*s", static_cast<int>(n), s);
    log_head = static_cast<uint8_t>((log_head + 1) % LOG_SLOTS);
    log_len++;
}

}  // namespace

#if defined(RANCH_SIM)
namespace {
// The browser build has no broker. Frames still go to the console, which is
// what the ranch dashboard reads back, so the publish path stays exercised
// end to end and the counters below stay honest about what was not sent.
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
        broker.setBufferSize(512);
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
    log_head = log_tail = log_len = 0;
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "drone");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const MissionStatus& ms, const AirState& air) {
    VideoStats v{};
    videoStats(v);

    int32_t home_lat = 0, home_lon = 0;
    float home_alt = 0.0f;
    const bool has_home = safetyHomeGeodetic(home_lat, home_lon, home_alt);

    FrameWriter w(frame, sizeof(frame));
    w.begin("DRONE");
    w.add("mode", ms.mode);
    // Two altitudes with different references, both named: `alt` is what the GNSS
    // reported above sea level, `agl` is what a person watching the aircraft
    // means by height. Reporting only one makes a dashboard guess.
    w.add("alt", air.alt_m, 1);
    w.add("agl", has_home ? air.alt_m - home_alt : air.alt_m, 1);
    // Where the nose points, in degrees from north: the FC's own MAVLink ATTITUDE
    // yaw. It holds its last value while the link is down, which is why `link`
    // travels in the same frame -- a heading without a link is a memory, not a
    // reading, and a dashboard that shows the FPV horizon has to be able to tell
    // the two apart.
    w.add("hdg", air.heading_deg, 1);
    // The same ATTITUDE message's other two angles, in degrees: the FPV horizon is
    // drawn from these, and a horizon built from the sticks instead of from the
    // aircraft is a picture of the pilot's hands.
    w.add("roll", air.roll_deg, 1);
    w.add("pitch", air.pitch_deg, 1);
    w.add("wp", ms.target + 1);
    w.add("left", ms.remaining_m, 0);
    w.add("batt", air.batt_pct);
    w.add("mv", air.batt_mv_cell);
    w.add("amp", air.batt_a, 1);
    w.add("rssi", static_cast<int>(air.rssi_dbm));
    w.add("fix", air.fix_type);
    w.add("sats", air.sats);
    w.add("shots", static_cast<int>(ms.captures));
    w.add("fps", v.fps);
    w.add("safe", safetyIsSafe() ? 1 : 0);
    w.add("link", wifiUp() ? "up" : "down");
    w.add("up", static_cast<int>(air.uptime_ms / 1000u));
    if (*safetyReason()) w.add("why", safetyReason());
    if (ms.failsafe != Action::Continue) w.add("fs", actionName(ms.failsafe));
    w.endLine();

    if (w.overflow()) {
        // A truncated telemetry frame is worse than a missing one: the ground
        // app would read a stale field as fresh. Drop and count it.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    logPush(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryService() {
    while (log_len) {
        const char* s = log_[log_tail];
        log_tail = static_cast<uint8_t>((log_tail + 1) % LOG_SLOTS);
        log_len--;
        char path[24];
        std::snprintf(path, sizeof(path), "/log/%06lu.csv", static_cast<unsigned long>(up.logged + 1));
        if (sdOpen(path)) {
            sdAppend(s, std::strlen(s));
            sdClose();
            up.logged++;
        } else {
            up.log_dropped++;
            log_tail = static_cast<uint8_t>((log_tail + LOG_SLOTS - 1) % LOG_SLOTS);
            log_len++;
            break;                      // no card: stop draining, keep RAM ring
        }
    }
    brokerService();
}

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch

/* ==================== src/hal_sim.cpp ==================== */

// Synthetic airframe and flight controller for the browser (Velxio) and host
// test builds.
//
// Why it is written this way:
//  - fcWrite() feeds the firmware's own bytes back through MavParser, and the
//    simulated FC answers with real MAVLink frames. A wrong CRC_EXTRA, a wrong
//    field order, or an unconverted waypoint therefore surfaces as "the FC
//    never acknowledged the upload" or "the aircraft flew to a strange place",
//    instead of passing silently the way a stubbed-out HAL would.
//  - the airframe is a second, independent implementation of mission following:
//    the planner in firmware/lib and the FC must agree through telemetry only,
//    so a disagreement is measurable.
//  - no allocation, no threads, and time only moves when the firmware asks for
//    it, which lets the host test run a two-minute patrol in milliseconds.
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
// Anywhere will do, but it must be a real place: the simulated FC converts the
// absolute coordinates it receives back into local metres, so a georeference
// bug moves the aircraft by kilometres and the planner notices.
constexpr int32_t SIM_LAT_E7 = 305000000;        // 30.5000000 N
constexpr int32_t SIM_LON_E7 = 1143000000;       // 114.3000000 E
constexpr float SIM_HOME_ALT_M = 42.0f;
constexpr double SIM_M_PER_DEG_LAT = 111320.0;
constexpr double SIM_M_PER_DEG_LON = 111320.0 * 0.86163;   // cos(30.5 deg)
constexpr float SIM_RTL_ALT_M = SIM_HOME_ALT_M + 12.0f;

struct SimItem {
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    float alt = 0;
    float dwell_s = 0;
};

struct SimFc {
    // Truth.
    float n = 0.0f, e = 0.0f, alt = SIM_HOME_ALT_M;
    float heading_deg = 0.0f;
    bool armed = false;
    uint32_t mode = 0;                 // custom_mode echoed back in HEARTBEAT
    bool mission_running = false;
    float takeoff_alt = SIM_HOME_ALT_M;

    // Received mission.
    SimItem items[MISSION_MAX_WAYPOINTS];
    uint8_t count = 0;
    uint8_t current = 0;
    uint8_t reported = 0xFF;      // last MISSION_CURRENT sent to the companion
    bool accepted = false;
    float dwell_left = 0.0f;
    bool on_wp = false;            // dwell armed for the current waypoint
    uint32_t captures = 0;

    // Power.
    float pct = 100.0f;

    // Emission schedule, in milliseconds of simulated time.
    uint32_t t_heart = 0, t_gps = 0, t_att = 0, t_batt = 0, t_radio = 0;
};

SimFc fc;

// The attitude the FC reports lives with the airframe state because the encoder that
// sends it runs before the step that computes it.
float sim_roll_deg = 0.0f;
float sim_pitch_deg = 0.0f;
float sim_prev_heading = 0.0f;
bool sim_have_heading = false;

// --- clock ------------------------------------------------------------------
uint32_t g_now = 0;
bool g_safe = true;
bool g_arm_latched = true;
uint32_t g_wdt_feeds = 0;
uint32_t g_led_pattern = 0;

#if defined(RANCH_HOST)
uint32_t g_virt = 0;                 // advanced only by halDelayMs
inline uint32_t rawMillis() { return g_virt; }
inline void rawSleep(uint32_t ms) { g_virt += ms; }
#else
inline uint32_t rawMillis() { return ::millis(); }
inline void rawSleep(uint32_t ms) { ::delay(ms); }
#endif

void simService(uint32_t dt_ms);

void pump() {
    const uint32_t t = rawMillis();
    if (t == g_now) return;
    uint32_t dt = t - g_now;
    g_now = t;
    if (dt > 100) dt = 100;           // a stalled UI thread must not teleport
    simService(dt);
}

// --- MAVLink out of the simulated FC and into the firmware ------------------
constexpr size_t RX_CAP = 2048;
uint8_t rx[RX_CAP];
size_t rx_head = 0, rx_tail = 0;
uint32_t rx_overflow = 0, frames_emitted = 0;

void pushRx(const uint8_t* p, size_t n) {
    while (n--) {
        const size_t next = (rx_head + 1) % RX_CAP;
        if (next == rx_tail) { rx_overflow++; continue; }
        rx[rx_head++] = *p++;
        if (rx_head == RX_CAP) rx_head = 0;
    }
}

MavFrameBuilder sim_tx(1, 0);         // sysid 1 / autopilot, as an FC would be

void emit(uint32_t id, const uint8_t* payload, size_t len) {
    uint8_t frame[MAV_FRAME_MAX];
    const size_t n = sim_tx.build(id, payload, len, frame, sizeof(frame));
    if (!n) return;
    frames_emitted++;
    pushRx(frame, n);
}

void emitHeartbeat() {
    uint8_t pl[9];
    MavWriter w(pl, sizeof(pl));
    HeartbeatTx h{};
    h.custom_mode = fc.mode;
    h.type = 2;                       // MAV_TYPE_QUADROTOR
    h.autopilot = 3;                  // MAV_AUTOPILOT_ARDUPILOTMEGA
    h.base_mode = static_cast<uint8_t>(1 | (fc.armed ? 0x80 : 0x00));
    const bool flying = fc.alt > SIM_HOME_ALT_M + 0.4f;
    h.system_status = fc.armed ? (flying ? 4 : 2) : 1;
    h.pack(w);
    emit(MSG_HEARTBEAT, pl, w.size());
}

void emitGps() {
    uint8_t pl[34];
    MavWriter w(pl, sizeof(pl));
    const uint64_t usec = static_cast<uint64_t>(g_now) * 1000ull;
    w.u32(static_cast<uint32_t>(usec & 0xFFFFFFFFu));
    w.u32(static_cast<uint32_t>(usec >> 32));
    w.i32(SIM_LAT_E7 + static_cast<int32_t>(fc.n / SIM_M_PER_DEG_LAT * 1e7));
    w.i32(SIM_LON_E7 + static_cast<int32_t>(fc.e / SIM_M_PER_DEG_LON * 1e7));
    w.i32(static_cast<int32_t>(fc.alt * 1000.0f));
    w.i32(static_cast<int32_t>(fc.alt * 1000.0f));       // alt_geom
    w.u16(150);                                          // eph = hdop 1.50
    w.u16(220);
    w.u16(static_cast<uint16_t>(fc.mission_running ? RANCH_CRUISE_MS * 100.0f : 0.0f));
    w.u16(static_cast<uint16_t>(fc.heading_deg * 100.0f));
    w.u8(3);                                             // 3D fix
    w.u8(14);
    emit(MSG_GPS_RAW_INT, pl, w.size());
}

void emitAttitude() {
    uint8_t pl[28];
    MavWriter w(pl, sizeof(pl));
    w.u32(g_now);
    const float rad = 0.0174532925f;
    w.f32(sim_roll_deg * rad);                         // roll: the bank it is in
    w.f32(sim_pitch_deg * rad);                        // pitch: the climb angle
    w.f32(fc.heading_deg * rad);
    w.f32(0.0f); w.f32(0.0f); w.f32(0.0f);
    emit(MSG_ATTITUDE, pl, w.size());
}

void emitBattery() {
    uint8_t pl[37];
    MavWriter w(pl, sizeof(pl));
    const float mah = 5000.0f * (100.0f - fc.pct) / 100.0f;
    w.u32(static_cast<uint32_t>(mah));                 // current_consumed, hB
    w.u32(0);                                          // energy_consumed
    w.i16(310);                                        // temperature 31.0 C
    const uint16_t cell_mv = static_cast<uint16_t>(3300.0f + 900.0f * fc.pct / 100.0f);
    for (int i = 0; i < 10; ++i) w.u16(i < BATT_CELLS ? cell_mv : 0xFFFF);
    const float amps = (fc.alt > SIM_HOME_ALT_M + 0.4f) ? 14.2f : (fc.armed ? 1.6f : 0.35f);
    w.i16(static_cast<int16_t>(amps * 100.0f));        // current, cA
    w.u8(0); w.u8(0); w.u8(2);                         // id, function, LIPO
    w.i8(static_cast<int8_t>(fc.pct));                 // remaining
    w.u8(3);                                           // charge_state
    emit(MSG_BATTERY_STATUS, pl, w.size());
}

void emitRadio() {
    uint8_t pl[14];
    MavWriter w(pl, sizeof(pl));
    const float d = std::sqrt(fc.n * fc.n + fc.e * fc.e);
    float dbm = -40.0f - 0.28f * d;
    if (dbm < -95.0f) dbm = -95.0f;
    w.u16(0);                                          // txbuf
    w.u16(255);                                        // local rssi
    w.u16(static_cast<uint16_t>(256 + dbm));           // remrssi, dBm + 256
    w.u16(0);                                          // txerrors
    w.u16(0);                                          // fixederrors
    w.u8(100); w.u8(100); w.u8(100); w.u8(100); w.u8(0); w.u8(0);
    emit(MSG_RADIO_STATUS, pl, w.size());
}

void emitRequest(uint16_t seq) {
    uint8_t pl[5];
    MavWriter w(pl, sizeof(pl));
    w.u16(seq); w.u8(23); w.u8(190); w.u8(0);          // addressed to the companion
    emit(MSG_MISSION_REQUEST_INT, pl, w.size());
}

void emitMissionAck() {
    uint8_t pl[3];
    MavWriter w(pl, sizeof(pl));
    w.u8(23); w.u8(190); w.u8(0);                      // MAV_MISSION_ACCEPTED
    emit(MSG_MISSION_ACK, pl, w.size());
}

void emitCurrent(uint16_t seq) {
    uint8_t pl[2];
    MavWriter w(pl, sizeof(pl));
    w.u16(seq);
    emit(MSG_MISSION_CURRENT, pl, w.size());
}

// --- the simulated FC's own mission following -------------------------------
void enuOf(const SimItem& it, float& n, float& e) {
    n = static_cast<float>((it.lat_e7 - SIM_LAT_E7) * 1e-7 * SIM_M_PER_DEG_LAT);
    e = static_cast<float>((it.lon_e7 - SIM_LON_E7) * 1e-7 * SIM_M_PER_DEG_LON);
}

// The attitude the FC reports, derived from the motion this simulator is actually
// making: a multirotor pitches to climb and banks to turn, so both angles follow from
// the same numbers that move it. The bank is low-passed because the model swings the
// nose instantly at a waypoint while a real airframe rolls through the turn over a
// second or so -- without the filter the aircraft would appear to snap 90 degrees of
// roll for one message and be level again the next.
void attitudeStep(float vh, float vz, float heading_deg, float dt) {
    constexpr float DEG = 57.29578f;
    // A multirotor climbs level. Its body tilt is what produces horizontal
    // acceleration, so the angle follows the groundspeed, not the flight-path angle:
    // the first version here took atan2(vz, vh) and the aircraft pitched 90 degrees
    // on the vertical takeoff, which the sandbox's tilt envelope caught.
    constexpr float CRUISE_TILT_DEG = 12.0f;      // what a copter holds at cruise
    float pitch = CRUISE_TILT_DEG * (vh / RANCH_CRUISE_MS);
    if (pitch > CRUISE_TILT_DEG) pitch = CRUISE_TILT_DEG;
    if (pitch < -CRUISE_TILT_DEG) pitch = -CRUISE_TILT_DEG;
    (void) vz;                                    // level climb: vz tilts nothing
    float bank = 0.0f;
    if (sim_have_heading && dt > 0.001f) {
        float dh = heading_deg - sim_prev_heading;
        if (dh > 180.0f) dh -= 360.0f;
        if (dh < -180.0f) dh += 360.0f;
        const float omega = dh / DEG / dt;                  // yaw rate, rad/s
        bank = std::atan(omega * vh / 9.80665f) * DEG;      // coordinated turn
        if (bank > 45.0f) bank = 45.0f;
        if (bank < -45.0f) bank = -45.0f;
    }
    sim_pitch_deg = pitch;
    const float k = dt > 0.5f ? 1.0f : dt * 2.0f;
    sim_roll_deg += (bank - sim_roll_deg) * k;
    sim_prev_heading = heading_deg;
    sim_have_heading = true;
}

void fcStep(float dt) {
    // ArduPilot broadcasts MISSION_CURRENT whenever its cursor moves, and the
    // companion has to hear it: a zero-dwell waypoint is skipped in the same
    // tick it is reached, so a supervisor that keeps its own cursor ends up
    // measuring a leg the aircraft stopped flying minutes ago.
    if (fc.mission_running && fc.current != fc.reported) {
        fc.reported = fc.current;
        emitCurrent(fc.current);
    }

    float tn = fc.n, te = fc.e, ta = fc.alt;

    if (g_safe) {
        // The kill input is not advisory on this airframe.
        fc.armed = false;
        fc.mission_running = false;
    }

    if (!fc.armed) {
        tn = fc.n; te = fc.e; ta = SIM_HOME_ALT_M;
    } else if (fc.mode == FC_MODE_RTL) {
        tn = 0.0f; te = 0.0f;
        ta = std::sqrt(tn * tn + te * te) > 6.0f ? SIM_RTL_ALT_M : SIM_HOME_ALT_M;
    } else if (fc.mode == FC_MODE_LAND) {
        tn = fc.n; te = fc.e; ta = SIM_HOME_ALT_M;
    } else if (fc.mode == FC_MODE_LOITER) {
        tn = fc.n; te = fc.e; ta = fc.alt;
    } else if (fc.mode == FC_MODE_AUTO && fc.current < fc.count) {
        if (!fc.mission_running) {
            // NAV_TAKEOFF before MISSION_START: climb at the pad, hold there.
            ta = fc.takeoff_alt;
        } else {
            enuOf(fc.items[fc.current], tn, te);
            ta = fc.items[fc.current].alt;
            const float dn = tn - fc.n, de = te - fc.e;
            const float hd = std::sqrt(dn * dn + de * de);
            if (hd <= MISSION_DEFAULT_ARRIVAL_M) {
                // The dwell is armed once, on the arrival edge. Testing only
                // "dwell_left is spent" re-armed it every tick after it expired,
                // and the aircraft sat on the waypoint for the rest of the pack.
                if (!fc.on_wp) {
                    fc.on_wp = true;
                    fc.dwell_left = fc.items[fc.current].dwell_s;
                }
                if (fc.dwell_left > 0.0f) {
                    fc.dwell_left -= dt;
                    tn = fc.n;
                    te = fc.e;                          // hold on the waypoint
                } else {
                    fc.on_wp = false;
                    if (fc.current + 1 < fc.count) fc.current++;
                }
            } else {
                fc.on_wp = false;
            }
        }
    } else if (fc.mode == FC_MODE_AUTO) {
        ta = fc.takeoff_alt;
    }

    const float dn = tn - fc.n, de = te - fc.e;
    const float hd = std::sqrt(dn * dn + de * de);
    const float hstep = hd < RANCH_CRUISE_MS * dt ? hd : RANCH_CRUISE_MS * dt;
    if (hd > 0.001f) {
        fc.n += dn / hd * hstep;
        fc.e += de / hd * hstep;
        // The nose slews onto the new track instead of snapping to it. Snapping made
        // the turn happen in one message, so there was never a bank to report and the
        // FC's roll sat at zero for the whole flight; turning through a bounded rate
        // is also what makes the reported bank agree with the aircraft's path.
        float want = std::atan2(de, dn) * 57.29578f;
        if (want < 0.0f) want += 360.0f;
        float dh = want - fc.heading_deg;
        if (dh > 180.0f) dh -= 360.0f;
        if (dh < -180.0f) dh += 360.0f;
        constexpr float SIM_YAW_DPS = 90.0f;        // ArduCopter's default turn rate
        const float lim_yaw = SIM_YAW_DPS * dt;
        if (dh > lim_yaw) dh = lim_yaw;
        if (dh < -lim_yaw) dh = -lim_yaw;
        fc.heading_deg += dh;
        if (fc.heading_deg < 0.0f) fc.heading_deg += 360.0f;
        if (fc.heading_deg >= 360.0f) fc.heading_deg -= 360.0f;
    }

    const float da = ta - fc.alt;
    const float lim = (da > 0.0f ? RANCH_CLIMB_MS : 1.5f) * dt;
    float vstep = da;
    if (vstep > lim) vstep = lim;
    if (vstep < -lim) vstep = -lim;
    fc.alt += vstep;
    {
        const float d = dt > 0.001f ? dt : 0.001f;
        attitudeStep(hstep / d, vstep / d, fc.heading_deg, dt);
    }

    const bool flying = fc.alt > SIM_HOME_ALT_M + 0.4f;
    // ArduCopter disarms after touchdown. Without that the simulated pack keeps
    // draining on the pad, and the log says the aircraft flew for an hour after
    // it landed.
    if (!flying && fc.armed && (fc.mode == FC_MODE_LAND || fc.mode == FC_MODE_RTL)) {
        fc.armed = false;
        fc.mission_running = false;
    }

    // Drain derived from the same numbers the current telemetry reports:
    // 5000 mAh / 14.2 A = 21 min cruise, / 1.6 A = 52 min with motors idling,
    // / 0.35 A = 40 h on the pad. Anything faster is a made-up endurance.
    const float drain = flying ? 0.0789f : (fc.armed ? 0.032f : 0.0007f);
    fc.pct -= drain * dt;
    if (fc.pct < 0.0f) fc.pct = 0.0f;
}

void simService(uint32_t dt_ms) {
    const float dt = dt_ms / 1000.0f;
    fcStep(dt);

    fc.t_heart += dt_ms;
    if (fc.t_heart >= 500) { fc.t_heart = 0; emitHeartbeat(); }
    fc.t_gps += dt_ms;
    if (fc.t_gps >= 200) { fc.t_gps = 0; emitGps(); }
    fc.t_att += dt_ms;
    if (fc.t_att >= 100) { fc.t_att = 0; emitAttitude(); }
    fc.t_batt += dt_ms;
    if (fc.t_batt >= 1000) { fc.t_batt = 0; emitBattery(); }
    fc.t_radio += dt_ms;
    if (fc.t_radio >= 500) { fc.t_radio = 0; emitRadio(); }
}

// --- the FC's view of what the companion computer sent ----------------------
MavParser sim_rx;

void fcOnMessage(const MavMessage& m) {
    if (m.id == MSG_MISSION_COUNT) {
        uint16_t c = m.u16At(0);
        if (c > MISSION_MAX_WAYPOINTS) c = MISSION_MAX_WAYPOINTS;
        fc.count = static_cast<uint8_t>(c);
        fc.current = 0;
        fc.reported = 0xFF;
        fc.dwell_left = 0.0f;
        fc.on_wp = false;
        fc.mission_running = false;
        fc.accepted = false;
        for (uint8_t i = 0; i < c; ++i) fc.items[i] = SimItem{};
        if (c) emitRequest(0);
        return;
    }
    if (m.id == MSG_MISSION_ITEM_INT) {
        const uint16_t seq = m.u16At(28);
        if (seq >= MISSION_MAX_WAYPOINTS) return;
        SimItem& it = fc.items[seq];
        it.lat_e7 = m.i32At(16);
        it.lon_e7 = m.i32At(20);
        it.alt = m.f32At(24);
        it.dwell_s = m.f32At(4);       // param2, the waypoint delay
        if (static_cast<uint16_t>(seq + 1) < fc.count) {
            emitRequest(static_cast<uint16_t>(seq + 1));
        } else {
            fc.accepted = true;
            emitMissionAck();
        }
        return;
    }
    if (m.id == MSG_COMMAND_LONG) {
        const uint16_t cmd = m.u16At(28);
        const float p1 = m.f32At(0);
        const float p2 = m.f32At(4);
        switch (cmd) {
            case CMD_DO_SET_MODE:
                if (!g_safe) fc.mode = static_cast<uint32_t>(p2);
                break;
            case CMD_COMPONENT_ARM_DISARM:
                fc.armed = (p1 != 0.0f) && !g_safe;
                break;
            case CMD_NAV_TAKEOFF:
                fc.takeoff_alt = SIM_HOME_ALT_M + p1;
                fc.armed = !g_safe;
                if (fc.armed && fc.mode != FC_MODE_AUTO) fc.mode = FC_MODE_AUTO;
                break;
            case CMD_MISSION_START:
                fc.mission_running = true;
                break;
            case CMD_NAV_RETURN_TO_LAUNCH:
                fc.mode = FC_MODE_RTL;
                break;
            case CMD_NAV_LAND:
                fc.mode = FC_MODE_LAND;
                break;
            case CMD_NAV_LOITER_UNLIM:
                fc.mode = FC_MODE_LOITER;
                fc.mission_running = false;
                break;
            case CMD_DO_DIGICAM_CONTROL:
                fc.captures++;
                break;
            default:
                break;
        }
    }
}

// --- non-volatile parameters ------------------------------------------------
struct Kv {
    char key[16];
    uint8_t kind;                       // 0 free, 1 i32, 2 f32, 3 str
    int32_t i;
    float f;
    char s[64];
};
Kv nvs[16];

Kv* findKv(const char* key, bool create) {
    const size_t klen = std::strlen(key);
    Kv* free_slot = nullptr;
    for (auto& k : nvs) {
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

// --- the "SD card" ------------------------------------------------------------
// A card in the simulation is a capacity and a byte count, not a buffer: 32 KB
// of static RAM would be a third of the ESP32's SRAM, and the point of the
// exercise is whether the log drain keeps up, not whether it can store.
constexpr uint32_t CARD_CAP = 1000000u;
uint32_t card_len = 0;
bool card_open = false;

}  // namespace

// --- HAL --------------------------------------------------------------------
void halInit() {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    // The console is the only sink in the simulation, and on this core a
    // HardwareSerial that was never begun silently discards every write.
    Serial.begin(DBG_BAUD);
#endif
    g_now = rawMillis();
    g_safe = true;
    rx_head = rx_tail = 0;
    fc = SimFc{};
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

int fcAvailable() {
    pump();
    return static_cast<int>((RX_CAP + rx_head - rx_tail) % RX_CAP);
}

int fcRead(uint8_t* buf, size_t cap) {
    size_t n = 0;
    while (n < cap && rx_tail != rx_head) {
        buf[n++] = rx[rx_tail];
        rx_tail = (rx_tail + 1) % RX_CAP;
    }
    return static_cast<int>(n);
}

void fcWrite(const uint8_t* buf, size_t len) {
    MavMessage m{};
    for (size_t i = 0; i < len; ++i) {
        if (sim_rx.push(buf[i], m)) fcOnMessage(m);
    }
}

void setSafe(bool safe) { g_safe = safe; }
bool safeRequested() { return g_safe; }
// A compile-time flag to skip the arm switch check in the simulator (for debugging).
// This is only for sim, never on real board: real hardware must have the physical key.
#define SIM_SKIP_ARM_CHECK   // define this at build time to always assume ARM=close

void armSwitchClosed() {
#if defined(SIM_SKIP_ARM_CHECK)
    g_arm_latched = true;
#else
    // Real board or non-skip mode: read the actual pin
    g_arm_latched = digitalRead(PIN_ARM_IN) == LOW;
#endif
}
bool armSwitchIsClosed() { return g_arm_latched; }
void feedWatchdog() { g_wdt_feeds++; }
void setStatusLed(uint8_t pattern) { g_led_pattern = pattern; }

float batteryVolts() { return batteryCellVolts() * BATT_CELLS; }

float batteryCellVolts() { return 3.30f + 0.90f * fc.pct / 100.0f; }

float busCurrentAmps() {
    if (fc.alt > SIM_HOME_ALT_M + 0.4f) return 14.2f;
    return fc.armed ? 1.6f : 0.35f;
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

// The dashboard radio is always associated in the simulation, and its strength
// falls off with distance from the pad, so the RSSI field is a measurement
// rather than a constant.
bool wifiConnect(const char*, const char*) { return true; }
bool wifiUp() { return true; }

int wifiRssi() {
    const float d = std::sqrt(fc.n * fc.n + fc.e * fc.e);
    float dbm = -40.0f - 0.28f * d;
    if (dbm < -95.0f) dbm = -95.0f;
    return static_cast<int>(dbm);
}

void wifiReconnect() {}

bool sdOpen(const char*) {
    if (card_len + 512 > CARD_CAP) return false;
    card_open = true;
    return true;
}

bool sdAppend(const char* data, size_t len) {
    if (!card_open || card_len + len > CARD_CAP) return false;
    card_len += static_cast<uint32_t>(len);
    return true;
}

void sdClose() { card_open = false; }

const char* resetReason() { return "sim"; }

// --- introspection for the host test ----------------------------------------
void halSimCounters(SimCounters& out) {
    out.rx_overflow = rx_overflow;
    out.frames_emitted = frames_emitted;
    out.mavlink_crc_errors = sim_rx.crcErrors();
    out.mavlink_dropped = sim_rx.dropped();
    out.fc_acked_mission = fc.accepted;
    out.fc_items = fc.count;
    out.fc_captures = fc.captures;
    out.wdt_feeds = g_wdt_feeds;
    out.led_pattern = g_led_pattern;
    out.card_bytes = static_cast<uint32_t>(card_len);
}

}  // namespace ranch

#endif  // RANCH_SIM

/* ==================== src/main.cpp ==================== */

// Entry point: parameters, the job table, and the task layout.
//
// Shape of the concurrency, and why:
//  - one cooperative scheduler task at PRIO_MISSION runs the mission, the
//    uplink and the diagnostics. These share the airframe snapshot, and running
//    them in a known order is a smaller correctness risk on an aircraft than a
//    priority ceiling or a mutex around it.
//  - MAVLink input and the safety supervisor run every iteration, not on a
//    schedule: bytes arrive continuously and the debounce timers in the
//    failsafe table are in real seconds.
//  - the camera is the only thing on its own task, at the lowest priority,
//    because it is the only thing allowed to block for a long time.
//  - the Arduino loop task is deleted rather than used, so the mission loop gets
//    a real priority instead of the core's default.
//  - the external watchdog is fed by the safety job only. If the scheduler
//    stops, the watchdog stops, and the aircraft loses control inputs instead
//    of continuing on a frozen mission.
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
int job_safety = -1, job_mission = -1, job_telem = -1, job_uplink = -1, job_health = -1;
MavParser parser;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;
bool launched = false;
bool link_down = false;
uint32_t link_down_ms = 0;
uint32_t saved_captures = 0;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char craft_tag[16] = "drone-1";
uint16_t mqtt_port = MQTT_PORT;
// Zero means "never launch on a clock". A multirotor that arms and takes off
// because a timer expired is not an autonomous aircraft, it is an unsupervised
// one: the launch has to be asked for (the GCS `takeoff`, or the technician
// closing the arm switch and then commanding it). The timer stays available as a
// bench test mode and is read from NVS as launch_ms when someone wants it.
uint32_t auto_launch_ms = 0;
bool launch_requested = false;
bool switch_last = false;

// The factory patrol: the ranch's inspection points in the order the walk is
// actually done, in metres from the pad and above ground level. Altitude
// becomes AMSL at launch, once the home position is latched.
struct RoutePoint { float north, east, agl, dwell_s; uint8_t camera; };

const RoutePoint kFactory[] = {
    {   0.0f,   0.0f, 18.0f, 0.0f, 0 },   // pad, climb out
    {  38.0f,  12.0f, 18.0f, 4.0f, 1 },   // main house
    {  70.0f, -34.0f, 20.0f, 5.0f, 1 },   // barn
    {  22.0f, -78.0f, 18.0f, 3.0f, 1 },   // feed store
    { -34.0f, -96.0f, 26.0f, 6.0f, 1 },   // water tower
    { -78.0f, -40.0f, 18.0f, 4.0f, 1 },   // pump house
    { -66.0f,  36.0f, 16.0f, 3.0f, 1 },   // cattle yard
    { -20.0f,  70.0f, 18.0f, 4.0f, 1 },   // machinery shed
    {  14.0f,  46.0f, 22.0f, 3.0f, 1 },   // switch room
};
constexpr uint8_t kFactoryCount = sizeof(kFactory) / sizeof(kFactory[0]);

Mission route{};

// A stored route is a semicolon list of comma fields: n,e,agl,dwell,camera.
// Parsed with strtod against the caller's buffer, so a corrupt parameter can
// neither allocate nor walk off the end of the waypoint array, and cannot
// produce a half-record the aircraft would fly somewhere unexpected.
bool routeParse(const char* text, Mission& m) {
    missionReset(m);
    const char* p = text;
    while (p && *p && m.count < MISSION_MAX_WAYPOINTS) {
        char* end = nullptr;
        double v[5] = {0, 0, 0, 0, 0};
        v[0] = std::strtod(p, &end);
        if (end == p) break;
        p = end;
        int field = 1;
        while (field < 5 && *p == ',') {
            ++p;
            v[field] = std::strtod(p, &end);
            if (end == p) return false;        // ",," or a trailing comma
            p = end;
            ++field;
        }
        if (field < 5) return false;           // incomplete record
        Waypoint w{};
        w.north = static_cast<float>(v[0]);
        w.east = static_cast<float>(v[1]);
        w.alt = static_cast<float>(v[2]);       // above ground until launch
        w.dwell_s = static_cast<float>(v[3]);
        w.camera = static_cast<uint8_t>(v[4]);
        if (!missionAdd(m, w)) break;
        while (*p == ';') ++p;
    }
    return m.count > 0;
}

void routeLoadOrFactory() {
    char buf[512];
    if (nvGetStr("route", buf, sizeof(buf)) && routeParse(buf, route)) return;
    missionReset(route);
    for (uint8_t i = 0; i < kFactoryCount; ++i) {
        Waypoint w{};
        w.north = kFactory[i].north;
        w.east = kFactory[i].east;
        w.alt = kFactory[i].agl;
        w.dwell_s = kFactory[i].dwell_s;
        w.camera = kFactory[i].camera;
        if (!missionAdd(route, w)) break;
    }
}

void paramsLoad() {
    int32_t v = 0;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", craft_tag, sizeof(craft_tag)) || !craft_tag[0]) {
        std::snprintf(craft_tag, sizeof(craft_tag), "%s", "drone-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);
    if (nvGetI32("launch_ms", v) && v >= 0) auto_launch_ms = static_cast<uint32_t>(v);

    // The decision table is loadable: a fleet that flies without anyone
    // watching the dashboard needs the ground-link rule switched off, and that
    // belongs in a parameter rather than in a second binary.
    FailsafeConfig cfg = safetyFailsafeConfig();
    if (nvGetI32("unattended", v) && v) cfg.gcs_timeout_ms = 0x7FFFFFFFu / 8u;
    safetySetFailsafeConfig(cfg);
}

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", craft_tag);
    w.add("wp", route.count);
    w.endLine();
    consoleWrite(line, w.size());
}

// Diagnostic line, on its own source token so the dashboard cannot mistake it
// for a state frame: a health line that is parsed as telemetry would report
// mode=TRANSIT forever after the last real frame.
void health() {
    UplinkStats u{};
    telemetryStats(u);
    char line[128];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "health");
    w.add("over", static_cast<int>(sched.totalOverruns()));
    w.add("runs", static_cast<int>(sched.totalRuns()));
    w.add("rxcrc", static_cast<int>(parser.crcErrors()));
    w.add("rxdrop", static_cast<int>(parser.dropped()));
    w.add("pub", static_cast<int>(u.published));
    w.add("mqtt", u.broker_connected ? 1 : 0);
    w.add("failed", static_cast<int>(u.mqtt_failed));
    w.add("logged", static_cast<int>(u.logged));
    w.add("nologue", static_cast<int>(u.log_dropped));
    w.add("arm", missionArmName());
    w.endLine();
    consoleWrite(line, w.size());
}

void drainFc() {
    uint8_t buf[128];
    // Bounded per call: a flooded UART must not starve the safety supervisor,
    // which shares this task.
    for (int batch = 0; batch < 4; ++batch) {
        const int n = fcRead(buf, batch == 0 ? sizeof(buf) : 32);
        if (n <= 0) break;
        MavMessage m{};
        for (int i = 0; i < n; ++i) {
            if (!parser.push(buf[i], m)) continue;
            safetyOnMavlink(m);
            missionOnMavlink(m);
        }
    }
}

void savePhoto() {
    MissionStatus ms{};
    missionStatus(ms);
    if (ms.captures == saved_captures) return;
    saved_captures = ms.captures;
    if (!ms.captures) return;
    char path[32];
    std::snprintf(path, sizeof(path), "/photo/%06lu.jpg", static_cast<unsigned long>(ms.captures));
    videoSaveSnapshot(path);
}

void tryLaunch() {
    if (launched || route.count == 0) return;
    // Either somebody asked for this flight, or a bench deliberately configured the
    // timer. The two are kept apart so the log can say which one lifted it.
    const bool timed = auto_launch_ms > 0 &&
                       static_cast<int32_t>(halMillis() - (boot_ms + auto_launch_ms)) >= 0;
    if (!launch_requested && !timed) return;
    if (!safetyAirHasFix()) return;

    int32_t lat_e7 = 0, lon_e7 = 0;
    float home_alt = 0.0f;
    if (!safetyHomeGeodetic(lat_e7, lon_e7, home_alt)) return;

    // AGL to AMSL here, not in the planner: the route is authored on a map in
    // metres above the pad, and the FC is commanded in absolute altitude. The
    // copy keeps `route` in the units it was written in, so a relaunch after a
    // landing cannot add the home altitude twice.
    Mission m = route;
    for (uint8_t i = 0; i < m.count; ++i) m.items[i].alt += home_alt;
    m.arrival_m = RANCH_SUPERVISE_ARRIVAL_M;

    if (!missionUpload(m, MissionSource::Rtc)) return;
    // Upload only. The hand-over sequence in missionTick takes the aircraft
    // from AUTO to airborne one confirmed step at a time, and the planner's
    // phase follows what the FC reports instead of what we hoped for.
    launched = true;
}

void supervision() {
    // A link that has been down for half a minute is not a fade: ask the stack
    // to re-associate instead of waiting for its own backoff.
    const bool up = wifiUp();
    if (up) {
        link_down = false;
        return;
    }
    if (!link_down) {
        link_down = true;
        link_down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (link_down_ms + 30000u)) >= 0) {
        link_down_ms = halMillis();
        wifiReconnect();
    }
}

// "nmea"-free command grammar from the ground app: a verb, optionally
// "verb=value". Anything else is ignored rather than guessed at.
void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[16], value[64];
    size_t i = 0;
    while (i < len && i < sizeof(verb) - 1 && payload[i] != '=' && payload[i] != '\n') {
        verb[i] = payload[i];
        ++i;
    }
    verb[i] = '\0';
    size_t n = 0;
    if (i < len && payload[i] == '=') {
        ++i;
        while (i < len && n < sizeof(value) - 1) value[n++] = payload[i++];
        value[n] = '\0';
    } else {
        value[0] = '\0';
    }
    missionOnCommand(verb, value);
    if (std::strcmp(verb, "safe") == 0) safetyForceSafe(value[0] == '1');
    // The two commands a person actually reaches for: take the safety off, then
    // send it up. `land` also withdraws a launch that has not happened yet, so a
    // cancelled sortie cannot lift off behind the operator's back.
    else if (std::strcmp(verb, "arm") == 0) safetyForceSafe(value[0] != '1');
    else if (std::strcmp(verb, "takeoff") == 0) launch_requested = true;
    else if (std::strcmp(verb, "land") == 0) launch_requested = false;
    safetyNoteGcsContact();
}

uint8_t ledPhase() {
    const uint32_t period = safetyIsSafe() ? 800u : 150u;
    return static_cast<uint8_t>((halMillis() / period) & 1u);
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;              // a stalled loop must not teleport

    drainFc();
    safetyUpdate(dt);

    if (sched.due(job_safety, dt)) {
        armSwitchClosed();
        const bool closed = armSwitchIsClosed();
        if (closed != switch_last) {
            switch_last = closed;
            safetyForceSafe(!closed);
        }
        safetyKick();
        setStatusLed(ledPhase());
    }
    if (sched.due(job_mission, dt)) {
        tryLaunch();
        missionTick(sched.elapsed(job_mission));
        savePhoto();
    }
    if (sched.due(job_telem, dt)) {
        MissionStatus ms{};
        missionStatus(ms);
        telemetryPublish(ms, safetyAir());
#if defined(RANCH_SIM)
        // In the simulation the console is the ground station and it just took
        // the frame, so the dashboard is by definition in contact.
        safetyNoteGcsContact();
#else
        UplinkStats u{};
        telemetryStats(u);
        if (u.broker_connected) safetyNoteGcsContact();
#endif
    }
    // The log drain does SD card I/O and the broker keepalive does socket I/O,
    // so neither runs on the mission job: a slow card must not delay a setpoint.
    if (sched.due(job_uplink, dt)) telemetryService();
    if (sched.due(job_health, dt)) health();
    supervision();
}

#if !defined(RANCH_SIM)
void loopTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(4);
    }
}
#endif

}  // namespace

// The host sandbox drives the command channel the way the ground station does, so
// "arm, then take off" is exercised as a command rather than as a timer nobody had
// to press. Outside the anonymous namespace because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }

// The console's input, as a command door.
//
// In the simulation there is no MQTT broker (telemetry.cpp compiles an empty uplink
// under RANCH_SIM), so the ground command channel that flies this aircraft on the
// bench does not exist in the browser -- the ranch page can read the console and
// nothing else. That made the page's "take off" a scene-only animation: the board
// stayed in GROUND forever and the dashboard, quite correctly, kept saying so.
//
// This reads the other direction of the same port the telemetry is printed to. What
// arrives is exactly what the broker would have delivered: `takeoff`, `land`,
// `arm=1`, one line each. The parser is deliberately the same `onGroundCommand`, so
// a verb the bench accepts and a verb the console accepts cannot drift apart.
//
// ARDUINO only: the host sandbox drives `simCommand` from its own test, and giving
// that build a stdin reader would make a unit test wait on a terminal.
#if defined(ARDUINO)
void consoleCommandPump() {
    static char line[64];
    static size_t n = 0;
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;                    // CRLF from a terminal
        if (c == '\n') {
            line[n] = '\0';
            if (n) simCommand(line);
            n = 0;
            continue;
        }
        if (n < sizeof(line) - 1) line[n++] = static_cast<char>(c);
        else n = 0;   // a line long enough to be nonsense: drop it, keep the port clean
    }
}
#endif
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    launched = false;
    launch_requested = false;
    saved_captures = 0;
    link_down = false;
    link_down_ms = 0;
    parser = MavParser{};
    sched.clear();

    armSwitchClosed();
    // Power-on state of the airframe's arm switch is the kill output's state:
    // a key in the ARM position releases the relay, an open key holds it.
    // Afterwards only a change of the switch moves it, so a command from the
    // ground is not stepped on every 20 ms by a pin read.
    switch_last = armSwitchIsClosed();
    safetyInit();
    safetyForceSafe(!switch_last);
    missionInit();
    telemetryInit();
    paramsLoad();
    routeLoadOrFactory();
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_safety = sched.add("safety", 20);
    job_mission = sched.add("mission", 50);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_uplink = sched.add("uplink", 100);
    job_health = sched.add("health", 5000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, craft_tag);
    if (videoInit()) videoSetStreaming(true);

#if !defined(RANCH_SIM)
    xTaskCreatePinnedToCore(loopTask, "ranch", STACK_MISSION, nullptr, PRIO_MISSION, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
    // Commands first, so a line typed into the console is applied on the same tick
    // its answer is published rather than a scheduler period later.
#if defined(ARDUINO)
    consoleCommandPump();
#endif
    halSimPump();
    runJobs();
    halDelayMs(4);
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
