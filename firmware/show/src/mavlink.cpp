#include "mavlink.h"

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
