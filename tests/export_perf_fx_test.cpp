// [I] #51: song exports render the song slots' Performance scenes and ramps
// (File > Export, F7 "Perf FX", On by default); Off is the stock dry export.
// Stems get the per-track FX; the master FX stay on the full mix.
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "formats/wav.h"
#include "ui/screens.h"

using namespace ot;
using KeepAlive = PerformanceScreen::KeepAlive;

namespace {

struct Rig {
    FakeNvStore nv;
    std::unique_ptr<PerfPatternMemory> mem{new PerfPatternMemory};
    Harness h{false};
    Rig() {
        h.app->setNvStore(&nv);
        h.app->seq.setPerfPatternMemory(mem.get());
        h.app->boot();
        App& a = *h.app;
        // A sine on instrument 1, a note on every 4th step of track 1.
        std::vector<int16_t> s(44100);
        for (size_t i = 0; i < s.size(); ++i) s[i] = int16_t(12000.0 * std::sin(double(i) * 0.05));
        std::vector<uint8_t> out(s.size() * 2 + 64);
        out.resize(wavEncode(s.data(), uint32_t(s.size()), 1, out.data(), out.size()));
        h.fs.files["t.wav"] = out;
        CHECK(a.importSample("t.wav", 0));
        Pattern& p = a.pattern();
        for (auto& t : p.tracks) t.lastStep = 15;
        for (int st = 0; st < 16; st += 4) {
            p.tracks[0].steps[st].note = 60;
            p.tracks[0].steps[st].instrument = 0;
        }
        a.patternEdited();
        // Performance slot 1 = Volume, row 1 = -20; scene 1 holds that row.
        h.tap(btn::Performance);
        perf().src.setValue(0, 1, -20);
        a.project.perfScene[0][0] = 1;
        h.tap(btn::Song);   // stopped: the FX revert
        // The song: pattern 1 once, with scene 1 on A.
        a.project.playlist[0] = 1;
        a.project.playlist[1] = 0;
        a.project.playlistScene[0] = 1;
        h.fs.mkdir("Export");
    }
    PerformanceScreen& perf() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    std::vector<int16_t> samples(const std::string& path) {
        const std::vector<uint8_t>& f = h.fs.files[path];
        WavInfo info;
        if (!wavParseHeader(f.data(), f.size(), info) || info.dataOffset >= f.size()) return {};
        const size_t n = std::min<size_t>(info.dataSize, f.size() - info.dataOffset) / 2;
        std::vector<int16_t> out(n);
        std::memcpy(out.data(), f.data() + info.dataOffset, n * 2);
        return out;
    }
    static double rms(const std::vector<int16_t>& s) {
        double acc = 0;
        for (int16_t v : s) acc += double(v) * v;
        return s.empty() ? 0.0 : std::sqrt(acc / double(s.size()));
    }
};

}  // namespace

TEST(export_song_renders_slot_scenes) {
    Rig r;
    App& a = *r.h.app;
    CHECK(a.exportPerfFx);   // On by default
    a.exportPerfFx = false;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/dry.wav"));
    a.exportPerfFx = true;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/fx.wav"));
    const double dry = Rig::rms(r.samples("Export/dry.wav"));
    const double fx = Rig::rms(r.samples("Export/fx.wav"));
    std::printf("  song export rms: dry %.0f, scene (volume -20) %.0f\n", dry, fx);
    CHECK(dry > 100.0);
    CHECK(fx > 100.0);
    CHECK(fx < dry * 0.8);
    // Nothing stays live after the render.
    CHECK(r.perf().keepAlive() == KeepAlive::None);
    CHECK(!a.seq.performanceMode());
    CHECK_EQ(r.h.app->engine.voice(0).perf().volume, 0);
    // The same export again sounds the same (repeatable; renders aren't
    // bit-identical, so compare levels).
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/fx2.wav"));
    CHECK(std::fabs(Rig::rms(r.samples("Export/fx2.wav")) - fx) < fx * 0.01);
}

TEST(export_song_renders_slot_ramp) {
    Rig r;
    App& a = *r.h.app;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/still.wav"));
    // Scene 1 (volume -20) on A ramps to scene 2 (nothing) on B over the slot.
    a.project.playlistSceneTo[0] = 2;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/ramp.wav"));
    a.exportPerfFx = false;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/dry.wav"));
    const double still = Rig::rms(r.samples("Export/still.wav"));
    const double ramp = Rig::rms(r.samples("Export/ramp.wav"));
    const double dry = Rig::rms(r.samples("Export/dry.wav"));
    std::printf("  ramp export rms: scene %.0f < ramp %.0f < dry %.0f\n", still, ramp, dry);
    CHECK(ramp > still * 1.1);
    CHECK(ramp < dry * 0.95);
}

TEST(export_stems_get_per_track_fx) {
    Rig r;
    App& a = *r.h.app;
    a.exportPerfFx = false;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/dry", nullptr, true));
    a.exportPerfFx = true;
    CHECK(a.exportAudio(App::ExportKind::Song, "Export/fx", nullptr, true));
    const double dry = Rig::rms(r.samples("Export/dry/track1.wav"));
    const double fx = Rig::rms(r.samples("Export/fx/track1.wav"));
    CHECK(dry > 100.0);
    CHECK(fx < dry * 0.8);
    CHECK(!a.exportingStems());
}

TEST(export_pattern_stays_dry) {
    Rig r;
    App& a = *r.h.app;
    a.exportPerfFx = false;
    CHECK(a.exportAudio(App::ExportKind::Pattern, "Export/p_dry.wav"));
    a.exportPerfFx = true;
    CHECK(a.exportAudio(App::ExportKind::Pattern, "Export/p_fx.wav"));
    // A pattern export plays no song slots: no scenes. (Two renders aren't
    // bit-identical, so compare levels.)
    const double dry = Rig::rms(r.samples("Export/p_dry.wav"));
    const double fx = Rig::rms(r.samples("Export/p_fx.wav"));
    CHECK(dry > 100.0);
    CHECK(std::fabs(fx - dry) < dry * 0.01);
}

TEST(export_page_perf_fx_key) {
    Rig r;
    App& a = *r.h.app;
    r.h.tap(btn::File);
    r.h.tap(btn::F6);   // Export page
    CHECK_STR(a.ui.bar.label[6].top, "Perf FX");
    CHECK_STR(a.ui.bar.label[6].bottom, "On");
    r.h.tap(btn::F7);
    CHECK(!a.exportPerfFx);
    CHECK_STR(a.ui.bar.label[6].bottom, "Off");
    r.h.tap(btn::F7);
    CHECK(a.exportPerfFx);
}
