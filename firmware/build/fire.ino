// GENERATED FILE - do not edit, edit the project and re-run:
//   node tools/bundle.mjs
// Source: firmware/fire + firmware/lib  (11 files, 70.8 KB before bundling)
// The detectors, bell and wiring faults in hal_sim.cpp are what the loops are wired to.
//
// Build the same code for hardware with:  pio run -d firmware/fire

#define RANCH_SIM 1

/* ==================== src/board.h ==================== */

// Pin map and electrical configuration for the ranch fire panel.
//
// What this board is: four supervised detector loops, the manual call point, the
// flow switch, the bell and the pump permit. It is the board that has to keep
// working when everything else has stopped being interesting, which is why the
// loop supervision is analog rather than a contacted "fine" LED.
//
// The analog channel budget shaped this design the same way it shaped the
// switchboard board: the classic ESP32 has eight ADC1 pins (32-39, six on the
// header) and ADC2 cannot
// be read while WiFi is associated. A fire panel that loses its uplink is still a
// fire panel, but one that mis-reads a loop because the radio came up is not. Six
// loops, six pins, no second ADC: four detector zones plus the call point plus the
// flow switch.
//
// Proven by the static_asserts below: no duplicate pin, nothing on the SPI flash
// pins (6-11, 16-17), no output on the input-only pins (34-36, 39), and every
// supervised loop on ADC1.

#include <cstddef>

// --- supervised loops (see lib/fire_logic.h for the four bands) ---------------
// Each input is a 4k7 pull-up to 3V3 with the loop's end-of-line resistor in
// series; the detector's relay shorts the 1k alarm resistor when it operates.
#define PIN_Z_HOUSE         39      // ADC1_CH3 (VN), main house
#define PIN_Z_BARN          36      // ADC1_CH0, cow shed
#define PIN_Z_STORE         32      // ADC1_CH4, store / feed room
#define PIN_Z_POWER         33      // ADC1_CH5, switch room -- the fire that starts here
#define PIN_MCP             34      // ADC1_CH6, break-glass call point
#define PIN_FLOW            35      // ADC1_CH7, wet-pipe flow switch

// --- outputs ------------------------------------------------------------------
#define PIN_SIREN           25      // internal bell relay, 24 V side
#define PIN_STROBE          27      // external beacons
#define PIN_PUMP            14      // sprinkler pump permit, dry contact
#define PIN_WDT_FEED         4      // external watchdog, must toggle >= 10 Hz
#define PIN_LED_STATUS       2      // the dev kit's own LED

// --- inputs -------------------------------------------------------------------
#define PIN_SIREN_FB        26      // armature relay auxiliary: the bell moved
#define PIN_KEY_ARM          5      // key switch, ARM / BYPASS, dry make to GND
#define PIN_BTN_SILENCE     13      // panel silence, momentary to GND
#define PIN_BTN_TEST        18      // bell test, momentary to GND
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define RTC_ADDR            0x68    // DS3231: the log has to survive a power cut

// A dry contact to GND on an internal pull-up. Every one of these is inverted by
// the HAL, so a snapped wire reads as the safe state: key missing reads BYPASS and
// raises a fault rather than silently disarming the building.
#ifndef LOOP_PULLUP_OHM
#define LOOP_PULLUP_OHM     4700u   // the precision resistor the loop hangs off
#endif
#ifndef LOOP_EOL_OHM
#define LOOP_EOL_OHM        4700u   // end of line, fitted in the last device
#endif
#ifndef LOOP_ALARM_OHM
#define LOOP_ALARM_OHM      1000u   // shorted by the detector relay
#endif
#ifndef LOOP_RAIL_MV
#define LOOP_RAIL_MV        3300u
#endif

// --- task layout --------------------------------------------------------------
#define PRIO_PANEL          18      // the highest task on the board, deliberately
#define PRIO_DIAL           10
#define STACK_PANEL         6144
#define STACK_DIAL          6144
#define TASK_WDT_TIMEOUT_MS 2000

// The loop scan is 20 ms: a detector relay takes longer than that to pull in, and
// three samples of hysteresis (60 ms) is still far inside the time it takes smoke
// to reach a ceiling device.
#define SAMPLE_PERIOD_MS    20
#define CONTROL_PERIOD_MS   50
#define DIAL_HZ             1       // steady-state reporting; an event goes out at once

// --- uplink -------------------------------------------------------------------
#define MQTT_PORT             1883
#define MQTT_TOPIC_STATE      "ranch/fire/state"
#define MQTT_TOPIC_CMD        "ranch/fire/cmd"
#define MQTT_TOPIC_DIAL       "ranch/fire/dial"   // kept asserted while a condition exists

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

// ADC1 on the classic ESP32 is GPIO 32-39 and nothing else: CH4-CH7 are 32-35 and
// CH0-CH3 are 36-39. Of those eight, the DevKit V1 header carries six (36 and 39 come
// out as VP and VN; 37 and 38 stay inside the module).
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

// No strapping pin may be an output: a bell relay that is driven high while the
// chip resets would hold GPIO0 low through the download-mode divider... which is
// exactly the GPIO0 trap the switchboard board documents. Here the rule is simpler:
// the six loop pins and the two strapping pins (0, 2 used as status LED only).
constexpr int kOutputs[] = { PIN_SIREN, PIN_STROBE, PIN_PUMP, PIN_WDT_FEED,
                             PIN_I2C_SDA, PIN_I2C_SCL };
constexpr int kLoops[] = { PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER,
                           PIN_MCP, PIN_FLOW };
constexpr int kAll[] = {
    PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER, PIN_MCP, PIN_FLOW,
    PIN_SIREN, PIN_STROBE, PIN_PUMP, PIN_WDT_FEED, PIN_LED_STATUS,
    PIN_SIREN_FB, PIN_KEY_ARM, PIN_BTN_SILENCE, PIN_BTN_TEST,
    PIN_I2C_SDA, PIN_I2C_SCL,
};
constexpr size_t kAllLen = sizeof(kAll) / sizeof(kAll[0]);
constexpr size_t kOutLen = sizeof(kOutputs) / sizeof(kOutputs[0]);
constexpr size_t kLoopLen = sizeof(kLoops) / sizeof(kLoops[0]);

