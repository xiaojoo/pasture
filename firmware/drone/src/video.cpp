#include "video.h"
#include "board.h"

#if defined(RANCH_SIM)

#include "hal.h"

// --- simulated back-end ----------------------------------------------------
// The browser build has no camera. It still has to exercise the mission's photo
// triggers and the telemetry fields, so the encoder is replaced by a counter.
// The frame rate is measured from the calls that actually happen, not assumed
// from the target, so a starved mission loop shows up as a low fps number here
// exactly as it would on the aircraft.
namespace ranch {
namespace {
VideoStats vid{};
uint32_t g_window_start = 0;
uint32_t g_window_frames = 0;
uint32_t g_seq = 0;
}

bool videoInit() {
    vid.width = RANCH_VIDEO_W;
    vid.height = RANCH_VIDEO_H;
    vid.quality = RANCH_VIDEO_Q;
    vid.streaming = true;
    g_window_start = halMillis();
    return true;
}

void videoTaskLoop() {}

bool videoLatest(Snapshot& out) {
    if (!vid.streaming) return false;
    static uint8_t hdr[4] = {0xFF, 0xD8, 0xFF, 0xD9};
    out.data = hdr;
    out.len = sizeof(hdr);
    out.seq = ++g_seq;
    out.taken_ms = halMillis();
    vid.frames++;
    vid.bytes += out.len;
    g_window_frames++;
    const uint32_t span = out.taken_ms - g_window_start;
    if (span >= 1000) {
        vid.fps = static_cast<uint16_t>(g_window_frames * 1000u / span);
        g_window_start = out.taken_ms;
        g_window_frames = 0;
    }
    return true;
}

void videoRelease() {}

bool videoSaveSnapshot(const char*) { return false; }

void videoSetStreaming(bool on) { vid.streaming = on; }
void videoSetRecording(bool on) { vid.recording = on; }

void videoStats(VideoStats& out) {
    out = vid;
    if (!vid.streaming) out.fps = 0;
}
}  // namespace ranch

#else

#include <cstring>

#include "esp_camera.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal.h"

namespace ranch {
namespace {

VideoStats vid{};
volatile bool g_streaming = true;
volatile bool g_recording = false;
uint32_t g_window_start = 0;
uint32_t g_window_frames = 0;
uint8_t g_bad_windows = 0;
uint8_t g_good_windows = 0;
camera_fb_t* g_last = nullptr;
uint32_t g_last_seq = 0;

bool applyProfile(uint16_t w, uint16_t h, uint8_t q) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    s->set_framesize(s, w <= 320 ? FRAMESIZE_QVGA : FRAMESIZE_VGA);
    s->set_quality(s, q);
    vid.width = w;
    vid.height = h;
    vid.quality = q;
    return true;
}

// Rate control: the encoder is the bottleneck, not the socket. Three bad
// windows degrades quality one step, ten good windows buys one step back. The
// hysteresis is deliberate so the picture does not pump on a marginal link.
void adaptRate(uint32_t now) {
    const uint32_t span = now - g_window_start;
    if (span < 2000) return;
    const uint32_t fps = (g_window_frames * 1000u) / (span ? span : 1);
    vid.fps = static_cast<uint16_t>(fps);
    if (fps + 2 < VIDEO_FPS_TARGET) {
        g_bad_windows++;
        g_good_windows = 0;
        if (g_bad_windows >= 3) {
            g_bad_windows = 0;
            const uint16_t q = vid.quality < 40 ? vid.quality + 4 : 40;
            applyProfile(vid.width, vid.height, q);
        }
    } else {
        g_good_windows++;
        g_bad_windows = 0;
        if (g_good_windows >= 10 && vid.quality > 10) {
            g_good_windows = 0;
            applyProfile(vid.width, vid.height, vid.quality - 2);
        }
    }
    g_window_start = now;
    g_window_frames = 0;
}

void onStream(uint32_t now, size_t frame_bytes) {
    vid.frames++;
    g_window_frames++;
    vid.bytes += static_cast<uint32_t>(frame_bytes);
    adaptRate(now);
}

}  // namespace

