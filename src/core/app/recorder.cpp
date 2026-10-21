// App side of the Sample Recorder (spec 05 §2.11, appendix C §2; spec 02
// §8.3 recordings): input routing, output folder and file naming, WAV write.
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "fmath.h"
#include "formats/wav.h"

namespace ot {

bool App::selectInput(uint8_t source, uint8_t gain) {
    if (source > 3) return false;
    if (source == 1 && !hal_.radio) return false;   // radio: needs a tuner driver
    capture.setChannelMode(cfg.lineInChannel);
    if (hal_.selectInput) return hal_.selectInput(hal_.inputCtx, source, gain);
    return source == 0;   // without a routing hook only the line input exists
}

namespace {
bool countEntry(const IStorage::DirEntry&, void*) { return true; }
}  // namespace

bool App::recordingsFolder(char* out, int cap) {
    IStorage& f = fs();
    // "Recordings", then the highest existing "Recordings N" (N >= 2).
    int n = 1;
    for (int k = 2; k < 1000; ++k) {
        char p[32];
        std::snprintf(p, sizeof(p), "Recordings %d", k);
        if (!f.exists(p)) break;
        n = k;
    }
    auto name = [&](int k) {
        if (k <= 1) std::snprintf(out, size_t(cap), "Recordings");
        else std::snprintf(out, size_t(cap), "Recordings %d", k);
    };
    name(n);
    if (f.exists(out)) {
        // More than 98 entries: start the next folder (spec 02 §8.3).
        const int32_t entries = f.listDir(out, &countEntry, nullptr);
        if (entries > 98) name(n + 1);
    }
    return f.mkdir(out);
}

bool App::recordingName(const char* folder, char* out, int cap) {
    for (int k = 1; k <= 999; ++k) {
        char p[96];
        std::snprintf(p, sizeof(p), "%s/recording%03d.wav", folder, k);
        if (!fs().exists(p)) {
            std::snprintf(out, size_t(cap), "recording%03d", k);
            return true;
        }
    }
    return false;
}

int App::firstFreeSampleSlot() const {
    for (int i = 0; i < kSampleInstrumentCount; ++i)
        if (!instrPresent[i]) return i;
    return -1;
}

bool App::writeWav(const char* path, const int16_t* mono, uint32_t frames) {
    IStorage& f = fs();
    if (f.exists(path)) f.remove(path);
    WavWriter w;
    if (!w.open(f, path, 1)) return false;
    bool ok = true;
    for (uint32_t i = 0; i < frames && ok; i += 4096) {
        const uint32_t n = frames - i < 4096 ? frames - i : 4096;
        ok = w.write(mono + i, n);
    }
    return w.close() && ok;
}

// ---------------------------------------------------------------------------
// [I] Live resampling (app/resample.h)

int App::nextResampleSlot() const {
    int top = bank.lastLoaded();
    for (int i = kSampleInstrumentCount - 1; i > top; --i)
        if (instrPresent[i]) {
            top = i;
            break;
        }
    return top + 1 < kSampleInstrumentCount ? top + 1 : -1;
}

App::ResampleAction App::resampleToggle() {
    switch (resampler.state()) {
        case Resampler::kIdle: {
            if (sampleBankLent || rendering_ || (jobBusy() && jobOp() != JobOp::Autosave)) return ResampleAction::kBusy;
            if (nextResampleSlot() < 0) return ResampleAction::kNoSlot;
            // All free sample memory is held for the recording (two planar
            // halves); loads meanwhile see none and cannot touch it.
            const size_t free = bank.freeSamples() & ~size_t(1);
            if (free < 2u * 44100u || free > 0xFFFFFFFEu || !bank.setReserved(free)) return ResampleAction::kNoMemory;
            resampler.arm(bank.reservedArea(), uint32_t(free), kResampleBars[resampleCfg.length % ResampleSettings::kLengths]);
            engine.setResampleTap(&resampler, resampleCfg.source);
            return ResampleAction::kArmed;
        }
        case Resampler::kArmed:
            resampleCancel();
            return ResampleAction::kCancelled;
        case Resampler::kRecording:
            resampler.requestStop();
            return ResampleAction::kStopping;
        default:
            return ResampleAction::kBusy;
    }
}

void App::resampleCancel() {
    engine.setResampleTap(nullptr, 0);
    if (resampler.state() >= Resampler::kRecording) reportResample(ResampleReport::kCancelled, -1);
    resampler.reset();
    bank.setReserved(0);
}

void App::serviceResample() {
    if (!resampler.busy()) return;
    // The bank was cleared (project load / New project) or lent (Games):
    // the reserved memory is gone.
    if (sampleBankLent || bank.reserved() == 0) {
        engine.setResampleTap(nullptr, 0);
        if (resampler.state() >= Resampler::kRecording) reportResample(ResampleReport::kCancelled, -1);
        resampler.reset();
        bank.setReserved(0);
        ui.notify("Resample cancelled");
        return;
    }
    if (resampler.state() == Resampler::kDone) finishResample();
}

// Snapshot of the take for the diag log. Call after the tap is removed (the
// audio side writes no more stats) and before resampler.reset().
void App::reportResample(uint8_t outcome, int slot) {
    ResampleReport& r = lastResample;
    ++r.seq;
    r.source = resampleCfg.source;
    r.end = resampler.end();
    r.outcome = outcome;
    r.slot = int8_t(slot);
    r.frames = resampler.frames();
    r.stats = resampler.stats();
}

void App::finishResample() {
    engine.setResampleTap(nullptr, 0);
    const uint32_t n = resampler.frames();
    const Resampler::End end = resampler.end();
    const int slot = nextResampleSlot();
    auto release = [this] {
        resampler.reset();
        bank.setReserved(0);
    };
    if (n < 441) {   // under 10 ms: nothing worth keeping
        reportResample(ResampleReport::kEmpty, -1);
        release();
        ui.notify("Resample: nothing recorded");
        return;
    }
    if (slot < 0) {
        reportResample(ResampleReport::kNoSlot, -1);
        release();
        ui.notify("Resample: no free instrument slot after the last one", 4000);
        return;
    }
    // Planar stereo at the tail (left block, right block). Moving down never
    // overlaps the right half's source (the area starts at or after tail()).
    int16_t* dst = bank.tail();
    std::memmove(dst, resampler.left(), size_t(n) * sizeof(int16_t));
    std::memmove(dst + n, resampler.right(), size_t(n) * sizeof(int16_t));
    reportResample(ResampleReport::kOk, slot);
    release();
    // Above every loaded instrument: commitTail() moves no playing sample.
    if (!bank.commitTail(slot, n, true)) {
        lastResample.outcome = ResampleReport::kFailed;
        ui.notify("Resample failed");
        return;
    }
    Instrument ins;
    ins.setDefault();
    const uint8_t src = resampleCfg.source;
    if (src == 0) std::snprintf(ins.name, sizeof(ins.name), "Resample Master");
    else if (src == audio::AudioEngine::kTapLineIn) std::snprintf(ins.name, sizeof(ins.name), "Resample Line in");
    else std::snprintf(ins.name, sizeof(ins.name), "Resample Track %d", src);
    ins.lengthFrames = n;
    ins.stereo = true;
    instruments[slot] = ins;
    instrPresent[slot] = true;
    markInstrumentDirty(slot, true);
    setCurrentInstrument(slot);
    // Whole bars when it ended on a bar: placement repeats every that many.
    const uint32_t bars = end == Resampler::kEndBars ? resampler.bars()
                        : end == Resampler::kEndStopped ? resampler.barsDone() : 0;
    resampler.reset();
    char msg[64];
    if (bars) std::snprintf(msg, sizeof(msg), "Resampled %lu bar%s -> instrument %d", static_cast<unsigned long>(bars),
                            bars == 1 ? "" : "s", slot + 1);
    else std::snprintf(msg, sizeof(msg), "Resampled %.1f s -> instrument %d%s", double(n) / 44100.0, slot + 1,
                       end == Resampler::kEndFull ? " (memory full)" : "");
    ui.notify(msg, 3000);
    placeResample(slot, bars);
    ui.dirty();
}

void App::placeResample(int slot, uint32_t bars) {
    const int t = int(resampleCfg.place) - 1;
    if (t < 0 || t >= Sequencer::kVoiceTracks) return;
    Pattern& p = pattern();
    const int len = int(p.tracks[0].lastStep) + 1;
    const int every = int(bars) * resampleBarSteps();
    pushUndo();
    // Note 60 plays the sample at its own pitch (audio_tables kUnityNote).
    for (int s = 0; s < len; s += every) {
        Step& st = p.tracks[t].steps[s];
        st.note = 60;
        st.instrument = uint8_t(slot);
        if (every <= 0) break;
    }
    patternEdited();
    if (every > len) ui.notify("Resample is longer than the pattern: placed once", 3000);
}

bool App::writeWav(const char* path, const int16_t* l, const int16_t* r, uint32_t frames) {
    if (!r) return writeWav(path, l, frames);
    IStorage& f = fs();
    if (f.exists(path)) f.remove(path);
    WavWriter w;
    if (!w.open(f, path, 2)) return false;
    // 2.x stereo recording: the planar lanes interleaved (L, R) through a
    // small transient chunk (2 KB of stack, no sample-memory use).
    constexpr uint32_t kChunk = 512;
    int16_t buf[2 * kChunk];
    bool ok = true;
    for (uint32_t i = 0; i < frames && ok; i += kChunk) {
        const uint32_t n = frames - i < kChunk ? frames - i : kChunk;
        for (uint32_t k = 0; k < n; ++k) {
            buf[2 * k] = l[i + k];
            buf[2 * k + 1] = r[i + k];
        }
        ok = w.write(buf, n);
    }
    return w.close() && ok;
}

uint32_t App::previewStep(int note) {
    // 2^((note - 60) / 12) as 16.16: 2048 (note 0) .. ~3.1e6 (note 127).
    const int n = note < 0 ? 0 : (note > 127 ? 127 : note);
    return uint32_t(fm::kNoteRatio[n] * 65536.0 + 0.5);
}

}  // namespace ot
