// [I] Live resampling (app/resample.h): arming, bar sync, fixed / manual
// length, sources, the new instrument, placement on a track, the reserved
// sample memory and cancelling.
#include <cmath>
#include <vector>

#include "test.h"
#include "ui_fakes.h"

#include "formats/wav.h"
#include "ui/screens.h"

using namespace ot;

namespace {

void addSaw(Harness& h, const char* path, uint32_t frames) {
    std::vector<int16_t> s(frames);
    for (uint32_t i = 0; i < frames; ++i) s[i] = int16_t((i % 100) * 300 - 15000);
    std::vector<uint8_t> out(frames * 2 + 64);
    out.resize(wavEncode(s.data(), frames, 1, out.data(), out.size()));
    h.fs.files[path] = out;
}

// A sample on track 1 every step; tempo 120 (one 16-step bar = 2 s).
void setup(Harness& h) {
    App& a = *h.app;
    addSaw(h, "saw.wav", 3000);
    CHECK(a.importSample("saw.wav", 0));
    for (int s = 0; s < 64; ++s) {
        Step& st = a.pattern().tracks[0].steps[s];
        st.note = 60;
        st.instrument = 0;
    }
    a.pattern().tracks[0].lastStep = 63;
    a.patternEdited();
    a.project.setTempo(120.0f);
}

constexpr double kBlockS = 128.0 / 44122.6484;
int blocksFor(double seconds) { return int(seconds / kBlockS + 0.5); }
constexpr uint32_t kBarFrames = 88245;   // 2 s at the live rate 44122.65 Hz

bool near(uint32_t a, uint32_t b, uint32_t tol) { return a > b ? a - b <= tol : b - a <= tol; }

int peakOf(App& a, int slot) {
    const int16_t* l = a.bank.left(slot);
    int pk = 0;
    for (uint32_t i = 0; l && i < a.bank.frames(slot); ++i) pk = std::abs(l[i]) > pk ? std::abs(l[i]) : pk;
    return pk;
}

}  // namespace

TEST(resample_fixed_bars_master) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.resampleCfg.length = 1;   // 1 bar
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    CHECK(a.resampler.state() == Resampler::kArmed);
    // All free memory is held back while it runs.
    CHECK(a.bank.reserved() > 0);
    CHECK_EQ(a.bank.freeSamples(), 0u);
    h.audio.pump(50);   // stopped: stays armed
    CHECK(a.resampler.state() == Resampler::kArmed);
    a.seq.play(0);
    h.audio.pump(10);
    CHECK(a.resampler.state() == Resampler::kRecording);   // play start = bar 1
    h.audio.pump(blocksFor(2.2));
    CHECK(a.resampler.state() == Resampler::kDone);
    h.run(5);
    CHECK(!a.resampler.busy());
    CHECK_EQ(a.bank.reserved(), 0u);
    CHECK(a.instrPresent[1]);
    CHECK(a.bank.stereo(1));
    CHECK(near(a.bank.frames(1), kBarFrames, 128));
    CHECK(peakOf(a, 1) > 1000);
    CHECK_STR(a.instruments[1].name, "Resample Master");
    CHECK_EQ(a.currentInstrument(), 1);
    // The sequencer kept playing throughout.
    CHECK(a.seq.isPlaying());
}

TEST(resample_starts_on_the_next_bar_and_manual_stop_snaps) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.resampleCfg.length = 0;   // Manual
    a.seq.play(0);
    h.audio.pump(blocksFor(0.5));   // a quarter into bar 1
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    h.audio.pump(blocksFor(1.0));
    CHECK(a.resampler.state() == Resampler::kArmed);   // waits for bar 2
    h.audio.pump(blocksFor(0.6));
    CHECK(a.resampler.state() == Resampler::kRecording);
    h.audio.pump(blocksFor(2.5));   // into the middle of the second recorded bar
    CHECK(a.resampleToggle() == App::ResampleAction::kStopping);
    CHECK(a.resampler.state() == Resampler::kStopping);
    h.audio.pump(blocksFor(1.2));   // 5.8 s: the next bar (6.0 s) has not begun
    CHECK(a.resampler.state() == Resampler::kStopping);
    h.audio.pump(blocksFor(0.4));
    CHECK(a.resampler.state() == Resampler::kDone);
    CHECK_EQ(a.resampler.barsDone(), 2u);
    h.run(5);
    CHECK(near(a.bank.frames(1), 2 * kBarFrames, 256));
}

