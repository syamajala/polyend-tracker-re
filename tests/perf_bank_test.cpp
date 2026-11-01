// [I] #70 Performance scene banks: the Performance/Scenes/<name>.txt text
// format, App load / save and the bank page's F5 Export / F6 Import.
#include <cstring>
#include <memory>
#include <string>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"
#include "formats/file_io.h"
#include "formats/perf_bank.h"
#include "ui/screens.h"

using namespace ot;
using View = PerformanceScreen::View;

namespace {

PerfBank sample() {
    PerfBank b;
    b.type[0] = perf::kLowPass;
    b.value[0][0] = -60, b.value[0][1] = 0, b.value[0][2] = 60;
    b.type[1] = perf::kTapeStop;
    b.value[1][0] = 1, b.value[1][1] = 3, b.value[1][2] = 6;
    b.type[2] = perf::kMidiCc;
    b.value[2][0] = 0, b.value[2][1] = 65, b.value[2][2] = 128;   // off, CC 64, CC 127
    b.target[2] = perf::ccTarget(1, 3);                            // MIDI 2, slot D
    b.type[11] = perf::kDjFilter;
    b.value[11][0] = -100;
    b.scene[0][0] = 1, b.scene[0][2] = 2;
    b.scene[15][11] = 3;
    b.fxSet = uint16_t(1u << 1 | 1u << 4);
    b.fxOff[1] = uint16_t(1u << 2 | 1u << 3 | 1u << 11);
    b.fxOff[4] = 0;   // stores "all on"
    b.on[0] = 15, b.on[1] = 1;
    return b;
}

bool same(const PerfBank& a, const PerfBank& b) {
    return std::memcmp(a.type, b.type, sizeof(a.type)) == 0 && std::memcmp(a.value, b.value, sizeof(a.value)) == 0 &&
           std::memcmp(a.target, b.target, sizeof(a.target)) == 0 &&
           std::memcmp(a.scene, b.scene, sizeof(a.scene)) == 0 && a.fxSet == b.fxSet &&
           std::memcmp(a.fxOff, b.fxOff, sizeof(a.fxOff)) == 0 && a.on[0] == b.on[0] && a.on[1] == b.on[1];
}

void writeText(FakeStorage& fs, const char* path, const std::string& t) {
    CHECK(writeWholeFile(fs, path, reinterpret_cast<const uint8_t*>(t.data()), uint32_t(t.size())) == FmtStatus::Ok);
}

std::string readText(FakeStorage& fs, const char* path) {
    static uint8_t buf[pbank::kTextMax];
    uint32_t got = 0;
    if (readWholeFile(fs, path, buf, sizeof(buf), &got) != FmtStatus::Ok) return "";
    return std::string(reinterpret_cast<char*>(buf), got);
}

struct Rig {
    FakeNvStore nv;
    std::unique_ptr<PerfPatternMemory> mem{new PerfPatternMemory};
    Harness h{false};
    Rig() {
        h.app->setNvStore(&nv);
        h.app->seq.setPerfPatternMemory(mem.get());
        h.app->boot();
        h.app->ui.fragments.enabled = false;
        h.tap(btn::Performance);
    }
    App& a() { return *h.app; }
    Project& pj() { return h.app->project; }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void row(int c, int r) { h.pad(uint8_t(12 * r + c)); }
    void with(uint8_t mod, uint8_t key) {
        h.down(mod);
        h.tap(key);
        h.up(mod);
    }
};

}  // namespace

TEST(perf_bank_text_round_trip) {
    char buf[pbank::kTextMax];
    const PerfBank b = sample();
    const size_t n = encodePerfBank(b, buf, sizeof(buf));
    CHECK(n > 0);
    const std::string t(buf, n);
    CHECK(t.find("# OpenTracker scene bank\n") == 0);
    CHECK(t.find("column 1 = Sample LP cutoff: -60 0 60\n") != std::string::npos);
    CHECK(t.find("column 2 = Tape stop: 1 3 6\n") != std::string::npos);
    CHECK(t.find("column 3 = MIDI CC: - 64 127 @ midi 2 slot D\n") != std::string::npos);
    CHECK(t.find("column 4 = ---\n") != std::string::npos);
    CHECK(t.find("scene 1 = 1 0 2 0 0 0 0 0 0 0 0 0\n") != std::string::npos);
    CHECK(t.find("scene 2 =") == std::string::npos);   // empty scenes are left out
    CHECK(t.find("scene 2 fx off = 3 4 12\n") != std::string::npos);
    CHECK(t.find("scene 5 fx off = none\n") != std::string::npos);
    CHECK(t.find("a = 16\nb = 2\n") != std::string::npos);
    PerfBank r;
    CHECK(decodePerfBank(buf, n, r));
    CHECK(same(r, b));
    // Too small a buffer: nothing.
    CHECK_EQ(encodePerfBank(b, buf, 40), size_t(0));
}

