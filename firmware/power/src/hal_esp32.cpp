// ESP32 back-end for the switchboard board's hal.h.
//
// This is the file the shipped image runs against. It is deliberately not
// compiled by the simulator build or by the host tests, so the notes below about
// ordering and ranges are the record of what has to be checked on the bench; the
// README carries the acceptance list that goes with them.
#if !defined(RANCH_SIM)

#include "hal.h"

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <cstring>
#include <esp_system.h>
#include <nvs.h>
#include <time.h>

#include "board.h"

namespace ranch {
namespace {

nvs_handle_t nvs = 0;
bool wdt_level = false;

// Transitions from the phase A comparator, stamped in microseconds: the firmware
// measures a period, so the clock behind it has to be finer than the period.
volatile uint16_t zc_trans = 0;
volatile uint32_t zc_first_us = 0;
volatile uint32_t zc_last_us = 0;
volatile uint32_t zc_prev_us = 0;

void onTransition() {
    const uint32_t t = micros();
    // A comparator over a rectified front end can chatter through its threshold
    // twice around each zero crossing. 500 microseconds is 5% of a half period at
    // 50 Hz: long enough to reject the chatter, far too short to merge two real
    // transitions.
    if (t - zc_prev_us < 500u) return;
    zc_prev_us = t;
    if (zc_trans == 0u) zc_first_us = t;
    if (zc_trans < 0xFFFFu) zc_trans++;
    zc_last_us = t;
}

bool rtc_present = false;
bool have_sntp = false;
uint32_t sntp_synced_ms = 0;
uint32_t sntp_epoch = 0;

uint8_t bcd2bin(uint8_t b) { return static_cast<uint8_t>((b >> 4) * 10 + (b & 0x0F)); }
uint8_t bin2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

bool rtcRead(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(static_cast<uint8_t>(0x00));
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(RTC_ADDR), 7) != 7) return false;
    const uint8_t s = bcd2bin(Wire.read() & 0x7F);
    const uint8_t m = bcd2bin(Wire.read());
    const uint8_t h = bcd2bin(Wire.read() & 0x3F);
    const uint8_t d = bcd2bin(Wire.read());
    const uint8_t mo = bcd2bin(Wire.read() & 0x1F);
    const uint8_t y = bcd2bin(Wire.read());
    Wire.read();
    second_of_day = static_cast<uint32_t>(h) * 3600u + static_cast<uint32_t>(m) * 60u + s;
    year = 2000u + y;
    month = mo;
    day = d;
    // A DS3231 that has lost its battery returns 00:00 on day 0 of month 0, which
    // is a valid BCD pattern and not a valid date. The energy day key depends on
    // this check.
    return mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h <= 23 && m <= 59 && s <= 59;
}

void rtcWrite(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(static_cast<uint8_t>(0x00));
    Wire.write(bin2bcd(second));
    Wire.write(bin2bcd(minute));
    Wire.write(bin2bcd(hour));
    Wire.write(bin2bcd(day));
    Wire.write(bin2bcd(month));
    Wire.write(bin2bcd(static_cast<uint8_t>(year - 2000)));
    Wire.write(static_cast<uint8_t>(1));
    Wire.endTransmission(true);
}

bool clockFromSntp(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (!have_sntp) return false;
    const uint32_t age_s = (millis() - sntp_synced_ms) / 1000u;
    if (age_s > 60u * 60u * 24u) return false;
    const time_t t = static_cast<time_t>(sntp_epoch + age_s);
    struct tm local;
    localtime_r(&t, &local);
    second_of_day = static_cast<uint32_t>(local.tm_hour) * 3600u +
                    static_cast<uint32_t>(local.tm_min) * 60u + local.tm_sec;
    year = static_cast<uint16_t>(local.tm_year + 1900);
    month = static_cast<uint8_t>(local.tm_mon + 1);
    day = static_cast<uint8_t>(local.tm_mday);
    return true;
}

// ADS1115 in single-ended mode against GND, ±2.048 V range, 860 SPS. Written to
// the part directly rather than through a driver library: there are three
// registers this board touches and a dependency that tracks them is a dependency
// that will be updated under the firmware.
constexpr uint8_t ADS_REG_CONFIG = 0x01;
constexpr uint8_t ADS_REG_CONVERT = 0x00;
constexpr uint16_t ADS_MUX_GND(uint8_t ch) { return static_cast<uint16_t>(ch & 0x07u) << 12; }
constexpr uint16_t ADS_PGA_2048 = 0x0200;
constexpr uint16_t ADS_SINGLE = 0x0100;
constexpr uint16_t ADS_DR_860 = 0x0080;
constexpr uint16_t ADS_OS = 0x8000;
constexpr uint16_t ADS_CDISABLE = 0x0003;
// 2.048 V over 32768 counts.
constexpr float ADS_LSB_MV = 2048.0f / 32768.0f;

