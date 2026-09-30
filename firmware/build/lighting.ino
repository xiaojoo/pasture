// GENERATED FILE - do not edit, edit the project and re-run:
//   node tools/bundle.mjs
// Source: firmware/lighting + firmware/lib  (12 files, 64.1 KB before bundling)
// The sky in hal_sim.cpp is computed a different way from lib/astro.h on purpose.
//
// Build the same code for hardware with:  pio run -d firmware/lighting

#define RANCH_SIM 1

/* ==================== src/board.h ==================== */

// Pin map and photometric configuration for the ranch lighting board.
//
// One board drives the yard perimeter, the barn dimmer and the house porch, and
// switches them on the sun rather than on a clock, because the ranch's working day
// moves with the season and a fixed 18:00 timer is an hour wrong in June.
//
// Allocation rules, proven by the static_asserts below:
//   1. no GPIO appears twice
//   2. nothing on the SPI flash pins (6-11, 16-17), and no output on the
//      input-only pins (34-36, 39)
//   3. both analog inputs on ADC1: ADC2 cannot be read with WiFi associated

#include <cstddef>

// --- lamps ------------------------------------------------------------------
#define PIN_RELAY_STREET     4      // yard perimeter contactor
#define PIN_RELAY_HOUSE      2      // porch and kitchen outside; the dev kit's own
                                    // LED is on this pin, so a switching is visible
#define PIN_DIM_BARN        27      // 0-10 V driver for the barn high bay

// --- sensors ----------------------------------------------------------------
#define PIN_PIR             35      // input only: barn motion, active high module
#define PIN_LDR             36      // ADC1_CH0: daylight sensor at the eaves
#define PIN_BALLAST         39      // ADC1_CH3 (VN): current transformer on the lamp bus
#define PIN_FAULT           34      // driver fault contact, open = healthy

// --- bus, clock, watchdog ---------------------------------------------------
#define PIN_RTC_SDA         21      // DS3231: the schedule survives a power cut
#define PIN_RTC_SCL         22
#define PIN_WDT_FEED        18

// --- dimmer -----------------------------------------------------------------
// 1 kHz, not the LEDC default: a 0-10 V driver's output filter buzzes at 20 kHz
// and the cattle hear it.
#define DIM_HZ              1000
#define DIM_BITS            10

// --- photometrics -----------------------------------------------------------
// The LDR divider is log-ish, so the conversion below is a calibrated table
// fit, not a physics model: it only has to answer "is it daylight".
#ifndef LDR_MV_AT_DARK
#define LDR_MV_AT_DARK      120.0f
#endif
#ifndef LDR_MV_AT_DAYLIGHT
#define LDR_MV_AT_DAYLIGHT  2600.0f
#endif
#ifndef LUX_AT_DAYLIGHT
#define LUX_AT_DAYLIGHT     400.0f
#endif
#ifndef LUX_SUNRISE
#define LUX_SUNRISE         3.0f
#endif

// --- lamp bus ---------------------------------------------------------------
#define CT_ZERO_MV          2500.0f     // current transformer live zero
#define CT_MV_PER_A         100.0f      // ACS712-20A sensitivity

#ifndef LAMP_NOMINAL_A
#define LAMP_NOMINAL_A      3.2f    // one high bay at full duty
#endif
#ifndef LAMP_DEAD_A
#define LAMP_DEAD_A         0.15f   // commanded on, drawing nothing: driver or lamp dead
#endif
#ifndef LAMP_OVER_A
#define LAMP_OVER_A         5.5f
#endif
#ifndef LAMP_CONFIRM_S
#define LAMP_CONFIRM_S      12.0f   // how long a discrepancy must last to be a fault
#endif

// --- site -------------------------------------------------------------------
// Latitude, longitude and the UTC offset are the only numbers that make the sun
// times local. They are parameters, not constants, because the day the ranch
// moves the board is the day they change.
#ifndef RANCH_LATITUDE
#define RANCH_LATITUDE      30.5
#endif
#ifndef RANCH_LONGITUDE
#define RANCH_LONGITUDE     114.3
#endif
#ifndef RANCH_UTC_OFFSET_H
#define RANCH_UTC_OFFSET_H  8.0
#endif

// --- uplink -----------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/light/state"
#define MQTT_TOPIC_CMD        "ranch/light/cmd"
#define TELEMETRY_HZ          1

// --- task layout ------------------------------------------------------------
#define PRIO_LIGHT            16
#define PRIO_TELEMETRY        10
#define STACK_LIGHT           6144
#define STACK_TELEMETRY       6144
#define TASK_WDT_TIMEOUT_MS   3000

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

constexpr bool pinsAvoidFlash(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p >= 6 && p <= 11) return false;
    if (p == 16 || p == 17) return false;
    return pinsAvoidFlash(v, n, i + 1);
}

constexpr bool outputsAreDriven(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p == 34 || p == 35 || p == 36 || p == 39) return false;
    return outputsAreDriven(v, n, i + 1);
}

constexpr bool analogIsAdc1(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    const int p = v[i];
    if (p < 32 || p > 39) return false;
    return analogIsAdc1(v, n, i + 1);
}

// The DevKit V1 only breaks out 26 of the ESP32's 34 usable GPIOs: 37 and 38 exist
// on the chip (and are valid ADC1 channels) but never reach a header, and 0 is inside
// the USB-serial bridge's flow-control circuit. An allocation that passes every other
// rule here still cannot be wired on the board a person actually buys, so the set
// below is the header, read off the part the simulator loads.
constexpr bool pinOnHeader(int p) {
    const int k[] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (k[i] == p) return true;
    return false;
}

