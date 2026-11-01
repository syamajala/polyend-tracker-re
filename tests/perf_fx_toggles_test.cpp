// [I] #54: the Performance screen's per-track FX toggles (F-keys) are
// remembered across visits and saved with the project, and a scene can carry
// them: storing a whole scene keeps the toggles, loading it (A / B, the
// fader crossing the middle, a song slot) applies them; a scene without
// toggles ("-") leaves them alone.
#include <cstring>
#include <memory>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"
#include "formats/file_io.h"
#include "formats/perf_scenes.h"
#include "ui/screens.h"

using namespace ot;

namespace {

struct Rig {
    FakeNvStore nv;
    std::unique_ptr<PerfPatternMemory> mem{new PerfPatternMemory};
    Harness h{false};
    Rig() {
        h.app->setNvStore(&nv);
        h.app->seq.setPerfPatternMemory(mem.get());
        h.app->boot();
        h.tap(btn::Performance);
    }
    App& a() { return *h.app; }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void store(int scene) {
        h.down(btn::Shift);
        h.tap(scene ? btn::Right : btn::Left);
        h.up(btn::Shift);
    }
    void clear(int scene) {
        h.down(btn::Delete);
        h.tap(scene ? btn::Right : btn::Left);
        h.up(btn::Delete);
    }
    void fade(int x) { h.enc(int8_t((x - p().fader) / PerformanceScreen::kFaderStep)); }
};

}  // namespace

TEST(perf_fx_toggles_codec) {
    Project p;
    CHECK(!perfScenesUsed(p));
    p.perfFxOff = 0x0005;
    CHECK(perfScenesUsed(p));
    p.perfSceneFxSet = 0x8001;
    p.perfSceneFxOff[0] = 0x00F0;
    p.perfSceneFxOff[15] = 0x1234;
    p.perfScene[2][4] = 1;
    uint8_t buf[pscn::kFileSize];
    encodePerfScenes(p, buf);
    CHECK_EQ(buf[4], pscn::kVersion);
    Project q;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.perfFxOff, 0x0005);
    CHECK_EQ(q.perfSceneFxSet, 0x8001);
    CHECK_EQ(q.perfSceneFxOff[0], 0x00F0);
    CHECK_EQ(q.perfSceneFxOff[15], 0x1234);
    CHECK(q.sceneHasFx(0) && q.sceneHasFx(15) && !q.sceneHasFx(1));
    // A bad toggles block: the rest loads, every track on.
    buf[pscn::kAbEnd + 1] ^= 1;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.perfScene[2][4], 1);
    CHECK_EQ(q.perfFxOff, 0);
    CHECK_EQ(q.perfSceneFxSet, 0);
    // A version 3 file (no toggles block) loads with every track on.
    encodePerfScenes(p, buf);
    buf[4] = 3;
    CHECK(decodePerfScenes(buf, pscn::kAbEnd, q) == FmtStatus::Ok);
    CHECK_EQ(q.perfScene[2][4], 1);
    CHECK_EQ(q.perfFxOff, 0);
    // A new project: all on, no scene toggles.
    q.setDefault();
    CHECK_EQ(q.perfFxOff, 0);
    CHECK_EQ(q.perfSceneFxSet, 0);
}

TEST(perf_fx_toggles_remembered_and_saved) {
    Rig r;
    App& a = r.a();
    r.h.tap(btn::F2);   // track 2 off
    CHECK(!r.p().fxOn[1]);
    CHECK_EQ(a.project.perfFxOff, 0x0002);
    // Leaving and coming back keeps it (stock turned every track on again).
    r.h.tap(btn::Pattern);
    r.h.tap(btn::Performance);
    CHECK(!r.p().fxOn[1]);
    CHECK(r.p().fxOn[0]);
    // Saved with the project, and back after reopening it.
    CHECK(a.saveProjectAs("toggles"));
    CHECK(r.h.fs.exists("Projects/toggles/perf_scenes.bin"));
    CHECK(a.newProject());
    CHECK_EQ(a.project.perfFxOff, 0);
    CHECK(a.openProject(kProjectsDir, "toggles"));
    CHECK_EQ(a.project.perfFxOff, 0x0002);
    r.h.tap(btn::Pattern);
    r.h.tap(btn::Performance);
    CHECK(!r.p().fxOn[1]);
    r.h.tap(btn::F2);   // on again: nothing left to save
    CHECK_EQ(a.project.perfFxOff, 0);
}