bool videoInit() {
    camera_config_t cfg{};
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = CAM_PIN_D0;
    cfg.pin_d1 = CAM_PIN_D1;
    cfg.pin_d2 = CAM_PIN_D2;
    cfg.pin_d3 = CAM_PIN_D3;
    cfg.pin_d4 = CAM_PIN_D4;
    cfg.pin_d5 = CAM_PIN_D5;
    cfg.pin_d6 = CAM_PIN_D6;
    cfg.pin_d7 = CAM_PIN_D7;
    cfg.pin_xclk = CAM_PIN_XCLK;
    cfg.pin_pclk = CAM_PIN_PCLK;
    cfg.pin_pwdn = CAM_PIN_PWDN;
    cfg.pin_reset = CAM_PIN_RESET;
    cfg.pin_sccb_sda = CAM_PIN_SIOD;
    cfg.pin_sccb_scl = CAM_PIN_SIOC;
    cfg.xclk_freq_hz = 20000000;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAMESIZE_VGA;
    cfg.jpeg_quality = RANCH_VIDEO_Q;
    cfg.fb_count = 2;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        // PSRAM-less boards fall back to a single internal buffer at QVGA.
        cfg.fb_location = CAMERA_FB_IN_DRAM;
        cfg.fb_count = 1;
        cfg.frame_size = FRAMESIZE_QVGA;
        err = esp_camera_init(&cfg);
        vid.error = static_cast<int8_t>(err);
    }
    applyProfile(640, 480, RANCH_VIDEO_Q);
    vid.streaming = true;
    g_window_start = halMillis();
    return err == ESP_OK;
}

bool videoLatest(Snapshot& out) {
    if (!g_streaming) return false;
    if (g_last) {
        // A frame older than two periods means the consumer is not draining;
        // drop it rather than sending stale video.
        if (halMillis() - g_last->timestamp.tv_sec * 1000u > 250) {
            vid.dropped++;
            return false;
        }
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        vid.error = -1;
        vid.dropped++;
        return false;
    }
    if (fb->len > RANCH_VIDEO_MAX_FRAME_KB * 1024u) {
        esp_camera_fb_return(fb);
        vid.dropped++;
        vid.error = -2;
        return false;
    }
    g_last = fb;
    g_last_seq++;
    out.data = fb->buf;
    out.len = fb->len;
    out.seq = g_last_seq;
    out.taken_ms = halMillis();
    onStream(out.taken_ms, fb->len);
    return true;
}

void videoRelease() {
    if (g_last) {
        esp_camera_fb_return(g_last);
        g_last = nullptr;
    }
}

void videoSetStreaming(bool on) { g_streaming = on; if (!on) videoRelease(); }
void videoSetRecording(bool on) { g_recording = on; }

bool videoSaveSnapshot(const char* path) {
    if (!g_recording) return false;
    Snapshot s{};
    if (!videoLatest(s)) return false;
    const bool ok = sdOpen(path) && sdAppend(reinterpret_cast<const char*>(s.data), s.len);
    sdClose();
    videoRelease();
    return ok;
}

void videoStats(VideoStats& out) {
    out = vid;
    out.streaming = g_streaming;
    out.recording = g_recording;
}

// The stream task is a plain multipart writer: one client, non-blocking
// semantics through the socket send timeout, and a reject counter so the ground
// app can see it is being starved rather than silently getting a frozen image.
void videoTaskLoop() {
    for (;;) {
        if (!wifiUp()) {
            halDelayMs(500);
            continue;
        }
        static httpd_handle_t srv = nullptr;
        if (!srv) {
            httpd_config_t c = HTTPD_DEFAULT_CONFIG();
            c.server_port = VIDEO_PORT;
            c.task_priority = PRIO_VIDEO;
            c.stack_size = STACK_VIDEO;
            c.max_open_sockets = 2;
            if (httpd_start(&srv, &c) != ESP_OK) {
                vid.client_rejects++;
                halDelayMs(1000);
                continue;
            }
        }
        // esp_http_server runs its own task; this loop only supervises it and
        // keeps the watchdog fed while the camera is idle.
        feedWatchdog();
        halDelayMs(1000);
    }
}

}  // namespace ranch

#endif