constexpr bool pinsOnHeader(const int* v, size_t n, size_t i = 0) {
    if (i >= n) return true;
    if (!pinOnHeader(v[i])) return false;
    return pinsOnHeader(v, n, i + 1);
}

constexpr int kOutputs[] = { PIN_RELAY_STREET, PIN_RELAY_HOUSE, PIN_DIM_BARN,
                             PIN_RTC_SDA, PIN_RTC_SCL, PIN_WDT_FEED };
constexpr int kAnalog[] = { PIN_LDR, PIN_BALLAST };
constexpr int kAll[] = {
    PIN_RELAY_STREET, PIN_RELAY_HOUSE, PIN_DIM_BARN, PIN_PIR, PIN_LDR, PIN_BALLAST,
    PIN_FAULT, PIN_RTC_SDA, PIN_RTC_SCL, PIN_WDT_FEED,
};
constexpr size_t kAllLen = sizeof(kAll) / sizeof(kAll[0]);
constexpr size_t kOutLen = sizeof(kOutputs) / sizeof(kOutputs[0]);
constexpr size_t kAnLen = sizeof(kAnalog) / sizeof(kAnalog[0]);

static_assert(pinsDisjoint(kAll, kAllLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidFlash(kAll, kAllLen), "board.h: a pin sits on the SPI flash bus");
static_assert(outputsAreDriven(kOutputs, kOutLen), "board.h: an output is on an input-only pin");
static_assert(analogIsAdc1(kAnalog, kAnLen), "board.h: an analog input must be on ADC1");
static_assert(pinsOnHeader(kAll, kAllLen), "board.h: a pin is not broken out on the DevKit V1 header");

}  // namespace ranch
#endif

/* ==================== src/hal.h ==================== */

// Hardware abstraction for the lighting board. Two implementations:
// hal_esp32.cpp drives contactors and a 0-10 V driver; hal_sim.cpp closes the
// loop against a sky and a lamp, so the firmware's lux and current readings come
// from what a day and a driver would actually do.

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);
void consoleWrite(const char* data, size_t len);

// Outputs. The street and house circuits are contactors (on or off); the barn is
// a dimmer channel that takes 0..1 and owns the ramp between ticks.
void setRelayStreet(bool on);
void setRelayHouse(bool on);
void setDimDuty(float duty);
bool relayStreetIsOn();
bool relayHouseIsOn();
float dimDuty();

// Sensors.
bool motionSeen();                   // PIR input as it stands this instant
uint32_t motionAgeMs();              // since the last trigger
uint16_t analogMillivolts(uint8_t pin);
bool driverFaultContact();           // open = healthy, closed = the driver is unhappy

// Wall clock, same contract as the other boards: false when nothing has answered.
bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day);
void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second);
bool clockWasSet();

bool nvGetI32(const char* key, int32_t& out);
bool nvSetI32(const char* key, int32_t value);
bool nvGetF32(const char* key, float& out);
bool nvSetF32(const char* key, float value);
bool nvGetStr(const char* key, char* buf, size_t cap);
bool nvSetStr(const char* key, const char* value);

bool wifiConnect(const char* ssid, const char* pass);
bool wifiUp();
int  wifiRssi();
void wifiReconnect();

void feedWatchdog();
const char* resetReason();

#if defined(RANCH_SIM)
void halSimPump();
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
void halSimMotion(bool present);
// Take the RTC away without losing the simulated time, so "no clock" can be
// tested on a board that had one. A dead coin cell looks exactly like this.
void halSimClock(bool enabled);
void halSimDriverFault(bool fault);

// What the simulated sky and lamp did, so the host test can assert that a
// commanded lamp actually drew current, and that nothing lit at midday.
struct SkyCounters {
    float lux;                  // sky brightness the sensor saw
    float max_duty;             // brightest the barn driver was ever asked for
    float lamp_amp;             // current the bus drew at the last sample
    uint32_t street_on_frames;
    uint32_t house_on_frames;
    uint32_t barn_lit_frames;
    uint32_t lamp_blown_events;
    uint32_t midday_lit_events; // a lamp on while the sky said daylight
    bool driver_fault;
    bool motion;
};
void halSky(SkyCounters& out);
#endif

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

/* ==================== ../lib/astro.h ==================== */

// Sun position for the lighting board: sunrise, sunset and the twilight
// boundaries, from latitude, longitude, date and the solar zenith wanted.
//
// Platform neutral, and the coefficients are the published NOAA Almanac ones
// (Fourier series for declination and equation of time), so the tests can check
// the output against a printed almanac instead of against itself.
//
// Why the zenith is a parameter and not a constant: "lights on" is not sunrise.
// A ranch switches at civil twilight (96 deg), when the cattle are moving and the
// floodlights still do something useful, and a house porch waits for the sun to
// actually be down (90.833 deg, the official sunrise angle including refraction).

#include <cmath>
#include <cstdint>