TEST(perf_bank_longest_text_fits) {
    PerfBank b;
    for (int c = 0; c < PerfBank::kSlots; ++c) {
        b.type[c] = perf::kFineLfo;   // the longest name
        for (auto& v : b.value[c]) v = -28;
    }
    b.type[0] = perf::kMidiCc;
    b.target[0] = perf::ccTarget(15, 5);
    for (auto& v : b.value[0]) v = 128;
    for (auto& s : b.scene)
        for (auto& r : s) r = 3;
    b.fxSet = 0xFFFF;
    for (auto& m : b.fxOff) m = 0xFFFF;
    b.on[0] = b.on[1] = 15;
    static char buf[pbank::kTextMax];
    const size_t n = encodePerfBank(b, buf, sizeof(buf));
    CHECK(n > 0 && n < pbank::kTextMax);
    PerfBank r;
    CHECK(decodePerfBank(buf, n, r));
    CHECK(same(r, b));
}

TEST(perf_bank_text_is_forgiving) {
    const std::string t =
        "# a bank typed on a computer\r\n"
        "COLUMN 1 = sample lp CUTOFF : -50 999 50   # 999 is out of range\r\n"
        "column 2 = Not an FX: 1 2 3\n"
        "column 3 = Volume/Velocity: 10\n"          // rows 2 / 3 missing
        "column 4 = MIDI CC: 0 x 127 @ midi 17 slot A\n"   // bad target: MIDI 1 slot A
        "column 13 = Volume/Velocity: 1 2 3\n"      // no column 13
        "scene 3 = 1 2 3 4\n"                       // 4 is no row
        "scene 17 = 1 1 1\n"
        "scene 4 fx off = 1 99 16\n"
        "tempo = 120\n"
        "A = 3\n"
        "b = 0\n";
    PerfBank b;
    CHECK(decodePerfBank(t.data(), t.size(), b));
    CHECK_EQ(b.type[0], perf::kLowPass);
    CHECK_EQ(b.value[0][0], -50);
    CHECK_EQ(b.value[0][1], 0);
    CHECK_EQ(b.value[0][2], 50);
    CHECK_EQ(b.type[1], 0);
    CHECK_EQ(b.type[2], perf::kVolume);
    CHECK_EQ(b.value[2][0], 10);
    CHECK_EQ(b.value[2][1], 0);
    CHECK_EQ(b.type[3], perf::kMidiCc);
    CHECK_EQ(b.value[3][0], 1);   // CC 0
    CHECK_EQ(b.value[3][1], 0);
    CHECK_EQ(b.value[3][2], 128);
    CHECK_EQ(b.target[3], 0);
    CHECK_EQ(b.scene[2][0], 1);
    CHECK_EQ(b.scene[2][2], 3);
    CHECK_EQ(b.scene[2][3], 0);
    CHECK_EQ(b.fxSet, uint16_t(1u << 3));
    CHECK_EQ(b.fxOff[3], uint16_t(1u | 1u << 15));
    CHECK_EQ(b.on[0], 2);
    CHECK_EQ(b.on[1], 1);   // b = 0 is no scene: the default
    // Not a bank.
    const std::string junk = "hello\nvelocity = 3\n";
    CHECK(!decodePerfBank(junk.data(), junk.size(), b));
}

