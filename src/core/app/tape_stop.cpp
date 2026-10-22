// [I] Master tape stop: the main-loop side of audio::TapeStop (app.h
// setTapeStop). The buffer is borrowed from free sample memory while the
// tape is off the live mix, through the bank reservation that live
// resampling uses too (one of them at a time).
#include "app/app.h"
#include "audio/audio_tables.h"

namespace ot {

uint32_t App::tapeStopFrames(int v) const {
    // 1 / 2 steps, 1 / 2 beats, 1 / 2 bars at the project tempo.
    if (v < 1 || v > 6) return 0;
    const uint32_t beat = uint32_t(cfg.tsDen + 1), bar = uint32_t(resampleBarSteps());
    const uint32_t steps[6] = {1, 2, beat, 2 * beat, bar, 2 * bar};
    const float t = project.tempo();
    const float stepSecs = 15.0f / (t >= 1.0f ? t : 120.0f);
    return uint32_t(float(steps[v - 1]) * stepSecs * audio::kSampleRate + 0.5f);
}

void App::setTapeStop(int v) {
    if (v < 0 || v > 6) v = 0;
    if (v == tapeVal_) return;
    const int prev = tapeVal_;
    tapeVal_ = v;
    audio::TapeStop& tape = engine.masterTape();
    if (v == 0) {
        if (tapeHolds_) tape.release(tapeStopFrames(prev) / 4);
        return;
    }
    const uint32_t stop = tapeStopFrames(v);
    if (tapeHolds_) {   // braking again while it spins up: same buffer
        tape.engage(nullptr, 0, stop);
        return;
    }
    if (sampleBankLent || bank.reserved() != 0) {
        ui.notify("Tape stop: sample memory is held by a resample take", 2000);
        return;
    }
    const uint32_t frames = audio::TapeStop::framesFor(stop);
    if (bank.freeSamples() < 2u * size_t(frames) || !bank.setReserved(2u * size_t(frames))) {
        ui.notify("Tape stop: not enough free sample memory", 2000);
        return;
    }
    tapeHolds_ = true;
    tape.engage(bank.reservedArea(), frames, stop);
}

void App::serviceTapeStop() {
    if (!tapeHolds_) return;
    audio::TapeStop& tape = engine.masterTape();
    // The bank was cleared (project load / New project) or lent (Games):
    // the buffer is gone.
    if (sampleBankLent || bank.reserved() == 0) {
        tape.abort();
        tapeHolds_ = false;
        tapeVal_ = 0;
        return;
    }
    if (!tape.busy()) {   // back on the live mix
        bank.setReserved(0);
        tapeHolds_ = false;
    }
}

}  // namespace ot