namespace ranch {

// Minutes of local time always come back in [0, 1440), whatever the longitude or
// the time zone did to the intermediate value.
inline double wrapMinutes(double m) {
    double x = std::fmod(m, 1440.0);
    if (x < 0.0) x += 1440.0;
    return x;
}

constexpr double SUN_OFFICIAL_ZENITH = 90.833;   // upper limb plus refraction
constexpr double SUN_CIVIL_ZENITH = 96.0;        // 6 deg below the horizon
constexpr double SUN_NAUTICAL_ZENITH = 102.0;
constexpr double SUN_DEG = 0.017453292519943295;

struct SunTimes {
    // Minutes past local midnight. Negative when the event does not happen:
    // within the polar day or night there is no sunrise to switch on.
    float event_min;
    float sunrise_min;
    float sunset_min;
    float day_length_min;
    bool valid;
    bool circumpolar;      // no sunrise and no sunset on this date at this latitude
};

inline int daysFromCivil(int y, int m, int d) {
    // Howard Hinnant's days_from_civil: no month-length table, no leap special
    // case, and it is exact for the whole proleptic Gregorian range we need.
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

inline unsigned dayOfYear(int y, int m, int d) {
    const int cur = daysFromCivil(y, m, d);
    const int jan1 = daysFromCivil(y, 1, 1);
    return static_cast<unsigned>(cur - jan1 + 1);
}

// `tz_hours` is the board's local offset, because the programme is expressed in
// the time the ranch lives in. `lon_deg` is east positive.
inline SunTimes sunTimes(int year, int month, int day, double lat_deg, double lon_deg,
                         double zenith_deg, double tz_hours) {
    SunTimes out{};
    out.event_min = -1.0f;
    out.sunrise_min = -1.0f;
    out.sunset_min = -1.0f;
    out.day_length_min = 0.0f;
    out.valid = false;
    out.circumpolar = false;

    // Fractional year, accurate to well under a minute of time for the purposes
    // of switching a lamp.
    const double n = static_cast<double>(dayOfYear(year, month, day));
    const double g = 2.0 * M_PI / 365.0 * (n - 1.0);

    const double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) -
                        0.006758 * std::cos(2.0 * g) + 0.000907 * std::sin(2.0 * g) -
                        0.002697 * std::cos(3.0 * g) + 0.001480 * std::sin(3.0 * g);
    const double eqtime = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) -
                                    0.014615 * std::cos(2.0 * g) - 0.040849 * std::sin(2.0 * g));

    const double lat_r = lat_deg * SUN_DEG;
    const double cos_ha = (std::cos(zenith_deg * SUN_DEG) / (std::cos(lat_r) * std::cos(decl))) -
                          std::tan(lat_r) * std::tan(decl);
    // |cos ha| > 1 is the polar case: the sun never reaches this circle, either
    // above it or below it. Report it instead of clamping, which would invent a
    // sunrise at the wrong hour and light the yard at noon.
    if (cos_ha > 1.0 || cos_ha < -1.0) {
        out.circumpolar = true;
        out.valid = false;
        return out;
    }

    const double ha_deg = std::acos(cos_ha) / SUN_DEG;      // half arc of the day
    // Solar noon in UTC, then the hour angle either side of it, at four minutes
    // of time per degree of longitude or arc. (The hour angle is an angle, not an
    // hours value: multiplying it by 15 as well puts sunrise nine hours late.)
    const double noon_utc = 720.0 - 4.0 * lon_deg - eqtime;
    const double rise = noon_utc - 4.0 * ha_deg + tz_hours * 60.0;
    const double set = noon_utc + 4.0 * ha_deg + tz_hours * 60.0;

    out.sunrise_min = static_cast<float>(wrapMinutes(rise));
    out.sunset_min = static_cast<float>(wrapMinutes(set));
    out.day_length_min = out.sunset_min - out.sunrise_min;
    if (out.day_length_min < 0.0f) out.day_length_min += 1440.0f;
    out.valid = true;
    return out;
}

// True between sunrise and sunset, false outside, and undecided on a polar day:
// a controller that has to choose picks the safe answer, which is "dark", because
// the alternative is that nothing ever switches on.
inline bool sunIsUp(const SunTimes& t, uint32_t minute_of_day) {
    if (!t.valid) return false;
    return minute_of_day > static_cast<uint32_t>(t.sunrise_min) &&
           minute_of_day < static_cast<uint32_t>(t.sunset_min);
}

}  // namespace ranch

/* ==================== ../lib/light_policy.h ==================== */

// Switching and dimming policy for the ranch lighting board. Pure: it takes the
// sun times and the sensors and returns lamp states, so the whole dusk ramp can
// be tested on the host without a lamp in the room.
//
// Three things this encodes that a plain "on at 18:00" timer does not:
//  - the ramp. A floodlight that steps from dark to full at dusk startles the
//    cattle and trips the driver's inrush; 30 minutes of ramp is the fix.
//  - the economy window. After the yard is empty the barn holds a third of the
//    light, and goes to full only while something is moving in it.
//  - the override that expires. A tap left on at 21:00 must not burn until the
//    spring, so every manual state carries a countdown.

#include <cmath>
#include <cstdint>

