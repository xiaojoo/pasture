// ESP32 back-end for the fire panel's hal.h.
//
// This is the file the shipped image runs against. It is compiled by nothing in the
// current gates -- the esp32 platform package still cannot be fetched on this
// machine -- so the notes below are the record of what has to be measured on the
// bench, and the README's acceptance list is the procedure that goes with them.
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
    // A DS3231 with a dead coin cell returns day 0 of month 0, which is well-formed
    // BCD and not a date. The log timestamps depend on this check.
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

}  // namespace

void halInit() {
    Serial.begin(115200);

    // Outputs first and de-energised: a panel that boots with the bell driven has
    // been dialling the fire service since the brownout, and one that boots with the
    // pump permit closed has flooded a riser nobody asked for.
    pinMode(PIN_SIREN, OUTPUT);
    pinMode(PIN_STROBE, OUTPUT);
    pinMode(PIN_PUMP, OUTPUT);
    pinMode(PIN_WDT_FEED, OUTPUT);
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_SIREN, LOW);
    digitalWrite(PIN_STROBE, LOW);
    digitalWrite(PIN_PUMP, LOW);
    digitalWrite(PIN_WDT_FEED, LOW);
    digitalWrite(PIN_LED_STATUS, LOW);

    pinMode(PIN_SIREN_FB, INPUT_PULLUP);      // armature relay auxiliary to GND
    pinMode(PIN_KEY_ARM, INPUT_PULLUP);       // open = BYPASS, which is the safe read
    pinMode(PIN_BTN_SILENCE, INPUT_PULLUP);
    pinMode(PIN_BTN_TEST, INPUT_PULLUP);

    // Six supervised loops, all on ADC1 by construction (board.h asserts it). ADC2
    // cannot be read with the radio associated, and a fire panel has to read the same
    // loops whether or not it can reach a broker.
    for (int p : {PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER, PIN_MCP, PIN_FLOW}) {
        analogSetPinAttenuation(p, ADC_11db);
    }
    analogSetWidth(12);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    Wire.beginTransmission(RTC_ADDR);
    rtc_present = (Wire.endTransmission(true) == 0);

    if (nvs_open("ranch", NVS_READWRITE, &nvs) != ESP_OK) nvs = 0;

    // One scan before anything is trusted, so the panel does not announce a
    // supervision fault about loops it has not read yet.
    for (int p : {PIN_Z_HOUSE, PIN_Z_BARN, PIN_Z_STORE, PIN_Z_POWER, PIN_MCP, PIN_FLOW}) {
        analogReadMilliVolts(p);
    }
}

uint32_t halMillis() { return millis(); }
void halDelayMs(uint32_t ms) { ::delay(ms); }

void consoleWrite(const char* data, size_t len) {
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
}

uint16_t loopMillivolts(uint8_t pin) {
    // Calibrated milliVolts, in the same unit the band edges in NVS are given. The
    // 11db input is non-linear at both ends, which is why the bench list measures the
    // four bands rather than trusting the divider arithmetic.
    return static_cast<uint16_t>(analogReadMilliVolts(pin));
}

void setSiren(bool on) { digitalWrite(PIN_SIREN, on ? HIGH : LOW); }
void setStrobe(bool on) { digitalWrite(PIN_STROBE, on ? HIGH : LOW); }
void setPumpPermit(bool on) { digitalWrite(PIN_PUMP, on ? HIGH : LOW); }
bool pumpPermitIsClosed() { return permit_closed; }

bool sirenFeedbackClosed() { return digitalRead(PIN_SIREN_FB) == LOW; }
bool keyArmed() { return digitalRead(PIN_KEY_ARM) == LOW; }
bool silencePressed() { return digitalRead(PIN_BTN_SILENCE) == LOW; }
bool testPressed() { return digitalRead(PIN_BTN_TEST) == LOW; }

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
        // SNTP is a fallback for the RTC. After a power cut with no network the RTC is
        // the only thing that can date a log entry, and a fire log that cannot say
        // when is worth much less than one that can.
        configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
        have_sntp = true;
        sntp_synced_ms = millis();
        sntp_epoch = static_cast<uint32_t>(time(nullptr));
        return true;
    }
    return false;
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
int wifiRssi() { return WiFi.RSSI(); }
void wifiReconnect() { WiFi.disconnect(); WiFi.reconnect(); }

void feedWatchdog() {
    static bool level_state = false;
    level_state = !level_state;
    digitalWrite(PIN_WDT_FEED, level_state ? HIGH : LOW);
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
        // A watchdog reset of a fire panel is a supervisory event in its own right:
        // the building was unwatched for the duration of the boot.
        case ESP_RST_BROWNOUT: return "brownout";
        default:               return "unknown";
    }
}

}  // namespace ranch

#endif  // !RANCH_SIM
