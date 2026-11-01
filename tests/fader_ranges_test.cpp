// [I] #12 Fader ranges: each Performance column's interval on the scene
// crossfader (Project::perfRange), perf_scenes.bin v6, and the Fader
// Ranges page (bank page F7). The default (the whole fader) blends as
// before; a column with an interval moves from A to B only inside it.
#include <cstring>
#include <memory>
#include <string>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"
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
    }
    App& a() { return *h.app; }
    Project& pj() { return h.app->project; }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void store(int scene) {
        h.down(btn::Shift);
        h.tap(scene ? btn::Right : btn::Left);
        h.up(btn::Shift);
    }
    void fade(int x) { h.enc(int8_t((x - p().fader) / PerformanceScreen::kFaderStep)); }
    // The build-up: slot 0 Volume -50 (A) -> +50 (B), slot 6 Delay send
    // 0 -> 60 (B only), slot 10 Direction ">>>" -> "<<<" (B only, stepped).
    void buildUp() {
        PerformanceScreen& s = p();
        CHECK_EQ(s.src.type(0), perf::kVolume);
        CHECK_EQ(s.src.type(6), perf::kDelaySend);
        CHECK_EQ(s.src.type(10), perf::kDirection);
        s.src.setValue(0, 1, -50);
        s.src.setValue(0, 2, 50);
        s.src.setValue(6, 1, 60);
        s.src.setValue(10, 1, 1);
        h.pad(12 + 0);
        store(0);
        h.pad(12 * 2 + 0);
        h.pad(12 + 6);
        h.pad(12 + 10);
        store(1);
    }
};

}  // namespace

TEST(fader_ranges_default_is_the_whole_fader) {
    Project p;
    for (int c = 0; c < Project::kPerfSceneSlots; ++c) {
        CHECK_EQ(p.rangeStart(c), 0);
        CHECK_EQ(p.rangeEnd(c), 100);
    }
    CHECK(!perfScenesUsed(p));   // no file for a plain project
    Rig r;
    r.buildUp();
    PerformanceScreen& s = r.p();
    // As before #12: Volume and Delay send blend over the whole fader, the
    // stepped Direction switches at the middle.
    r.fade(24);
    CHECK_EQ(s.cur[0], -26);
    CHECK_EQ(s.cur[6], 14);
    CHECK_EQ(s.cur[10], 0);
    r.fade(50);
    CHECK_EQ(s.cur[0], 0);
    CHECK_EQ(s.cur[6], 30);
    CHECK_EQ(s.cur[10], 1);
    for (int c = 0; c < perf::kSlots; ++c) CHECK_EQ(s.columnFader(c), 50);
}

TEST(fader_ranges_stage_the_columns) {
    Rig r;
    r.buildUp();
    PerformanceScreen& s = r.p();
    r.pj().setRange(0, 0, 50);    // Volume first
    r.pj().setRange(6, 50, 100);  // then the delay
    r.pj().setRange(10, 76, 76);  // Direction switches at 76
    r.fade(24);
    CHECK_EQ(s.cur[0], -2);       // half way through its interval
    CHECK_EQ(s.cur[6], 0);
    CHECK_EQ(s.cur[10], 0);
    CHECK_EQ(r.a().engine.voice(0).perf().volume, -2);
    r.fade(50);
    CHECK_EQ(s.cur[0], 50);       // done: holds B
    CHECK_EQ(s.cur[6], 0);
    r.fade(74);
    CHECK_EQ(s.cur[0], 50);
    CHECK_EQ(s.cur[6], 29);       // 60 * 24 / 50
    CHECK_EQ(s.cur[10], 0);
    r.fade(76);
    CHECK_EQ(s.cur[10], 1);
    CHECK(r.a().engine.voice(0).perf().reverse);
    r.fade(100);
    CHECK_EQ(s.cur[6], 60);
    // Back down undoes it in reverse.
    r.fade(30);
    CHECK_EQ(s.cur[10], 0);
    CHECK_EQ(s.cur[6], 0);
    CHECK_EQ(s.cur[0], 10);
    // The scene shares (marks, pad lights) follow the interval.
    CHECK_EQ(s.sceneShare(1, 0), 60);
    CHECK_EQ(s.sceneShare(1, 6), 0);
}

TEST(fader_ranges_pair_in_two_slots_uses_scene_a_slot) {
    Rig r;
    PerformanceScreen& s = r.p();
    s.src.setValue(0, 1, 40);          // A: Volume in slot 0
    s.src.setType(5, perf::kVolume);   // B: Volume in slot 5
    s.src.setValue(5, 1, -40);
    r.h.pad(12 + 0);
    r.store(0);
    r.h.pad(12 + 5);
    r.store(1);
    r.pj().setRange(0, 50, 100);       // A's slot: the second half
    r.pj().setRange(5, 0, 10);         // B's slot's own interval is not used
    CHECK_EQ(s.rangeSlot(5), 0);
    r.fade(40);
    CHECK_EQ(s.cur[0], 40);
    CHECK_EQ(s.cur[5], 0);
    r.fade(100);
    CHECK_EQ(s.cur[5], -40);
}

