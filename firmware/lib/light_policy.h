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
#pragma once

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
