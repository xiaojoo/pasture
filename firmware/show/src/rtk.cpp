#include "rtk.h"

#include <cmath>
#include <cstring>

#include "board.h"
#include "hal.h"

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
