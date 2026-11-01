// [I] Performance scene per song slot (#10): song.bin fields 1-3, the Song
// screen's Scene and Ramp columns, the scene going on as the slot starts,
// on any screen (PerformanceScreen::KeepAlive::Song), and the A -> B ramp.
#include <cstring>
#include <memory>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "formats/file_io.h"
#include "formats/song_side.h"
#include "ui/screens.h"

using namespace ot;
using KeepAlive = PerformanceScreen::KeepAlive;

TEST(song_scene_side_round_trip) {
    Project p;
    p.playlist[0] = 1;
    p.playlist[1] = 2;
    CHECK(!songSideUsed(p));
    p.playlistScene[1] = 3;
    p.playlistSceneTo[1] = 16;
    p.playlistRamp[1] = 255;
    CHECK(songSideUsed(p));   // a scene alone writes the file
    uint8_t buf[song::kFileSize];
    encodeSongSide(p, buf);
    Project q;
    CHECK(decodeSongSide(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.slotScene(1), 3);
    CHECK_EQ(q.slotSceneTo(1), 16);
    CHECK_EQ(q.slotRamp(1), 255);
    CHECK_EQ(q.slotRamp(0), Project::kRampSlot);
    CHECK_EQ(q.slotScene(0), 0);
    CHECK_EQ(q.slotCount(1), 1);
    // B without A reads as none.
    q.playlistScene[1] = 0;
    CHECK_EQ(q.slotSceneTo(1), 0);
    // A file with the count field only (fields = 1): no scenes.
    encodeSongSide(p, buf);
    buf[6] = 1;
    CHECK(decodeSongSide(buf, song::kHeaderSize + kSongSlots + 1, q) == FmtStatus::Ok);
    CHECK_EQ(q.slotScene(1), 0);
    // Out of range: none.
    encodeSongSide(p, buf);
    uint8_t* f1 = buf + song::kHeaderSize + (kSongSlots + 1);
    f1[1] = 17;
    f1[kSongSlots] = uint8_t(f1[kSongSlots] + 14);
    CHECK(decodeSongSide(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.slotScene(1), 0);
    // Saved and loaded next to project.mt.
    Harness h;
    h.fs.mkdir("P");
    CHECK(saveProjectFile(h.fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(h.fs.exists("P/song.bin"));
    Project r;
    CHECK(loadProjectFile(h.fs, "P/project.mt", r, nullptr) == FmtStatus::Ok);
    CHECK_EQ(r.slotScene(1), 3);
    CHECK_EQ(r.slotSceneTo(1), 16);
}

TEST(song_scene_column_keys) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Song);
    SongScreen& s = *static_cast<SongScreen*>(a.ui.screen(kModSong));
    h.tap(btn::Rec);   // edit
    h.tap(btn::Right);
    h.tap(btn::Right);   // list -> Count -> Scene
    CHECK_EQ(s.focus, 4);
    h.down(btn::Shift);
    h.enc(2);   // no scene on A yet: B stays none
    h.up(btn::Shift);
    CHECK_EQ(a.project.slotSceneTo(0), 0);
    h.enc(3);
    CHECK_EQ(a.project.slotScene(0), 3);
    h.enc(30);
    CHECK_EQ(a.project.slotScene(0), Project::kPerfScenes);
    h.enc(-14);
    CHECK_EQ(a.project.slotScene(0), 2);
    h.down(btn::Shift);
    h.enc(5);
    h.up(btn::Shift);
    CHECK_EQ(a.project.slotSceneTo(0), 5);
    h.run(100);
    CHECK(h.disp.lastHasText("Scene"));
    CHECK(h.disp.lastHasText("2>5"));
    // Add Slot copies the scenes; Delete Slot moves them up.
    h.tap(btn::F4);
    CHECK_EQ(a.project.slotScene(1), 2);
    CHECK_EQ(a.project.slotSceneTo(1), 5);
    h.tap(btn::Delete);   // in the Scene column: back to keep
    CHECK_EQ(a.project.slotScene(1), 0);
    CHECK_EQ(a.project.slotSceneTo(1), 0);
    h.run(100);
    CHECK(h.disp.lastHasText("-"));
    h.tap(btn::Up);
    h.tap(btn::Left);
    h.tap(btn::Left);   // Pattern list
    h.tap(btn::F5);     // delete slot 1: slot 2 (keep) moves up
    CHECK_EQ(a.project.slotScene(0), 0);
    CHECK_EQ(a.project.slotScene(1), 0);
    // Out of edit mode the encoder does not change the scene.
    a.project.playlistScene[0] = 4;
    h.tap(btn::Right);
    h.tap(btn::Right);
    h.tap(btn::Rec);
    h.enc(1);
    CHECK_EQ(a.project.slotScene(0), 4);
}

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
        // Slot 0 = Volume: rows 1 / 2 = -50 / +50. Scenes 3 (row 1), 6 (row 2).
        h.tap(btn::Performance);
        p().src.setValue(0, 1, -50);
        p().src.setValue(0, 2, 50);
        pj().perfScene[2][0] = 1;
        pj().perfScene[5][0] = 2;
        h.tap(btn::Song);   // stopped: the FX revert
        // Song: pattern 1 (scene 3 > 6), pattern 1 (keep), pattern 1 (scene 6).
        pj().playlist[0] = pj().playlist[1] = pj().playlist[2] = 1;
        pj().playlist[3] = 0;
        pj().playlistScene[0] = 3;
        pj().playlistSceneTo[0] = 6;
        pj().playlistScene[2] = 6;
        pj().setTempo(400.0f);
    }
    Project& pj() { return h.app->project; }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void untilSlot(int slot) {
        for (int i = 0; i < 20000 && h.app->seq.songPosition() != slot; ++i) h.audio.pump(1);
        h.run(20);
    }
};
}  // namespace