bool adsWrite(uint16_t cfg) {
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(ADS_REG_CONFIG);
    Wire.write(static_cast<uint8_t>(cfg >> 8));
    Wire.write(static_cast<uint8_t>(cfg & 0xFF));
    return Wire.endTransmission(true) == 0;
}

}  // namespace

void halInit() {
    Serial.begin(115200);

    // Coils first, and de-energised: the permit inputs on GPIO0 read whatever the
    // boot strapping left behind until they are configured, so nothing may be
    // allowed to close before that has happened.
    pinMode(PIN_PUMP, OUTPUT);
    pinMode(PIN_LIGHTS, OUTPUT);
    pinMode(PIN_WDT_FEED, OUTPUT);
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_PUMP, LOW);
    digitalWrite(PIN_LIGHTS, LOW);
    digitalWrite(PIN_WDT_FEED, LOW);
    digitalWrite(PIN_LED_STATUS, LOW);

    pinMode(PIN_BRK_AUX, INPUT_PULLUP);     // dry make to GND
    pinMode(PIN_PUMP_FB, INPUT_PULLUP);
    pinMode(PIN_IN_PUMP, INPUT_PULLDOWN);   // active high, see board.h
    pinMode(PIN_IN_LIGHTS, INPUT_PULLDOWN);
    pinMode(PIN_ZC_A, INPUT_PULLUP);        // open-collector comparator output

    // All six analog inputs are on ADC1 by construction (board.h asserts it):
    // ADC2 cannot be read while WiFi is associated, and a channel that silently
    // returns the last value is worse than one that does not exist.
    for (int p : {PIN_VA, PIN_VB, PIN_VC, PIN_IA, PIN_IB, PIN_IC}) {
        analogSetPinAttenuation(p, ADC_11db);
    }
    analogSetWidth(12);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);              // 400 kHz with a 3.3 V pull-up on a cabinet
                                        // loom is how a bus starts losing bits

    Wire.beginTransmission(RTC_ADDR);
    rtc_present = (Wire.endTransmission(true) == 0);
    Wire.beginTransmission(ADS1115_ADDR);
    // Leave the converter in its reset state (single-shot, comparator disabled,
    // alerts floating): every read below starts its own conversion, so there is no
    // continuous-mode drift to fight and no data rate to renegotiate.
    adsWrite(ADS_PGA_2048 | ADS_CDISABLE);

    // Both edges: the square wave out of the comparator gives two transitions per
    // mains cycle, which is what frequencyFromSpan() divides by.
    attachInterrupt(digitalPinToInterrupt(PIN_ZC_A), onTransition, CHANGE);

    if (nvs_open("ranch", NVS_READWRITE, &nvs) != ESP_OK) nvs = 0;
}

uint32_t halMillis() { return millis(); }
void halDelayMs(uint32_t ms) { ::delay(ms); }

void consoleWrite(const char* data, size_t len) {
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
}

uint16_t analogMillivolts(uint8_t pin) {
    // The filtered, calibrated milliVolt reading, not a raw count: the meter's
    // range checks are in milliVolts and have to be the same quantity on both
    // back-ends.
    return static_cast<uint16_t>(analogReadMilliVolts(pin));
}

void zeroCrossSpan(uint32_t& microseconds, uint16_t& intervals) {
    noInterrupts();
    const uint16_t n = zc_trans;
    const uint32_t first = zc_first_us;
    const uint32_t last = zc_last_us;
    zc_trans = 0;
    interrupts();
    if (n >= 2u) {
        microseconds = last - first;
        intervals = static_cast<uint16_t>(n - 1u);
    } else {
        microseconds = 0u;
        intervals = 0u;
    }
}

