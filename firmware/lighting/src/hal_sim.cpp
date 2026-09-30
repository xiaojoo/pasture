// Simulated sky, lamp and driver for the lighting board.
//
// Deliberately not the firmware's own astronomy: the sky here comes from a sun
// altitude computed with the textbook declination approximation, while the board
// decides with the NOAA series. They agree to a few minutes and are derived
// differently, so "the lamp came on while it was still daylight" is a measurable
// disagreement rather than a tautology.
#if defined(RANCH_SIM)

#include "hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "board.h"

namespace ranch {
namespace {

constexpr double SUN_RAD = M_PI / 180.0;
constexpr float NOON_LUX = 110000.0f;
constexpr float CIVIL_DEPTH_RAD = 6.0 * SUN_RAD;      // civil twilight

uint32_t g_now = 0;
// Starts a few minutes before the site civil sunset, with a clock of its own, so
// the dusk ramp is observable in the browser without an RTC or NTP: the board
// runs in real time there. halSimTime() overrides it in a test.
uint32_t sod = 19u * 3600u + 15u * 60u;   // 19:15 local, one minute of daylight left
uint32_t sod_ms = 0;
uint16_t yy = 2026;
uint8_t mm = 4;
uint8_t dd = 18;
bool clock_valid = true;

bool street = false;
bool house = false;
float dim = 0.0f;
bool motion_present = false;
uint32_t motion_last_ms = 0;
bool previous_high = false;
bool driver_fault = false;
bool lamp_blown = false;

float sky_lux = 0.0f;
float lamp_amps = 0.0f;
SkyCounters counters{};

#if defined(RANCH_HOST)
uint32_t g_virt = 0;
inline uint32_t rawMillis() { return g_virt; }
inline void rawSleep(uint32_t ms) { g_virt += ms; }
#else
inline uint32_t rawMillis() { return ::millis(); }
inline void rawSleep(uint32_t ms) { ::delay(ms); }
#endif

void skyStep(uint32_t dt_ms);

void pump() {
    const uint32_t t = rawMillis();
    if (t == g_now) return;
    // The whole elapsed span, not a capped slice. The cap was inherited from the
    // host tests, where a bounded dt keeps the plant model stable -- but the sky has
    // no integrator, and in the browser the firmware loop runs slower than 100 ms per
    // iteration, so capping threw the rest of the time away: measured, 20 minutes of
    // wall clock moved the simulated clock by under a minute, and dusk never arrived.
    const uint32_t dt = t - g_now;
    g_now = t;
    skyStep(dt);
}

double sunAltitudeRad(double lat_deg, int year, int month, int day, uint32_t second_of_day) {
    // Textbook approximation: a sinusoidal declination and local solar time, with
    // no equation-of-time term. It is a different road to the same number, which
    // is the point.
    const int doy = (month - 1) * 31 + day;
    const double decl = 23.44 * SUN_RAD * std::sin(2.0 * M_PI * (doy - 81) / 365.0);
    const double solar_minutes = static_cast<double>(second_of_day / 60);
    // Longitude is folded in so that local noon lands where the ranch's noon is.
    const double local_solar = solar_minutes + (RANCH_LONGITUDE - 15.0 * RANCH_UTC_OFFSET_H) * 4.0;
    // Four minutes of clock per degree of hour angle, not fifteen: getting this
    // wrong makes the simulated sun crawl, and the sky is still bright at dusk.
    const double hour_angle = (local_solar - 720.0) / 4.0 * SUN_RAD;
    const double lat = lat_deg * SUN_RAD;
    const double s = std::sin(lat) * std::sin(decl) +
                     std::cos(lat) * std::cos(decl) * std::cos(hour_angle);
    return std::asin(s > 1.0 ? 1.0 : (s < -1.0 ? -1.0 : s));
}

float luxForAltitude(double alt) {
    if (alt > 0.0) return NOON_LUX * std::pow(std::sin(alt) > 0.0 ? std::sin(alt) : 0.0, 1.2);
    if (alt > -CIVIL_DEPTH_RAD) {
        // Civil twilight: a smooth fall from the horizon value to near dark.
        const float t = static_cast<float>(-alt / CIVIL_DEPTH_RAD);
        return 3.0f + 70.0f * (1.0f - t) * (1.0f - t);
    }
    return 0.15f;
}

// The LDR is the firmware's only view of the sky, so the simulation has to speak
// in millivolts out of the same divider the real one uses: invert the board's
// exponential calibration rather than hand the lux over, which would let a wrong
// calibration pass every test.
uint16_t millivoltsForLux(float lux) {
    if (lux <= LUX_SUNRISE) return static_cast<uint16_t>(LDR_MV_AT_DARK);
    const float lo = LDR_MV_AT_DARK;
    const float hi = LDR_MV_AT_DAYLIGHT;
    const float t = std::log(lux / LUX_SUNRISE) / std::log(LUX_AT_DAYLIGHT / LUX_SUNRISE);
    float mv = lo + t * (hi - lo);
    if (mv < lo) mv = lo;
    if (mv > hi) mv = hi;
    return static_cast<uint16_t>(mv);
}

void skyStep(uint32_t dt_ms) {
    if (clock_valid) {
        sod_ms += dt_ms;
        while (sod_ms >= 1000u) {
            sod_ms -= 1000u;
            sod++;
            if (sod >= 86400u) {
                sod -= 86400u;
                dd++;
                if (dd > 28) {
                    dd = 1;
                    mm++;
                    if (mm > 12) { mm = 1; yy++; }
                }
            }
        }
    }

    sky_lux = clock_valid ? luxForAltitude(sunAltitudeRad(RANCH_LATITUDE, yy, mm, dd, sod)) : 0.15f;

    // The lamp: a driver at duty D draws roughly D times the nominal current, a
    // blown lamp draws nothing, and a driver in fault draws nothing either.
    const bool lit = !driver_fault && !lamp_blown;
    lamp_amps = lit ? dim * LAMP_NOMINAL_A : 0.0f;

    if (motion_present && !previous_high) motion_last_ms = g_now;
    previous_high = motion_present;

    counters.lux = sky_lux;
    counters.lamp_amp = lamp_amps;
    counters.motion = motion_present;
    counters.driver_fault = driver_fault;
    if (dim > counters.max_duty) counters.max_duty = dim;
    const bool anything_lit = street || house || dim > 0.05f;
    if (anything_lit) {
        if (street) counters.street_on_frames++;
        if (house) counters.house_on_frames++;
        if (dim > 0.05f) counters.barn_lit_frames++;
        // The invariant the whole design rests on: nothing burns while the sky
        // says it is day. Counted per tick so the test can bound the energy too.
        if (sky_lux > 120.0f) counters.midday_lit_events++;
    }
    if (dim > 0.05f && lamp_amps < 0.01f) {
        if (counters.lamp_blown_events == 0) counters.lamp_blown_events = 1;
    }
}

struct Kv {
    char key[16];
    uint8_t kind;
    int32_t i;
    float f;
    char s[64];
};
Kv nvs_store[16];

Kv* findKv(const char* key, bool create) {
    const size_t klen = std::strlen(key);
    Kv* slot = nullptr;
    for (auto& k : nvs_store) {
        if (k.kind && std::strlen(k.key) == klen && std::memcmp(k.key, key, klen) == 0) return &k;
        if (!k.kind && !slot) slot = &k;
    }
    if (!create || !slot) return nullptr;
    std::snprintf(slot->key, sizeof(slot->key), "%s", key);
    slot->i = 0;
    slot->f = 0.0f;
    slot->s[0] = '\0';
    return slot;
}

}  // namespace

void halInit() {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.begin(115200);
#endif
    g_now = rawMillis();
    street = false;
    house = false;
    dim = 0.0f;
    driver_fault = false;
    lamp_blown = false;
    counters = SkyCounters{};
}

uint32_t halMillis() { return g_now; }
void halDelayMs(uint32_t ms) { rawSleep(ms); pump(); }
void halSimPump() { pump(); }

void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day) {
    sod = second_of_day;
    yy = year;
    mm = month;
    dd = day;
    sod_ms = 0;
    clock_valid = true;
}

