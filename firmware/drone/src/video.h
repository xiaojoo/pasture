// Video downlink (图传): camera capture task feeding an MJPEG stream plus a
// snapshot sink for the mission's photo triggers.
//
// Design notes that matter on a 2.4 GHz link shared with telemetry:
//  - the camera task owns the frame buffer and never blocks on a client; a
//    client that cannot drain the socket loses frames and is counted
//  - quality and frame size are stepped down when the encoder cannot keep the
//    target rate, stepped back up only after a stable window
//  - the payload trigger takes a frame from the same buffer, so a capture never
//    stalls the stream
#pragma once

#include <cstddef>
#include <cstdint>

namespace ranch {

struct VideoStats {
    uint32_t frames;
    uint32_t dropped;        // no consumer, or stale before send
    uint32_t bytes;
    uint32_t client_rejects; // back-pressured clients cut loose
    uint16_t fps;
    uint16_t quality;        // esp32-cam quality actually in use
    uint16_t width;
    uint16_t height;
    bool     streaming;
    bool     recording;
    int8_t   error;          // last camera driver status, <0 is a fault
};

struct Snapshot {
    const uint8_t* data;
    size_t len;
    uint32_t seq;
    uint32_t taken_ms;
};

bool videoInit();
void videoTaskLoop();                 // runs on its own FreeRTOS task
bool videoLatest(Snapshot& out);      // borrows the buffer; copy before returning
void videoRelease();                  // give the frame back to the encoder
bool videoSaveSnapshot(const char* path);

void videoSetStreaming(bool on);
void videoSetRecording(bool on);
void videoStats(VideoStats& out);

}  // namespace ranch
