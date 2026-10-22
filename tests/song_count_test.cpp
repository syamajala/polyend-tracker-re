// [I] Song repeat counts (Project::playlistCount): song.bin, its save / load
// next to project.mt, and the Song screen's Count column.
#include <vector>

#include "test.h"
#include "ui_fakes.h"

#include "formats/file_io.h"
#include "formats/song_side.h"
#include "ui/screens.h"

using namespace ot;

TEST(song_side_round_trip) {
    Project p;
    p.playlist[0] = 1;
    p.playlist[1] = 2;
    p.playlist[2] = 0;
    CHECK(!songSideUsed(p));
    p.playlistCount[1] = 4;
    CHECK(songSideUsed(p));
    p.playlistCount[5] = 9;   // past the end of the song: not "used" on its own
    uint8_t buf[song::kFileSize];
    encodeSongSide(p, buf);
    Project q;
    CHECK(decodeSongSide(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.slotCount(1), 4);
    CHECK_EQ(q.slotCount(0), 1);
    CHECK_EQ(q.slotCount(5), 9);
    // Damaged: every slot plays once.
    buf[song::kHeaderSize + 1] ^= 1;
    CHECK(decodeSongSide(buf, sizeof(buf), q) == FmtStatus::BadMagic);
    CHECK_EQ(q.slotCount(1), 1);
    CHECK(decodeSongSide(buf, 4, q) == FmtStatus::TooShort);
    // Out-of-range values read as once.
    encodeSongSide(p, buf);
    buf[song::kHeaderSize + 1] = 200;
    buf[song::kHeaderSize + kSongSlots] = uint8_t(buf[song::kHeaderSize + kSongSlots] + 196);
    CHECK(decodeSongSide(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.slotCount(1), 1);
}

TEST(song_side_saved_with_the_project) {
    Harness h;
    Project p;
    p.playlist[0] = 1;
    p.playlist[1] = 2;
    p.playlistCount[1] = 3;
    h.fs.mkdir("P");
    CHECK(saveProjectFile(h.fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(h.fs.exists("P/song.bin"));
    Project q;
    CHECK(loadProjectFile(h.fs, "P/project.mt", q, nullptr) == FmtStatus::Ok);
    CHECK_EQ(q.slotCount(1), 3);
    // Back to once everywhere: the file goes (stock file set).
    p.playlistCount[1] = 1;
    CHECK(saveProjectFile(h.fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(!h.fs.exists("P/song.bin"));
    // A project without song.bin loads with every slot once.
    q.playlistCount[1] = 5;
    CHECK(loadProjectFile(h.fs, "P/project.mt", q, nullptr) == FmtStatus::Ok);
    CHECK_EQ(q.slotCount(1), 1);
}

TEST(song_screen_count_column) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Song);
    SongScreen& s = *static_cast<SongScreen*>(a.ui.screen(kModSong));
    h.tap(btn::Rec);   // edit
    h.tap(btn::F4);    // slot 2 (copy of slot 1)
    h.tap(btn::F4);    // slot 3
    CHECK_EQ(s.cursor, 2);
    h.tap(btn::Up);
    CHECK_EQ(s.focus, 1);
    h.tap(btn::Right);   // list -> Count
    CHECK_EQ(s.focus, 3);
    h.enc(3);
    CHECK_EQ(a.project.slotCount(1), 4);
    h.enc(20);
    CHECK_EQ(a.project.slotCount(1), 16);   // max
    h.enc(-13);
    CHECK_EQ(a.project.slotCount(1), 3);
    h.run(100);
    CHECK(h.disp.lastHasText("Count"));
    CHECK(h.disp.lastHasText("x3"));
    // Add Slot copies the count; Shift + Add (empty slot) starts at 1.
    h.tap(btn::F4);
    CHECK_EQ(s.cursor, 2);
    CHECK_EQ(a.project.slotCount(2), 3);
    h.down(btn::Shift);
    h.tap(btn::F4);
    h.up(btn::Shift);
    CHECK_EQ(a.project.slotCount(3), 1);
    // Delete Slot moves the counts up with their slots.
    h.tap(btn::Up);
    h.tap(btn::Up);   // slot 2 (x3)
    h.tap(btn::F5);
    CHECK_EQ(a.project.slotCount(1), 3);   // the copy moved up
    CHECK_EQ(a.project.slotCount(2), 1);
    CHECK_EQ(a.project.slotCount(4), 1);   // cleared past the end
    // Delete in the Count column: back to once.
    h.tap(btn::Up);
    CHECK_EQ(s.cursor, 0);
    h.tap(btn::Down);
    h.tap(btn::Delete);
    CHECK_EQ(a.project.slotCount(1), 1);
    // Right goes on to Tempo; out of edit mode Count is skipped.
    h.tap(btn::Right);
    CHECK_EQ(s.focus, 2);
    h.tap(btn::Left);
    CHECK_EQ(s.focus, 3);
    h.tap(btn::Rec);   // edit off: leaves the Count column
    CHECK_EQ(s.focus, 1);
    h.tap(btn::Right);
    CHECK_EQ(s.focus, 2);
    h.tap(btn::Left);
    CHECK_EQ(s.focus, 1);
}

TEST(song_screen_shows_the_pass) {
    Harness h;
    App& a = *h.app;
    a.project.playlist[0] = 1;
    a.project.playlist[1] = 0;
    a.project.playlistCount[0] = 4;
    a.project.setTempo(300.0f);
    h.tap(btn::Song);
    h.tap(btn::F2);   // Play Song
    CHECK(a.seq.isSongMode());
    h.run(300);
    CHECK(h.disp.lastHasText("1/4"));
    // The next pass of the same slot redraws the column on its own.
    for (int i = 0; i < 6000 && a.seq.songPass() < 2; ++i) h.audio.pump(1);
    CHECK_EQ(a.seq.songPass(), 2);
    h.run(200);
    CHECK(h.disp.lastHasText("2/4"));
}