static_assert(pinsDisjoint(kAll, kAllLen), "board.h: two functions share one GPIO");
static_assert(pinsAvoidFlash(kAll, kAllLen), "board.h: a pin sits on the SPI flash bus");
static_assert(outputsAreDriven(kOutputs, kOutLen), "board.h: an output is on an input-only pin");
static_assert(analogIsAdc1(kLoops, kLoopLen), "board.h: a supervised loop must be on ADC1");
static_assert(pinsOnHeader(kAll, kAllLen), "board.h: a pin is not broken out on the DevKit V1 header");

}  // namespace ranch
#endif

/* ==================== src/hal.h ==================== */

// Hardware abstraction for the fire panel. Two implementations: hal_esp32.cpp
// reads the real loops and drives the real relays, hal_sim.cpp models the
// detectors, the bell and the wiring faults, so an alarm arrives through a relay
// that takes time to pull in and a loop that can be cut, shorted or left dirty.

#include <cstddef>
#include <cstdint>

namespace ranch {

void halInit();
uint32_t halMillis();
void halDelayMs(uint32_t ms);
void consoleWrite(const char* data, size_t len);

// A supervised loop presented as milliVolts across the 4k7 pull-up divider.
uint16_t loopMillivolts(uint8_t pin);

// Outputs.
void setSiren(bool on);
void setStrobe(bool on);
void setPumpPermit(bool on);
bool pumpPermitIsClosed();

// Feedback and the panel's own controls.
bool sirenFeedbackClosed();     // the armature relay's auxiliary: the bell moved
bool keyArmed();                // false = BYPASS
bool silencePressed();
bool testPressed();

// Wall clock: a fire log with no timestamps is not evidence, and the RTC is the
// only thing that knows what time it was after the supply went down.
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
void setStatusLed(bool on);

#if defined(RANCH_SIM)
void halSimPump();
void halSimTime(uint32_t second_of_day, uint16_t year, uint8_t month, uint8_t day);
void halSimClock(bool enabled);

// Which of the six loops a test is allowed to talk about. Index 0..3 are the
// detector zones, 4 the call point, 5 the flow switch.
enum SimLoop : uint8_t {
    SIM_LOOP_HOUSE = 0, SIM_LOOP_BARN, SIM_LOOP_STORE, SIM_LOOP_POWER,
    SIM_LOOP_MCP, SIM_LOOP_FLOW,
};
// What to present on that loop. The point is that the firmware sees milliVolts, so
// a "smoke" here is a relay that takes milliseconds to close, and a "cut wire" is a
// loop that floats rather than a boolean the test got to pick.
enum SimLoopFault : uint8_t {
    SIM_LOOP_CLEAR = 0,     // end of line in circuit
    SIM_LOOP_SMOKE,         // detector relay closes the 1k alarm path
    SIM_LOOP_CUT,           // the loop floats high
    SIM_LOOP_SHORT,         // 0 ohm: crushed cable, terminal screwed to the rail
    SIM_LOOP_DIRTY,         // corrosion mid-band: no honest reading exists
    SIM_LOOP_RELAY_CHATTER, // the relay buzzes at the threshold
};
void halSimLoop(uint8_t which, SimLoopFault fault);
void halSimBellDead(bool dead);        // coil intact, armature relay never pulls in
void halSimPumpRan(bool running);      // the pump's own contactor, for the frame
void halSimKey(bool armed);
void halSimButton(bool silence, bool test);
// Put the *building* back the way it was. Each test phase is a new board on the same
// ranch, and a board reboot does not un-cut a wire: without this, a fault injected by
// one phase is silently inherited by the next, which is how a dead bell ended up in
// a bypass test.
void halSimClear();

struct PanelCounters {
    uint32_t siren_on_ms;
    uint32_t strobe_on_ms;
    uint32_t pump_permit_ms;
    uint32_t bell_attempts;      // times the coil was energised
    uint32_t bell_failures;      // ... and the auxiliary did not follow
    uint32_t smoke_events;
    uint32_t false_alarm_starts; // a zone alarmed and cleared with no second opinion
    bool pump_was_permitted;
};
void halPanel(PanelCounters& out);
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

/* ==================== ../lib/fire_logic.h ==================== */

// Fire panel logic: supervised detector loops, the alarm decision, and the two
// things a panel must never let an operator do by accident -- silence a bell that
// has a reason to ring, and start the sprinkler pump on one detector's opinion.
//
// Platform neutral and side-effect free like the rest of firmware/lib: loop
// milliVolts and contact states go in, one level plus one reason comes out, and
// the caller decides which relay that closes.
//
// Two decisions here are deliberate and are the ones a maintenance technician will
// question:
//
//  - a single zone alarms the building but does NOT start the pump. The pump needs
//    a second, independent confirmation (another zone, a manual call point, water
//    already moving, or twenty seconds of the first alarm persisting). One detector
//    cooking dust should not flood a barn; twenty seconds of the same detector
//    saying "fire" should.
//  - silence is refused while the cause is still present, and a silence that expires
//    while the alarm is still latched re-sounds. A bell that can be silenced until
//    the next alarm is not a bell.

#include <cstdint>
#include <cstring>

namespace ranch {

constexpr uint8_t FIRE_ZONES = 4;      // house, barn, store room, switch room

// --- the supervised loop ------------------------------------------------------
// One analog input per zone, a 4k7 pull-up to 3V3, and an end-of-line resistor in
// the last device. The loop resistance therefore appears as four distinct bands:
//
//   dead short across the loop      0 ohm    ~0.00 V   a shorted run, not a fire
//   detector relay closed        1 kohm    ~0.58 V   ALARM
//   end of line in circuit       4.7 kohm  ~1.65 V   healthy
//   wire cut / tamper open          inf    ~3.30 V   SUPERVISION
//
// The gaps between the bands are Unknown rather than a nearest-guess: a loop
// sitting at 1.0 V is dirty, corroded or mid-transition, and a panel that picked a
// side for it would be reporting something it did not measure.
struct LoopBands {
    uint16_t short_hi_mv;
    uint16_t alarm_lo_mv, alarm_hi_mv;
    uint16_t normal_lo_mv, normal_hi_mv;
    uint16_t open_lo_mv;
};

constexpr LoopBands LOOP_DEFAULT_BANDS{
    /*short_hi*/  300,
    /*alarm_lo*/  400,   /*alarm_hi*/  760,
    /*normal_lo*/ 1350,  /*normal_hi*/ 2000,
    /*open_lo*/   2900,
};

enum class LoopState : uint8_t {
    Unknown,
    Normal,
    Alarm,           // relay closed the 1k path
    Open,            // cut or tamper: the loop is no longer watching anything
    Shorted,         // 0 ohm: a wiring fault that looks like an alarm and must be named
};

inline const char* loopStateName(LoopState s) {
    switch (s) {
        case LoopState::Normal:  return "ok";
        case LoopState::Alarm:   return "alarm";
        case LoopState::Open:    return "open";
        case LoopState::Shorted: return "short";
        case LoopState::Unknown: return "dirty";
    }
    return "?";
}

inline LoopState loopClassify(uint16_t mv, const LoopBands& b) {
    if (mv < b.short_hi_mv) return LoopState::Shorted;
    if (mv >= b.alarm_lo_mv && mv <= b.alarm_hi_mv) return LoopState::Alarm;
    if (mv >= b.normal_lo_mv && mv <= b.normal_hi_mv) return LoopState::Normal;
    if (mv >= b.open_lo_mv) return LoopState::Open;
    return LoopState::Unknown;
}

// Relay contacts bounce and a loop being re-terminated wiggles through the bands.
// A state is only accepted once it has been seen for `hold` consecutive samples,
// and the hold is counted per zone rather than globally so one chattering loop
// cannot delay the reading of the other three.
constexpr uint8_t LOOP_HOLD_SAMPLES = 3;

struct LoopTracker {
    LoopState state;
    LoopState candidate;
    uint8_t count;
};

inline void loopTrackerReset(LoopTracker& t) {
    t.state = LoopState::Unknown;
    t.candidate = LoopState::Unknown;
    t.count = 0;
}

inline LoopState loopTrack(LoopTracker& t, LoopState sample, uint8_t hold = LOOP_HOLD_SAMPLES) {
    if (sample == t.state) {
        t.candidate = sample;
        t.count = 0;
        return t.state;
    }
    if (sample != t.candidate) {
        t.candidate = sample;
        t.count = 1;
        return t.state;
    }
    t.count++;
    if (t.count >= hold) {
        t.state = sample;
        t.count = 0;
    }
    return t.state;
}

// --- the panel decision ------------------------------------------------------
enum class FireLevel : uint8_t { Normal, Fault, Alarm, Emergency };

inline const char* fireLevelName(FireLevel l) {
    switch (l) {
        case FireLevel::Normal:    return "NORMAL";
        case FireLevel::Fault:     return "FAULT";
        case FireLevel::Alarm:     return "ALARM";
        case FireLevel::Emergency: return "EMERGENCY";
    }
    return "?";
}

struct FireLimits {
    uint16_t confirm_s;          // an unconfirmed alarm escalates to pump-worthy after this
    uint16_t silence_s;          // how long a silenced bell stays quiet
    uint16_t silence_max;        // silences allowed before the panel refuses more
    uint16_t test_period_s;      // how often the panel proves it can make noise
    uint16_t test_ms;            // and for how long it pulses the siren
    uint16_t siren_proof_ms;     // within this the armature relay must prove it sounded
    uint8_t fault_zones_max;     // that many loops out of service is an outage of its own
};

constexpr FireLimits FIRE_DEFAULTS{
    /*confirm_s*/      20,
    /*silence_s*/     120,
    /*silence_max*/     3,
    /*test_period_s*/ 3600,
    /*test_ms*/         200,
    /*siren_proof_ms*/  150,
    /*fault_zones_max*/ 2,
};

struct FireInput {
    LoopState zone[FIRE_ZONES];
    bool manual_call;        // the break-glass is its own supervised loop
    bool flow;               // sprinkler/wet-pipe flow switch: water is already moving
    bool key_armed;
    bool siren_feedback;     // armature relay auxiliary
    bool silence_edge;       // true on the tick the button was pressed
    bool test_edge;          // ditto for the test button
    bool clock_valid;
};

struct FireState {
    float alarm_s;           // how long the panel has been in alarm
    float silence_left_s;
    float test_left_s;
    float next_test_s;
    float unfed_s;           // how long a commanded bell has gone without proof
    bool siren_cmd;          // the previous tick's bell command, for that proof
    uint8_t silences;
    bool latched;            // an alarm stays until reset even if the loop goes back
    bool siren_failed;
    bool reset_edge;         // the panel's own RESET, from the command channel
    uint16_t alarm_zones;    // bit set per zone that has ever alarmed
    uint16_t fault_zones;
};

struct FireDecision {
    FireLevel level;
    bool siren;
    bool strobe;
    bool pump_permit;
    bool dial;               // the uplink should be shouting
    bool silenced;
    bool bypassed;           // key switch off: indications live, outputs dead
    bool supervision;        // at least one loop is not watching anything
    uint16_t zone_bits;
    char reason[20];
};

inline void fireReset(FireState& s) {
    s.alarm_s = 0.0f;
    s.silence_left_s = 0.0f;
    s.test_left_s = 0.0f;
    s.next_test_s = FIRE_DEFAULTS.test_period_s;
    s.unfed_s = 0.0f;
    s.siren_cmd = false;
    s.silences = 0;
    s.latched = false;
    s.siren_failed = false;
    s.reset_edge = false;
    s.alarm_zones = 0;
    s.fault_zones = 0;
}

inline void fireReason(char (&dst)[20], const char* src) {
    uint8_t i = 0;
    for (; src[i] != '\0' && i < 19; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

inline uint8_t fireZoneCount(uint16_t bits) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) if (bits & (1u << i)) n++;
    return n;
}

inline bool fireSilenceAllowed(const FireLimits& lim, const FireInput& in, const FireState& st) {
    // Silence quiets the announcement, never the reason, and two things make it
    // safe to offer it during an alarm: it expires while the cause is still latched
    // (the bell comes back by itself), and a manual call point cannot be silenced at
    // all -- a broken glass is a person asking for attention, and hanging up on them
    // from the control room is not this panel's call to make.
    if (in.manual_call) return false;
    if (st.silences >= lim.silence_max) return false;
    return true;
}

inline FireDecision fireEvaluate(const FireLimits& lim, FireState& st,
                                 const FireInput& in, float dt_s) {
    FireDecision out{FireLevel::Normal, false, false, false, false, false, false, false, 0, ""};
    fireReason(out.reason, "");

    uint16_t bits = 0;
    uint16_t faults = 0;
    uint8_t unknown = 0;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) {
        switch (in.zone[i]) {
            case LoopState::Alarm:   bits |= static_cast<uint16_t>(1u << i); break;
            case LoopState::Open:
            case LoopState::Shorted: faults |= static_cast<uint16_t>(1u << i); break;
            case LoopState::Unknown: unknown++; break;
            case LoopState::Normal:  break;
        }
    }

