// ESP32 back-end for the water board's hal.h. This is the code that runs on the
// panel in the pump house.
//
// Nothing in here decides anything about water: it drives a coil, counts a pulse
// tube, reads two voltage dividers, keeps a wall clock and stores parameters.
// Keeping it that thin is what makes hal_sim.cpp an honest stand-in - if the
// simulation and the panel disagree, the disagreement can only be here.
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

volatile uint32_t pulses_barn = 0;
volatile uint32_t pulses_house = 0;
nvs_handle_t nvs = 0;

bool pump_on = false;
bool led_on = false;
bool wdt_level = false;

// The RTC on this board is a DS3231 on the shared I2C bus. If it does not answer
// - a connector knocked loose during the winter - the board falls back to SNTP
// over WiFi, and if that has not answered yet, the clock is reported invalid
// rather than guessed at from millis().
bool rtc_present = false;
bool have_sntp = false;
uint32_t sntp_synced_ms = 0;
uint32_t sntp_epoch = 0;

uint8_t bcd2bin(uint8_t b) { return static_cast<uint8_t>((b >> 4) * 10 + (b & 0x0F)); }
uint8_t bin2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

void isrBarn() { pulses_barn++; }
void isrHouse() { pulses_house++; }

bool rtcRead(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    Wire.beginTransmission(0x68);
    Wire.write(static_cast<uint8_t>(0x00));            // register 0: seconds
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(0x68), 7) != 7) return false;
    const uint8_t s = bcd2bin(Wire.read() & 0x7F);
    const uint8_t m = bcd2bin(Wire.read());
    const uint8_t h = bcd2bin(Wire.read() & 0x3F);     // 12/24 flag ignored: set to 24h
    const uint8_t d = bcd2bin(Wire.read());
    const uint8_t mo = bcd2bin(Wire.read() & 0x1F);
    const uint8_t y = bcd2bin(Wire.read());
    Wire.read();                                        // weekday
    second_of_day = static_cast<uint32_t>(h) * 3600u + static_cast<uint32_t>(m) * 60u + s;
    year = 2000u + y;
    month = mo;
    day = d;
    return mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h <= 23 && m <= 59 && s <= 59;
}

void rtcWrite(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    Wire.beginTransmission(0x68);
    Wire.write(static_cast<uint8_t>(0x00));
    Wire.write(bin2bcd(second));
    Wire.write(bin2bcd(minute));
    Wire.write(bin2bcd(hour));                          // 24-hour mode
    Wire.write(bin2bcd(day));
    Wire.write(bin2bcd(month));
    Wire.write(bin2bcd(static_cast<uint8_t>(year - 2000)));
    Wire.write(static_cast<uint8_t>(1));                // Monday, unused
    Wire.endTransmission(true);
}

bool clockFromSntp(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (!have_sntp) return false;
    // Trust the epoch only while the tick source is still running from it; a board
    // that has been off since the sync would otherwise report the old time as now.
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

    pinMode(PIN_VALVE_BARN, OUTPUT);
    pinMode(PIN_VALVE_HOUSE, OUTPUT);
    pinMode(PIN_PUMP, OUTPUT);
    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_WDT_FEED, OUTPUT);
    digitalWrite(PIN_VALVE_BARN, LOW);
    digitalWrite(PIN_VALVE_HOUSE, LOW);
    digitalWrite(PIN_PUMP, LOW);
    digitalWrite(PIN_LED_STATUS, LOW);
    digitalWrite(PIN_WDT_FEED, LOW);

    pinMode(PIN_FLOW_BARN, INPUT_PULLUP);
    pinMode(PIN_FLOW_HOUSE, INPUT_PULLUP);
    pinMode(PIN_LEAK, INPUT_PULLUP);        // the rope sensor sinks when wet
    pinMode(PIN_TANK_HIGH, INPUT_PULLUP);

    // Both coil channels run at the same rate so one hold duty serves both;
    // 8 bit is coarser than the datasheet's current ripple wants, so use 10.
    ledcSetup(0, VALVE_HOLD_HZ, 10);
    ledcSetup(1, VALVE_HOLD_HZ, 10);
    ledcAttachPin(PIN_VALVE_BARN, 0);
    ledcAttachPin(PIN_VALVE_HOUSE, 1);

    // ADC1 only: ADC2 cannot be read while WiFi is associated.
    analogSetPinAttenuation(PIN_PRESSURE, ADC_11db);
    analogSetPinAttenuation(PIN_LEVEL, ADC_11db);
    analogSetWidth(12);

    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_BARN), isrBarn, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_FLOW_HOUSE), isrHouse, RISING);

    Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
    Wire.beginTransmission(0x68);
    rtc_present = (Wire.endTransmission(true) == 0);

    if (nvs_open("ranch", NVS_READWRITE, &nvs) != ESP_OK) nvs = 0;
}

