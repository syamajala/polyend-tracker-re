// [I] Master insert effects (audio/master_fx.h): the DJ filter's response,
// bypass at 0, smoothing, and the Performance FX type that drives it.
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"
#include "app/project_fields.h"
#include "formats/project_mt.h"
#include "audio/audio_tables.h"
#include "audio/master_fx.h"
#include "formats/perf_scenes.h"
#include "ui/screens.h"

using namespace ot;
using ot::audio::MasterFx;
using ot::audio::kBlock;

namespace {

// Peak of a sine of `hz` through the filter at value v, after it settled.
float gainAt(int v, float hz) {
    MasterFx fx;
    fx.setDjFilter(v);
    float l[kBlock], r[kBlock];
    float pk = 0.0f;
    uint64_t n = 0;
    for (int b = 0; b < 400; ++b) {
        for (uint32_t i = 0; i < kBlock; ++i, ++n)
            l[i] = r[i] = 0.5f * std::sin(2.0f * 3.14159265f * hz * float(n) / audio::kSampleRate);
        fx.process(l, r, kBlock);
        if (b >= 300)
            for (uint32_t i = 0; i < kBlock; ++i) pk = std::fabs(l[i]) > pk ? std::fabs(l[i]) : pk;
    }
    return pk / 0.5f;
}

}  // namespace

TEST(master_fx_dj_filter_response) {
    // Low-pass fully down: 5 kHz gone, 50 Hz through.
    CHECK(gainAt(-100, 5000.0f) < 0.01f);
    CHECK(gainAt(-100, 50.0f) > 0.8f);
    // Half way: about 1.4 kHz; 200 Hz passes, 8 kHz is well down.
    CHECK(gainAt(-50, 200.0f) > 0.9f);
    CHECK(gainAt(-50, 8000.0f) < 0.15f);
    // High-pass fully up: 100 Hz gone, 15 kHz through.
    CHECK(gainAt(100, 100.0f) < 0.01f);
    CHECK(gainAt(100, 15000.0f) > 0.8f);
    // Barely on either side: nearly open.
    CHECK(gainAt(-1, 10000.0f) > 0.95f);
    CHECK(gainAt(1, 60.0f) > 0.9f);
}

TEST(master_fx_off_is_bypassed_and_eases_out) {
    MasterFx fx;
    float l[kBlock], r[kBlock], ref[kBlock];
    for (uint32_t i = 0; i < kBlock; ++i) l[i] = r[i] = ref[i] = std::sin(float(i) * 0.7f);
    fx.process(l, r, kBlock);   // value 0 from the start: untouched
    CHECK(!fx.active());
    for (uint32_t i = 0; i < kBlock; ++i) CHECK_EQ(l[i], ref[i]);
    // On, then off: it eases open (no jump) and then stops working.
    fx.setDjFilter(-80);
    for (int b = 0; b < 50; ++b) fx.process(l, r, kBlock);
    CHECK(fx.active());
    fx.setDjFilter(0);
    int blocks = 0;
    while (fx.active() && blocks < 200) {
        fx.process(l, r, kBlock);
        ++blocks;
    }
    CHECK(!fx.active());
    CHECK(blocks > 10);   // smoothed, not instant
    CHECK(blocks < 120);  // ~30 ms time constant
    for (uint32_t i = 0; i < kBlock; ++i) l[i] = ref[i];
    fx.process(l, r, kBlock);
    for (uint32_t i = 0; i < kBlock; ++i) CHECK_EQ(l[i], ref[i]);
}

TEST(master_fx_no_clicks_on_jumps) {
    // A steady 300 Hz tone while the value jumps LP -> HP -> off: the output
    // never steps by more than the tone itself can in one sample (x2).
    MasterFx fx;
    float l[kBlock], r[kBlock];
    const float step = 0.5f * 2.0f * 3.14159265f * 300.0f / audio::kSampleRate;
    float prev = 0.0f, worst = 0.0f;
    uint64_t n = 0;
    const int seq[] = {-100, 100, -30, 0, 60, 0};
    for (int v : seq) {
        fx.setDjFilter(v);
        for (int b = 0; b < 60; ++b) {
            for (uint32_t i = 0; i < kBlock; ++i, ++n) l[i] = r[i] = 0.5f * std::sin(2.0f * 3.14159265f * 300.0f * float(n) / audio::kSampleRate);
            fx.process(l, r, kBlock);
            for (uint32_t i = 0; i < kBlock; ++i) {
                const float d = std::fabs(l[i] - prev);
                worst = d > worst ? d : worst;
                prev = l[i];
            }
        }
    }
    CHECK(worst < 2.0f * step);
}

