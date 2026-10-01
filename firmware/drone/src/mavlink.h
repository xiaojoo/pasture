// MAVLink v2 codec: framing, CRC, and the subset of messages the companion
// computer needs. Written by hand rather than generated so the build has no
// codegen step; the field offsets below are the wire order after the v2
// size-descending reordering rule, and each one is commented with the XML
// order it came from.
//
// No dynamic allocation, no exceptions, and a corrupt frame can never advance
// the parser past the buffer.
#pragma once

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
    MSG_RC_CHANNELS_OVERRIDE   = 70,
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

// Stick axes to the flight controller. Channels are PPM microseconds: 0 means
// "release this channel back to the radio", UINT16_MAX (65535) means "ignore this
// field", and a real value sits between 1000 and 2000.
struct RcOverrideTx {
    uint16_t chan[18];
    uint8_t target_sys, target_comp;
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