uint32_t halMillis() { return millis(); }
void halDelayMs(uint32_t ms) { ::delay(ms); }

void consoleWrite(const char* data, size_t len) {
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
}

void setValvePin(uint8_t pin, float duty) {
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    const uint32_t raw = static_cast<uint32_t>(duty * 1023.0f + 0.5f);
    if (pin == PIN_VALVE_BARN) ledcWrite(0, raw);
    else if (pin == PIN_VALVE_HOUSE) ledcWrite(1, raw);
}

void setPump(bool on) {
    pump_on = on;
    digitalWrite(PIN_PUMP, on ? HIGH : LOW);
}

uint32_t flowPulsesBarn() {
    noInterrupts();
    const uint32_t v = pulses_barn;
    interrupts();
    return v;
}

uint32_t flowPulsesHouse() {
    noInterrupts();
    const uint32_t v = pulses_house;
    interrupts();
    return v;
}

bool leakWet() { return digitalRead(PIN_LEAK) == LOW; }
bool tankHigh() { return digitalRead(PIN_TANK_HIGH) == LOW; }

uint16_t analogMillivolts(uint8_t pin) { return static_cast<uint16_t>(analogReadMilliVolts(pin)); }

bool clockNow(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    if (rtc_present && rtcRead(second_of_day, year, month, day)) return true;
    return clockFromSntp(second_of_day, year, month, day);
}

void clockSet(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    if (rtc_present) {
        rtcWrite(year, month, day, hour, minute, second);
        return;
    }
    // No RTC: seed the software clock and let SNTP correct it.
    struct tm t{};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    sntp_epoch = static_cast<uint32_t>(mktime(&t));
    sntp_synced_ms = millis();
    have_sntp = true;
}

bool clockWasSet() {
    uint32_t sod = 0;
    uint16_t y = 0;
    uint8_t mo = 0, d = 0;
    return clockNow(sod, y, mo, d);
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
    uint32_t bits = 0;
    if (nvs_get_u32(nvs, key, &bits) != ESP_OK) return false;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

bool nvSetF32(const char* key, float value) {
    if (!nvs) return false;
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return nvs_set_u32(nvs, key, bits) == ESP_OK && nvs_commit(nvs) == ESP_OK;
}

bool nvGetStr(const char* key, char* buf, size_t cap) {
    if (!nvs || cap == 0) return false;
    size_t want = cap;
    if (nvs_get_str(nvs, key, buf, &want) != ESP_OK) { buf[0] = '\0'; return false; }
    buf[cap - 1] = '\0';
    return true;
}

bool nvSetStr(const char* key, const char* value) {
    if (!nvs) return false;
    return nvs_set_str(nvs, key, value) == ESP_OK && nvs_commit(nvs) == ESP_OK;
}

bool wifiConnect(const char* ssid, const char* pass) {
    if (ssid == nullptr || ssid[0] == '\0') return false;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, pass && pass[0] ? pass : nullptr);
    // SNTP is the fallback clock. The timezone is an env string because the ranch
    // keeps its schedule in local time, and a UTC schedule dispenses at 02:00 in
    // the middle of the night.
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    return true;
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }
int wifiRssi() { return wifiUp() ? WiFi.RSSI() : -256; }

void wifiReconnect() {
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - last) < 15000) return;
    last = now;
    WiFi.disconnect(false);
    WiFi.begin();
}

void feedWatchdog() {
    wdt_level = !wdt_level;
    digitalWrite(PIN_WDT_FEED, wdt_level ? HIGH : LOW);
}

void setStatusLed(bool on) {
    led_on = on;
    digitalWrite(PIN_LED_STATUS, on ? HIGH : LOW);
}

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_EXT:      return "external";
        case ESP_RST_SW:       return "software";
        case ESP_RST_PANIC:    return "panic";
        case ESP_RST_INT_WDT:  return "int-wdt";
        case ESP_RST_TASK_WDT: return "task-wdt";
        case ESP_RST_WDT:      return "wdt";
        case ESP_RST_BROWNOUT: return "brownout";
        default:               return "unknown";
    }
}

}  // namespace ranch

#endif  // !RANCH_SIM
