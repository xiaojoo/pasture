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
#pragma once

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
