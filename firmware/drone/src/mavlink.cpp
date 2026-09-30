#include "mavlink.h"

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