namespace ranch {

constexpr uint32_t WEEK_MINUTES = 10080u;

struct LightConfig {
    uint16_t dusk_ramp_s;        // dark to full
    uint16_t dawn_ramp_s;        // full to dark
    uint16_t economy_after_min;  // minutes after dusk before the economy step
    float economy_duty;          // 0..1
    float motion_duty;           // 0..1 while motion is recent
    uint16_t motion_hold_s;
    uint16_t house_off_min;      // minutes after dusk the house goes off
    uint32_t override_s;         // how long a manual state is honoured
    float daylight_lux_hold;     // above this, nothing switches on
};

constexpr LightConfig LIGHT_DEFAULTS{
    /*dusk_ramp_s*/      1800,
    /*dawn_ramp_s*/      900,
    /*economy_after_min*/ 180,
    /*economy_duty*/     0.34f,
    /*motion_duty*/      1.0f,
    /*motion_hold_s*/    240,
    /*house_off_min*/    330,
    /*override_s*/       5400,
    /*daylight_lux*/     120.0f,
};

struct LightInput {
    uint32_t minute_of_day;        // local
    float civil_sunrise_min;       // zenith 96, or -1 when unavailable
    float civil_sunset_min;
    float official_sunrise_min;    // zenith 90.833
    float official_sunset_min;
    bool motion;
    uint32_t motion_age_s;
    float lux;
    bool lux_valid;
    bool street_override;          // manual, from the app or the wall switch
    bool house_override;
    uint32_t override_left_s;      // 0 = no override in force
    bool clock_valid;
};

enum class LightMode : uint8_t { Astro, DaylightHold, Override, Economy, Motion, NoClock, Polar };

struct LightOutput {
    bool street;
    bool house;
    float barn_duty;        // 0..1 on the 0-10 V driver
    LightMode mode;
};

inline const char* lightModeName(LightMode m) {
    switch (m) {
        case LightMode::Astro:        return "ASTRO";
        case LightMode::DaylightHold: return "DAYLIGHT";
        case LightMode::Override:     return "MANUAL";
        case LightMode::Economy:      return "ECONOMY";
        case LightMode::Motion:       return "MOTION";
        case LightMode::NoClock:      return "NO CLOCK";
        case LightMode::Polar:        return "FIXED TIME";
    }
    return "?";
}

// Minutes from `from` to `to`, forward around midnight.
inline int32_t minutesBetween(uint32_t from, uint32_t to) {
    const int32_t a = static_cast<int32_t>(from % WEEK_MINUTES);
    const int32_t b = static_cast<int32_t>(to % WEEK_MINUTES);
    int32_t d = b - a;
    if (d < 0) d += 1440;
    return d;
}

// A negative event time means the sun never crossed that circle, so the caller
// falls back to the fixed clock times rather than to "always" or "never".
inline bool eventUsable(float minutes) {
    return minutes >= 0.0f && minutes < 1440.0f;
}

inline LightOutput lightDecide(const LightConfig& cfg, const LightInput& in) {
    LightOutput out{};
    out.street = false;
    out.house = false;
    out.barn_duty = 0.0f;
    out.mode = LightMode::Astro;

    float dusk = in.civil_sunset_min;
    float dawn = in.civil_sunrise_min;
    if (!in.clock_valid) {
        out.mode = LightMode::NoClock;
        return out;                       // nothing is scheduled without a clock
    }
    if (!eventUsable(dusk) || !eventUsable(dawn)) {
        // No twilight at this latitude on this date: fall back to clock times so
        // the yard is never left dark because the maths refused to answer.
        dusk = 1140.0f;                   // 19:00
        dawn = 330.0f;                    // 05:30
        out.mode = LightMode::Polar;
    }

    const bool daylight = in.lux_valid && in.lux > cfg.daylight_lux_hold;
    const int32_t after_dusk = minutesBetween(static_cast<uint32_t>(dusk), in.minute_of_day);
    // The night is the window from dusk to dawn, and it wraps midnight, so
    // everything is measured forward from dusk and compared against its length.
    // (Testing "now is before dawn" separately is wrong at 12:00: both distances
    // look small enough and the yard lights come on at midday.)
    const int32_t night_len = minutesBetween(static_cast<uint32_t>(dusk),
                                             static_cast<uint32_t>(dawn));
    const bool night = after_dusk >= 0 && after_dusk <= night_len;
    const int32_t before_dawn = night_len - after_dusk;

    if (in.override_left_s > 0) {
        out.street = in.street_override;
        out.house = in.house_override;
        out.barn_duty = in.street_override ? 1.0f : 0.0f;
        out.mode = LightMode::Override;
        // A manual state still does not switch on in daylight: the sensor wins
        // over the tap, because the tap cannot see the sky.
        if (daylight) {
            out.street = false;
            out.house = false;
            out.barn_duty = 0.0f;
            out.mode = LightMode::DaylightHold;
        }
        return out;
    }

    if (daylight) {
        out.mode = LightMode::DaylightHold;
        return out;
    }

    if (!night) {
        out.mode = LightMode::Astro;
        return out;                       // daytime: everything off
    }

    out.street = true;                    // perimeter stays lit all night
    out.house = after_dusk <= cfg.house_off_min;

    // The ramp is measured against dusk, so a shift in the sun moves the lamps
    // with it instead of leaving them an hour early.
    const float ramp_s = cfg.dusk_ramp_s ? cfg.dusk_ramp_s : 1;
    const float elapsed_s = static_cast<float>(after_dusk) * 60.0f;
    float duty = elapsed_s / static_cast<float>(ramp_s);
    if (duty > 1.0f) duty = 1.0f;

    // Dawn ramp down takes precedence in the last window before sunrise.
    const float to_dawn_s = static_cast<float>(before_dawn) * 60.0f;
    const float dawn_s = cfg.dawn_ramp_s ? cfg.dawn_ramp_s : 1;
    if (to_dawn_s < dawn_s) {
        const float down = 1.0f - (dawn_s - to_dawn_s) / dawn_s;
        duty = down < duty ? down : duty;
    }

    if (after_dusk >= cfg.economy_after_min && duty > cfg.economy_duty) {
        duty = cfg.economy_duty;
        out.mode = LightMode::Economy;
    }
    if (in.motion && in.motion_age_s <= cfg.motion_hold_s) {
        duty = cfg.motion_duty;
        out.mode = LightMode::Motion;
    }

    out.barn_duty = duty < 0.0f ? 0.0f : (duty > 1.0f ? 1.0f : duty);
    return out;
}

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

/* ==================== src/lights.h ==================== */

// The only module that moves a lamp. The policy in firmware/lib says what level
// each circuit should be at; this decides how fast it may get there, checks that
// the lamp drew the current it was supposed to, and keeps the burn hours that
// maintenance asks for.
//
// Two mechanisms that look like one and are not:
//  - the dusk ramp is a *schedule* decision (the policy), measured in minutes
//  - the slew limit here is a *hardware* limit, measured in percent per second,
//    and it is what stops a command from the app stepping a cold high bay from
//    dark to full in one tick

#include <cstdint>

namespace ranch {

enum LampCircuit : uint8_t { LAMP_STREET = 0, LAMP_HOUSE = 1, LAMP_BARN = 2, LAMP_COUNT = 3 };

struct LampFault {
    bool barn_dead;        // commanded bright, drawing nothing: lamp or driver gone
    bool barn_over;        // drawing more than the bus is rated for
    bool driver_contact;   // the driver's own fault relay
    bool latched;          // over-current cut the output and stays cut until cleared
};

struct LampState {
    bool street;
    bool house;
    float duty_target;
    float duty_now;
    float amps;
    float lux;
    bool lux_valid;
    LampFault fault;
    uint32_t seconds_to_full;   // ramp distance the slew limiter is working through
};

void lightsInit();

// Requested states from the policy. Level, not edge: calling this with the same
// values every tick is the normal case.
void lightsRequest(bool street, bool house, float barn_duty);

void lightsTick(uint32_t dt_ms);

const LampState& lightsGet();

// Cumulative energised time, in tenths of a second so a minute of bookkeeping
// does not need a float. Persisted by main.cpp on a minute boundary.
uint32_t lightsBurnTenths(LampCircuit which);
void lightsSetBurnTenths(LampCircuit which, uint32_t tenths);

// The over-current latch is the only thing that can be cleared; a dead lamp is a
// maintenance finding, not something the board should keep retrying into a fire.
void lightsClearLatch();

}  // namespace ranch

/* ==================== src/lights.cpp ==================== */


#include <cmath>

namespace ranch {
namespace {

// Percent of full per second the driver may move. A cold high bay takes a few
// seconds to strike, and the inrush on a bus that is already carrying a lamp
// makes an instant step the fastest way to trip the driver.
constexpr float SLEW_PER_SECOND = 0.08f;
constexpr float MIN_USEFUL_DUTY = 0.05f;
constexpr uint16_t ADC_OPEN_MV = 100;
constexpr uint16_t ADC_RAIL_MV = 4900;

float duty_target = 0.0f;
float duty_now = 0.0f;
bool want_street = false;
bool want_house = false;
bool street_now = false;
bool house_now = false;
LampState state{};
uint32_t burn[LAMP_COUNT] = {0, 0, 0};
float dead_s = 0.0f;
float over_s = 0.0f;
uint32_t burn_carry = 0;

// An LDR on a resistor divider is logarithmic in illuminance, so the calibration
// is an exponential fit between the two measured points rather than a straight
// line, which would report "daylight" for a bright moon.
float luxFromMillivolts(uint16_t mv) {
    const float lo = LDR_MV_AT_DARK;
    const float hi = LDR_MV_AT_DAYLIGHT;
    const float x = static_cast<float>(mv);
    if (x <= lo) return 0.0f;
    float t = (x - lo) / (hi - lo);
    if (t > 1.0f) t = 1.0f;
    const float ratio = LUX_AT_DAYLIGHT / LUX_SUNRISE;
    return LUX_SUNRISE * std::pow(ratio, t);
}

}  // namespace

void lightsInit() {
    duty_target = 0.0f;
    duty_now = 0.0f;
    want_street = false;
    want_house = false;
    street_now = false;
    house_now = false;
    state = LampState{};
    dead_s = 0.0f;
    over_s = 0.0f;
    burn_carry = 0;
    setRelayStreet(false);
    setRelayHouse(false);
    setDimDuty(0.0f);
}

void lightsRequest(bool street, bool house, float barn_duty) {
    if (barn_duty < 0.0f) barn_duty = 0.0f;
    if (barn_duty > 1.0f) barn_duty = 1.0f;
    want_street = street;
    want_house = house;
    duty_target = barn_duty;
}

void lightsTick(uint32_t dt_ms) {
    const float dt_s = dt_ms / 1000.0f;

    // --- contactors ----------------------------------------------------------
    street_now = want_street && !state.fault.latched;
    house_now = want_house;
    setRelayStreet(street_now);
    setRelayHouse(house_now);

    // --- dimmer slew ---------------------------------------------------------
    const float step = SLEW_PER_SECOND * dt_s;
    if (duty_now < duty_target) {
        duty_now += step;
        if (duty_now > duty_target) duty_now = duty_target;
    } else if (duty_now > duty_target) {
        duty_now -= step;
        if (duty_now < duty_target) duty_now = duty_target;
    }
    // Over-current is the one condition that cuts the output on its own.
    if (state.fault.latched) duty_now = 0.0f;
    setDimDuty(duty_now);
    state.duty_now = duty_now;
    state.duty_target = duty_target;
    state.street = street_now;
    state.house = house_now;

    // --- what the lamp actually did ------------------------------------------
    const uint16_t amv = analogMillivolts(PIN_BALLAST);
    if (amv < ADC_OPEN_MV || amv > ADC_RAIL_MV) {
        state.amps = 0.0f;
        // No current reading is not evidence of a dead lamp: it is evidence of a
        // broken sensor, and only the driver's own contact may fault the circuit.
        dead_s = 0.0f;
        over_s = 0.0f;
    } else {
        const float a = (amv - CT_ZERO_MV) / CT_MV_PER_A;
        state.amps = a > -0.15f && a < 0.15f ? 0.0f : a;
    }
    state.fault.driver_contact = driverFaultContact();

    const bool commanded_bright = duty_now > MIN_USEFUL_DUTY;
    if (commanded_bright && state.amps < LAMP_DEAD_A) {
        dead_s += dt_s;
        if (dead_s >= LAMP_CONFIRM_S) state.fault.barn_dead = true;
    } else if (!commanded_bright) {
        dead_s = 0.0f;
    }
    if (commanded_bright && state.amps > LAMP_OVER_A) {
        over_s += dt_s;
        if (over_s >= 2.0f) {
            // Two seconds, not twelve: a bus that is over its rating is already
            // hot, and the point of the check is to stop before the magic smoke.
            state.fault.barn_over = true;
            state.fault.latched = true;
        }
    } else {
        over_s = 0.0f;
    }

    // --- the sky --------------------------------------------------------------
    const uint16_t lmv = analogMillivolts(PIN_LDR);
    if (lmv < ADC_OPEN_MV || lmv > ADC_RAIL_MV) {
        state.lux_valid = false;
    } else {
        state.lux_valid = true;
        state.lux = luxFromMillivolts(lmv);
    }

    // --- burn hours -----------------------------------------------------------
    burn_carry += dt_ms;
    while (burn_carry >= 100u) {
        burn_carry -= 100u;
        if (street_now) burn[LAMP_STREET]++;
        if (house_now) burn[LAMP_HOUSE]++;
        if (duty_now > MIN_USEFUL_DUTY) burn[LAMP_BARN]++;
    }
    state.seconds_to_full = duty_now > duty_target
                                ? 0u
                                : static_cast<uint32_t>((duty_target - duty_now) / SLEW_PER_SECOND);
}

const LampState& lightsGet() { return state; }

uint32_t lightsBurnTenths(LampCircuit which) {
    return which < LAMP_COUNT ? burn[which] : 0u;
}

void lightsSetBurnTenths(LampCircuit which, uint32_t tenths) {
    if (which < LAMP_COUNT) burn[which] = tenths;
}

void lightsClearLatch() {
    state.fault.latched = false;
    state.fault.barn_over = false;
    over_s = 0.0f;
}

}  // namespace ranch

/* ==================== src/telemetry.h ==================== */

// State uplink for the lighting board.
//
// `sunrise`/`sunset` are reported as minutes past local midnight so the dashboard
// can show what the board decided the sky would do, and `why` names the rule that
// won. A lamp that is off because it is daytime must not look like a lamp that is
// off because the board is dead.

#include <cstddef>

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint16_t last_len;
    bool broker_connected;
};

