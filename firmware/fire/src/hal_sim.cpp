// A model of the ranch's detection: loops with relays that take time to move, a
// bell with an armature, a pump that may or may not be behind its permit, and the
// four ways a wire fails.
//
// Everything the firmware sees here is a milliVolt level or a contact state. It is
// never handed "zone 2 is on fire", because a test that hands over the conclusion
// proves only that the code can read a variable.
#if defined(RANCH_SIM)

#include "hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "board.h"
#include "fire_logic.h"

namespace ranch {
namespace {

// The divider: 4k7 to 3V3, the loop's own resistance to ground.
uint16_t mvForOhm(uint32_t loop_ohm) {
    const float v = static_cast<float>(LOOP_RAIL_MV) * static_cast<float>(loop_ohm) /
                    static_cast<float>(loop_ohm + LOOP_PULLUP_OHM);
    return static_cast<uint16_t>(v + 0.5f);
}

constexpr uint16_t RELAY_PULL_IN_MS = 15;
constexpr uint16_t ARMATURE_PULL_IN_MS = 10;
constexpr uint16_t CHATTER_MS = 40;

uint32_t g_now = 0;
uint32_t sod = 20u * 3600u + 10u;      // 20:10, a barn on a normal evening
uint32_t sod_ms = 0;
uint16_t yy = 2026;
uint8_t mm = 4;
uint8_t dd = 18;
bool sim_clock_ok = true;

SimLoopFault loop_fault[6] = {SIM_LOOP_CLEAR, SIM_LOOP_CLEAR, SIM_LOOP_CLEAR,
                              SIM_LOOP_CLEAR, SIM_LOOP_CLEAR, SIM_LOOP_CLEAR};
uint16_t relay_ms[6] = {0, 0, 0, 0, 0, 0};
uint16_t chatter_ms = 0;
uint16_t loop_mv[6] = {0, 0, 0, 0, 0, 0};
bool loop_was_smoke[6] = {false, false, false, false, false, false};

bool bell_coil = false;
bool strobe_on = false;
bool pump_permit = false;
bool bell_dead = false;
bool armature = false;
bool pump_ran = false;
bool key = true;
bool btn_silence = false;
bool btn_test = false;
uint16_t armature_ms = 0;
bool counted_attempt = false;
bool counted_bell_failure = false;
bool episode_permitted = false;

PanelCounters plant{};

#if defined(RANCH_HOST)
uint32_t g_virt = 0;
inline uint32_t rawMillis() { return g_virt; }
inline void rawSleep(uint32_t ms) { g_virt += ms; }
#else
inline uint32_t rawMillis() { return ::millis(); }
inline void rawSleep(uint32_t ms) { ::delay(ms); }
#endif

void plantStep(uint32_t dt_ms);

void pump() {
    const uint32_t t = rawMillis();
    if (t == g_now) return;
    uint32_t dt = t - g_now;
    g_now = t;
    if (dt > 100) dt = 100;
    plantStep(dt);
}

// What the loop should present for a given fault, ignoring relay travel: this is
// the state the detector *wants* to be in, and the relay is what delays it.
uint16_t targetMv(SimLoopFault f, bool chatter_high) {
    switch (f) {
        case SIM_LOOP_SMOKE: return mvForOhm(LOOP_ALARM_OHM);
        case SIM_LOOP_CUT:   return LOOP_RAIL_MV;
        case SIM_LOOP_SHORT: return 0u;
        case SIM_LOOP_DIRTY: return 1050u;    // between the alarm and normal bands
        case SIM_LOOP_RELAY_CHATTER:
            return chatter_high ? mvForOhm(LOOP_ALARM_OHM) : 1050u;
        case SIM_LOOP_CLEAR:
        default:             return mvForOhm(LOOP_EOL_OHM);
    }
}

void plantStep(uint32_t dt_ms) {
    if (sim_clock_ok) {
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

    chatter_ms = static_cast<uint16_t>(chatter_ms + dt_ms);
    const bool chatter_high = (chatter_ms / CHATTER_MS) % 2u == 0u;

    for (uint8_t i = 0; i < 6; ++i) {
        const bool wanted = loop_fault[i] == SIM_LOOP_SMOKE ||
                            loop_fault[i] == SIM_LOOP_RELAY_CHATTER;
        if (wanted && !loop_was_smoke[i]) {
            if (loop_fault[i] == SIM_LOOP_SMOKE) plant.smoke_events++;
            loop_was_smoke[i] = true;
            episode_permitted = false;
            relay_ms[i] = 0;
        }
        // The relay has to travel, so the alarm band arrives 15 ms after the smoke
        // does. At a 20 ms scan that is the difference between a detector and a
        // debounce, and the panel has to survive it either way.
        if (wanted && relay_ms[i] < RELAY_PULL_IN_MS) {
            relay_ms[i] = static_cast<uint16_t>(relay_ms[i] + dt_ms);
        }
        if (!wanted) relay_ms[i] = 0;
        const bool conducting = wanted && relay_ms[i] >= RELAY_PULL_IN_MS;

        if (loop_fault[i] == SIM_LOOP_CUT || loop_fault[i] == SIM_LOOP_SHORT ||
            loop_fault[i] == SIM_LOOP_DIRTY) {
            // A wiring fault is not something a relay does: the line is simply at a
            // different resistance now.
            loop_mv[i] = targetMv(loop_fault[i], false);
        } else if (conducting) {
            loop_mv[i] = targetMv(loop_fault[i], chatter_high);
        } else {
            loop_mv[i] = targetMv(SIM_LOOP_CLEAR, false);
        }

        if (!wanted && loop_was_smoke[i]) {
            loop_was_smoke[i] = false;
            // A zone that alarmed, cleared, and never got a second opinion is the
            // false alarm. Counting it is the only way a test can tell "the panel was
            // right not to start the pump" from "the panel never noticed".
            if (!episode_permitted && i < FIRE_ZONES) plant.false_alarm_starts++;
        }
    }
    if (pump_permit) episode_permitted = true;

    // The bell: coil, then armature, then the auxiliary contact the firmware reads.
    if (bell_coil) {
        if (!counted_attempt) {
            plant.bell_attempts++;
            counted_attempt = true;
        }
        if (bell_dead) {
            armature = false;
            if (!counted_bell_failure) {
                plant.bell_failures++;
                counted_bell_failure = true;
            }
        } else {
            armature_ms = static_cast<uint16_t>(armature_ms + dt_ms);
            if (armature_ms >= ARMATURE_PULL_IN_MS) armature = true;
        }
        plant.siren_on_ms += dt_ms;
    } else {
        armature = false;
        armature_ms = 0;
        counted_attempt = false;
        counted_bell_failure = false;
    }
    if (strobe_on) plant.strobe_on_ms += dt_ms;
    if (pump_permit) {
        plant.pump_permit_ms += dt_ms;
        // The permit is this board's output; whether water actually moved is the
        // pump's business, and the two are only ever the same statement by luck.
        if (pump_ran) plant.pump_was_permitted = true;
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
    bell_coil = false;
    strobe_on = false;
    pump_permit = false;
    armature = false;
    plant = PanelCounters{};
    for (uint8_t i = 0; i < 6; ++i) {
        loop_fault[i] = SIM_LOOP_CLEAR;
        loop_was_smoke[i] = false;
        relay_ms[i] = 0;
        loop_mv[i] = mvForOhm(LOOP_EOL_OHM);
    }
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
    sim_clock_ok = true;
}

void halSimClock(bool enabled) { sim_clock_ok = enabled; }

void halSimLoop(uint8_t which, SimLoopFault fault) {
    if (which < 6) loop_fault[which] = fault;
}
void halSimBellDead(bool dead) { bell_dead = dead; }
void halSimPumpRan(bool running) { pump_ran = running; }
void halSimKey(bool armed) { key = armed; }
void halSimButton(bool silence, bool test) {
    btn_silence = silence;
    btn_test = test;
}

void halSimClear() {
    for (uint8_t i = 0; i < 6; ++i) {
        loop_fault[i] = SIM_LOOP_CLEAR;
        loop_was_smoke[i] = false;
        relay_ms[i] = 0;
        loop_mv[i] = mvForOhm(LOOP_EOL_OHM);
    }
    bell_dead = false;
    bell_coil = false;
    strobe_on = false;
    pump_permit = false;
    armature = false;
    armature_ms = 0;
    counted_attempt = false;
    counted_bell_failure = false;
    episode_permitted = false;
    pump_ran = false;
    key = true;
    btn_silence = false;
    btn_test = false;
    chatter_ms = 0;
    plant = PanelCounters{};
}

void consoleWrite(const char* data, size_t len) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#else
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
#endif
}

uint16_t loopMillivolts(uint8_t pin) {
    if (pin == PIN_Z_HOUSE) return loop_mv[0];
    if (pin == PIN_Z_BARN) return loop_mv[1];
    if (pin == PIN_Z_STORE) return loop_mv[2];
    if (pin == PIN_Z_POWER) return loop_mv[3];
    if (pin == PIN_MCP) return loop_mv[4];
    if (pin == PIN_FLOW) return loop_mv[5];
    return LOOP_RAIL_MV;      // an unconnected input floats high, which is a fault
}

void setSiren(bool on) { bell_coil = on; }
void setStrobe(bool on) { strobe_on = on; }
void setPumpPermit(bool on) { pump_permit = on; }
// The permit contact this board closed. Whether water moved behind it is a separate
// fact (plant.pump_was_permitted), and conflating the two is how a panel reports a
// charged riser that was never charged.
bool pumpPermitIsClosed() { return pump_permit; }

bool sirenFeedbackClosed() { return armature; }
bool keyArmed() { return key; }
bool silencePressed() { return btn_silence; }
bool testPressed() { return btn_test; }

bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (!sim_clock_ok) return false;
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
    sim_clock_ok = true;
}

bool clockWasSet() { return sim_clock_ok; }

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
void setStatusLed(bool) {}
const char* resetReason() { return "sim"; }

void halPanel(PanelCounters& out) { out = plant; }

}  // namespace ranch

#endif  // RANCH_SIM