    // The latched alarm is what the building heard. A loop coming back to normal
    // does not un-ring that; `reset` is the only thing that does. So reset is gated
    // on the *cause*, never on the latch -- gating on the latch would make reset
    // unable to do the one thing it exists for. A reset with smoke still present is
    // refused rather than performed and immediately re-asserted, because a panel that
    // cleared on a burning zone would look safe for exactly one frame, and that is
    // the frame nobody is looking at.
    const bool manual = in.manual_call;
    if (bits || manual) {
        st.latched = true;
        st.alarm_zones |= bits;
    }
    st.fault_zones |= faults;
    const bool latched_alarm = st.latched;

    const bool cause_present = bits != 0 || manual;
    const bool reset_ok = st.reset_edge && !cause_present && faults == 0 && unknown == 0;
    if (st.reset_edge && !reset_ok && out.reason[0] == '\0') {
        fireReason(out.reason, "RESET BLOCKED");
    }
    st.reset_edge = false;
    if (reset_ok) {
        fireReset(st);
        out.level = FireLevel::Normal;
        fireReason(out.reason, "RESET");
        return out;
    }

    if (latched_alarm) st.alarm_s += dt_s;
    else st.alarm_s = 0.0f;

    if (st.silence_left_s > 0.0f) {
        st.silence_left_s -= dt_s;
        if (st.silence_left_s < 0.0f) st.silence_left_s = 0.0f;
    }
    if (st.test_left_s > 0.0f) {
        st.test_left_s -= dt_s;
        if (st.test_left_s < 0.0f) st.test_left_s = 0.0f;
    }

