// [I] Master insert effects (OpenTracker only) on the master bus, after the
// master sum and before the limiter (AudioEngine::renderBlock), played from
// the Performance screen (perf::kDjFilter, perf::kTapeStop).
//
// DJ filter: one value -100..100; < 0 a low-pass sweeping down from fully
// open (20 kHz .. 100 Hz), > 0 a high-pass sweeping up (20 Hz .. 8 kHz),
// 0 off. A low-pass and a high-pass stage in series, stereo TPT
// state-variable filters (no clamping: the bus may exceed full scale until
// the limiter), mild fixed resonance; the stage not in use sits fully open,
// so crossing 0 never switches a filter's mode (no click). The value is
// smoothed per block (~30 ms) so pads and the scene fader do not click; the
// effect is bypassed (no CPU) once it has eased fully open.
#pragma once

#include <cstdint>

namespace ot {
namespace audio {

// Tape stop: on engage the mix is written to a borrowed buffer (int16 at
// half scale, planar L / R) and read back at a speed falling linearly from
// 1x to 0 over stopFrames, fading to silence just before it stops (no DC).
// Stopped, nothing is written or read, so holding costs no memory. On
// release the speed rises to 1x over upFrames, then a short crossfade
// returns to the live mix and the buffer is free again (busy() false).
// Buffer: about 5/8 of stopFrames plus a margin (kTapeMargin) per channel.
class TapeStop {
public:
    static constexpr uint32_t kTapeMargin = 2048;
    static uint32_t framesFor(uint32_t stopFrames) { return stopFrames / 8 * 5 + kTapeMargin; }
    // Main loop. engage: start braking (area = 2 * frames int16, used only
    // when idle; while busy the current buffer is kept). release: spin up.
    void engage(int16_t* area, uint32_t frames, uint32_t stopFrames);
    void release(uint32_t upFrames);
    void abort();   // the buffer is gone (bank cleared): off at once
    // True from engage until the return to the live mix completed.
    bool busy() const { return state_ != kIdle || cmd_ != kCmdNone; }
    bool stopped() const { return state_ == kStopped; }
    // Audio context, in place.
    void process(float* l, float* r, uint32_t n);

private:
    enum State : uint8_t { kIdle, kBrake, kStopped, kSpinUp, kReturn };
    enum Cmd : uint8_t { kCmdNone, kCmdEngage, kCmdRelease, kCmdAbort };
    volatile uint8_t state_ = kIdle;
    volatile uint8_t cmd_ = kCmdNone;
    // Command parameters (written before cmd_).
    int16_t* newArea_ = nullptr;
    uint32_t newFrames_ = 0, newStop_ = 0, newUp_ = 0;
    // Audio-side state.
    int16_t* area_ = nullptr;
    uint32_t frames_ = 0;    // ring length per channel
    uint32_t w_ = 0;         // next write index
    uint32_t lagInt_ = 0;    // read = w - lag (frames behind the last write)
    float lagFrac_ = 0.0f;
    float rate_ = 1.0f, dRate_ = 0.0f;
    uint32_t xfade_ = 0;     // kReturn: frames into the crossfade
};

class MasterFx {
public:
    // Main loop: the DJ filter value, -100..100 (0 = off).
    void setDjFilter(int v) { target_ = v < -100 ? -100 : (v > 100 ? 100 : v); }
    int djFilter() const { return target_; }
    // Audio context: true while it changes the signal (CPU in use).
    bool active() const { return on_; }
    TapeStop& tape() { return tape_; }
    const TapeStop& tape() const { return tape_; }
    // Audio context, in place on the master bus (n frames per channel).
    void process(float* l, float* r, uint32_t n);
    void reset();

private:
    void processFilter(float* l, float* r, uint32_t n);
    volatile int target_ = 0;
    bool on_ = false;
    float pos_ = 0.0f;   // smoothed value, -1..1
    struct Stage {
        float g = 0.0f;   // coefficient at the end of the last block
        float ic1L = 0.0f, ic2L = 0.0f, ic1R = 0.0f, ic2R = 0.0f;
    };
    Stage lp_, hp_;
    TapeStop tape_;   // after the DJ filter
};

}  // namespace audio
}  // namespace ot