TEST(song_scene_goes_on_with_its_slot_on_any_screen) {
    Rig r;
    App& a = *r.h.app;
    PerformanceScreen& p = r.p();
    CHECK(p.keepAlive() == KeepAlive::None);
    CHECK(!a.seq.performanceMode());
    r.h.tap(btn::F2);   // Play Song
    r.h.run(20);
    CHECK(a.seq.isSongMode());
    CHECK(p.keepAlive() == KeepAlive::Song);
    CHECK(a.seq.performanceMode());
    CHECK_EQ(r.pj().perfSceneOn[0], 2);   // scene 3 on A
    CHECK_EQ(r.pj().perfSceneOn[1], 5);   // scene 6 on B
    CHECK_EQ(p.fader, 0);
    CHECK_EQ(p.cur[0], -50);
    // Slot 2 keeps nothing on other screens: the song's FX go.
    r.untilSlot(1);
    CHECK(p.keepAlive() == KeepAlive::None);
    CHECK(!a.seq.performanceMode());
    // Slot 3: scene 6 on A (B stays).
    r.untilSlot(2);
    CHECK(p.keepAlive() == KeepAlive::Song);
    CHECK_EQ(r.pj().perfSceneOn[0], 5);
    CHECK_EQ(p.cur[0], 50);
    // Stop reverts.
    r.h.tap(btn::Play);
    r.h.run(20);
    CHECK(!a.seq.isPlaying());
    CHECK(p.keepAlive() == KeepAlive::None);
    CHECK(!a.seq.performanceMode());
}

TEST(song_scene_on_the_performance_screen) {
    Rig r;
    App& a = *r.h.app;
    PerformanceScreen& p = r.p();
    r.h.tap(btn::F2);   // Play Song (slot 1: scene 3)
    r.h.run(20);
    r.h.tap(btn::Performance);   // resumes the song's FX
    CHECK(a.ui.active() == kModPerformance);
    CHECK(p.keepAlive() == KeepAlive::None);
    CHECK_EQ(p.cur[0], -50);
    // A slot without a scene keeps what plays here.
    r.untilSlot(1);
    CHECK_EQ(p.cur[0], -50);
    r.untilSlot(2);
    CHECK_EQ(p.cur[0], 50);
    // Leaving during a slot with a scene: it plays on (Song, not until the
    // pattern end).
    r.h.tap(btn::Song);
    CHECK(p.keepAlive() == KeepAlive::Song);
    r.h.tap(btn::Play);
    r.h.run(20);
    CHECK(p.keepAlive() == KeepAlive::None);
}