struct LightReport {
    bool street;
    bool house;
    float barn_duty;
    float lux;
    bool lux_valid;
    float amps;
    LightMode mode;
    float sunrise_min;
    float sunset_min;
    uint32_t minute_of_day;
    bool clock_valid;
    bool motion;
    bool override_pending;
    uint32_t override_left_s;
    LampFault fault;
    uint32_t burn_tenths[LAMP_COUNT];
};

void telemetryInit();
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const LightReport& r);
void telemetryEvent(const char* kind);

void telemetryService();
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();

}  // namespace ranch

/* ==================== src/telemetry.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

constexpr size_t FRAME_CAP = 192;

UplinkStats up{};
char frame[FRAME_CAP];
char broker_host[64] = "";
char broker_client[24] = "light";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

// Minutes past local midnight as hh:mm, which is what a person reading the
// dashboard wants; the float version is what the policy works in.
void hhmm(char (&dst)[12], float minutes) {
    if (minutes < 0.0f || minutes >= 1440.0f) {
        std::snprintf(dst, sizeof(dst), "%s", "--:--");
        return;
    }
    // Clamped rather than wrapped, so the range is something the compiler can
    // see and a formatted time cannot ask for more buffer than it has.
    int m = static_cast<int>(minutes + 0.5f);
    if (m < 0) m = 0;
    if (m > 1439) m = 1439;
    std::snprintf(dst, sizeof(dst), "%02d:%02d", m / 60, m % 60);
}

}  // namespace

#if defined(RANCH_SIM)
namespace {
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

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

void onMqttPayload(char* topic, uint8_t* payload, unsigned int len) {
    if (!cmd_fn) return;
    if (std::strcmp(topic, MQTT_TOPIC_CMD) != 0) return;
    cmd_fn(reinterpret_cast<const char*>(payload), len);
}

void brokerService() {
    const uint32_t now = halMillis();
    if (*broker_host == '\0') return;
    if (!broker.connected()) {
        if (static_cast<int32_t>(now - next_connect) < 0) return;
        next_connect = now + 5000;
        broker.setServer(broker_host, broker_port);
        broker.setBufferSize(256);
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
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "light");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const LightReport& r) {
    char rise[12], set[12];
    hhmm(rise, r.sunrise_min);
    hhmm(set, r.sunset_min);

    FrameWriter w(frame, sizeof(frame));
    w.begin("LIGHT");
    // The board's own reading of the clock, in minutes past local midnight. Every
    // other field is a decision, and a decision cannot be checked against the
    // sky without knowing when the board thought it was.
    w.add("t", static_cast<int>(r.minute_of_day));
    w.add("street", r.street ? "on" : "off");
    w.add("house", r.house ? "on" : "off");
    w.add("duty", r.barn_duty * 100.0f, 0);
    w.add("lux", r.lux_valid ? r.lux : -1.0f, 1);
    w.add("amp", r.amps, 2);
    w.add("mode", lightModeName(r.mode));
    w.add("rise", rise);
    w.add("set", set);
    w.add("link", wifiUp() ? "up" : "down");
    if (r.motion) w.add("motion", 1);
    if (r.override_pending) w.add("manual", static_cast<int>(r.override_left_s));
    if (!r.clock_valid) w.add("clock", "unset");
    if (r.fault.barn_dead) w.add("fault", "LAMP");
    else if (r.fault.barn_over) w.add("fault", "OVERCURRENT");
    else if (r.fault.driver_contact) w.add("fault", "DRIVER");
    w.add("h_street", static_cast<int>(r.burn_tenths[LAMP_STREET] / 36000u));
    w.endLine();

    if (w.overflow()) {
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryEvent(const char* kind) {
    char line[64];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    w.endLine();
    consoleWrite(line, w.size());
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }

}  // namespace ranch

/* ==================== src/hal_sim.cpp ==================== */

