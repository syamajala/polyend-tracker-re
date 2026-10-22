// [I] Performance scene bank (#10): 16 scenes with the crossfader's ends
// A / B pointing into them, the bank page (a view of the Performance
// screen), editing a scene there, the track bank on Left / Right, and
// perf_scenes.bin v3.
#include <cstring>
#include <memory>
#include <string>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "formats/perf_scenes.h"
#include "ui/screens.h"

using namespace ot;
using View = PerformanceScreen::View;

namespace {

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
        // Slot 0 = Volume: rows 1 / 2 / 3 = -50 / +50 / +20.
        p().src.setValue(0, 1, -50);
        p().src.setValue(0, 2, 50);
        p().src.setValue(0, 3, 20);
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
    void fade(int x) { h.enc(int8_t((x - p().fader) / PerformanceScreen::kFaderStep)); }
    std::string note() { return h.app->ui.notice(); }
};

}  // namespace

TEST(scene_bank_page_keys) {
    Rig r;
    PerformanceScreen& p = r.p();
    CHECK(p.view == View::Play);
    r.h.tap(btn::Performance);   // on the Performance screen: the bank page
    CHECK(p.view == View::Bank);
    CHECK_EQ(p.bankCursor, 0);   // starts on the scene on A
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("Scenes"));
    CHECK(r.h.disp.lastHasText("Set A"));
    r.h.tap(btn::Right);
    r.h.tap(btn::Down);
    CHECK_EQ(p.bankCursor, 9);
    r.h.enc(3);
    CHECK_EQ(p.bankCursor, 12);
    r.h.enc(20);
    CHECK_EQ(p.bankCursor, 15);
    r.h.tap(btn::Up);
    CHECK_EQ(p.bankCursor, 7);
    r.h.tap(btn::F8);   // Back
    CHECK(p.view == View::Play);
    r.h.tap(btn::Performance);
    r.h.tap(btn::Performance);   // and the Performance key again
    CHECK(p.view == View::Play);
    // From another screen the key goes to the Performance screen (playing).
    r.h.tap(btn::Performance);
    r.h.tap(btn::Pattern);
    r.h.tap(btn::Performance);
    CHECK(r.a().ui.active() == kModPerformance);
    CHECK(p.view == View::Play);
}

TEST(scene_bank_track_bank_on_left_right) {
    Rig r;
    PerformanceScreen& p = r.p();
    CHECK_EQ(p.bank, 0);
    r.h.tap(btn::Right);
    CHECK_EQ(p.bank, 1);
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("Tracks 9-16"));
    r.h.tap(btn::Right);
    CHECK_EQ(p.bank, 1);
    r.h.tap(btn::Left);
    CHECK_EQ(p.bank, 0);
    // In edit mode Left / Right still move the column.
    r.h.tap(btn::Rec);
    r.h.tap(btn::Right);
    CHECK_EQ(p.column, 1);
    CHECK_EQ(p.bank, 0);
}

TEST(scene_bank_edit_a_scene_saves_and_restores) {
    Rig r;
    PerformanceScreen& p = r.p();
    r.row(1, 2);   // a live row outside any scene
    const uint8_t live1 = p.src.row(1);
    r.h.tap(btn::Performance);
    r.h.tap(btn::Right);
    r.h.tap(btn::Right);   // scene 3
    r.h.tap(btn::F3);      // Edit
    CHECK(p.view == View::EditScene);
    CHECK_EQ(p.editScene, 2);
    CHECK_EQ(p.src.row(1), 0);   // the scene's rows (empty) on the pads
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("Scene 3"));
    CHECK(r.h.disp.lastHasText("Editing"));
    r.row(0, 2);   // Volume +50: heard at once
    CHECK_EQ(p.cur[0], 50);
    const int f = p.fader;
    r.h.enc(5);   // no fader while editing
    CHECK_EQ(p.fader, f);
    r.h.tap(btn::Performance);   // back: saved
    CHECK(p.view == View::Bank);
    CHECK(r.note() == "Scene 3 saved");
    CHECK_EQ(r.pj().perfScene[2][0], 2);
    CHECK_EQ(r.pj().perfScene[2][1], 0);
    CHECK_EQ(p.src.row(1), live1);   // the live rows are back
    CHECK_EQ(p.bankCursor, 2);
    // The selected scene's pad opens it again; another pad selects.
    r.h.pad(3);   // scene 4 (row 1, column 3)
    CHECK_EQ(p.bankCursor, 3);
    CHECK(p.view == View::Bank);
    r.h.pad(2);
    r.h.pad(2);
    CHECK(p.view == View::EditScene);
    CHECK_EQ(p.src.row(0), 2);
    // Leaving the screen while editing saves too.
    r.row(0, 3);
    r.h.tap(btn::Pattern);
    CHECK_EQ(r.pj().perfScene[2][0], 3);
    CHECK(p.view == View::Play);
}

