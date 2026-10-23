// [I] Performance scene per song slot (#10): song.bin fields 1 / 2, the
// Song screen's Scene column, and the scene going on as the slot starts,
// on any screen (PerformanceScreen::KeepAlive::Song).
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
    CHECK(songSideUsed(p));   // a scene alone writes the file
    uint8_t buf[song::kFileSize];
    encodeSongSide(p, buf);
    Project q;
    CHECK(decodeSongSide(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.slotScene(1), 3);
    CHECK_EQ(q.slotSceneTo(1), 16);
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
