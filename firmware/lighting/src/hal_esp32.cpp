// ESP32 back-end for the lighting board's hal.h.
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
bool street_on = false;
bool house_on = false;
float dim_now = 0.0f;
bool wdt_level = false;

// The PIR is a level that goes high for a fixed hold time; the board needs both
// the level and "how long since it last moved", so edges are tracked here rather
// than in the policy.
uint32_t motion_last_ms = 0;
bool motion_previous = false;

bool rtc_present = false;
bool have_sntp = false;
uint32_t sntp_synced_ms = 0;
uint32_t sntp_epoch = 0;

uint8_t bcd2bin(uint8_t b) { return static_cast<uint8_t>((b >> 4) * 10 + (b & 0x0F)); }
uint8_t bin2bcd(uint8_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }

bool rtcRead(uint32_t& second_of_day, uint16_t& year, uint8_t& month, uint8_t& day) {
    Wire.beginTransmission(0x68);
    Wire.write(static_cast<uint8_t>(0x00));
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(0x68), 7) != 7) return false;
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
    return mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h <= 23 && m <= 59 && s <= 59;
}

void rtcWrite(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
              uint8_t second) {
    Wire.beginTransmission(0x68);
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

    pinMode(PIN_RELAY_STREET, OUTPUT);
    pinMode(PIN_RELAY_HOUSE, OUTPUT);
    pinMode(PIN_DIM_BARN, OUTPUT);
    pinMode(PIN_WDT_FEED, OUTPUT);
    digitalWrite(PIN_RELAY_STREET, LOW);
    digitalWrite(PIN_RELAY_HOUSE, LOW);
    digitalWrite(PIN_WDT_FEED, LOW);

    pinMode(PIN_PIR, INPUT);              // an HC-SR501 drives its own line
    pinMode(PIN_FAULT, INPUT_PULLUP);     // open collector, closed to GND = fault

    ledcSetup(0, DIM_HZ, DIM_BITS);
    ledcAttachPin(PIN_DIM_BARN, 0);
    ledcWrite(0, 0);

    analogSetPinAttenuation(PIN_LDR, ADC_11db);
    analogSetPinAttenuation(PIN_BALLAST, ADC_11db);
    analogSetWidth(12);

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

void setRelayStreet(bool on) {
    street_on = on;
    digitalWrite(PIN_RELAY_STREET, on ? HIGH : LOW);
}

void setRelayHouse(bool on) {
    house_on = on;
    digitalWrite(PIN_RELAY_HOUSE, on ? HIGH : LOW);
}

void setDimDuty(float duty) {
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    dim_now = duty;
    ledcWrite(0, static_cast<uint32_t>(duty * ((1u << DIM_BITS) - 1u) + 0.5f));
}

bool relayStreetIsOn() { return street_on; }
bool relayHouseIsOn() { return house_on; }
float dimDuty() { return dim_now; }

bool motionSeen() {
    const bool now_high = digitalRead(PIN_PIR) == HIGH;
    if (now_high && !motion_previous) motion_last_ms = millis();
    motion_previous = now_high;
    return now_high;
}

uint32_t motionAgeMs() {
    if (motion_last_ms == 0) return 0xFFFFFFFFu;
    return millis() - motion_last_ms;
}

bool driverFaultContact() { return digitalRead(PIN_FAULT) == LOW; }

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
    configTime(RANCH_UTC_OFFSET_H * 3600.0, 0, "pool.ntp.org", "time.google.com");
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
