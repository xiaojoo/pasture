// ESP32-S3 back-end for hal.h. This is the code that runs on the airframe.
//
// Deliberately thin: every function here is a register, a driver call, or a
// bounded conversion, and nothing in it decides anything about the show. That is
// what makes the simulated back-end an honest stand-in -- if behaviour diverges,
// the divergence can only come from these calls.
#if !defined(RANCH_SIM)

#include "hal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <cstring>
#include <esp_attr.h>
#include <esp_system.h>
#include <nvs.h>

#include "board.h"

namespace ranch {
namespace {

// UART_NUM_1 for the receiver and 2 for the flight controller leaves 0 for the
// flasher. Both are begun with the RX and TX pins given explicitly, because the
// core's defaults for UART1 are not on this board's headers.
HardwareSerial rtk_serial(1);
HardwareSerial fc_serial(2);

// GRB, because that is the order a WS2812B latches its three channels in, and
// 800 kHz because that is the bit rate the part was designed at. The strip's own
// brightness is left at full: the headroom is applied in firmware, by
// ledBrightness(), so that the number the show is planned with and the number the
// pixels get are the same number and can be asserted on.
Adafruit_NeoPixel strip(SHOW_LED_PIXELS, PIN_LED_DATA, NEO_GRB + NEO_KHZ800);

nvs_handle_t nvs = 0;
bool safe_closed = false;
bool wdt_level = false;
bool buzzer_state = false;
bool led_state = false;
uint32_t last_wifi_try = 0;
float batt_div_k = 1.0f;            // calibration multiplier, stored in NVS as batt_k

// The receiver's TIMEPULSE. Set in the ISR, consumed by rtkPpsEdge(), so a show
// that starts on the GNSS second cannot start twice and cannot miss the edge
// between two polls.
volatile bool pps_edge = false;

void IRAM_ATTR onPps() { pps_edge = true; }

// Five-sample median-ish average: the ADC sits next to a switching BEC and a
// single read is not repeatable enough to make a fuel decision on.
uint32_t readMillivolts(uint8_t pin) {
    uint32_t acc = 0;
    for (int i = 0; i < 5; ++i) acc += analogReadMilliVolts(pin);
    return acc / 5;
}

}  // namespace

void halInit() {
    Serial.begin(DBG_BAUD);
    rtk_serial.begin(RTK_BAUD, SERIAL_8N1, PIN_RTK_RX, PIN_RTK_TX);
    fc_serial.begin(FC_BAUD, SERIAL_8N1, PIN_FC_RX, PIN_FC_TX);

    pinMode(PIN_RTK_PPS, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_RTK_PPS), onPps, RISING);

    pinMode(PIN_SAFE_IN, INPUT_PULLUP);
    safe_closed = (digitalRead(PIN_SAFE_IN) == LOW);
    pinMode(PIN_WDT_FEED, OUTPUT);
    digitalWrite(PIN_WDT_FEED, LOW);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);

    strip.begin();
    strip.setBrightness(255);
    strip.clear();
    strip.show();

    analogSetAttenuation(ADC_11db);         // the 3.1 V input range the divider lands in
    if (nvs_open("ranch", NVS_READWRITE, &nvs) != ESP_OK) nvs = 0;
    float k = 1.0f;
    if (nvs && nvGetF32("batt_k", k) && k > 0.5f && k < 2.0f) batt_div_k = k;
}

uint32_t halMillis() { return millis(); }

void halDelayMs(uint32_t ms) { ::delay(ms); }

void consoleWrite(const char* data, size_t len) {
    Serial.write(reinterpret_cast<const uint8_t*>(data), len);
}

// USB-CDC RX: the same port the banner goes out of, so a technician with a cable can
// type a programme into an aircraft that has no wifi to join. Bounded per call like
// every other reader here -- a monitor left open must not starve the show job.
int consoleRead(char* buf, size_t cap) {
    int n = 0;
    while (n < static_cast<int>(cap) && Serial.available() > 0) {
        buf[n++] = static_cast<char>(Serial.read());
    }
    return n;
}

int rtkAvailable() { return rtk_serial.available(); }

int rtkRead(uint8_t* buf, size_t cap) {
    if (cap == 0) return 0;
    const int n = rtk_serial.read(buf, cap);
    return n < 0 ? 0 : n;
}

void rtkWrite(const uint8_t* buf, size_t len) {
    rtk_serial.write(buf, len);
    rtk_serial.flush();
}

bool rtkPpsEdge() {
    if (!pps_edge) return false;
    // Clear between checking and returning, with the interrupt off for the three
    // instructions: an edge that lands inside the read would otherwise be spent
    // twice, and a fleet that starts on the same second has to mean it.
    noInterrupts();
    pps_edge = false;
    interrupts();
    return true;
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

void ledSetPixel(uint8_t index, uint32_t rgb) {
    if (index >= strip.numPixels()) return;
    strip.setPixelColor(index, rgb);
}

void ledCommit() { strip.show(); }

bool safeSwitchClosed() {
    safe_closed = (digitalRead(PIN_SAFE_IN) == LOW);
    return safe_closed;
}

bool safeSwitchIsClosed() { return safe_closed; }

void setBuzzer(bool on) {
    buzzer_state = on;
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

bool buzzerIsOn() { return buzzer_state; }

// The external watchdog wants edges, not a level: a firmware that hung with this
// pin high would look healthy to anything that only samples it.
void feedWatchdog() {
    wdt_level = !wdt_level;
    digitalWrite(PIN_WDT_FEED, wdt_level ? HIGH : LOW);
}

void setStatusLed(bool on) {
    led_state = on;
    digitalWrite(PIN_LED_STATUS, on ? HIGH : LOW);
}

float batteryVolts() {
    return static_cast<float>(readMillivolts(PIN_BATT_ADC)) / 1000.0f * BATT_DIVIDER * batt_div_k;
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

bool wifiConnect(const char* ap_ssid, const char* pass) {
    if (ap_ssid == nullptr || ap_ssid[0] == '\0') return false;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);          // modem sleep adds seconds to a keepalive, and a
                                   // ground station that misses three of them goes home
    WiFi.begin(ap_ssid, pass && pass[0] ? pass : nullptr);
    last_wifi_try = millis();
    return true;
}

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

int wifiRssi() { return wifiUp() ? WiFi.RSSI() : -256; }

// The Arduino core retries association by itself; this is for the case where it has
// given up (an access point on a pole that was power-cycled between rehearsal and
// show), which needs a fresh begin() rather than waiting for the internal backoff.
void wifiReconnect() {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - last_wifi_try) < 15000) return;
    last_wifi_try = now;
    WiFi.disconnect(false);
    WiFi.begin();
}

const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_EXT:      return "external";      // the watchdog, on this board
        case ESP_RST_SW:       return "software";
        case ESP_RST_PANIC:    return "panic";
        case ESP_RST_INT_WDT:  return "int-wdt";
        case ESP_RST_TASK_WDT: return "task-wdt";
        case ESP_RST_WDT:      return "wdt";
        case ESP_RST_BROWNOUT: return "brownout";       // a sagging pack under two pixels
        default:               return "unknown";
    }
}

}  // namespace ranch

#endif  // !RANCH_SIM
