// ESP32 back-end for hal.h. This is the code that runs on the aircraft.
//
// Deliberately thin: every function here is a register, a driver call, or a
// bounded conversion, and nothing in it decides anything about flight. That is
// what makes the simulated back-end an honest stand-in -- if behaviour diverges,
// the divergence can only come from these calls.
#if !defined(RANCH_SIM)

#include "hal.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <cstring>
#include <esp_system.h>
#include <nvs.h>

#include "board.h"

namespace ranch {
namespace {

// UART_NUM_2 leaves 0 for the flasher and 1 for anything the airframe adds
// later (an RC bridge, a rangefinder).
HardwareSerial fc_serial(2);
SPIClass sd_spi(HSPI);
File card_file;
nvs_handle_t nvs = 0;

bool safe_state = true;
bool arm_closed = false;
bool wdt_level = false;
bool card_ready = false;
bool led_state = false;
uint32_t last_wifi_try = 0;
uint8_t batt_samples = 0;
uint32_t batt_sum_mv = 0;
float batt_div_k = 1.0f;   // calibration multiplier, stored in NVS as batt_k

// Five-sample median-ish average: the ADC sits next to a switching BEC and a
// single read is not repeatable enough to make a battery decision on.
uint32_t readMillivolts(uint8_t pin) {
    uint32_t acc = 0;
    for (int i = 0; i < 5; ++i) acc += analogReadMilliVolts(pin);
    return acc / 5;
}

}  // namespace

void halInit() {
    Serial.begin(DBG_BAUD);
    fc_serial.begin(FC_BAUD, SERIAL_8N1, PIN_FC_RX, PIN_FC_TX);

    pinMode(PIN_SAFE_OUT, OUTPUT);
    digitalWrite(PIN_SAFE_OUT, HIGH);              // safe until told otherwise
    safe_state = true;
    pinMode(PIN_ARM_IN, INPUT_PULLUP);
    pinMode(PIN_WDT_FEED, OUTPUT);
    digitalWrite(PIN_WDT_FEED, LOW);
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    if (nvs_open("ranch", NVS_READWRITE, &nvs) != ESP_OK) nvs = 0;
    float k = 1.0f;
    if (nvs && nvGetF32("batt_k", k) && k > 0.5f && k < 2.0f) batt_div_k = k;

    sd_spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    // Arduino-ESP32 core 2.x signature. The mount point is the default
    // "/sdcard"; core 3.x moved the SPIClass argument into SD_card's config, so
    // this is the one line to change if the core is ever upgraded.
    card_ready = SD.begin(PIN_SD_CS, &sd_spi);
}

uint32_t halMillis() { return millis(); }

void halDelayMs(uint32_t ms) { ::delay(ms); }

void consoleWrite(const char* data, size_t len) {
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
}

int fcAvailable() { return fc_serial.available(); }

int fcRead(uint8_t* buf, size_t cap) {
    if (cap == 0) return 0;
    const int n = fc_serial.read(buf, cap);
    return n < 0 ? 0 : n;
}

void fcWrite(const uint8_t* buf, size_t len) {
    fc_serial.write(buf, len);
    fc_serial.flush();
}

void setSafe(bool safe) {
    safe_state = safe;
    digitalWrite(PIN_SAFE_OUT, safe ? HIGH : LOW);
}

bool safeRequested() { return safe_state; }

void armSwitchClosed() { arm_closed = (digitalRead(PIN_ARM_IN) == LOW); }
bool armSwitchIsClosed() { return arm_closed; }

void feedWatchdog() {
    wdt_level = !wdt_level;
    digitalWrite(PIN_WDT_FEED, wdt_level ? HIGH : LOW);
}

// The pattern index carries the phase; the caller drives the cadence, so the
// blink rate lives with the state machine that chooses the pattern.
void setStatusLed(uint8_t pattern) {
    led_state = (pattern & 1u) != 0u;
    digitalWrite(PIN_LED_STATUS, led_state ? HIGH : LOW);
}

float batteryVolts() {
    batt_sum_mv = readMillivolts(PIN_BATT_ADC);
    return static_cast<float>(batt_sum_mv) / 1000.0f * BATT_DIVIDER * batt_div_k;
}

float batteryCellVolts() { return batteryVolts() / static_cast<float>(BATT_CELLS); }

float busCurrentAmps() {
    const uint32_t mv = readMillivolts(PIN_CURR_ADC);
    const float a = (static_cast<float>(mv) - CURR_ZERO_MV) / CURR_MV_PER_A;
    // The ACS712 idles 25 mV or so off its nominal zero; dead-banding that keeps
    // a stationary aircraft from reporting a tenth of an amp of phantom load.
    return a > 0.15f || a < -0.15f ? a : 0.0f;
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
    WiFi.setSleep(false);          // modem sleep adds seconds to a PING
    WiFi.begin(ssid, pass && pass[0] ? pass : nullptr);
    last_wifi_try = millis();
    return true;
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

int wifiRssi() { return wifiUp() ? WiFi.RSSI() : -256; }

// The Arduino core retries association by itself; this is for the case where it
// has given up (a router that was power-cycled mid-flight), which needs a fresh
// begin() rather than waiting for the internal backoff.
void wifiReconnect() {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - last_wifi_try) < 15000) return;
    last_wifi_try = now;
    WiFi.disconnect(false);
    WiFi.begin();
}

bool sdOpen(const char* path) {
    if (!card_ready) return false;
    if (card_file) card_file.close();
    card_file = SD.open(path, FILE_APPEND);
    if (!card_file) card_file = SD.open(path, FILE_WRITE);
    return static_cast<bool>(card_file);
}

bool sdAppend(const char* data, size_t len) {
    if (!card_file) return false;
    const size_t written = card_file.write(reinterpret_cast<const uint8_t*>(data), len);
    return written == len;
}

void sdClose() {
    if (card_file) {
        card_file.flush();
        card_file.close();
    }
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
