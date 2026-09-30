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
#pragma once

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