TEST(fader_ranges_codec) {
    Project p;
    p.setRange(3, 20, 60);
    CHECK(perfScenesUsed(p));
    uint8_t buf[pscn::kFileSize];
    encodePerfScenes(p, buf);
    CHECK_EQ(buf[4], 6);
    Project q;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.rangeStart(3), 20);
    CHECK_EQ(q.rangeEnd(3), 60);
    CHECK_EQ(q.rangeEnd(4), 100);
    // A version 5 file (no intervals): the whole fader.
    buf[4] = 5;
    Project v5;
    v5.setRange(3, 1, 2);
    CHECK(decodePerfScenes(buf, pscn::kTargetsEnd, v5) == FmtStatus::Ok);
    CHECK_EQ(v5.rangeStart(3), 0);
    CHECK_EQ(v5.rangeEnd(3), 100);
    // A damaged block is ignored.
    buf[4] = 6;
    buf[pscn::kTargetsEnd + 6] ^= 1;
    Project d;
    CHECK(decodePerfScenes(buf, sizeof(buf), d) == FmtStatus::Ok);
    CHECK_EQ(d.rangeStart(3), 0);
}

TEST(fader_ranges_page_keys) {
    Rig r;
    r.buildUp();
    PerformanceScreen& s = r.p();
    r.h.tap(btn::Performance);   // the bank page
    CHECK(s.view == View::Bank);
    r.h.run(40);
    CHECK(r.h.disp.lastHasText("Ranges"));
    r.h.tap(btn::F7);
    CHECK(s.ranges);
    r.h.run(40);
    CHECK(r.h.disp.lastHasText("Fader Ranges"));
    CHECK(!r.h.disp.lastHasText("[0]-100"));   // no edge toggled on
    CHECK(r.h.disp.lastHasText("0-100"));
    CHECK(r.h.disp.lastHasText("at 50"));     // Direction (stepped)
    // F1 toggles the start on: the encoder moves it; F2 then the end.
    r.h.tap(btn::F1);
    r.h.run(40);
    CHECK(r.h.disp.lastHasText("[0]-100"));
    r.h.enc(10);
    CHECK_EQ(r.pj().rangeStart(0), 10);
    r.h.tap(btn::F2);
    r.h.enc(-20);
    CHECK_EQ(r.pj().rangeEnd(0), 80);
    r.h.enc(-100);
    CHECK_EQ(r.pj().rangeEnd(0), 11);   // stays after the start
    r.h.run(40);
    CHECK(r.h.disp.lastHasText("10-[11]"));
    // F3: back to the whole fader.
    r.h.tap(btn::F3);
    CHECK_EQ(r.pj().rangeStart(0), 0);
    CHECK_EQ(r.pj().rangeEnd(0), 100);
    // Down / a pad pick the column; a stepped column's point moves.
    r.h.tap(btn::Down);
    CHECK_EQ(s.rangeCursor, 1);
    r.h.pad(12 * 3 + 10);
    CHECK_EQ(s.rangeCursor, 10);
    CHECK(s.ranges);
    r.h.enc(16);
    CHECK_EQ(r.pj().rangeStart(10), 66);
    CHECK_EQ(r.pj().rangeEnd(10), 66);
    // F2 again toggles the end off: the encoder moves the fader, heard live.
    r.h.tap(btn::F2);
    CHECK_EQ(s.rangeEdge, -1);
    for (int i = 0; i < 34; ++i) r.h.enc(1);
    CHECK_EQ(s.fader, 68);
    CHECK_EQ(r.pj().rangeStart(10), 66);   // the switch point stayed
    CHECK_EQ(s.cur[10], 1);
    CHECK_EQ(s.cur[0], 18);
    CHECK(r.a().unsavedChanges());
    // F8: back to the scenes; the Performance key: back to playing.
    r.h.tap(btn::F8);
    CHECK(!s.ranges);
    CHECK(s.view == View::Bank);
    r.h.tap(btn::F7);
    r.h.tap(btn::Performance);
    CHECK(s.view == View::Play);
    CHECK(!s.ranges);
}

// The page shows what each column plays at the ends: scene A's value left
// of its track, B's right (a scene without the column: "---", no effect).
TEST(fader_ranges_page_shows_scene_values) {
    Rig r;
    r.buildUp();
    r.h.tap(btn::Performance);
    r.h.tap(btn::F7);
    r.h.run(40);
    CHECK(r.h.disp.lastHasText("-50"));   // Volume on A
    CHECK(r.h.disp.lastHasText("+50"));   // Volume on B
    CHECK(r.h.disp.lastHasText("+60"));   // Delay send on B
    CHECK(r.h.disp.lastHasText(">>>"));   // Direction: A plays forward
    CHECK(r.h.disp.lastHasText("<<<"));   // Direction on B
}
