#include "app/resample.h"

namespace ot {

namespace {
inline int16_t toS16(float x) {
    const float v = x * 32767.0f;
    return int16_t(v >= 32767.0f ? 32767 : (v <= -32768.0f ? -32768 : int32_t(v)));
}
}  // namespace

void Resampler::arm(int16_t* area, uint32_t areaSamples, uint8_t bars) {
    area_ = area;
    half_ = areaSamples / 2;
    bars_ = bars;
    frames_ = 0;
    barsDone_ = 0;
    end_ = kEndNone;
    state_ = kArmed;
}

void Resampler::requestStop() {
    if (state_ == kRecording) state_ = kStopping;
}

void Resampler::reset() {
    // area_ stays valid: on the host the audio thread may still be inside
    // tapBlock(); it sees kIdle on its next block.
    state_ = kIdle;
    end_ = kEndNone;
    frames_ = 0;
    barsDone_ = 0;
}

void Resampler::onBlock(bool playing, bool barStart) {
    const uint8_t s = state_;
    if (s == kRecording || s == kStopping) {
        if (!playing) {
            finish(kEndTransport);
            return;
        }
        if (barStart) {
            // This block belongs to the next bar: not recorded.
            barsDone_ = barsDone_ + 1;
            if (s == kStopping) {
                finish(kEndStopped);
                return;
            }
            if (bars_ && barsDone_ >= bars_) {
                finish(kEndBars);
                return;
            }
        }
        return;
    }
    if (s == kArmed && playing && barStart) {
        frames_ = 0;
        barsDone_ = 0;
        state_ = kRecording;
    }
}

void Resampler::tapBlock(const float* l, const float* r, uint32_t n) {
    const uint8_t s = state_;
    if (s != kRecording && s != kStopping) return;
    uint32_t f = frames_;
    const uint32_t room = half_ - f;
    const uint32_t m = n < room ? n : room;
    int16_t* L = area_ + f;
    int16_t* R = area_ + half_ + f;
    for (uint32_t i = 0; i < m; ++i) {
        L[i] = toS16(l[i]);
        R[i] = toS16(r[i]);
    }
    frames_ = f + m;
    if (m < n || frames_ >= half_) finish(kEndFull);
}

}  // namespace ot