TEST(master_fx_perf_type_storage) {
    // The DJ filter is an OpenTracker-only type: the stock byte stays 0.
    uint8_t types[12] = {}, values[96] = {}, rows[12] = {}, ext[12] = {};
    perf::Source s;
    s.types = types;
    s.values = values;
    s.rows = rows;
    s.ext = ext;
    s.setType(3, perf::kDjFilter);
    CHECK_EQ(s.type(3), perf::kDjFilter);
    CHECK_EQ(types[3], 0);
    CHECK_EQ(ext[3], perf::kDjFilter);
    s.setType(3, perf::kVolume);   // back to a stock type
    CHECK_EQ(types[3], perf::kVolume);
    CHECK_EQ(ext[3], 0);
    // Without ext storage it cannot be set (the slot stays empty).
    perf::Source noExt = s;
    noExt.ext = nullptr;
    noExt.setType(4, perf::kDjFilter);
    CHECK_EQ(noExt.type(4), 0);
    // validate drops unknown ext types and keeps known ones.
    ext[5] = perf::kDjFilter;
    ext[6] = 200;
    ext[7] = perf::kVolume;   // a stock id in ext: not allowed there
    s.validate();
    CHECK_EQ(ext[5], perf::kDjFilter);
    CHECK_EQ(ext[6], 0);
    CHECK_EQ(ext[7], 0);
    CHECK(std::strcmp(perf::fx(perf::kDjFilter).name, "DJ filter") == 0);
    CHECK(perf::fx(perf::kDjFilter).global);
}

TEST(master_fx_perf_scenes_ext_types) {
    // The OpenTracker-only FX types in perf_scenes.bin (v3, with the scene
    // bank; older versions are not read: no such projects exist).
    Project p;
    CHECK(!perfScenesUsed(p));
    p.perfExtType[2] = perf::kDjFilter;   // a DJ filter slot alone writes the file
    CHECK(perfScenesUsed(p));
    p.perfScene[0][1] = 2;
    uint8_t buf[pscn::kFileSize];
    encodePerfScenes(p, buf);
    CHECK_EQ(buf[4], 4);
    Project q;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.perfExtType[2], perf::kDjFilter);
    CHECK_EQ(q.perfScene[0][1], 2);
    // A damaged FX types block is ignored, the scenes still load.
    buf[pscn::kScenesEnd + 3] ^= 0x40;
    Project r;
    CHECK(decodePerfScenes(buf, sizeof(buf), r) == FmtStatus::Ok);
    CHECK_EQ(r.perfScene[0][1], 2);
    CHECK_EQ(r.perfExtType[2], 0);
}

TEST(master_fx_config_record) {
    FakeNvStore nv;
    Config c;
    c.perfExt[4] = perf::kDjFilter;   // alone writes the record
    CHECK(c.save(nv));
    Config r;
    CHECK(r.load(nv));
    CHECK_EQ(r.perfExt[4], perf::kDjFilter);
    // A shorter record (up to Note names, payload byte 28): no ext types.
    uint8_t old[5 + 28 + 1] = {'O', 'T', 'X', 5, 28, 0, 4, 0, 1, 0, 1};   // themeFile[21] empty
    old[5 + 27] = 1;                                                       // [I] Note names: Flats
    old[5 + 28] = 7;                                                       // sum
    for (size_t i = 0; i < sizeof(old); ++i) nv.mem[0xF4 + i] = old[i];
    for (size_t i = sizeof(old); i < Config::kExtSize; ++i) nv.mem[0xF4 + i] = 0xFF;
    CHECK(r.load(nv));
    CHECK_EQ(r.noteNames, 1);
    CHECK_EQ(r.perfExt[4], 0);
}