    // The periodic proof that the panel can still make noise. Not a nicety: what
    // every silent-fire investigation has in common is that nobody looked at the
    // bell between the week it worked and the night it was needed.
    st.next_test_s -= dt_s;
    if (in.test_edge || st.next_test_s <= 0.0f) {
        st.test_left_s = lim.test_ms / 1000.0f;
        st.next_test_s = lim.test_period_s;
        if (in.test_edge && out.reason[0] == '\0') fireReason(out.reason, "MANUAL TEST");
    }
    const bool testing = st.test_left_s > 0.0f;

    if (in.silence_edge) {
        if (fireSilenceAllowed(lim, in, st)) {
            st.silence_left_s = lim.silence_s;
            st.silences++;
        } else if (out.reason[0] == '\0') {
            fireReason(out.reason, "SILENCE REFUSED");
        }
    }
    const bool silenced = st.silence_left_s > 0.0f;

    // A pump is permitted on a second opinion, never on one detector's. The four
    // independent opinions available here are: two zones, a person who broke the
    // glass, water that is already moving, and one zone that has not stopped
    // alarming for `confirm_s`. The last is deliberately time rather than another
    // device -- the ranch has one detector per building, and a barn that has been
    // saying "fire" for twenty seconds should not need a second one to agree.
    const bool confirmed = fireZoneCount(bits) >= 2 || manual || in.flow ||
                           (bits != 0 && st.alarm_s >= lim.confirm_s);

    // Two or more loops out of service means the buildings behind them are no
    // The bell is proven only when it was commanded on the previous tick and the
    // armature relay's auxiliary did not close. Judged against the previous command
    // because a relay that chatters open again after closing has to be caught, not
    // excused by this tick's own optimistic reading of the coil.
    if (st.siren_cmd && !in.siren_feedback) {
        st.unfed_s += dt_s;
        if (st.unfed_s > lim.siren_proof_ms / 1000.0f) st.siren_failed = true;
    } else {
        st.unfed_s = 0.0f;
    }

    // longer watched, which is reportable whether or not anything is burning.
    const bool supervision = faults != 0 || unknown > 0 || st.siren_failed;
    const bool outage = fireZoneCount(st.fault_zones) >= lim.fault_zones_max;
    const bool bypassed = !in.key_armed;

    out.zone_bits = bits;
    out.bypassed = bypassed;
    out.silenced = silenced;
    out.supervision = supervision || outage;
    out.dial = latched_alarm || out.supervision || bypassed;
    out.pump_permit = latched_alarm && confirmed && !bypassed;
    out.siren = (latched_alarm || testing) && !silenced && !bypassed;
    // Silence is an audible courtesy only. The strobe and the uplink keep going,
    // because whoever quieted the bell in the control room still has to be told the
    // barn is alight.
    out.strobe = (latched_alarm || testing) && !bypassed;
    st.siren_cmd = out.siren;

    if (latched_alarm && confirmed) out.level = FireLevel::Emergency;
    else if (latched_alarm) out.level = FireLevel::Alarm;
    else if (bypassed || out.supervision) out.level = FireLevel::Fault;
    else out.level = FireLevel::Normal;