TEST(resample_places_on_track_with_undo) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.resampleCfg.length = 1;   // 1 bar = 16 steps
    a.resampleCfg.place = 3;    // track 3
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    a.seq.play(0);
    h.audio.pump(blocksFor(2.3));
    h.run(5);
    const Track& t = a.pattern().tracks[2];
    for (int s = 0; s < 64; ++s) {
        const bool on = s % 16 == 0;
        CHECK_EQ(t.steps[s].note, on ? 60 : kNoteEmpty);
        if (on) CHECK_EQ(t.steps[s].instrument, 1);
    }
    a.seq.stop();
    a.undoEdit();
    CHECK_EQ(a.pattern().tracks[2].steps[0].note, kNoteEmpty);
    // Longer than the pattern: placed once.
    a.pattern().tracks[0].lastStep = 7;
    a.patternEdited();
    a.resampleCfg.length = 1;
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    a.seq.play(0);
    h.audio.pump(blocksFor(2.3));
    h.run(5);
    CHECK_EQ(a.pattern().tracks[2].steps[0].note, 60);
    CHECK_EQ(a.pattern().tracks[2].steps[0].instrument, 2);
}

TEST(resample_track_and_line_sources) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.resampleCfg.length = 1;
    a.resampleCfg.source = 2;   // track 2: silent
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    a.seq.play(0);
    h.audio.pump(blocksFor(2.3));
    h.run(5);
    CHECK(a.instrPresent[1]);
    CHECK_EQ(peakOf(a, 1), 0);
    CHECK_STR(a.instruments[1].name, "Resample Track 2");
    a.resampleCfg.source = 1;   // track 1: the saw
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    h.audio.pump(blocksFor(4.3));
    h.run(5);
    CHECK(a.instrPresent[2]);
    CHECK(peakOf(a, 2) > 1000);
    a.resampleCfg.source = audio::AudioEngine::kTapLineIn;   // no line input here: silence
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    h.audio.pump(blocksFor(4.3));
    h.run(5);
    CHECK(a.instrPresent[3]);
    CHECK_EQ(peakOf(a, 3), 0);
    CHECK_STR(a.instruments[3].name, "Resample Line in");
}

// A synth on a track (2.x: Synth 1..3 play on shared slots, not the
// track's voice): a Track take records the synth the track plays, dry; a
// take of another track does not pick it up.
TEST(resample_track_source_synth) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.setSynthModel(0, int(SynthModel::ACD));
    for (int s = 0; s < 64; s += 4) {
        Step& st = a.pattern().tracks[2].steps[s];
        st.note = 48;
        st.instrument = uint8_t(kFirstSynthInstrument);   // Synth 1
    }
    a.pattern().tracks[2].lastStep = 63;
    a.patternEdited();
    a.resampleCfg.length = 1;
    a.resampleCfg.source = 3;   // track 3: the synth
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    a.seq.play(0);
    h.audio.pump(blocksFor(2.3));
    h.run(5);
    CHECK(a.instrPresent[1]);
    CHECK(peakOf(a, 1) > 1000);
    CHECK_STR(a.instruments[1].name, "Resample Track 3");
    a.resampleCfg.source = 2;   // track 2: silent, Synth 1 still sounding on track 3
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    h.audio.pump(blocksFor(4.3));
    h.run(5);
    CHECK(a.instrPresent[2]);
    CHECK_EQ(peakOf(a, 2), 0);
}

TEST(resample_slot_memory_cancel_and_transport) {
    Harness h;
    App& a = *h.app;
    setup(h);
    // The slot goes above the highest loaded instrument (nothing moves).
    addSaw(h, "b.wav", 500);
    CHECK(a.importSample("b.wav", 10));
    CHECK_EQ(a.nextResampleSlot(), 11);
    // Cancel while armed: memory released.
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    CHECK(a.resampleToggle() == App::ResampleAction::kCancelled);
    CHECK(!a.resampler.busy());
    CHECK_EQ(a.bank.reserved(), 0u);
    CHECK(a.bank.freeSamples() > 0u);
    // Loads during a resample cannot use the held memory.
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    CHECK(!a.importSample("b.wav", 20));
    // Stopping the transport ends it with what was recorded.
    a.seq.play(0);
    h.audio.pump(blocksFor(0.7));
    a.seq.stop();
    h.audio.pump(2);
    CHECK(a.resampler.state() == Resampler::kDone);
    CHECK(a.resampler.end() == Resampler::kEndTransport);
    h.run(5);
    CHECK(a.instrPresent[11]);
    CHECK(near(a.bank.frames(11), uint32_t(0.7 * 44122.65), 512));
    // Last slot taken: refused.
    CHECK(a.importSample("b.wav", 47));
    CHECK(a.resampleToggle() == App::ResampleAction::kNoSlot);
    CHECK(!a.resampler.busy());
    // A new project while armed: cancelled, memory back.
    CHECK(a.deleteInstrument(47));
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    CHECK(a.newProject());
    h.run(5);
    CHECK(!a.resampler.busy());
    CHECK_EQ(a.bank.reserved(), 0u);
}