namespace {
struct PerfRig {
    FakeNvStore nv;
    std::unique_ptr<PerfPatternMemory> mem{new PerfPatternMemory};
    Harness h{false};
    PerfRig() {
        h.app->setNvStore(&nv);
        h.app->seq.setPerfPatternMemory(mem.get());
        h.app->boot();
    }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void setRow(int pad, int value) {
        h.push(InputEvent::kPadDown, uint8_t(pad));
        h.enc(int8_t(value));
        h.push(InputEvent::kPadUp, uint8_t(pad));
    }
};
}  // namespace

// The Performance screen: a DJ filter slot drives the master bus whatever
// the per-track FX keys say, is off when leaving, and saves as an empty
// stock slot plus its type in perf_scenes.bin.
TEST(master_fx_perf_screen_project_presets) {
    PerfRig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;   // per project
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);       // edit
    r.h.tap(btn::Down);      // column 0: Volume -> empty
    r.h.tap(btn::Down);      // -> Tape stop (wraps to the last type)
    r.h.tap(btn::Down);      // -> DJ filter
    CHECK_EQ(p.src.type(0), perf::kDjFilter);
    CHECK_EQ(a.project.u8(pf::kPerfTypes), 0);   // stock byte: empty
    CHECK_EQ(a.project.perfExtType[0], perf::kDjFilter);
    r.setRow(12, -60);
    r.setRow(36, 50);
    r.h.tap(btn::Rec);       // play
    r.h.pad(12);             // row 1: -60
    CHECK_EQ(a.engine.masterDjFilter(), -60);
    r.h.tap(btn::F1);        // track 1 FX off: the master is not a track
    CHECK_EQ(a.engine.masterDjFilter(), -60);
    r.h.pad(36);             // row 3: +50
    CHECK_EQ(a.engine.masterDjFilter(), 50);
    r.h.pad(0);              // row 0: off
    CHECK_EQ(a.engine.masterDjFilter(), 0);
    r.h.pad(36);
    r.h.tap(btn::Pattern);   // leave: everything reverts, Workspace autosave
    CHECK_EQ(a.engine.masterDjFilter(), 0);
    Project chk;
    const std::vector<uint8_t>& f = r.h.fs.files["Workspace/project.mt"];
    CHECK(decodeProject(f.data(), f.size(), chk) == FmtStatus::Ok);
    CHECK_EQ(chk.u8(pf::kPerfTypes), 0);
    CHECK_EQ(int16_t(chk.u16(pf::kPerfValues + 2)), -60);
    CHECK(r.h.fs.files.count("Workspace/perf_scenes.bin") == 1);
    const std::vector<uint8_t>& sf = r.h.fs.files["Workspace/perf_scenes.bin"];
    CHECK(decodePerfScenes(sf.data(), sf.size(), chk) == FmtStatus::Ok);
    CHECK_EQ(chk.perfExtType[0], perf::kDjFilter);
    // Back in: the slot is still a DJ filter; rows start cleared (stock,
    // spec 12 §A.5), so it is off until a pad is pressed.
    r.h.tap(btn::Performance);
    CHECK_EQ(r.p().src.type(0), perf::kDjFilter);
    CHECK_EQ(a.engine.masterDjFilter(), 0);
    r.h.pad(12);
    CHECK_EQ(a.engine.masterDjFilter(), -60);
}

// Global presets keep the type in the config's extension record.
TEST(master_fx_perf_screen_global_presets) {
    PerfRig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 0;
    r.h.tap(btn::Performance);
    r.h.tap(btn::Rec);
    r.h.tap(btn::Right);     // column 1
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);      // Panning -> Volume -> empty -> Tape stop -> DJ filter
    CHECK_EQ(r.p().src.type(1), perf::kDjFilter);
    CHECK_EQ(a.cfg.perfTypes()[1], 0);
    CHECK_EQ(a.cfg.perfExt[1], perf::kDjFilter);
    r.h.tap(btn::Rec);
    r.h.tap(btn::Pattern);   // leave: global presets are written
    Config c;
    CHECK(c.load(r.nv));
    CHECK_EQ(c.perfExt[1], perf::kDjFilter);
    CHECK_EQ(c.perfTypes()[1], 0);
}