    if (out.reason[0] == '\0') {
        if (latched_alarm) fireReason(out.reason, confirmed ? "CONFIRMED FIRE" : "SINGLE ZONE");
        else if (bypassed) fireReason(out.reason, "KEY BYPASSED");
        else if (st.siren_failed) fireReason(out.reason, "SIREN FAILED");
        else if (faults) fireReason(out.reason, "LOOP FAULT");
        else if (unknown) fireReason(out.reason, "LOOP DIRTY");
    }
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

/* ==================== src/panel.h ==================== */

// The only module that touches the loops and the relays.
//
// Sampling and driving live together on purpose: the decision needs a loop state
// that was debounced over samples, and the outputs need a place where "commanded"
// and "proven" are kept apart. Both are invisible from `main.cpp`, and both are
// where a panel goes wrong -- a raw ADC reading promoted straight to "alarm" makes
// a chattering relay page the fire service, and a coil drive counted as a sounding
// bell makes a silent building.

#include <cstdint>

namespace ranch {

struct LoopRead {
    LoopState zone[FIRE_ZONES];
    LoopState mcp;
    LoopState flow;
    uint16_t mv[FIRE_ZONES + 2];       // what the panel actually saw, for the log
    bool manual_call;                  // the call point's alarm band
    bool flow_active;                  // water is moving
    bool supervision;                  // any loop open, shorted or unreadable
};

struct PanelStats {
    uint32_t siren_on_ms;              // time the bell relay was driven
    uint32_t strobe_on_ms;
    uint32_t pump_permit_ms;
    uint32_t samples;
    uint32_t dirty_samples;            // loop readings that fell between the bands
};

void panelInit();

// One loop scan. Call every SAMPLE_PERIOD_MS with the bands the panel was
// configured with -- a site rewired for a 10k pull-up has to be able to say so.
void panelSample(const LoopBands& bands);

const LoopRead& panelLoops();

// Drives the relays from a decision and counts what the outputs were asked to do.
void panelApply(const FireDecision& d, uint32_t dt_ms);

void panelStats(PanelStats& out);

// The button levels. Edges are main.cpp's business: a held silence button must not
// re-silence every tick, and a momentary contact that bounces must not either.
bool panelSilenceHeld();
bool panelTestHeld();

}  // namespace ranch

/* ==================== src/panel.cpp ==================== */



namespace ranch {
namespace {

LoopRead rd{};
PanelStats stats{};
LoopTracker tr[FIRE_ZONES + 2]{};

// The panel's six inputs in the order the frame reports them, so a zone index means
// the same thing in the log, the frame and the terminal block.
constexpr uint8_t kPins[FIRE_ZONES + 2] = {
    PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER, PIN_MCP, PIN_FLOW,
};

}  // namespace

void panelInit() {
    rd = LoopRead{};
    stats = PanelStats{};
    for (auto& t : tr) loopTrackerReset(t);
    setSiren(false);
    setStrobe(false);
    setPumpPermit(false);
}

void panelSample(const LoopBands& bands) {
    stats.samples++;
    for (uint8_t i = 0; i < FIRE_ZONES + 2; ++i) {
        const uint16_t mv = loopMillivolts(kPins[i]);
        rd.mv[i] = mv;
        const LoopState raw = loopClassify(mv, bands);
        if (raw == LoopState::Unknown) stats.dirty_samples++;
        const LoopState s = loopTrack(tr[i], raw);
        if (i < FIRE_ZONES) rd.zone[i] = s;
        else if (i == FIRE_ZONES) {
            rd.mcp = s;
            rd.manual_call = (s == LoopState::Alarm);
        } else {
            rd.flow = s;
            rd.flow_active = (s == LoopState::Alarm);
        }
    }

    bool sup = rd.mcp == LoopState::Open || rd.mcp == LoopState::Shorted ||
               rd.flow == LoopState::Open || rd.flow == LoopState::Shorted;
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) {
        if (rd.zone[i] == LoopState::Open || rd.zone[i] == LoopState::Shorted ||
            rd.zone[i] == LoopState::Unknown) {
            sup = true;
        }
    }
    rd.supervision = sup;
}

const LoopRead& panelLoops() { return rd; }

void panelApply(const FireDecision& d, uint32_t dt_ms) {
    setSiren(d.siren);
    setStrobe(d.strobe);
    setPumpPermit(d.pump_permit);

    // Output time, which is the thing a maintenance visit wants: not "was it told to
    // sound" but for how long the relay actually carried current.
    if (d.siren) stats.siren_on_ms += dt_ms;
    if (d.strobe) stats.strobe_on_ms += dt_ms;
    if (d.pump_permit) stats.pump_permit_ms += dt_ms;

    // The armature relay's auxiliary is the only evidence the building was warned, but
    // it is *judged* in lib/fire_logic.h against its own proof window, because the
    // relay needs several milliseconds to travel. Counting a failure here, on the same
    // tick the coil was commanded, put a bell fault on all eight hourly self-tests of
    // an eight hour run in which the bell sounded every single time.
}

void panelStats(PanelStats& out) { out = stats; }

bool panelSilenceHeld() { return silencePressed(); }
bool panelTestHeld() { return testPressed(); }

}  // namespace ranch

/* ==================== src/telemetry.h ==================== */

// State uplink for the fire panel.
//
// `z=` carries four digits, one per zone, left to right house / barn / store /
// switch room, and each digit is a loop state: 0 healthy, 1 in alarm, 2 loop open,
// 3 loop shorted, 4 unreadable. It is a code because a fire frame has to be small
// enough to survive a bad radio, and it is documented here because a code nobody can
// read is how a "z=2030" gets argued about at 2 a.m.
//
// `siren=1` is what the relay was *commanded* to do; `pfb=` and the dial event are
// what proves the outputs actually moved. A dashboard that shows only the former is
// showing intentions, and on this board that is the difference between a notified
// building and a quiet one.

#include <cstddef>

namespace ranch {

struct UplinkStats {
    uint32_t published;
    uint32_t mqtt_sent;
    uint32_t mqtt_failed;
    uint32_t dial_asserts;
    uint16_t last_len;
    bool broker_connected;
};

struct FireReport {
    LoopRead loops;
    FireDecision d;
    PanelStats stats;
    uint32_t uptime_s;
    uint32_t second_of_day;
    bool clock_valid;
    bool siren_failed;         // a commanded bell that never proved it moved
    float silence_left_s;
    uint8_t silences;
};

void telemetryInit();
void telemetrySetLink(const char* host, uint16_t port, const char* client_id);

using CommandHandler = void (*)(const char* payload, size_t len);
void telemetrySetCommandHandler(CommandHandler fn);

void telemetryPublish(const FireReport& r);

// Events go out on a different source token than the state frame, so a dashboard
// reading the newest FIRE line as state cannot take "reset" for "all clear".
void telemetryEvent(const char* kind, const char* why);

// The dial-out line is separate from the state frame and is *kept asserted*: a
// single "fire" message lost to a brownout is a fire nobody was told about, so this
// repeats for as long as the condition stands.
void telemetryDial(bool active, const char* reason);

void telemetryService();
void telemetryStats(UplinkStats& out);
const char* telemetryLastFrame();
const char* telemetryLastEvent();

}  // namespace ranch

/* ==================== src/telemetry.cpp ==================== */


#include <cstdio>
#include <cstring>

namespace ranch {
namespace {

constexpr size_t FRAME_CAP = 224;

UplinkStats up{};
char frame[FRAME_CAP];
char last_event[64] = "";
char broker_host[64] = "";
char broker_client[24] = "fire";
uint16_t broker_port = MQTT_PORT;
CommandHandler cmd_fn = nullptr;

char loopDigit(LoopState s) {
    switch (s) {
        case LoopState::Normal:  return '0';
        case LoopState::Alarm:   return '1';
        case LoopState::Open:    return '2';
        case LoopState::Shorted: return '3';
        case LoopState::Unknown: return '4';
    }
    return '?';
}

}  // namespace

#if defined(RANCH_SIM)
namespace {
void uplinkMqtt(const char*, size_t) {}
[[maybe_unused]] void uplinkDialMqtt(const char*, size_t) {}
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
bool dial_held = false;

void uplinkMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_STATE, reinterpret_cast<const uint8_t*>(s), n, true)) up.mqtt_sent++;
    else up.mqtt_failed++;
}