TEST(song_scene_ramp_column_keys) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Song);
    SongScreen& s = *static_cast<SongScreen*>(a.ui.screen(kModSong));
    a.project.playlistScene[0] = 2;
    a.project.playlistSceneTo[0] = 5;
    h.tap(btn::Rec);
    for (int i = 0; i < 3; ++i) h.tap(btn::Right);   // Count, Scene, Ramp
    CHECK_EQ(s.focus, 5);
    h.run(100);
    CHECK(h.disp.lastHasText("Ramp"));
    CHECK(h.disp.lastHasText("Slot"));
    const char* const names[] = {"Each", "First", "Last"};
    for (int i = 0; i < 3; ++i) {
        h.enc(1);
        CHECK_EQ(a.project.slotRamp(0), Project::kRampEach + i);
        h.run(100);
        CHECK(h.disp.lastHasText(names[i]));
    }
    CHECK_EQ(a.project.slotRamp(0), Project::kRampLast);
    h.enc(1);   // count 1: no Last 2, so 1 step
    CHECK_EQ(Project::rampSteps(a.project.slotRamp(0)), 1);
    h.down(btn::Shift);
    h.enc(1);   // + 16 steps
    h.up(btn::Shift);
    CHECK_EQ(Project::rampSteps(a.project.slotRamp(0)), 17);
    h.run(100);
    CHECK(h.disp.lastHasText("17"));
    h.down(btn::Shift);
    h.enc(20);
    h.up(btn::Shift);
    CHECK_EQ(a.project.slotRamp(0), Project::kRampMax);   // a pattern's steps at most
    CHECK_EQ(Project::rampSteps(Project::kRampMax), kStepCount);
    for (int i = 0; i < 3; ++i) h.enc(-100);
    CHECK_EQ(a.project.slotRamp(0), Project::kRampSlot);
    h.enc(3);
    h.tap(btn::F4);   // Add Slot copies the ramp
    CHECK_EQ(a.project.slotRamp(1), 3);
    h.tap(btn::Delete);   // back to Slot
    CHECK_EQ(a.project.slotRamp(1), Project::kRampSlot);
}

TEST(song_scene_ramp_order_follows_count) {
    using S = SongScreen;
    // Count 4: Slot, Each, First, Last, Last2, Last3, then steps.
    int v = Project::kRampSlot;
    const int want[] = {Project::kRampEach, Project::kRampFirst, Project::kRampLast, Project::kRampLast + 1,
                        Project::kRampLast + 2, Project::kRampSteps + 1};
    for (int w : want) {
        v = S::rampAfter(v, 4, 1);
        CHECK_EQ(v, w);
    }
    CHECK_EQ(S::rampAfter(v, 4, -1), Project::kRampLast + 2);
    // Count 1 and 2: Last only.
    CHECK_EQ(S::rampAfter(Project::kRampLast, 1, 1), Project::kRampSteps + 1);
    CHECK_EQ(S::rampAfter(Project::kRampLast, 2, 1), Project::kRampSteps + 1);
    // A Last N longer than the count moves from the longest that fits.
    CHECK_EQ(S::rampAfter(Project::kRampLast + 5, 3, -1), Project::kRampLast);
    CHECK_EQ(S::rampAfter(Project::kRampLast + 5, 3, 1), Project::kRampSteps + 1);
    // Clamped at both ends.
    CHECK_EQ(S::rampAfter(Project::kRampSlot, 4, -3), Project::kRampSlot);
    CHECK_EQ(S::rampAfter(Project::kRampMax, 4, 5), Project::kRampMax);
}

TEST(song_scene_ramp_fader) {
    using P = PerformanceScreen;
    // Two passes of 16 steps.
    CHECK_EQ(P::rampFader(Project::kRampSlot, 2, 1, 16, 0, 1), 0);
    CHECK_EQ(P::rampFader(Project::kRampSlot, 2, 1, 16, 8, 1), 25);
    CHECK_EQ(P::rampFader(Project::kRampSlot, 2, 2, 16, 0, 1), 50);
    CHECK_EQ(P::rampFader(Project::kRampSlot, 2, 2, 16, 15, 48), 99);
    // The last pass only.
    CHECK_EQ(P::rampFader(Project::kRampLast, 2, 1, 16, 15, 48), 0);
    CHECK_EQ(P::rampFader(Project::kRampLast, 2, 2, 16, 8, 1), 50);
    // The first pass only, then B.
    CHECK_EQ(P::rampFader(Project::kRampFirst, 2, 1, 16, 0, 1), 0);
    CHECK_EQ(P::rampFader(Project::kRampFirst, 2, 1, 16, 8, 1), 50);
    CHECK_EQ(P::rampFader(Project::kRampFirst, 2, 2, 16, 0, 1), 100);
    CHECK_EQ(P::rampFader(Project::kRampFirst, 2, 2, 16, 8, 1), 100);
    // Each pass, back to A as each starts.
    CHECK_EQ(P::rampFader(Project::kRampEach, 2, 1, 16, 8, 1), 50);
    CHECK_EQ(P::rampFader(Project::kRampEach, 2, 2, 16, 0, 1), 0);
    CHECK_EQ(P::rampFader(Project::kRampEach, 2, 2, 16, 8, 1), 50);
    CHECK_EQ(P::rampFader(Project::kRampEach, 2, 2, 16, 15, 48), 99);
    // The last 2 of 4 passes; Last N of a shorter slot = all of it.
    const int l2 = Project::kRampLast + 1;
    CHECK_EQ(Project::rampLastPasses(l2), 2);
    CHECK_EQ(P::rampFader(l2, 4, 2, 16, 15, 48), 0);
    CHECK_EQ(P::rampFader(l2, 4, 3, 16, 0, 1), 0);
    CHECK_EQ(P::rampFader(l2, 4, 4, 16, 0, 1), 50);
    CHECK_EQ(P::rampFader(l2, 2, 2, 16, 0, 1), 50);
    // The last 4 steps (value kRampSteps + 4); between steps it moves by micro step.
    const int s4 = Project::kRampSteps + 4;
    CHECK_EQ(P::rampFader(s4, 2, 2, 16, 11, 48), 0);
    CHECK_EQ(P::rampFader(s4, 2, 2, 16, 12, 1), 0);
    CHECK_EQ(P::rampFader(s4, 2, 2, 16, 14, 1), 50);
    CHECK_EQ(P::rampFader(s4, 2, 2, 16, 14, 25), 62);
    // Longer than the slot: the whole slot.
    CHECK_EQ(P::rampFader(200, 2, 2, 16, 0, 1), 50);
}