TEST(perf_fx_toggles_stored_in_scene_and_applied_on_load) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    r.h.pad(12 * 1 + 0);   // a row, so the scene holds something
    r.h.tap(btn::F3);      // track 3 off
    r.store(0);            // whole scene 1 on A: rows + toggles
    CHECK(a.project.sceneHasFx(0));
    CHECK_EQ(a.project.perfSceneFxOff[0], 0x0004);
    r.h.tap(btn::F3);      // track 3 on again by hand
    CHECK(p.fxOn[2]);
    // Putting the scene on A applies its toggles.
    p.setSceneOn(0, 0);
    CHECK(!p.fxOn[2]);
    CHECK_EQ(a.project.perfFxOff, 0x0004);
    // A per-column store (pad held) leaves the scene's toggles alone.
    r.h.tap(btn::F3);      // on
    r.h.push(InputEvent::kPadDown, 12 * 2 + 1);
    r.h.run(1);
    r.store(0);
    r.h.push(InputEvent::kPadUp, 12 * 2 + 1);
    CHECK_EQ(a.project.perfSceneFxOff[0], 0x0004);
    // Clearing the whole scene puts it back to "-".
    r.clear(0);
    CHECK(!a.project.sceneHasFx(0));
    p.setSceneOn(0, 0);
    CHECK(p.fxOn[2]);      // "-": the toggles stay as they are
}

TEST(perf_fx_toggles_switch_at_fader_middle) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    r.h.pad(12 * 1 + 0);
    r.h.tap(btn::F3);      // track 3 off
    r.store(0);            // A: track 3 off
    r.h.tap(btn::F3);      // on
    r.h.pad(12 * 2 + 0);
    r.store(1);            // B: all on
    CHECK_EQ(a.project.perfSceneFxOff[1], 0);
    CHECK(a.project.sceneHasFx(1));
    r.fade(40);            // A side: A's toggles
    CHECK(!p.fxOn[2]);
    r.fade(60);            // past the middle: B's
    CHECK(p.fxOn[2]);
    r.fade(30);
    CHECK(!p.fxOn[2]);
    // A key tapped meanwhile holds until the fader changes sides.
    r.h.tap(btn::F3);
    CHECK(p.fxOn[2]);
    r.fade(20);
    CHECK(p.fxOn[2]);
    r.fade(70);
    CHECK(p.fxOn[2]);      // B: all on
    r.fade(10);
    CHECK(!p.fxOn[2]);     // A again
    // B back to "-": crossing the middle leaves the toggles.
    p.clearSceneFx(1);
    r.fade(80);
    CHECK(!p.fxOn[2]);
}

TEST(perf_fx_toggles_song_slot_applies_scene) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    r.h.pad(12 * 1 + 0);
    r.h.tap(btn::F5);      // track 5 off
    r.store(0);            // scene 1
    r.h.tap(btn::F5);      // on by hand
    CHECK(p.fxOn[4]);
    a.project.playlistScene[0] = 1;   // slot 1 plays scene 1 on A
    p.perfSongSlot(0);
    CHECK(!p.fxOn[4]);
    r.h.tap(btn::F5);      // on by hand again
    p.perfSongSlot(0);     // the next slot start with the same scene: applied again
    CHECK(!p.fxOn[4]);
}

// Editing a scene (bank page, F3 Edit): the F keys edit that scene's
// toggles; leaving saves them and puts the live toggles back (user report:
// the scene on B kept its old toggles after being edited).
TEST(perf_fx_toggles_edit_scene) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    r.h.pad(12 * 1 + 0);
    r.store(0);            // scene 1 on A: all on
    r.h.pad(12 * 2 + 0);
    r.store(1);            // scene 2 on B: all on
    r.h.tap(btn::Performance);   // bank page
    r.h.tap(btn::Right);         // scene 2
    r.h.tap(btn::F3);            // Edit
    CHECK(p.view == PerformanceScreen::View::EditScene);
    r.h.tap(btn::F4);            // track 4 off in scene 2
    CHECK(!p.fxOn[3]);
    r.h.tap(btn::Performance);   // back to the bank: saved
    CHECK(a.project.sceneHasFx(1));
    CHECK_EQ(a.project.perfSceneFxOff[1], 0x0008);
    CHECK(p.fxOn[3]);            // the live toggles are back
    CHECK_EQ(a.project.perfFxOff, 0);
    r.h.tap(btn::F8);            // playing
    r.fade(70);                  // B: scene 2's edited toggles
    CHECK(!p.fxOn[3]);
    r.fade(20);                  // A: all on
    CHECK(p.fxOn[3]);
    // Editing a scene without toggles and not touching the F keys keeps "-".
    p.clearSceneFx(0);
    r.h.tap(btn::Performance);
    r.h.tap(btn::Left);          // scene 1
    r.h.tap(btn::F3);
    r.h.tap(btn::Performance);
    CHECK(!a.project.sceneHasFx(0));
}