// The dial-out is retained, so whatever broker the ranch has on the other end keeps
// the last fire condition until someone clears it. A non-retained "fire" published
// in the second the broker reconnects is a message to nobody.
void uplinkDialMqtt(const char* s, size_t n) {
    if (!broker.connected()) {
        up.mqtt_failed++;
        return;
    }
    if (broker.publish(MQTT_TOPIC_DIAL, reinterpret_cast<const uint8_t*>(s), n, true)) {
        up.mqtt_sent++;
        up.dial_asserts++;
    } else {
        up.mqtt_failed++;
    }
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
    last_event[0] = '\0';
}

void telemetrySetLink(const char* host, uint16_t port, const char* client_id) {
    std::snprintf(broker_host, sizeof(broker_host), "%s", host ? host : "");
    std::snprintf(broker_client, sizeof(broker_client), "%s",
                  (client_id && *client_id) ? client_id : "fire");
    broker_port = port ? port : MQTT_PORT;
}

void telemetrySetCommandHandler(CommandHandler fn) { cmd_fn = fn; }

void telemetryPublish(const FireReport& r) {
    char z[8];
    z[0] = loopDigit(r.loops.zone[0]);
    z[1] = loopDigit(r.loops.zone[1]);
    z[2] = loopDigit(r.loops.zone[2]);
    z[3] = loopDigit(r.loops.zone[3]);
    z[4] = '\0';

    FrameWriter w(frame, sizeof(frame));
    w.begin("FIRE");
    w.add("lvl", fireLevelName(r.d.level));
    w.add("z", z);
    w.add("mcp", r.loops.manual_call ? 1 : 0);
    w.add("flow", r.loops.flow_active ? 1 : 0);
    w.add("arm", r.d.bypassed ? 0 : 1);
    w.add("siren", r.d.siren ? 1 : 0);
    w.add("strb", r.d.strobe ? 1 : 0);
    w.add("pump", r.d.pump_permit ? 1 : 0);
    // The pump's own contactor, seen through the permit's feedback: a permit that is
    // closed with nothing running behind it is a dry riser, and the log has to be
    // able to say which of the two it saw.
    w.add("pfb", pumpPermitIsClosed() ? 1 : 0);
    w.add("sil", r.d.silenced ? 1 : 0);
    if (r.d.supervision) w.add("sup", 1);
    if (r.loops.mcp == LoopState::Open || r.loops.mcp == LoopState::Shorted) w.add("mcps", "fault");
    if (r.loops.flow == LoopState::Open || r.loops.flow == LoopState::Shorted) w.add("flows", "fault");
    w.add("sirenm", static_cast<int>(r.stats.siren_on_ms / 1000u));
    // The decision layer's proof, not this module's guess: 1 means the bell was
    // commanded and the armature relay did not close inside the proof window.
    w.add("bellfail", r.siren_failed ? 1 : 0);
    w.add("sil_n", static_cast<int>(r.silences));
    w.add("sil_left", static_cast<int>(r.silence_left_s));
    w.add("up", static_cast<int>(r.uptime_s > 999999u ? 999999u : r.uptime_s));
    w.add("link", wifiUp() ? "up" : "down");
    if (r.clock_valid) {
        // Modulo before formatting: the RTC is read as seconds-of-day and a device
        // that has been told a leap second or two can hand back 86400, which would
        // otherwise print as "24:00:00" in a log that is supposed to be evidence.
        const uint32_t sod = r.second_of_day % 86400u;
        char ts[16];
        std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", static_cast<int>(sod / 3600u),
                      static_cast<int>((sod / 60u) % 60u), static_cast<int>(sod % 60u));
        w.add("t", ts);
    } else {
        w.add("clock", "unset");
    }
    if (r.d.reason[0] != '\0') w.add("why", r.d.reason);
    w.endLine();

    if (w.overflow()) {
        // A truncated frame would show the dashboard a stale field as fresh, and on
        // this board that field could be the one that says the bell failed.
        up.mqtt_failed++;
        return;
    }

    up.last_len = static_cast<uint16_t>(w.size());
    up.published++;
    consoleWrite(frame, w.size());
    uplinkMqtt(frame, w.size());
}

void telemetryEvent(const char* kind, const char* why) {
    char line[64];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", kind);
    if (why && *why) w.add("why", why);
    w.endLine();
    consoleWrite(line, w.size());
    // Kept for the diagnostic reader. A refused reset and a silence refusal last one
    // frame each in the state stream, which is exactly the kind of thing that has to
    // be readable after the fact rather than caught in the act.
    std::snprintf(last_event, sizeof(last_event), "%s", line);
}

void telemetryDial(bool active, const char* reason) {
    char line[80];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", active ? "dial" : "dial-clear");
    if (reason && *reason) w.add("why", reason);
    w.endLine();
    consoleWrite(line, w.size());
#if !defined(RANCH_SIM)
    if (active) {
        uplinkDialMqtt(line, w.size());
        dial_held = true;
    } else if (dial_held) {
        // One clear message, then silence: the retained dial line has to be emptied,
        // not left holding yesterday's fire.
        uplinkDialMqtt(line, w.size());
        dial_held = false;
    }
#endif
}

void telemetryService() { brokerService(); }

void telemetryStats(UplinkStats& out) { out = up; out.broker_connected = brokerUp(); }
const char* telemetryLastFrame() { return frame; }
const char* telemetryLastEvent() { return last_event; }

}  // namespace ranch

/* ==================== src/hal_sim.cpp ==================== */

// A model of the ranch's detection: loops with relays that take time to move, a
// bell with an armature, a pump that may or may not be behind its permit, and the
// four ways a wire fails.
//
// Everything the firmware sees here is a milliVolt level or a contact state. It is
// never handed "zone 2 is on fire", because a test that hands over the conclusion
// proves only that the code can read a variable.
#if defined(RANCH_SIM)

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

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

