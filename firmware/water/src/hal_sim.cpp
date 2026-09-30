// Simulated ranch water system for the browser and the host test.
//
// Layout it models, because the firmware's rules only make sense against it:
//
//   borehole --pump--> header tank --gravity--> barn trough
//                                        \\----> house line
//
// The pump's job is to keep the header tank full; the trough and the house are
// fed by head, not by the pump. So:
//  - "pump running with an empty tank" is a dry run, and pressure collapses
//  - "valve open and nothing moving" is a blocked line or an empty tank, and the
//    sequencer must give up rather than hold the coil forever
//  - a barn run that is too long fills the trough past its brim, which is how the
//    rope sensor gets wet without anyone injecting a fault
// The firmware does not know any of this in advance: it reads pressures and
// pulses, and the tests below only pass if what it decides matches what happened.
#if defined(RANCH_SIM)

#include "hal.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "board.h"
#include "programme.h"

namespace ranch {
namespace {

// --- the plant --------------------------------------------------------------
constexpr float TANK_LITRES = 2000.0f;      // header tank capacity
constexpr float PUMP_LITMIN = 20.0f;        // borehole yield through the pump
constexpr float HEAD_BAR_FULL = 2.2f;       // static head at a full tank
constexpr float PUMP_BOOST_BAR = 1.6f;
constexpr float BARN_DEMAND_LMIN = 12.0f;   // trough fill rate
constexpr float HOUSE_DEMAND_LMIN = 3.4f;
constexpr float LINE_RESIST_BAR_PER_LMIN = 0.09f;
constexpr float TROUGH_LITRES = 240.0f;     // what the trough can hold
constexpr float TROUGH_DRAIN_LMIN = 1.6f;   // what the cattle drink / the overflow drains

constexpr float LEAK_LMIN = 0.6f;           // the stuck-seat orifice, when injected

struct SimPlant {
    float tank_pct = 68.0f;
    float trough_pct = 20.0f;
    float pressure = 0.0f;
    float barn_flow = 0.0f;
    float house_flow = 0.0f;
    float barn_pulses = 0.0f;
    float house_pulses = 0.0f;
    bool barn_open = false;
    bool house_open = false;
    bool pump = false;
    bool barn_leak = false;                 // injected stuck seat
    bool supply_lost = false;               // injected: borehole dry
    bool stuck_latched = false;
    uint16_t barn_close_ms = 0;
    uint16_t house_close_ms = 0;
    float barn_litres = 0.0f;
    float house_litres = 0.0f;
    float leaked_litres = 0.0f;
    uint32_t pump_starts = 0;
    uint32_t dry_run_events = 0;
    uint32_t stuck_events = 0;
    uint32_t refills = 0;
    bool refill_open = false;
    float coil_barn = 0.0f;                 // last duty written to each coil
    float coil_house = 0.0f;
};

SimPlant plant;

// --- clock ------------------------------------------------------------------
uint32_t g_now = 0;
// The simulation starts with a clock, because neither the browser nor the host
// harness has an RTC or an NTP server. A minute before a scheduled dispense puts
// the first cycle in view immediately; a test that wants another time calls
// halSimTime().
uint32_t sod = 8u * 3600u + 3540u;
uint32_t sod_ms = 0;
uint16_t yy = 2026;
uint8_t mm = 4;
uint8_t dd = 18;
bool clock_valid = true;

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

// A valve only passes what the head across it can push, and it takes a second or
// two for the column of water to get moving. That lag is the whole reason the
// firmware has a no-flow timeout: an instant model would hide it.
float lineFlow(bool open, float demand_lmin, float available_bar) {
    if (!open) return 0.0f;
    if (available_bar < 0.35f) return 0.0f;          // cracked seat weeps, that is all
    const float limit = demand_lmin * (available_bar / 2.4f);
    return limit > demand_lmin ? demand_lmin : limit;
}

void plantStep(uint32_t dt_ms) {
    const float dt = dt_ms / 1000.0f;

    // Advance the wall clock with the plant. The milliseconds have to accumulate:
    // at a 20 ms tick, dt/1000 is integer zero, and a clock that never moves
    // silently disables every scheduled cycle.
    if (clock_valid) {
        sod_ms += dt_ms;
        while (sod_ms >= 1000u) {
            sod_ms -= 1000u;
            sod++;
            if (sod >= DAY_SECONDS) {
                sod -= DAY_SECONDS;
                dd++;
                if (dd > 28) {
                    dd = 1;
                    mm++;
                    if (mm > 12) { mm = 1; yy++; }
                }
            }
        }
    }

    const bool barn_current = plant.coil_barn > 0.05f;      // what the coil says
    const bool house_current = plant.coil_house > 0.05f;
    if (barn_current && !plant.barn_open) plant.barn_open = true;
    if (house_current && !plant.house_open) plant.house_open = true;
    // A 25 mm solenoid does not shut the instant the coil drops.
    if (!barn_current && plant.barn_open) {
        plant.barn_close_ms += dt_ms;
        if (plant.barn_close_ms > 400) { plant.barn_open = false; plant.barn_close_ms = 0; }
    } else {
        plant.barn_close_ms = 0;
    }
    if (!house_current && plant.house_open) {
        plant.house_close_ms += dt_ms;
        if (plant.house_close_ms > 400) { plant.house_open = false; plant.house_close_ms = 0; }
    } else {
        plant.house_close_ms = 0;
    }

    // --- pressures -----------------------------------------------------------
    float head = HEAD_BAR_FULL * plant.tank_pct / 100.0f;
    if (plant.supply_lost) head = 0.0f;
    const bool pump_running = plant.pump;
    if (pump_running && !plant.supply_lost && plant.tank_pct > 1.0f) head += PUMP_BOOST_BAR;

    const float draw = (plant.barn_open ? plant.barn_flow : 0.0f) +
                       (plant.house_open ? plant.house_flow : 0.0f);
    const float target = head - LINE_RESIST_BAR_PER_LMIN * draw;
    const float settled = target < 0.0f ? 0.0f : target;
    plant.pressure += (settled - plant.pressure) * (1.0f - std::exp(-dt / 0.6f));

    if (pump_running && plant.pressure < 0.2f) plant.dry_run_events++;

    // --- flows ---------------------------------------------------------------
    // The trough can only accept water until it is full; overrunning the schedule
    // therefore stops filling rather than flooding, and the rope sensor is what
    // notices the difference.
    const float barn_avail = TROUGH_LITRES * (1.0f - plant.trough_pct / 100.0f);
    const float barn_target = lineFlow(plant.barn_open, BARN_DEMAND_LMIN, plant.pressure);
    const float barn_capped = barn_target > barn_avail / dt ? barn_avail / dt : barn_target;
    plant.barn_flow += (barn_capped - plant.barn_flow) * (1.0f - std::exp(-dt / 1.1f));

    const float house_target = lineFlow(plant.house_open, HOUSE_DEMAND_LMIN, plant.pressure);
    plant.house_flow += (house_target - plant.house_flow) * (1.0f - std::exp(-dt / 0.9f));

    // A stuck seat leaks the orifice rate even with the coil cold, and it does so
    // into the trough's overflow, which is exactly what the rope sensor sees.
    const float leak_flow = plant.barn_leak ? LEAK_LMIN : 0.0f;

    const float moved_barn = plant.barn_flow * dt / 60.0f;
    const float moved_house = plant.house_flow * dt / 60.0f;
    const float moved_leak = leak_flow * dt / 60.0f;
    plant.barn_litres += moved_barn;
    plant.house_litres += moved_house;
    plant.leaked_litres += moved_leak;
    plant.barn_pulses += moved_barn * FLOW_PULSES_PER_LITRE;
    plant.house_pulses += moved_house * FLOW_PULSES_PER_LITRE;
    // The leak is on the barn side of the meter and reaches the trough, but the
    // meter only counts turbulence in its own bore, so it stays blind to it.
    plant.trough_pct += (moved_barn + moved_leak) * 100.0f / TROUGH_LITRES;
    plant.tank_pct -= (moved_barn + moved_house) * 100.0f / TANK_LITRES;

    // The trough drinks and spills at a fixed rate; over 100 % is an overflow.
    plant.trough_pct -= TROUGH_DRAIN_LMIN * dt * 100.0f / TROUGH_LITRES;
    if (plant.trough_pct < 0.0f) plant.trough_pct = 0.0f;
    if (plant.trough_pct > 100.0f) plant.trough_pct = 100.0f;

    // --- refill ---------------------------------------------------------------
    if (pump_running && !plant.supply_lost) {
        plant.tank_pct += PUMP_LITMIN * dt * 100.0f / TANK_LITRES;
        if (!plant.refill_open) { plant.refill_open = true; plant.refills++; }
    } else {
        plant.refill_open = false;
    }
    if (plant.tank_pct > 100.0f) plant.tank_pct = 100.0f;
    if (plant.tank_pct < 0.0f) plant.tank_pct = 0.0f;

    // Counted on the edge, not per tick: "how many times did a line fail to
    // deliver" has to mean cycles, not milliseconds.
    const bool now_stuck = (plant.barn_open || plant.house_open) &&
                           plant.pressure < 0.35f &&
                           plant.barn_flow < 0.1f && plant.house_flow < 0.1f;
    if (now_stuck && !plant.stuck_latched) {
        plant.stuck_latched = true;
        plant.stuck_events++;
    } else if (!now_stuck) {
        plant.stuck_latched = false;
    }
}

// --- parameters -------------------------------------------------------------
struct Kv {
    char key[16];
    uint8_t kind;
    int32_t i;
    float f;
    char s[64];
};
Kv nvs[16];

Kv* findKv(const char* key, bool create) {
    const size_t klen = std::strlen(key);
    Kv* slot = nullptr;
    for (auto& k : nvs) {
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

// --- HAL --------------------------------------------------------------------
void halInit() {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.begin(115200);
#endif
    g_now = rawMillis();
    plant = SimPlant{};
    clock_valid = true;
}

uint32_t halMillis() { return g_now; }

void halDelayMs(uint32_t ms) {
    rawSleep(ms);
    pump();
}

void halSimPump() { pump(); }

void halSimClock(bool enabled) { clock_valid = enabled; }

void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day) {
    sod = second_of_day;
    yy = year;
    mm = month;
    dd = day;
    clock_valid = true;
}

void consoleWrite(const char* data, size_t len) {
#if defined(ARDUINO) && !defined(RANCH_HOST)
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
#else
    std::fwrite(data, 1, len, stdout);
    std::fflush(stdout);
#endif
}

void setValvePin(uint8_t pin, float duty) {
    if (pin == PIN_VALVE_BARN) plant.coil_barn = duty;
    else if (pin == PIN_VALVE_HOUSE) plant.coil_house = duty;
}

void setPump(bool on) {
    if (on && !plant.pump) plant.pump_starts++;
    plant.pump = on;
}

uint32_t flowPulsesBarn() { return static_cast<uint32_t>(plant.barn_pulses); }
uint32_t flowPulsesHouse() { return static_cast<uint32_t>(plant.house_pulses); }

// The rope sensor is wet when the trough has overflowed, or when a fault has been
// injected. Reporting only the first would make the leak rule untestable.
bool leakWet() { return plant.trough_pct >= 99.5f || plant.barn_leak; }
bool tankHigh() { return plant.tank_pct >= 99.5f; }

// Millivolts out of the divider, so the firmware's own conversion and its
// out-of-range tests are what get exercised.
uint16_t analogMillivolts(uint8_t pin) {
    if (pin == PIN_PRESSURE) {
        return static_cast<uint16_t>(PRESSURE_MV_AT_ZERO + plant.pressure * PRESSURE_MV_PER_BAR);
    }
    if (pin == PIN_LEVEL) {
        const float mv = LEVEL_MV_EMPTY + (LEVEL_MV_FULL - LEVEL_MV_EMPTY) * plant.tank_pct / 100.0f;
        return static_cast<uint16_t>(mv);
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
int wifiRssi() { return -52; }
void wifiReconnect() {}

void feedWatchdog() {}
void setStatusLed(bool) {}                   // there is no lamp in the simulation
const char* resetReason() { return "sim"; }

// --- the plant's own accounting ----------------------------------------------
void halPlant(PlantCounters& out) {
    out.barn_litres = plant.barn_litres;
    out.house_litres = plant.house_litres;
    out.leaked_litres = plant.leaked_litres;
    out.pump_starts = plant.pump_starts;
    out.dry_run_events = plant.dry_run_events;
    out.stuck_valve_events = plant.stuck_events;
    out.tank_refills = plant.refills;
    out.leak_active = leakWet();
    out.tank_high_active = tankHigh();
    out.pressure_bar = plant.pressure;
    out.tank_pct = plant.tank_pct;
    out.barn_flow_lmin = plant.barn_flow;
    out.house_flow_lmin = plant.house_flow;
}

void halPlantLeak(bool on) { plant.barn_leak = on; }
void halPlantSupplyLost(bool on) { plant.supply_lost = on; }

}  // namespace ranch

#endif  // RANCH_SIM