void halSimMotion(bool present) { motion_present = present; }
void halSimClock(bool enabled) { clock_valid = enabled; }
void halSimDriverFault(bool fault) {
    driver_fault = fault;
    lamp_blown = fault;
}

void consoleWrite(const char* data, size_t len) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#else
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
#endif
}

void setRelayStreet(bool on) { street = on; }
void setRelayHouse(bool on) { house = on; }
void setDimDuty(float duty) { dim = duty < 0.0f ? 0.0f : (duty > 1.0f ? 1.0f : duty); }
bool relayStreetIsOn() { return street; }
bool relayHouseIsOn() { return house; }
float dimDuty() { return dim; }

// A PIR module holds its output for its own retrace period, so the level is what
// the sensor says; "how long since it last moved" is the policy's question and is
// answered by motionAgeMs().
bool motionSeen() { return motion_present; }
uint32_t motionAgeMs() {
    if (motion_last_ms == 0) return 0xFFFFFFFFu;
    return g_now - motion_last_ms;
}
bool driverFaultContact() { return driver_fault; }

uint16_t analogMillivolts(uint8_t pin) {
    if (pin == PIN_LDR) return millivoltsForLux(sky_lux);
    if (pin == PIN_BALLAST) {
        // The transformer is a bipolar part: 2.5 V at zero amps. A blown lamp
        // reads zero current even though the channel is healthy.
        return static_cast<uint16_t>(2500.0f + lamp_amps * CT_MV_PER_A);
    }
    return 0;
}

bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (!clock_valid) return false;
    second_of_day = sod;
    year = yy;
    month = mm;
    day = dd;
    return true;
}

void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    sod = static_cast<uint32_t>(hour) * 3600u + static_cast<uint32_t>(minute) * 60u + second;
    yy = year;
    mm = month;
    dd = day;
    sod_ms = 0;
    clock_valid = true;
}

bool clockWasSet() { return clock_valid; }

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

bool wifiConnect(const char*, const char*) { return true; }
bool wifiUp() { return true; }
int wifiRssi() { return -55; }
void wifiReconnect() {}

void feedWatchdog() {}
const char* resetReason() { return "sim"; }

void halSky(SkyCounters& out) { out = counters; }

}  // namespace ranch

#endif  // RANCH_SIM