TEST(song_scene_ramps_and_the_fader_takes_over) {
    Rig r;
    App& a = *r.h.app;
    PerformanceScreen& p = r.p();
    r.pj().playlistCount[0] = 2;   // slot 1: scene 3 > 6 over both passes (Slot)
    r.h.tap(btn::F2);              // Play Song, on the Song screen
    r.h.run(20);
    CHECK(p.fader < 10);
    CHECK(p.cur[0] < -40);
    for (int i = 0; i < 20000 && a.seq.songPass() < 2; ++i) r.h.audio.pump(1);
    r.h.run(20);
    CHECK(p.fader >= 45 && p.fader <= 60);   // half way
    CHECK(p.cur[0] > -10 && p.cur[0] < 20);   // the Volume blend follows
    // Near the end of the slot the fader is near B.
    const int len = a.seq.patternLength();
    for (int i = 0; i < 20000 && a.seq.patternPosition() < len - 1; ++i) r.h.audio.pump(1);
    r.h.run(20);
    CHECK(p.fader >= 90);
    CHECK(p.cur[0] > 40);
    // Next time round, moving the fader on the Performance screen stops the ramp.
    r.untilSlot(1);
    r.untilSlot(0);
    r.h.tap(btn::Performance);
    CHECK(p.fader < 15);
    r.h.enc(-60);   // back to A
    CHECK_EQ(p.fader, 0);
    for (int i = 0; i < 20000 && a.seq.songPass() < 2; ++i) r.h.audio.pump(1);
    r.h.run(20);
    CHECK_EQ(p.fader, 0);
}

// [I] #12 a song slot's ramp moves the same fader: a column with an
// interval moves only inside it.
TEST(song_scene_ramp_follows_fader_ranges) {
    Rig r;
    App& a = *r.h.app;
    PerformanceScreen& p = r.p();
    r.pj().setRange(0, 50, 100);   // Volume: the second half of the ramp
    r.h.tap(btn::F2);              // Play Song
    auto until = [&](int x) {
        for (int i = 0; i < 400 && p.fader < x; ++i) {
            r.h.audio.pump(50);
            r.h.run(1);
        }
    };
    until(40);
    CHECK(p.fader >= 40 && p.fader < 50);
    CHECK_EQ(p.cur[0], -50);       // the ramp is not in Volume's interval yet
    until(75);
    CHECK(p.fader >= 75 && p.fader < 85);
    CHECK(p.cur[0] > -10 && p.cur[0] < 30);   // half way through its interval
    CHECK(a.seq.songPosition() == 0);
}

// [I] A slot's scene with FX toggles applies while the Song screen shows;
// the Performance labels must not land on the Song screen's bar (device
// report: the bar flickered to "Track 1 Patt: 1" at each slot start).
TEST(song_scene_keeps_the_song_bar) {
    Rig r;
    App& a = *r.h.app;
    r.pj().perfSceneFxOff[5] = 1;   // scene 6 (slot 3) carries FX toggles
    r.pj().perfSceneFxSet = 1u << 5;
    r.h.tap(btn::F2);   // Play Song
    r.h.run(20);
    CHECK(a.seq.isSongMode());
    CHECK_EQ(a.ui.active(), kModSong);
    // Step the app loop across the start of slot 3, checking the bar each time.
    bool perfLabels = false, reached = false;
    for (int i = 0; i < 20000 && !reached; ++i) {
        r.h.audio.pump(1);
        a.loop();
        reached = a.seq.songPosition() == 2;
        perfLabels = perfLabels || std::strstr(a.ui.bar.label[0].bottom, "Patt") != nullptr;
    }
    CHECK(reached);
    CHECK(!perfLabels);
}
