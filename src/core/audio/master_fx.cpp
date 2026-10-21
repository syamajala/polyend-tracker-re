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

// ---------------------------------------------------------------------------
// Tape stop

void TapeStop::engage(int16_t* area, uint32_t frames, uint32_t stopFrames) {
    newArea_ = area;
    newFrames_ = frames;
    newStop_ = stopFrames ? stopFrames : 1;
    cmd_ = kCmdEngage;
}

void TapeStop::release(uint32_t upFrames) {
    newUp_ = upFrames ? upFrames : 1;
    cmd_ = kCmdRelease;
}

void TapeStop::abort() { cmd_ = kCmdAbort; }

namespace {
constexpr float kTapeScale = 16383.0f;   // half scale: +-2.0 fits int16
constexpr float kFadeRate = 0.03f;       // fade to silence below this speed
constexpr uint32_t kXfade = 882;         // 20 ms back to the live mix
inline int16_t toTape(float x) {
    float v = x * kTapeScale;
    v = v > 32767.0f ? 32767.0f : (v < -32767.0f ? -32767.0f : v);
    return int16_t(v);
}
}  // namespace

OT_FASTRUN_FN void TapeStop::process(float* l, float* r, uint32_t n) {
    // Commands from the main loop (one per block is enough).
    const uint8_t cmd = cmd_;
    if (cmd != kCmdNone) {
        if (cmd == kCmdAbort) {
            state_ = kIdle;
        } else if (cmd == kCmdEngage) {
            if (state_ == kIdle) {
                area_ = newArea_;
                frames_ = newFrames_;
                w_ = 0;
                lagInt_ = 0;
                lagFrac_ = 0.0f;
                rate_ = 1.0f;
            }
            if (area_ && frames_ > 4) {
                dRate_ = -1.0f / float(newStop_);
                if (state_ != kStopped) state_ = kBrake;
            }
        } else if (cmd == kCmdRelease && state_ != kIdle) {
            dRate_ = 1.0f / float(newUp_);
            if (state_ == kBrake || state_ == kStopped) state_ = kSpinUp;
        }
        cmd_ = kCmdNone;
    }
    if (state_ == kIdle) return;
    if (state_ == kStopped) {
        for (uint32_t i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
        return;
    }
    int16_t* bl = area_;
    int16_t* br = area_ + frames_;
    const uint32_t N = frames_;
    for (uint32_t i = 0; i < n; ++i) {
        // Write the live sample, then read behind it.
        bl[w_] = toTape(l[i]);
        br[w_] = toTape(r[i]);
        const float liveL = l[i], liveR = r[i];
        if (state_ == kBrake || state_ == kSpinUp) {
            rate_ += dRate_;
            if (rate_ <= 0.0f) {
                rate_ = 0.0f;
                state_ = kStopped;
                for (uint32_t k = i; k < n; ++k) l[k] = r[k] = 0.0f;
                w_ = w_ + 1 < N ? w_ + 1 : 0;
                return;
            }
            if (rate_ >= 1.0f) {
                rate_ = 1.0f;
                state_ = kReturn;
                xfade_ = 0;
            }
            lagFrac_ += 1.0f - rate_;
            if (lagFrac_ >= 1.0f) {
                lagFrac_ -= 1.0f;
                if (lagInt_ + 2 < N) ++lagInt_;
            }
        }
        // read = w - lagInt - lagFrac, between idx (older) and idx + 1.
        const uint32_t i1 = w_ >= lagInt_ ? w_ - lagInt_ : w_ + N - lagInt_;
        const uint32_t i0 = i1 ? i1 - 1 : N - 1;
        const float t = 1.0f - lagFrac_;
        float tl = (float(bl[i0]) + (float(bl[i1]) - float(bl[i0])) * t) / kTapeScale;
        float tr = (float(br[i0]) + (float(br[i1]) - float(br[i0])) * t) / kTapeScale;
        if (rate_ < kFadeRate) {
            const float g = rate_ / kFadeRate;
            tl *= g;
            tr *= g;
        }
        if (state_ == kReturn) {
            const float a = float(xfade_) / float(kXfade);
            tl += (liveL - tl) * a;
            tr += (liveR - tr) * a;
            if (++xfade_ >= kXfade) {
                state_ = kIdle;   // live again (the rest of the block is): the buffer is free
                l[i] = liveL;
                r[i] = liveR;
                return;
            }
        }
        l[i] = tl;
        r[i] = tr;
        w_ = w_ + 1 < N ? w_ + 1 : 0;
    }
}

// ---------------------------------------------------------------------------
// DJ filter + chain

OT_FASTRUN_FN void MasterFx::process(float* l, float* r, uint32_t n) {
    processFilter(l, r, n);
    tape_.process(l, r, n);
}

OT_FASTRUN_FN void MasterFx::processFilter(float* l, float* r, uint32_t n) {
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