// Simulated sky, lamp and driver for the lighting board.
//
// Deliberately not the firmware's own astronomy: the sky here comes from a sun
// altitude computed with the textbook declination approximation, while the board
// decides with the NOAA series. They agree to a few minutes and are derived
// differently, so "the lamp came on while it was still daylight" is a measurable
// disagreement rather than a tautology.
#if defined(RANCH_SIM)

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

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

/* ==================== src/main.cpp ==================== */

// Lighting board entry point: clock, sun times, policy, outputs, uplink.
//
// The sun is recalculated once a day (and on any clock correction), not every
// tick: the times move by seconds per day, and recomputing them at 10 Hz would
// only add a way to get it wrong.
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
int job_light = -1, job_telem = -1, job_burn = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

LightConfig cfg = LIGHT_DEFAULTS;
double latitude = RANCH_LATITUDE;
double longitude = RANCH_LONGITUDE;
double tz_hours = RANCH_UTC_OFFSET_H;

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "light-1";
uint16_t mqtt_port = MQTT_PORT;

bool have_clock = false;
uint32_t minute_of_day = 0;
uint32_t sun_day_key = 0xFFFFFFFFu;
float civil_rise = -1.0f;
float civil_set = -1.0f;
float official_rise = -1.0f;
float official_set = -1.0f;

