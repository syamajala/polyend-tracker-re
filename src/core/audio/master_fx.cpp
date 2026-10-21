// [I] Master insert effects (master_fx.h).
#include "audio/master_fx.h"

#include <cmath>

#include "audio/audio_tables.h"
#include "fastrun.h"

namespace ot {
namespace audio {

namespace {
constexpr float kSmooth = 0.1f;        // per block (~2.9 ms): ~30 ms to settle
constexpr float kOpen = 0.002f;        // |pos| below this: fully open, bypassed
constexpr float kDamp = 1.0f / 0.9f;   // k = 1 / Q
constexpr float kPi = 3.14159265f;

// Cutoff for the smoothed position: low-pass 20 kHz .. 100 Hz, high-pass
// 20 Hz .. 8 kHz, both exponential in |pos|.
float djCutoffHz(float pos) {
    return pos < 0.0f ? 20000.0f * std::pow(0.005f, -pos) : 20.0f * std::pow(400.0f, pos);
}

float coef(float hz) {
    const float limit = 0.45f * kSampleRate;
    return std::tan(kPi * (hz < limit ? hz : limit) / kSampleRate);
}

template <bool kHigh>
void run(float* __restrict x, uint32_t n, float g0, float g1, float& ic1, float& ic2) {
    const float dg = (g1 - g0) / float(n);
    float g = g0, s1 = ic1, s2 = ic2;
    for (uint32_t i = 0; i < n; ++i) {
        g += dg;
        const float a1 = 1.0f / (1.0f + g * (g + kDamp));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = x[i] - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        x[i] = kHigh ? x[i] - kDamp * v1 - v2 : v2;
    }
    ic1 = s1;
    ic2 = s2;
}
}  // namespace

void MasterFx::reset() {
    on_ = false;
    pos_ = 0.0f;
    lp_ = Stage{};
    hp_ = Stage{};
}

OT_FASTRUN_FN void MasterFx::process(float* l, float* r, uint32_t n) {
    const float target = float(target_) / 100.0f;
    if (!on_ && target == 0.0f) return;
    pos_ += (target - pos_) * kSmooth;
    if (target == 0.0f && pos_ > -kOpen && pos_ < kOpen) {
        reset();   // eased fully open: off until the next value
        return;
    }
    // Each stage sweeps on its own side of 0 and rests fully open on the
    // other; a stage starting from rest ramps from its open coefficient.
    const float lpG = coef(pos_ < 0.0f ? djCutoffHz(pos_) : 20000.0f);
    const float hpG = coef(pos_ > 0.0f ? djCutoffHz(pos_) : 20.0f);
    if (!on_) {
        // Start as if it had been running open: the low-pass output (its
        // second integrator) already at the signal, the high-pass at rest.
        lp_.g = coef(20000.0f);
        hp_.g = coef(20.0f);
        lp_.ic2L = l[0];
        lp_.ic2R = r[0];
        on_ = true;
    }
    run<false>(l, n, lp_.g, lpG, lp_.ic1L, lp_.ic2L);
    run<false>(r, n, lp_.g, lpG, lp_.ic1R, lp_.ic2R);
    run<true>(l, n, hp_.g, hpG, hp_.ic1L, hp_.ic2L);
    run<true>(r, n, hp_.g, hpG, hp_.ic1R, hp_.ic2R);
    lp_.g = lpG;
    hp_.g = hpG;
}

}  // namespace audio
}  // namespace ot