TEST(scene_bank_set_a_b_and_blend) {
    Rig r;
    PerformanceScreen& p = r.p();
    // Scenes 5 (Volume row 1 = -50) and 9 (row 2 = +50).
    r.pj().perfScene[4][0] = 1;
    r.pj().perfScene[8][0] = 2;
    r.h.tap(btn::Performance);
    for (int i = 0; i < 4; ++i) r.h.tap(btn::Right);
    r.h.tap(btn::F1);   // scene 5 on A
    CHECK_EQ(r.pj().perfSceneOn[0], 4);
    CHECK(r.note() == "Scene 5 on A");
    r.h.pad(12);   // scene 9 (row 2 of the pads, column 0)
    r.h.tap(btn::F2);
    CHECK_EQ(r.pj().perfSceneOn[1], 8);
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("A 5"));
    CHECK(r.h.disp.lastHasText("B 9"));
    r.h.tap(btn::F8);
    r.fade(100);
    CHECK_EQ(p.cur[0], 50);
    r.fade(0);
    CHECK_EQ(p.cur[0], -50);
    r.fade(50);
    CHECK_EQ(p.cur[0], 0);
    r.h.run(50);
    CHECK(r.h.disp.lastHasText("A 5"));   // the title's fader names the pair
    // Another scene on B: the fader stays, the blend follows.
    r.pj().perfScene[9][0] = 3;   // scene 10: +20
    r.h.tap(btn::Performance);
    r.h.pad(13);
    r.h.tap(btn::F2);
    CHECK_EQ(p.fader, 50);
    CHECK_EQ(p.cur[0], -15);   // (-50 + 20) / 2
}

TEST(scene_bank_shortcuts_store_and_clear_the_scenes_on_a_b) {
    Rig r;
    PerformanceScreen& p = r.p();
    r.pj().perfSceneOn[0] = 6;    // A = scene 7
    r.pj().perfSceneOn[1] = 11;   // B = scene 12
    r.row(0, 1);
    r.with(btn::Shift, btn::Left);
    CHECK_EQ(r.pj().perfScene[6][0], 1);
    CHECK(r.note() == "Scene 7 stored (A)");
    r.row(0, 2);
    r.with(btn::Shift, btn::Right);
    CHECK_EQ(r.pj().perfScene[11][0], 2);
    CHECK_EQ(r.pj().perfScene[0][0], 0);   // scenes 1 / 2 untouched
    r.with(btn::Delete, btn::Right);
    CHECK_EQ(r.pj().perfScene[11][0], 0);
    CHECK(r.note() == "Scene 12 cleared (B)");
    // Not while editing a scene: the pads are that scene.
    r.h.tap(btn::Performance);
    r.h.tap(btn::F3);
    r.row(0, 3);
    r.with(btn::Shift, btn::Left);
    CHECK_EQ(r.pj().perfScene[6][0], 1);
    // Clear on the bank page (the scene on A): its slot back to the pad row.
    r.h.tap(btn::Performance);   // back to the bank, scene 7 saved
    CHECK_EQ(r.pj().perfScene[6][0], 3);
    r.h.tap(btn::F4);
    CHECK_EQ(r.pj().perfScene[6][0], 0);
    CHECK(r.note() == "Scene 7 cleared");
    (void)p;
}

TEST(scene_bank_midi_fader_waits_while_editing) {
    Rig r;
    PerformanceScreen& p = r.p();
    r.pj().perfScene[0][0] = 1;
    r.pj().perfScene[1][0] = 2;
    r.h.tap(btn::Performance);
    r.h.tap(btn::Right);
    r.h.tap(btn::F3);   // editing scene 2
    p.perfSceneCc(127);
    r.h.run(50);
    CHECK_EQ(p.fader, 0);   // kept while editing
    r.h.tap(btn::Performance);
    r.h.tap(btn::F8);
    r.h.run(50);
    CHECK_EQ(p.fader, PerformanceScreen::kFaderMax);
}

TEST(scene_bank_perf_scenes_v3) {
    Project p;
    CHECK(!perfScenesUsed(p));
    p.perfScene[15][11] = 3;
    p.perfSceneOn[0] = 15;
    p.perfSceneOn[1] = 4;
    p.perfExtType[2] = 22;
    CHECK(perfScenesUsed(p));
    CHECK(!perfFaderUsed(Project{}));
    CHECK(perfFaderUsed(p));   // scene 16 on A holds a row
    uint8_t buf[pscn::kFileSize];
    encodePerfScenes(p, buf);
    Project q;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.perfScene[15][11], 3);
    CHECK_EQ(q.perfSceneOn[0], 15);
    CHECK_EQ(q.perfSceneOn[1], 4);
    CHECK_EQ(q.perfExtType[2], 22);
    // A / B only moved: still written.
    Project m;
    m.perfSceneOn[1] = 7;
    CHECK(perfScenesUsed(m));
    // Older versions are not read.
    buf[4] = 2;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::BadMagic);
    CHECK_EQ(q.perfScene[15][11], 0);
    CHECK_EQ(q.perfSceneOn[0], 0);
    CHECK_EQ(q.perfSceneOn[1], 1);
}