bool street_override = false;
bool house_override = false;
uint32_t override_left_s = 0;

LightMode last_mode = LightMode::Astro;
bool last_street = false;
bool last_house = false;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.endLine();
    consoleWrite(line, w.size());
}

void paramsLoad() {
    int32_t v = 0;
    float f = 0.0f;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", tag, sizeof(tag)) || !tag[0]) std::snprintf(tag, sizeof(tag), "%s", "light-1");
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    // A board that is moved to another barn must be told where it is; the sun
    // times are meaningless without that.
    if (nvGetF32("lat", f) && f > -66.0f && f < 66.0f) latitude = f;
    if (nvGetF32("lon", f) && f > -180.0f && f < 180.0f) longitude = f;
    if (nvGetF32("tz", f) && f > -12.0f && f < 14.0f) tz_hours = f;

    if (nvGetI32("ramp_min", v) && v >= 1 && v <= 120) cfg.dusk_ramp_s = static_cast<uint16_t>(v * 60);
    if (nvGetI32("econ_after", v) && v >= 30 && v <= 600) cfg.economy_after_min = static_cast<uint16_t>(v);
    if (nvGetF32("econ_duty", f) && f > 0.05f && f < 1.0f) cfg.economy_duty = f;
    if (nvGetI32("house_off", v) && v >= 30 && v <= 720) cfg.house_off_min = static_cast<uint16_t>(v);
    if (nvGetI32("override_min", v) && v >= 5 && v <= 720) cfg.override_s = static_cast<uint32_t>(v * 60);
    if (nvGetF32("lux_hold", f) && f > 5.0f && f < 5000.0f) cfg.daylight_lux_hold = f;

    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        char key[12];
        std::snprintf(key, sizeof(key), "h%u", static_cast<unsigned>(i));
        if (nvGetI32(key, v) && v >= 0) lightsSetBurnTenths(static_cast<LampCircuit>(i),
                                                            static_cast<uint32_t>(v));
    }
}

void sunRecalculate(uint16_t year, uint8_t month, uint8_t day) {
    const uint32_t key = static_cast<uint32_t>(year) * 400u + month * 32u + day;
    if (key == sun_day_key) return;
    sun_day_key = key;
    const SunTimes civil = sunTimes(year, month, day, latitude, longitude,
                                    SUN_CIVIL_ZENITH, tz_hours);
    const SunTimes official = sunTimes(year, month, day, latitude, longitude,
                                       SUN_OFFICIAL_ZENITH, tz_hours);
    civil_rise = civil.valid ? civil.sunrise_min : -1.0f;
    civil_set = civil.valid ? civil.sunset_min : -1.0f;
    official_rise = official.valid ? official.sunrise_min : -1.0f;
    official_set = official.valid ? official.sunset_min : -1.0f;
}