TEST(perf_bank_save_and_load) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    const perf::Source& s = p.src;
    s.setType(0, perf::kVolume);
    s.setValue(0, 1, -50);
    s.setValue(0, 2, 50);
    s.setValue(0, 3, 20);
    s.setType(1, perf::kTapeStop);
    s.setValue(1, 1, 2);
    r.pj().perfScene[0][0] = 2;
    r.pj().perfScene[3][1] = 1;
    r.pj().perfSceneOn[1] = 3;
    r.pj().perfSceneFxSet = 1u << 3;
    r.pj().perfSceneFxOff[3] = 0x00F0;
    CHECK(a.savePerfBank("Kit", s));
    CHECK(r.h.fs.exists("Performance/Scenes/Kit.txt"));
    CHECK(readText(r.h.fs, "Performance/Scenes/Kit.txt").find("column 2 = Tape stop: 2 0 0\n") != std::string::npos);
    // Another kit in its place, then the file back.
    s.setType(0, perf::kTune);
    s.setValue(0, 1, 7);
    s.setRow(0, 1);
    std::memset(r.pj().perfScene, 0, sizeof(r.pj().perfScene));
    r.pj().perfSceneOn[1] = 1;
    r.pj().perfSceneFxSet = 0;
    CHECK(a.loadPerfBank("Kit", s));
    CHECK_EQ(s.type(0), perf::kVolume);
    CHECK_EQ(s.value(0, 3), 20);
    CHECK_EQ(s.row(0), 0);   // the live pad rows are cleared
    CHECK_EQ(s.type(1), perf::kTapeStop);
    CHECK_EQ(s.ext[1], perf::kTapeStop);   // an OpenTracker-only type: stock sees the slot empty
    CHECK_EQ(s.types[1], 0);
    CHECK_EQ(r.pj().perfScene[0][0], 2);
    CHECK_EQ(r.pj().perfScene[3][1], 1);
    CHECK_EQ(r.pj().perfSceneOn[1], 3);
    CHECK(r.pj().sceneHasFx(3));
    CHECK_EQ(r.pj().perfSceneFxOff[3], 0x00F0);
    // A scene row on a column with no type is dropped (as on the device).
    writeText(r.h.fs, "Performance/Scenes/Bare.txt", "column 1 = Tune: 1 2 3\nscene 1 = 1 3\n");
    CHECK(a.loadPerfBank("Bare", s));
    CHECK_EQ(r.pj().perfScene[0][0], 1);
    CHECK_EQ(r.pj().perfScene[0][1], 0);
    CHECK_EQ(s.type(1), 0);
    CHECK(!a.loadPerfBank("Missing", s));
    CHECK(!a.savePerfBank("", s));
}

TEST(perf_bank_page_export_and_import) {
    Rig r;
    PerformanceScreen& p = r.p();
    const perf::Source& s = p.src;
    s.setType(0, perf::kVolume);
    s.setValue(0, 1, -50);
    s.setValue(0, 2, 50);
    r.row(0, 2);
    r.with(btn::Shift, btn::Left);   // scene 1 (on A): Volume row 2
    CHECK_EQ(r.pj().perfScene[0][0], 2);
    r.h.tap(btn::Performance);   // the bank page
    CHECK(p.view == View::Bank);
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("Export"));
    CHECK(r.h.disp.lastHasText("Import"));
    // Import with no files: a notice, no list.
    r.h.tap(btn::F6);
    CHECK(!p.bankFiles);
    CHECK(r.h.app->ui.notice()[0] != 0);
    // F5 Export: the keyboard, a name, Save.
    r.h.tap(btn::F5);
    CHECK(r.a().ui.kb.isOpen());
    for (int i = 0; i < 40; ++i) r.h.tap(btn::Delete);   // clear the suggested name
    r.h.type("dusk");
    r.h.tap(btn::F8);
    CHECK(!r.a().ui.kb.isOpen());
    CHECK(r.h.fs.exists("Performance/Scenes/dusk.txt"));
    // Change the kit, then import the file back.
    s.setType(0, perf::kTune);
    std::memset(r.pj().perfScene, 0, sizeof(r.pj().perfScene));
    writeText(r.h.fs, "Performance/Scenes/Another.txt", "column 1 = Overdrive: 10 20 30\n");
    r.h.tap(btn::F6);
    CHECK(p.bankFiles);
    CHECK_EQ(p.fileCount, 2);
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("Import Scene Bank"));
    CHECK(r.h.disp.lastHasText("Another"));
    CHECK(r.h.disp.lastHasText("dusk"));
    CHECK_EQ(p.fileCursor, 1);   // on the bank exported last (alphabetical: Another, dusk)
    r.h.tap(btn::F8);            // asks first
    CHECK(r.a().ui.askOpen());
    CHECK_EQ(s.type(0), perf::kTune);
    r.h.tap(btn::F8);            // Import
    CHECK(!p.bankFiles);
    CHECK_EQ(s.type(0), perf::kVolume);
    CHECK_EQ(s.value(0, 2), 50);
    CHECK_EQ(r.pj().perfScene[0][0], 2);
    // Up and cancel: nothing changes.
    r.h.tap(btn::F6);
    r.h.tap(btn::Up);
    CHECK_EQ(p.fileCursor, 0);
    r.h.tap(btn::F7);
    CHECK(!p.bankFiles);
    CHECK_EQ(s.type(0), perf::kVolume);
    // The Performance key from the list goes back to playing.
    r.h.tap(btn::F6);
    r.h.tap(btn::Performance);
    CHECK(p.view == View::Play);
    CHECK(!p.bankFiles);
}