bool extAdcMV(uint8_t channel, float& millivolts) {
    const uint16_t cfg = ADS_MUX_GND(channel) | ADS_PGA_2048 | ADS_SINGLE | ADS_DR_860 |
                         ADS_OS | ADS_CDISABLE;
    if (!adsWrite(cfg)) return false;
    // 860 SPS converts in about 1.2 ms. The done bit is polled rather than slept
    // on blind, because a converter that has lost its pull-up answers with an
    // all-ones read and the only thing that catches it is the bus not replying.
    bool done = false;
    for (uint8_t i = 0; i < 12 && !done; ++i) {
        delayMicroseconds(500);
        Wire.beginTransmission(ADS1115_ADDR);
        Wire.write(ADS_REG_CONFIG);
        if (Wire.endTransmission(true) != 0) return false;
        if (Wire.requestFrom(static_cast<int>(ADS1115_ADDR), 2) != 2) return false;
        const uint16_t hi = static_cast<uint16_t>(Wire.read()) << 8;
        const uint16_t reg = hi | Wire.read();
        if (reg == 0xFFFFu) return false;             // SDA stuck high
        done = (reg & ADS_OS) == 0;
    }
    if (!done) return false;

    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(ADS_REG_CONVERT);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(ADS1115_ADDR), 2) != 2) return false;
    const uint16_t hi = static_cast<uint16_t>(Wire.read()) << 8;
    const int16_t raw = static_cast<int16_t>(hi | Wire.read());
    millivolts = static_cast<float>(raw) * ADS_LSB_MV;
    if (millivolts < 0.0f) millivolts = 0.0f;         // single-ended: only negatives are noise
    return true;
}

void setPump(bool on) { digitalWrite(PIN_PUMP, on ? HIGH : LOW); }
void setLights(bool on) { digitalWrite(PIN_LIGHTS, on ? HIGH : LOW); }
bool pumpFeedbackClosed() { return digitalRead(PIN_PUMP_FB) == LOW; }
bool breakerClosed() { return digitalRead(PIN_BRK_AUX) == LOW; }
bool pumpPermitClosed() { return digitalRead(PIN_IN_PUMP) == HIGH; }
bool lightsPermitClosed() { return digitalRead(PIN_IN_LIGHTS) == HIGH; }

bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (rtc_present && rtcRead(second_of_day, year, month, day)) return true;
    return clockFromSntp(second_of_day, year, month, day);
}

void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    if (rtc_present) rtcWrite(year, month, day, hour, minute, second);
    sntp_epoch = 0;
    have_sntp = false;
}

bool clockWasSet() {
    uint32_t s = 0;
    uint16_t y = 0;
    uint8_t m = 0, d = 0;
    return clockNow(s, y, m, d) && y >= 2024;
}

bool nvGetI32(const char* key, int32_t& out) {
    if (!nvs) return false;
    return nvs_get_i32(nvs, key, &out) == ESP_OK;
}
bool nvSetI32(const char* key, int32_t value) {
    if (!nvs) return false;
    return nvs_set_i32(nvs, key, value) == ESP_OK && nvs_commit(nvs) == ESP_OK;
}
bool nvGetF32(const char* key, float& out) {
    if (!nvs) return false;
    return nvs_get_f32(nvs, key, &out) == ESP_OK;
}
bool nvSetF32(const char* key, float value) {
    if (!nvs) return false;
    return nvs_set_f32(nvs, key, value) == ESP_OK && nvs_commit(nvs) == ESP_OK;
}
bool nvGetStr(const char* key, char* buf, size_t cap) {
    if (!nvs) return false;
    size_t n = cap;
    if (nvs_get_str(nvs, key, buf, &n) != ESP_OK) return false;
    return true;
}
bool nvSetStr(const char* key, const char* value) {
    if (!nvs) return false;
    return nvs_set_str(nvs, key, value) == ESP_OK && nvs_commit(nvs) == ESP_OK;
}

bool wifiConnect(const char* s, const char* pass) {
    if (!s || !*s) return false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(s, pass && *pass ? pass : nullptr);
    for (uint8_t i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
        // SNTP is a fallback for the RTC, not a substitute for it: after a power
        // cut with no network the RTC is the only thing that knows what today is,
        // and the daily energy counter needs that.
        configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
        return true;
    }
    return false;
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
int wifiRssi() { return WiFi.RSSI(); }
void wifiReconnect() { WiFi.disconnect(); WiFi.reconnect(); }

void feedWatchdog() {
    // The external watchdog on this board is a windowed part: it wants a pulse
    // between 10 Hz and 100 Hz, so the toggle happens on the control tick and the
    // timeout is set to a value the tick can comfortably beat.
    wdt_level = !wdt_level;
    digitalWrite(PIN_WDT_FEED, wdt_level ? HIGH : LOW);
}

void setStatusLed(bool on) { digitalWrite(PIN_LED_STATUS, on ? HIGH : LOW); }

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_EXT:      return "external";
        case ESP_RST_SW:       return "software";
        case ESP_RST_PANIC:    return "panic";
        case ESP_RST_INT_WDT:  return "int-wdt";
        case ESP_RST_TASK_WDT: return "task-wdt";
        case ESP_RST_WDT:      return "wdt";
        // On a switchboard this one is the interesting column: a brownout during a
        // motor start is the same event the board is there to report.
        case ESP_RST_BROWNOUT: return "brownout";
        default:               return "unknown";
    }
}

}  // namespace ranch

#endif  // !RANCH_SIM