/* ==================== src/main.cpp ==================== */

// Fire panel entry point: loop bands, the alarm decision, the dial-out, and the two
// buttons on the door.
//
// One cooperative task scans the loops, decides, and drives the relays in that fixed
// order. The split into a radio task and a panel task is the obvious design and the
// wrong one: the decision needs a set of loop states that were all true at the same
// instant, and a scan that can land in the middle of it describes a building that
// never existed. The six ADC reads cost about a millisecond, so a 20 ms tick carries
// the scan, the decision every 50 ms, and the dial every second without anybody
// needing a mutex.
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
int job_scan = -1, job_control = -1, job_dial = -1, job_persist = -1;
uint32_t last_loop_ms = 0;
uint32_t boot_ms = 0;

FireLimits lim = FIRE_DEFAULTS;
FireState fstate{};
LoopBands bands = LOOP_DEFAULT_BANDS;
FireDecision last_d{};

char ssid[40] = "";
char wifi_pass[64] = "";
char mqtt_host[64] = "";
char tag[16] = "fire-1";
uint16_t mqtt_port = MQTT_PORT;

bool have_clock = false;
uint32_t second_of_day = 0;
FireLevel dial_level = FireLevel::Normal;
char prev_reason[20] = "";
bool silence_edge = false;
bool test_edge = false;
uint8_t sil_hold = 0;
uint8_t test_hold = 0;

void banner() {
    char line[96];
    FrameWriter w(line, sizeof(line));
    w.begin("RANCH");
    w.add("evt", "boot");
    w.add("fw", RANCH_FW_VERSION);
    w.add("rst", resetReason());
    w.add("tag", tag);
    w.add("arm", keyArmed() ? 1 : 0);
    w.add("rtc", clockWasSet() ? 1 : 0);
    w.endLine();
    consoleWrite(line, w.size());
}

void paramsLoad() {
    int32_t v = 0;
    if (!nvGetStr("ssid", ssid, sizeof(ssid))) ssid[0] = '\0';
    if (!nvGetStr("wpass", wifi_pass, sizeof(wifi_pass))) wifi_pass[0] = '\0';
    if (!nvGetStr("mqtt", mqtt_host, sizeof(mqtt_host))) mqtt_host[0] = '\0';
    if (!nvGetStr("tag", tag, sizeof(tag)) || !tag[0]) {
        std::snprintf(tag, sizeof(tag), "%s", "fire-1");
    }
    if (nvGetI32("mport", v) && v > 0 && v < 65536) mqtt_port = static_cast<uint16_t>(v);

    if (nvGetI32("confirm", v) && v >= 5 && v <= 300) lim.confirm_s = static_cast<uint16_t>(v);
    if (nvGetI32("silence", v) && v >= 10 && v <= 3600) lim.silence_s = static_cast<uint16_t>(v);
    if (nvGetI32("silmax", v) && v >= 1 && v <= 10) lim.silence_max = static_cast<uint8_t>(v);
    if (nvGetI32("testper", v) && v >= 60 && v <= 86400) lim.test_period_s = static_cast<uint16_t>(v);
    if (nvGetI32("testms", v) && v >= 50 && v <= 2000) lim.test_ms = static_cast<uint16_t>(v);
    if (nvGetI32("proofms", v) && v >= 20 && v <= 1000) lim.siren_proof_ms = static_cast<uint16_t>(v);
    if (nvGetI32("faultz", v) && v >= 1 && v <= FIRE_ZONES) {
        lim.fault_zones_max = static_cast<uint8_t>(v);
    }

    // The band edges are electrical facts about *this* installation: a site rewired
    // with a 10k pull-up reads a different mid-rail, and a panel that kept the old
    // thresholds would call every healthy loop dirty.
    if (nvGetI32("b_shi", v) && v > 0 && v < 800) bands.short_hi_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_alo", v) && v > 100 && v < 1500) bands.alarm_lo_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_ahi", v) && v > 200 && v < 2000) bands.alarm_hi_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_nlo", v) && v > 500 && v < 2800) bands.normal_lo_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_nhi", v) && v > 800 && v < 3200) bands.normal_hi_mv = static_cast<uint16_t>(v);
    if (nvGetI32("b_olo", v) && v > 1500 && v <= 3300) bands.open_lo_mv = static_cast<uint16_t>(v);
    // Bands typed over each other would make every reading ambiguous, so the order is
    // enforced rather than trusted.
    if (bands.alarm_hi_mv <= bands.alarm_lo_mv) bands.alarm_hi_mv = bands.alarm_lo_mv + 200;
    if (bands.normal_lo_mv <= bands.alarm_hi_mv) bands.normal_lo_mv = bands.alarm_hi_mv + 400;
    if (bands.normal_hi_mv <= bands.normal_lo_mv) bands.normal_hi_mv = bands.normal_lo_mv + 400;
    if (bands.open_lo_mv <= bands.normal_hi_mv) bands.open_lo_mv = bands.normal_hi_mv + 600;
    if (bands.open_lo_mv > LOOP_RAIL_MV) bands.open_lo_mv = LOOP_RAIL_MV;
}

void paramsPersist() {
    nvSetI32("confirm", lim.confirm_s);
    nvSetI32("silence", lim.silence_s);
    nvSetI32("silmax", lim.silence_max);
    nvSetI32("testper", lim.test_period_s);
    nvSetI32("faultz", lim.fault_zones_max);
    nvSetI32("b_shi", bands.short_hi_mv);
    nvSetI32("b_alo", bands.alarm_lo_mv);
    nvSetI32("b_ahi", bands.alarm_hi_mv);
    nvSetI32("b_nlo", bands.normal_lo_mv);
    nvSetI32("b_nhi", bands.normal_hi_mv);
    nvSetI32("b_olo", bands.open_lo_mv);
}

void clockUpdate() {
    uint16_t y = 0;
    uint8_t mo = 0, dm = 0;
    have_clock = clockNow(second_of_day, y, mo, dm);
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
    } else if (static_cast<int32_t>(halMillis() - (down_ms + 30000u)) >= 0) {
        down_ms = halMillis();
        wifiReconnect();
    }
}

