// [I] Master insert effects (OpenTracker only): one effect on the master bus,
// after the master sum and before the limiter (AudioEngine::renderBlock),
// played from the Performance screen (perf::kDjFilter).
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

class MasterFx {
public:
    // Main loop: the DJ filter value, -100..100 (0 = off).
    void setDjFilter(int v) { target_ = v < -100 ? -100 : (v > 100 ? 100 : v); }
    int djFilter() const { return target_; }
    // Audio context: true while it changes the signal (CPU in use).
    bool active() const { return on_; }
    // Audio context, in place on the master bus (n frames per channel).
    void process(float* l, float* r, uint32_t n);
    void reset();

private:
    volatile int target_ = 0;
    bool on_ = false;
    float pos_ = 0.0f;   // smoothed value, -1..1
    struct Stage {
        float g = 0.0f;   // coefficient at the end of the last block
        float ic1L = 0.0f, ic2L = 0.0f, ic1R = 0.0f, ic2R = 0.0f;
    };
    Stage lp_, hp_;
};

}  // namespace audio
}  // namespace ot
