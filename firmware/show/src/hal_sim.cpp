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

#include "hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "board.h"
#include "mavlink.h"

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