// Scenes: A = low-pass, B = high-pass; the fader sweeps through open.
TEST(master_fx_perf_scene_fader) {
    PerfRig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);      // Volume -> empty -> Tape stop -> DJ filter
    r.setRow(12, -60);
    r.setRow(36, 50);
    r.h.tap(btn::Rec);
    r.h.pad(12);
    r.h.down(btn::Shift);
    r.h.tap(btn::Left);    // scene A
    r.h.up(btn::Shift);
    r.h.pad(36);
    r.h.down(btn::Shift);
    r.h.tap(btn::Right);   // scene B
    r.h.up(btn::Shift);
    r.h.enc(int8_t(-100 / PerformanceScreen::kFaderStep));   // fader to A
    CHECK_EQ(p.fader, 0);
    CHECK_EQ(a.engine.masterDjFilter(), -60);
    r.h.enc(int8_t(50 / PerformanceScreen::kFaderStep));
    CHECK(a.engine.masterDjFilter() > -10 && a.engine.masterDjFilter() < 0);   // -5: nearly open
    r.h.enc(int8_t(50 / PerformanceScreen::kFaderStep));
    CHECK_EQ(a.engine.masterDjFilter(), 50);
}

// ---- Tape stop ----------------------------------------------------------------

namespace {
// Zero crossings of x over [a, b).
int crossings(const std::vector<float>& x, size_t a, size_t b) {
    int c = 0;
    for (size_t i = a + 1; i < b && i < x.size(); ++i) c += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
    return c;
}
}  // namespace

TEST(tape_stop_brakes_stops_and_returns) {
    using audio::TapeStop;
    constexpr uint32_t kStop = 8820, kUp = 2205;   // 0.2 s / 0.05 s
    const uint32_t frames = TapeStop::framesFor(kStop);
    std::vector<int16_t> area(2 * frames);
    TapeStop t;
    std::vector<float> in, out;
    float l[kBlock], r[kBlock];
    uint64_t n = 0;
    auto run = [&](int blocks) {
        for (int b = 0; b < blocks; ++b) {
            for (uint32_t i = 0; i < kBlock; ++i, ++n) {
                l[i] = r[i] = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * float(n) / audio::kSampleRate);
                in.push_back(l[i]);
            }
            t.process(l, r, kBlock);
            for (uint32_t i = 0; i < kBlock; ++i) out.push_back(l[i]);
        }
    };
    run(10);   // idle: untouched
    for (size_t i = 0; i < out.size(); ++i) CHECK_EQ(out[i], in[i]);
    CHECK(!t.busy());
    t.engage(area.data(), frames, kStop);
    CHECK(t.busy());
    const size_t e = out.size();
    run(int(kStop / kBlock) + 4);
    CHECK(t.stopped());
    // The pitch falls: fewer crossings late in the brake than early.
    const int early = crossings(out, e, e + 1000), late = crossings(out, e + 6000, e + 7000);
    CHECK(early >= 40);       // ~1 kHz: ~45 per 1000 frames
    CHECK(late < early / 2);
    // Stopped: silence, however long it is held.
    const size_t s0 = out.size();
    run(200);
    for (size_t i = s0; i < out.size(); ++i) CHECK_EQ(out[i], 0.0f);
    // Release: spins up, then back to the live signal exactly.
    t.release(kUp);
    run(int(kUp / kBlock) + 20);
    CHECK(!t.busy());
    const size_t s1 = out.size();
    run(4);
    for (size_t i = s1; i < out.size(); ++i) CHECK_EQ(out[i], in[i]);
    // No clicks anywhere: one sample never moves more than ~2x a 1 kHz sine can.
    const float step = 0.5f * 2.0f * 3.14159265f * 1000.0f / audio::kSampleRate;
    float worst = 0.0f;
    for (size_t i = 1; i < out.size(); ++i) {
        const float d = std::fabs(out[i] - out[i - 1]);
        worst = d > worst ? d : worst;
    }
    CHECK(worst < 2.0f * step);
}

TEST(tape_stop_release_while_braking) {
    using audio::TapeStop;
    const uint32_t frames = TapeStop::framesFor(44100);
    std::vector<int16_t> area(2 * frames);
    TapeStop t;
    float l[kBlock] = {}, r[kBlock] = {};
    t.engage(area.data(), frames, 44100);
    for (int b = 0; b < 50; ++b) t.process(l, r, kBlock);   // part way
    CHECK(t.busy() && !t.stopped());
    t.release(11025);
    for (int b = 0; b < 200 && t.busy(); ++b) t.process(l, r, kBlock);
    CHECK(!t.busy());
}