void clockTick() {
    uint16_t y = 0;
    uint8_t mo = 0, d = 0;
    uint32_t sod = 0;
    have_clock = clockNow(sod, y, mo, d);
    if (!have_clock) return;
    minute_of_day = sod / 60u;
    sunRecalculate(y, mo, d);
}

void lightTick(uint32_t dt_ms) {
    clockTick();

    LightInput in{};
    in.minute_of_day = minute_of_day;
    in.civil_sunrise_min = civil_rise;
    in.civil_sunset_min = civil_set;
    in.official_sunrise_min = official_rise;
    in.official_sunset_min = official_set;
    in.motion = motionSeen();
    in.motion_age_s = motionAgeMs() / 1000u;
    const LampState& lamps = lightsGet();
    in.lux = lamps.lux;
    in.lux_valid = lamps.lux_valid;
    in.street_override = street_override;
    in.house_override = house_override;
    in.override_left_s = override_left_s;
    in.clock_valid = have_clock;

    const LightOutput o = lightDecide(cfg, in);
    if (override_left_s > 0) {
        override_left_s -= (dt_ms + 999u) / 1000u;
        if (override_left_s > cfg.override_s) override_left_s = 0;   // underflow guard
    }

    lightsRequest(o.street, o.house, o.barn_duty);
    lightsTick(dt_ms);
    last_mode = o.mode;

    // Report a switching event the moment it happens rather than on the next
    // telemetry tick: a lamp that came on two minutes late is a complaint, and
    // the log has to be able to answer it.
    const LampState& now = lightsGet();
    if (now.street != last_street) {
        telemetryEvent(now.street ? "street-on" : "street-off");
        last_street = now.street;
    }
    if (now.house != last_house) {
        telemetryEvent(now.house ? "house-on" : "house-off");
        last_house = now.house;
    }
}

void publish() {
    const LampState& l = lightsGet();
    LightReport r{};
    r.street = l.street;
    r.house = l.house;
    r.barn_duty = l.duty_now;
    r.lux = l.lux;
    r.lux_valid = l.lux_valid;
    r.amps = l.amps;
    r.mode = last_mode;
    r.sunrise_min = civil_rise;
    r.sunset_min = civil_set;
    r.minute_of_day = minute_of_day;
    r.clock_valid = have_clock;
    r.motion = motionSeen();
    r.override_pending = override_left_s > 0;
    r.override_left_s = override_left_s;
    r.fault = l.fault;
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) r.burn_tenths[i] = lightsBurnTenths(static_cast<LampCircuit>(i));
    telemetryPublish(r);
}

void persistBurn() {
    for (uint8_t i = 0; i < LAMP_COUNT; ++i) {
        char key[12];
        std::snprintf(key, sizeof(key), "h%u", static_cast<unsigned>(i));
        nvSetI32(key, static_cast<int32_t>(lightsBurnTenths(static_cast<LampCircuit>(i))));
    }
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[16], value[24];
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

    const bool on = value[0] == '1' || value[0] == 'o';
    bool taken = true;
    if (std::strcmp(verb, "street") == 0) {
        street_override = on;
        override_left_s = cfg.override_s;
    } else if (std::strcmp(verb, "house") == 0) {
        house_override = on;
        override_left_s = cfg.override_s;
    } else if (std::strcmp(verb, "auto") == 0) {
        override_left_s = 0;
        street_override = false;
        house_override = false;
    } else if (std::strcmp(verb, "clear") == 0) {
        lightsClearLatch();
    } else if (std::strcmp(verb, "time") == 0) {
        // "time=2026-04-18T19:42:00" from the app: the RTC is the fallback, not
        // the assumption, and a board with a dead coin cell must still be
        // switchable on the right schedule.
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
        if (std::sscanf(value, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) == 6 &&
            y > 2000 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h < 24 && mi < 60) {
            clockSet(static_cast<uint16_t>(y), static_cast<uint8_t>(mo), static_cast<uint8_t>(d),
                     static_cast<uint8_t>(h), static_cast<uint8_t>(mi), static_cast<uint8_t>(se));
            sun_day_key = 0xFFFFFFFFu;
        } else {
            taken = false;
        }
    } else {
        taken = false;
    }
    telemetryEvent(taken ? "cmd" : "cmd-unknown");
}

void supervision() {
    static bool down = false;
    static uint32_t down_ms = 0;
    if (wifiUp()) {
        down = false;
        return;
    }
    if (!down) {
        down = true;
        down_ms = halMillis();
    } else if (static_cast<int32_t>(halMillis() - (down_ms + 60000u)) >= 0) {
        down_ms = halMillis();
        wifiReconnect();
    }
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;

    if (sched.due(job_light, dt)) {
        lightTick(sched.elapsed(job_light));
        feedWatchdog();
    }
    if (sched.due(job_telem, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_burn, dt)) persistBurn();
    supervision();
}

#if !defined(RANCH_SIM)
void lightTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(50);
    }
}
#endif

}  // namespace

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    override_left_s = 0;
    street_override = false;
    house_override = false;
    sun_day_key = 0xFFFFFFFFu;
    sched.clear();
    lightsInit();
    telemetryInit();
    paramsLoad();
    clockTick();
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_light = sched.add("light", 100);
    job_telem = sched.add("telemetry", 1000 / TELEMETRY_HZ);
    job_burn = sched.add("burn", 60000);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the schedule needs a task of its
    // own or the board boots, banners, and stops thinking.
    xTaskCreatePinnedToCore(lightTask, "light", STACK_LIGHT, nullptr, PRIO_LIGHT,
                            nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
    halSimPump();
    runJobs();
    halDelayMs(50);
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