void publish() {
    FireReport r{};
    r.loops = panelLoops();
    r.d = last_d;
    r.stats = PanelStats{};
    panelStats(r.stats);
    r.uptime_s = (halMillis() - boot_ms) / 1000u;
    r.second_of_day = second_of_day;
    r.clock_valid = have_clock;
    r.siren_failed = fstate.siren_failed;
    r.silence_left_s = fstate.silence_left_s;
    r.silences = fstate.silences;
    telemetryPublish(r);
}

void onGroundCommand(const char* payload, size_t len) {
    if (!payload || len == 0) return;
    char verb[20], value[32];
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

    bool taken = true;
    if (std::strcmp(verb, "reset") == 0) {
        // A remote reset is a reset, not an override: the same gate the door button
        // goes through, so an MQTT credential cannot do what a key cannot.
        fstate.reset_edge = true;
    } else if (std::strcmp(verb, "silence") == 0) {
        silence_edge = true;
    } else if (std::strcmp(verb, "test") == 0) {
        test_edge = true;
    } else if (std::strcmp(verb, "confirm") == 0) {
        const long v = std::strtol(value, nullptr, 10);
        if (v >= 5 && v <= 300) {
            lim.confirm_s = static_cast<uint16_t>(v);
            paramsPersist();
        } else {
            taken = false;
        }
    } else {
        taken = false;
    }
    telemetryEvent(taken ? "cmd" : "cmd-unknown", verb);
}

// A held button is one press. The three-sample agreement is the same rule the loops
// use, for the same reason: a contact bouncing on its way to closing should not spend
// the panel's three allowed silences in one press.
void buttonEdges() {
    const bool sil = panelSilenceHeld();
    const bool test = panelTestHeld();
    if (sil) {
        if (++sil_hold >= 3) {
            silence_edge = true;
            sil_hold = 0;
        }
    } else {
        sil_hold = 0;
    }
    if (test) {
        if (++test_hold >= 3) {
            test_edge = true;
            test_hold = 0;
        }
    } else {
        test_hold = 0;
    }
}

void controlTick(uint32_t dt_ms) {
    clockUpdate();

    const LoopRead& L = panelLoops();
    FireInput in{};
    for (uint8_t i = 0; i < FIRE_ZONES; ++i) in.zone[i] = L.zone[i];
    in.manual_call = L.manual_call;
    in.flow = L.flow_active;
    in.key_armed = keyArmed();
    in.siren_feedback = sirenFeedbackClosed();
    in.silence_edge = silence_edge;
    in.test_edge = test_edge;
    in.clock_valid = have_clock;
    silence_edge = false;
    test_edge = false;

    const FireDecision d = fireEvaluate(lim, fstate, in, dt_ms / 1000.0f);
    last_d = d;
    panelApply(d, dt_ms);

    if (std::strcmp(d.reason, "RESET") == 0) telemetryEvent("reset", "");
    else if (std::strcmp(d.reason, "RESET BLOCKED") == 0) telemetryEvent("reset-refused", "");
    if (std::strcmp(d.reason, "SILENCE REFUSED") == 0) telemetryEvent("silence-refused", "");

    if (d.level != dial_level || std::strcmp(d.reason, prev_reason) != 0) {
        dial_level = d.level;
        std::snprintf(prev_reason, sizeof(prev_reason), "%s", d.reason);
        // An alarm is announced when it starts and then kept asserted, because the
        // message that matters is the one that is still true when the radio comes up.
        telemetryDial(d.level != FireLevel::Normal, d.reason);
    }
    setStatusLed(d.level == FireLevel::Emergency || d.level == FireLevel::Alarm);
}

void runJobs() {
    const uint32_t now = halMillis();
    uint32_t dt = now - last_loop_ms;
    last_loop_ms = now;
    if (dt > 1000) dt = 1000;                 // a stalled loop must not skip a cycle

    if (sched.due(job_scan, dt)) {
        panelSample(bands);
        buttonEdges();
    }
    if (sched.due(job_control, dt)) controlTick(sched.elapsed(job_control));
    if (sched.due(job_dial, dt)) {
        publish();
        telemetryService();
    }
    if (sched.due(job_persist, dt)) paramsPersist();
    supervision();
}

#if !defined(RANCH_SIM)
void panelTask(void*) {
    esp_task_wdt_init(TASK_WDT_TIMEOUT_MS, true);
    esp_task_wdt_add(NULL);
    for (;;) {
        runJobs();
        esp_task_wdt_reset();
        halDelayMs(20);
    }
}
#endif

}  // namespace

// The host sandbox drives the command channel the same way the broker does, so a
// reset, a silence and a bell test are exercised rather than assumed. Outside the
// anonymous namespace because the test links against it.
#if defined(RANCH_SIM)
void simCommand(const char* cmd) { onGroundCommand(cmd, std::strlen(cmd)); }
void simLimits(uint16_t confirm_s, uint16_t silence_s, uint16_t test_period_s) {
    lim.confirm_s = confirm_s;
    lim.silence_s = silence_s;
    lim.test_period_s = test_period_s;
    fstate.next_test_s = test_period_s;
}
#endif

void appSetup() {
    halInit();
    boot_ms = halMillis();
    last_loop_ms = boot_ms;
    sched.clear();
    panelInit();
    telemetryInit();
    paramsLoad();
    fireReset(fstate);
    last_d = FireDecision{};
    last_d.level = FireLevel::Normal;
    dial_level = FireLevel::Normal;
    prev_reason[0] = '\0';
    silence_edge = false;
    test_edge = false;
    telemetrySetCommandHandler(onGroundCommand);
    banner();

    job_scan = sched.add("scan", SAMPLE_PERIOD_MS);
    job_control = sched.add("control", CONTROL_PERIOD_MS);
    job_dial = sched.add("dial", 1000 / DIAL_HZ);
    job_persist = sched.add("persist", 300000);

    // The first scan has to have happened before the first decision, or the panel
    // opens with every loop Unknown and announces a supervision fault it never
    // measured.
    panelSample(bands);

    if (ssid[0]) wifiConnect(ssid, wifi_pass);
    if (mqtt_host[0]) telemetrySetLink(mqtt_host, mqtt_port, tag);

#if !defined(RANCH_SIM)
    // loop() deletes the Arduino loop task, so the panel loop has to be created
    // here: without it the board prints its banner and then stops.
    xTaskCreatePinnedToCore(panelTask, "panel", STACK_PANEL, nullptr, PRIO_PANEL, nullptr, 1);
#endif
}

void appLoop() {
#if defined(RANCH_SIM)
    halSimPump();
    runJobs();
    halDelayMs(20);
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