TEST(tape_stop_app_memory) {
    Harness h;
    App& a = *h.app;
    a.project.setTempo(120.0f);
    const size_t free0 = a.bank.freeSamples();
    a.setTapeStop(3);   // 1 beat = 0.5 s
    CHECK(a.tapeHoldsMemory());
    CHECK(a.engine.masterTape().busy());
    const uint32_t frames = audio::TapeStop::framesFor(a.tapeStopFrames(3));
    CHECK_EQ(a.bank.reserved(), 2u * frames);
    CHECK(a.tapeStopFrames(3) > 22000 && a.tapeStopFrames(3) < 22100);   // 0.5 s at the live rate
    // A resample cannot start while the tape holds the memory.
    CHECK(a.resampleToggle() == App::ResampleAction::kBusy);
    a.setTapeStop(0);   // spin up, then the memory comes back
    h.audio.pump(200);
    h.run(5);
    CHECK(!a.tapeHoldsMemory());
    CHECK_EQ(a.bank.reserved(), 0u);
    CHECK_EQ(a.bank.freeSamples(), free0);
    // A resample take holds the memory: tape stop says so and does nothing.
    a.resampleCfg.length = 1;
    CHECK(a.resampleToggle() == App::ResampleAction::kArmed);
    a.setTapeStop(5);
    CHECK(!a.tapeHoldsMemory());   // (its notice yields to the take's status line)
    CHECK(!a.engine.masterTape().busy());
    a.resampleCancel();
    a.setTapeStop(0);
}

// Tape stop pads are momentary: hold = brake, release = spin up.
TEST(tape_stop_perf_pad_momentary) {
    PerfRig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);      // Volume -> empty -> Tape stop
    CHECK_EQ(p.src.type(0), perf::kTapeStop);
    r.setRow(12, 3);         // row 1: 1 beat
    r.h.tap(btn::Rec);
    r.h.push(InputEvent::kPadDown, 12);
    r.h.run(1);
    CHECK_EQ(a.tapeStop(), 3);
    CHECK(a.tapeHoldsMemory());
    r.h.push(InputEvent::kPadUp, 12);
    r.h.run(1);
    CHECK_EQ(a.tapeStop(), 0);
    CHECK_EQ(p.src.row(0), 0);   // not latched
    r.h.audio.pump(200);
    r.h.run(5);
    CHECK(!a.tapeHoldsMemory());
    // Leaving while held spins up too.
    r.h.push(InputEvent::kPadDown, 12);
    r.h.run(1);
    CHECK_EQ(a.tapeStop(), 3);
    r.h.tap(btn::Pattern);
    CHECK_EQ(a.tapeStop(), 0);
}

// Scenes switch tape stop at the middle of the fader (no stepping through
// the lengths): A off, B 1 bar -> off up to 50 %, then a 1-bar stop.
TEST(tape_stop_scene_switches_at_middle) {
    PerfRig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.h.tap(btn::Down);
    r.h.tap(btn::Down);      // Tape stop
    r.setRow(36, 5);         // row 3: 1 bar
    r.h.tap(btn::Rec);
    r.h.pad(0);              // row 0: off
    r.h.down(btn::Shift);
    r.h.tap(btn::Left);      // scene A: off
    r.h.up(btn::Shift);
    p.src.setRow(0, 3);      // scene B: 1 bar (a momentary pad can't latch it)
    r.h.down(btn::Shift);
    r.h.tap(btn::Right);
    r.h.up(btn::Shift);
    p.src.setRow(0, 0);
    CHECK_EQ(a.project.perfScene[1][0], 3);
    int seen[7] = {};
    for (int x = 0; x <= 100; x += PerformanceScreen::kFaderStep) {
        r.h.enc(int8_t(x == 0 ? -100 / PerformanceScreen::kFaderStep : 1));
        const int v = a.tapeStop();
        if (v >= 0 && v <= 6) ++seen[v];
        if (p.fader < 50) CHECK_EQ(v, 0);
        if (p.fader > 50) CHECK_EQ(v, 5);
    }
    CHECK(seen[1] + seen[2] + seen[3] + seen[4] + seen[6] == 0);   // never the other lengths
    r.h.enc(int8_t(-100 / PerformanceScreen::kFaderStep));
    CHECK_EQ(a.tapeStop(), 0);   // back to A: spins up
}