TEST(resample_key_from_any_screen) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.resampleCfg.length = 1;
    h.down(btn::Shift);
    h.tap(btn::SampleRecorder);
    h.up(btn::Shift);
    CHECK(a.resampler.state() == Resampler::kArmed);
    CHECK(a.ui.active() != kModRecorder);   // no screen switch
    h.run(300);
    CHECK(h.disp.lastHasText("Resample armed: starts with Play"));
    h.down(btn::Shift);
    h.tap(btn::SampleRecorder);
    h.up(btn::Shift);
    CHECK(!a.resampler.busy());
    // Without Shift the key still opens the Sample Recorder.
    h.tap(btn::SampleRecorder);
    CHECK(a.ui.active() == kModRecorder);
}

TEST(resample_recorder_source) {
    Harness h;
    App& a = *h.app;
    setup(h);
    a.seq.play(0);
    // Entering while playing: "Resample" keeps the pattern running.
    h.tap(btn::SampleRecorder);
    CHECK(a.ui.askOpen());
    h.tap(btn::F6);
    CHECK(a.ui.active() == kModRecorder);
    CHECK(a.seq.isPlaying());
    RecorderScreen& r = *static_cast<RecorderScreen*>(a.ui.screen(kModRecorder));
    CHECK(r.resample);
    // Settings: F3 Length (tap again: next), F4 Place on, encoder.
    a.resampleCfg = ResampleSettings{};
    h.tap(btn::F3);
    h.tap(btn::F3);
    CHECK_EQ(a.resampleCfg.length, 4);   // 4 -> 8 bars
    h.enc(-3);
    CHECK_EQ(a.resampleCfg.length, 1);   // 1 bar
    h.tap(btn::F4);
    h.enc(2);
    CHECK_EQ(a.resampleCfg.place, 2);
    h.tap(btn::F2);
    h.enc(9);
    CHECK_EQ(a.resampleCfg.source, 9);   // Line in
    h.enc(-9);
    // Left / Right step through the four setting columns. They are lists
    // drawn top to bottom: Up = the item above, Down = the item below.
    h.tap(btn::Right);
    h.tap(btn::Down);
    CHECK_EQ(a.resampleCfg.length, 2);   // 2 bars
    h.tap(btn::Right);
    h.tap(btn::Right);   // stays on Place on
    h.tap(btn::Up);
    CHECK_EQ(a.resampleCfg.place, 1);
    h.tap(btn::Left);
    h.tap(btn::Left);
    h.tap(btn::Down);
    CHECK_EQ(a.resampleCfg.source, 1);   // Track 1
    h.tap(btn::Up);
    CHECK_EQ(a.resampleCfg.source, 0);   // Master
    h.tap(btn::Left);
    h.tap(btn::Left);   // stays on Source
    h.tap(btn::Down);   // Resample is the last entry
    CHECK(r.resample);
    h.tap(btn::Right);
    h.tap(btn::Right);
    h.tap(btn::Up);
    CHECK_EQ(a.resampleCfg.length, 1);
    h.tap(btn::Right);
    h.tap(btn::Down);
    CHECK_EQ(a.resampleCfg.place, 2);
    // Source column: Up = the input above (Mic HG), Down = back to Resample.
    h.tap(btn::Left);
    h.tap(btn::Left);
    h.tap(btn::Left);
    h.tap(btn::Up);
    CHECK(!r.resample);
    CHECK_EQ(a.cfg.recSource, 3);
    h.tap(btn::Up);
    CHECK_EQ(a.cfg.recSource, 2);
    h.tap(btn::Down);
    h.tap(btn::Down);
    CHECK(r.resample);
    // Record arms, records the next bar, the result lands in slot 2.
    h.tap(btn::Rec);
    CHECK(a.resampler.state() == Resampler::kArmed);
    h.audio.pump(blocksFor(4.5));
    h.run(30);
    CHECK(a.instrPresent[1]);
    CHECK_EQ(a.pattern().tracks[1].steps[0].instrument, 1);
    CHECK(a.seq.isPlaying());
    // Leaving and coming back while playing: no prompt on the Resample source.
    h.tap(btn::Pattern);
    h.tap(btn::SampleRecorder);
    CHECK(!a.ui.askOpen());
    CHECK(a.ui.active() == kModRecorder);
    CHECK(a.seq.isPlaying());
    // Back to an input source: the stock behaviour (Play stops only).
    h.tap(btn::F1);
    h.tap(btn::F1);
    CHECK(!r.resample);
    h.tap(btn::Play);
    CHECK(!a.seq.isPlaying());
    h.tap(btn::Play);
    CHECK(!a.seq.isPlaying());
}
