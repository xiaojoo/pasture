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

#include "hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "board.h"
#include "mavlink.h"
#include "mission_planner.h"

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
    // Stick override in microseconds, and when the FC last heard one. ArduCopter
    // times an override out; a simulator that never did would keep flying a stale
    // setpoint after one dropped frame.
    uint16_t rc_us[4] = {1500, 1500, 1500, 1500};
    uint32_t rc_ms = 0;

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

// What a full stick does to this airframe. The timeout is ArduCopter's
// RC_OVERRIDE_TIMEOUT default, so an override that stops being repeated stops
// working -- the same failure the real stack has.
constexpr uint32_t SIM_RC_TIMEOUT_MS = 3000;
constexpr float SIM_STICK_MS = 5.0f;        // full-stick ground speed in a loiter
constexpr float SIM_STICK_CLIMB_MS = 2.5f;  // full-stick climb rate
constexpr float SIM_STICK_YAW_DPS = 60.0f;  // full-stick yaw rate

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
        // A loiter holds position unless the ground station is overriding the
        // sticks, and a stick at full deflection moves this airframe at
        // SIM_STICK_MS -- the same numbers the companion's own limits imply.
        tn = fc.n;
        te = fc.e;
        ta = fc.alt;
        if (g_now - fc.rc_ms < SIM_RC_TIMEOUT_MS) {
            const float roll = (fc.rc_us[0] - 1500) / 500.0f;
            const float pitch = (fc.rc_us[1] - 1500) / 500.0f;
            const float thr = (fc.rc_us[2] - 1500) / 500.0f;
            const float yaw = (fc.rc_us[3] - 1500) / 500.0f;
            const float rad = fc.heading_deg / 57.29578f;
            // ArduCopter's stick axes are the body's, so the FC's own reported
            // heading is what turns them into world motion: a right stick with the
            // nose east moves the aircraft south, not north.
            const float fwd = -pitch * SIM_STICK_MS;     // nose down (low us) is forward
            const float right = roll * SIM_STICK_MS;
            tn += (fwd * std::cos(rad) - right * std::sin(rad)) * dt;
            te += (fwd * std::sin(rad) + right * std::cos(rad)) * dt;
            ta += thr * SIM_STICK_CLIMB_MS * dt;
            if (ta < SIM_HOME_ALT_M + 0.5f) ta = SIM_HOME_ALT_M + 0.5f;
            if (yaw != 0.0f) {
                fc.heading_deg += yaw * SIM_STICK_YAW_DPS * dt;
                if (fc.heading_deg < 0.0f) fc.heading_deg += 360.0f;
                if (fc.heading_deg >= 360.0f) fc.heading_deg -= 360.0f;
            }
        }
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
        // A loiter is the exception: there the sticks are body-frame commands, and a
        // nose that chased its own velocity would curl the aircraft into a spiral
        // instead of sliding it sideways. Only the yaw stick turns it.
        if (fc.mode != FC_MODE_LOITER) {
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
    if (m.id == MSG_RC_CHANNELS_OVERRIDE) {
        // ArduCopter honours an override only in a mode that takes manual input.
        // In AUTO/RTL/LAND the frame is read and dropped, so a setpoint sent into
        // the wrong mode shows up here as no motion instead of as a simulator that
        // flatters whatever the companion transmitted.
        if (!fc.armed || fc.mode != FC_MODE_LOITER) return;
        // Wire order: chan1..chan8 as 16-bit, so the four stick channels are the
        // first eight bytes.
        for (int i = 0; i < 4; ++i) {
            const uint16_t v = m.u16At(i * 2);
            // 0 = release this channel, 65535 = ignore it (keep what was flying).
            if (v == 0) fc.rc_us[i] = 1500;
            else if (v != 0xFFFF) fc.rc_us[i] = v;
        }
        fc.rc_ms = g_now;
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
