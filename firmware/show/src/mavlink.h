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
#pragma once

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
