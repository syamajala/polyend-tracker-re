// [I] Performance scenes A / B and the crossfader (Octatrack-style, not in
// stock): storing with Shift+Left / Shift+Right, blending per FX type with
// the encoder or CC 63, stepped FX switching at the middle, filters of
// different types crossing over, perf_scenes.bin next to project.mt;
// clearing with Delete+Left / Delete+Right and the scene marks on the rows.
#include <cstring>
#include <string>

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
        h.app->ui.fragments.enabled = false;   // title text drawn inline every frame
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
    bool frameHas(const char* line) {
        h.app->ui.dirty();
        h.run(40);
        for (const std::string& l : h.disp.last)
            if (l == line) return true;
        return false;
    }
    // Fader to x (0..100) with the encoder (2 per detent).
    void fade(int x) { h.enc(int8_t((x - p().fader) / PerformanceScreen::kFaderStep)); }
    const audio::PerfOffsets& voice(int t) { return h.app->engine.voice(t).perf(); }
};

}  // namespace

TEST(perf_scene_volume_blend_with_encoder) {
    Rig r;
    PerformanceScreen& p = r.p();
    // Slot 0 = Volume (default slot types are FX 1..12).
    p.src.setValue(0, 1, -50);
    p.src.setValue(0, 2, 50);
    r.h.pad(12 * 1 + 0);   // row 1
    r.store(0);
    r.h.pad(12 * 2 + 0);   // row 2
    r.store(1);
    CHECK_EQ(r.a().project.perfScene[0][0], 1);
    CHECK_EQ(r.a().project.perfScene[1][0], 2);
    CHECK_EQ(r.a().project.perfScene[0][1], 0);
    CHECK_EQ(p.fader, 0);
    CHECK_EQ(p.cur[0], 50);   // storing changes nothing until the fader moves
    r.fade(50);
    CHECK_EQ(p.fader, 50);
    CHECK_EQ(p.cur[0], 0);
    CHECK_EQ(r.voice(0).volume, 0);
    r.fade(76);
    CHECK_EQ(p.cur[0], 26);
    r.fade(100);
    CHECK_EQ(p.cur[0], 50);
    CHECK_EQ(r.voice(3).volume, 50);
    r.h.enc(10);   // clamped at B
    CHECK_EQ(p.fader, 100);
    r.fade(0);
    CHECK_EQ(p.cur[0], -50);
    CHECK_EQ(r.voice(0).volume, -50);
    // The title shows the fader once a scene exists.
    r.fade(50);
    r.h.run(40);
    CHECK(r.h.disp.lastHasText("A 1 -----|----- B 2"));   // [I] the scenes on A / B by number
}

TEST(perf_scene_same_type_in_different_slots) {
    Rig r;
    PerformanceScreen& p = r.p();
    p.src.setValue(0, 1, 40);         // A: Volume in slot 0
    p.src.setType(5, perf::kVolume);  // B: Volume in slot 5
    p.src.setValue(5, 1, -40);
    r.h.pad(12 + 0);
    r.store(0);
    r.h.pad(12 + 5);   // resets slot 0 (same type)
    r.store(1);
    CHECK_EQ(r.a().project.perfScene[1][0], 0);
    CHECK_EQ(r.a().project.perfScene[1][5], 1);
    r.fade(24);
    CHECK_EQ(p.cur[0], 21);   // 40 -> -40 at 24 %, in A's slot
    CHECK_EQ(p.cur[5], 0);
    CHECK_EQ(r.voice(0).volume, 21);
    r.fade(76);
    CHECK_EQ(p.cur[0], 0);
    CHECK_EQ(p.cur[5], -21);  // in B's slot past the middle
    CHECK_EQ(r.voice(0).volume, -21);
}

TEST(perf_scene_filters_cross_over) {
    Rig r;
    PerformanceScreen& p = r.p();
    p.src.setValue(3, 1, 60);   // slot 3 Low-pass
    p.src.setValue(4, 1, 80);   // slot 4 High-pass
    r.h.pad(12 + 3);
    r.store(0);
    r.h.pad(12 + 4);            // excludes the low-pass
    r.store(1);
    r.fade(24);
    CHECK_EQ(p.cur[3], 31);     // 60 fading out towards the middle
    CHECK_EQ(p.cur[4], 0);
    CHECK_EQ(r.voice(0).filterType, 0);
    r.fade(50);
    CHECK_EQ(p.cur[3], 0);
    CHECK_EQ(p.cur[4], 0);      // no filter at the middle
    r.fade(74);
    CHECK_EQ(p.cur[3], 0);
    CHECK_EQ(p.cur[4], 38);     // 0 -> 80 over the second half
    CHECK_EQ(r.voice(0).filterType, 1);
    // Same filter type in both scenes: a plain blend.
    p.src.setValue(3, 2, 20);
    r.h.pad(12 * 2 + 3);
    r.store(1);
    r.fade(50);
    CHECK_EQ(p.cur[3], 40);
    CHECK_EQ(p.cur[4], 0);
}

TEST(perf_scene_stepped_fx_switch_at_middle) {
    Rig r;
    PerformanceScreen& p = r.p();
    p.src.setValue(0, 1, 30);   // Volume in both scenes
    p.src.setValue(10, 1, 1);   // Direction "<<<" only in B
    r.h.pad(12 + 0);
    r.store(0);
    r.h.pad(12 + 10);
    r.store(1);
    r.fade(48);
    CHECK_EQ(p.cur[10], 0);
    CHECK(!r.voice(0).reverse);
    r.fade(50);
    CHECK_EQ(p.cur[10], 1);
    CHECK(r.voice(0).reverse);
    r.fade(0);
    CHECK_EQ(p.cur[10], 0);
    CHECK_EQ(p.cur[0], 30);
}

TEST(perf_scene_leaves_other_fx_and_held_pads) {
    Rig r;
    PerformanceScreen& p = r.p();
    p.src.setValue(0, 1, -20);
    p.src.setValue(0, 2, 20);
    p.src.setValue(1, 1, 30);   // Panning: in neither scene
    r.h.pad(12 + 0);
    r.store(0);
    r.h.pad(24 + 0);
    r.store(1);
    r.h.pad(12 + 1);
    CHECK_EQ(p.cur[1], 30);
    r.fade(100);
    CHECK_EQ(p.cur[1], 30);
    CHECK_EQ(r.voice(0).pan, 30);
    // With a pad held the encoder edits that slot, not the fader.
    r.h.push(InputEvent::kPadDown, 12 + 1);
    r.h.enc(5);
    CHECK_EQ(p.fader, 100);
    CHECK_EQ(p.cur[1], 35);
    r.h.push(InputEvent::kPadUp, 12 + 1);
    // Shift+Left with a track key held is still the pattern queue, not a store.
    const uint8_t before = r.a().project.perfScene[0][0];
    r.h.down(btn::F1);
    r.h.down(btn::Shift);
    r.h.tap(btn::Left);
    r.h.up(btn::Shift);
    r.h.up(btn::F1);
    CHECK_EQ(r.a().project.perfScene[0][0], before);
    // Storing with nothing selected clears the scene.
    for (int c = 0; c < 12; ++c) p.src.setRow(c, 0);
    r.store(0);
    for (int c = 0; c < 12; ++c) CHECK_EQ(r.a().project.perfScene[0][c], 0);
}

TEST(perf_scene_cc63_moves_fader) {
    Rig r;
    PerformanceScreen& p = r.p();
    p.src.setValue(0, 1, -100);
    p.src.setValue(0, 2, 100);
    r.h.pad(12 + 0);
    r.store(0);
    r.h.pad(24 + 0);
    r.store(1);
    r.a().handleControlChange(63, 127);
    CHECK_EQ(p.fader, 0);   // latched, applied at the next update
    r.h.run(60);
    CHECK_EQ(p.fader, 100);
    CHECK_EQ(p.cur[0], 100);
    r.a().handleControlChange(63, 64);
    r.h.run(60);
    CHECK_EQ(p.fader, 50);
    r.a().handleControlChange(63, 0);
    r.h.run(60);
    CHECK_EQ(p.cur[0], -100);
    // Only on the Performance screen.
    r.h.tap(btn::Pattern);
    r.a().handleControlChange(63, 127);
    r.h.run(60);
    CHECK_EQ(p.fader, 0);
}

TEST(perf_scenes_file_and_codec) {
    Project p;
    CHECK(!perfScenesUsed(p));
    p.perfScene[0][3] = 2;
    p.perfScene[1][11] = 3;
    CHECK(perfScenesUsed(p));
    uint8_t buf[pscn::kFileSize];
    encodePerfScenes(p, buf);
    CHECK(std::memcmp(buf, "OTPS", 4) == 0);
    Project q;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK(std::memcmp(q.perfScene, p.perfScene, sizeof(p.perfScene)) == 0);
    buf[pscn::kHeaderSize + 3] ^= 1;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::BadMagic);
    CHECK(!perfScenesUsed(q));
    // Next to project.mt, only while used; project.mt itself is unchanged.
    FakeStorage fs;
    fs.mkdir("P");
    Project e;
    CHECK(saveProjectFile(fs, "P/project.mt", e) == FmtStatus::Ok);
    CHECK(!fs.exists("P/perf_scenes.bin"));
    const std::vector<uint8_t> plain = fs.files["P/project.mt"];
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(fs.exists("P/perf_scenes.bin"));
    CHECK(fs.files["P/project.mt"] == plain);
    CHECK(loadProjectFile(fs, "P/project.mt", q) == FmtStatus::Ok);
    CHECK(std::memcmp(q.perfScene, p.perfScene, sizeof(p.perfScene)) == 0);
    CHECK(saveProjectFile(fs, "P/project.mt", e) == FmtStatus::Ok);
    CHECK(!fs.exists("P/perf_scenes.bin"));
}

TEST(perf_scenes_survive_save_and_open) {
    Rig r;
    App& a = r.a();
    a.project.perfScene[1][2] = 3;
    a.markProjectDirty();
    CHECK(a.saveProjectAs("scenes"));
    CHECK(r.h.fs.exists("Projects/scenes/perf_scenes.bin"));
    CHECK(a.newProject());
    CHECK(!perfScenesUsed(a.project));
    CHECK(a.openProject(kProjectsDir, "scenes"));
    CHECK_EQ(a.project.perfScene[1][2], 3);
}

TEST(perf_scene_marks_on_stored_rows) {
    Rig r;
    PerformanceScreen& p = r.p();
    p.src.setValue(0, 1, -50);
    p.src.setValue(0, 2, 50);
    // No scene: no marks (a bar's top-left vertex: panel x 11 + 65 c, + 5
    // for A, + 55 for B; bottom at y 308 + 30 r, 21 px high at full share,
    // 5 px out of the mix -- the fader starts at A).
    CHECK(!r.frameHas("VERTEX 16 318"));
    r.h.pad(12 * 1 + 0);   // slot 0, row 1
    r.store(0);
    CHECK(r.frameHas("VERTEX 16 318"));    // A: left edge of row 1
    CHECK(!r.frameHas("VERTEX 66 318"));   // not in B
    r.h.pad(12 * 2 + 0);   // row 2
    r.store(1);
    CHECK(r.frameHas("VERTEX 16 318"));
    CHECK(r.frameHas("VERTEX 66 364"));    // B: right edge of row 2, minimum height at A
    CHECK(r.frameHas("COLOR 3C8CFF"));     // B is blue (A the theme's red accent)
    // Same row in both: both edges.
    r.h.pad(12 * 1 + 1);   // slot 1, row 1
    r.store(0);
    r.store(1);
    CHECK(r.frameHas("VERTEX 81 318"));
    CHECK(r.frameHas("VERTEX 131 334"));
}

namespace {
std::string gLastNote;
void onNote(const char* t) { gLastNote = t; }
}  // namespace

TEST(perf_scene_clear_restores_pad_rows) {
    Rig r;
    r.a().ui.platform.notified = &onNote;
    PerformanceScreen& p = r.p();
    App& a = r.a();
    // Slot 0 Volume: A = row 1 (-50), B = row 2 (+50); slot 1 is in no scene.
    p.src.setValue(0, 1, -50);
    p.src.setValue(0, 2, 50);
    p.src.setValue(1, 1, 30);
    r.h.pad(12 * 1 + 0);
    r.store(0);
    r.h.pad(12 * 2 + 0);
    r.store(1);
    r.h.pad(12 * 1 + 1);   // slot 1 row 1, after the scenes: in neither
    const int16_t slot1 = p.cur[1];
    r.fade(50);
    CHECK_EQ(p.cur[0], 0);
    // Clear B: A alone blends toward "off" on the B side (lerp -50 .. 0 at 50).
    r.clear(1);
    for (int s = 0; s < perf::kSlots; ++s) CHECK_EQ(a.project.perfScene[1][s], 0);
    CHECK_EQ(a.project.perfScene[0][0], 1);
    CHECK_EQ(p.cur[0], -25);
    CHECK_EQ(r.voice(0).volume, -25);
    CHECK_EQ(p.cur[1], slot1);
    CHECK_STR(gLastNote.c_str(), "Scene 2 cleared (B)");
    // Clear A too: the slot goes back to its pad row (row 2, +50), the
    // fader to A and out of the title bar.
    r.clear(0);
    CHECK(!perfScenesUsed(a.project));
    CHECK_EQ(p.cur[0], 50);
    CHECK_EQ(r.voice(0).volume, 50);
    CHECK_EQ(p.cur[1], slot1);
    CHECK_EQ(p.fader, 0);
    CHECK_STR(gLastNote.c_str(), "Scene 1 cleared (A)");
    r.h.run(40);
    CHECK(!r.h.disp.lastHasText(" B 2"));   // the fader's "A 1 |--- B 2" left the title bar
    // The encoder edits nothing scene-related any more; storing works again.
    r.h.pad(12 * 1 + 0);
    r.store(0);
    CHECK_EQ(a.project.perfScene[0][0], 1);
    // Clearing an empty scene is harmless.
    r.clear(1);
    CHECK_EQ(a.project.perfScene[0][0], 1);
    CHECK_EQ(p.cur[0], -50);
}

TEST(perf_scene_marks_fit_the_display_list) {
    Rig r;
    App& a = r.a();
    r.h.run(40);
    const uint32_t before = a.ui.lastDlWords();
    // Every slot in both scenes, on different rows: 24 bars, the most there can be.
    for (int s = 0; s < perf::kSlots; ++s) {
        a.project.perfScene[0][s] = 1;
        a.project.perfScene[1][s] = 2;
    }
    a.ui.dirty();
    r.h.run(40);
    const uint32_t after = a.ui.lastDlWords();
    std::printf("performance + 24 scene marks: %u -> %u DL words\n", unsigned(before), unsigned(after));
    CHECK(after > before);
    // Two vertices per bar + colour / begin / end per scene colour, and the
    // title-bar fader text that appears with the first scene (~38 words at
    // the CMD_TEXT RAM_DL cost, #71; + 7 for the scene numbers on A / B,
    // scene bank #10).
    CHECK(after - before <= 2 * 24 + 2 * 4 + 40 + 8);
    CHECK(after <= Ui::kDlTargetWords);
}

TEST(perf_scene_marks_follow_the_fader) {
    Rig r;
    App& a = r.a();
    // Slot 0 Volume (blends), slot 3 Low-pass in A and slot 4 High-pass in B
    // (filters of different types: cross over), slot 10 Direction (stepped).
    for (int s : {0, 10}) {
        a.project.perfScene[0][s] = 1;
        a.project.perfScene[1][s] = 2;
    }
    a.project.perfScene[0][3] = 1;
    a.project.perfScene[1][4] = 1;
    // Share 50 of 100: 5 + 16 * 0.5 = 13 px; bottom row 1 y 338, row 2 y 368.
    r.fade(50);
    CHECK(r.frameHas("VERTEX 16 326"));    // Volume A: half
    CHECK(r.frameHas("VERTEX 66 356"));    // Volume B: half
    CHECK(r.frameHas("VERTEX 666 334"));   // Direction A: out (B from the middle)
    CHECK(r.frameHas("VERTEX 716 348"));   // Direction B: full
    CHECK(r.frameHas("VERTEX 211 334"));   // Low-pass A: faded out by the middle
    CHECK(r.frameHas("VERTEX 326 334"));   // High-pass B: not in yet
    r.fade(40);
    CHECK(r.frameHas("VERTEX 666 318"));   // Direction A: full below the middle
    CHECK(r.frameHas("VERTEX 716 364"));
    r.fade(26);
    // Filter cross-over, not a linear blend: Low-pass A at 26 has share
    // 100 - 2 * 26 = 48 -> 13 px (linear would be 74 -> 17 px, top 322).
    CHECK(r.frameHas("VERTEX 211 326"));
    r.fade(76);
    CHECK(r.frameHas("VERTEX 326 326"));   // High-pass B: 2 * 76 - 100 = 52 -> 13 px
    CHECK(r.frameHas("VERTEX 211 334"));
    r.fade(100);
    CHECK(r.frameHas("VERTEX 16 334"));    // Volume A: out
    CHECK(r.frameHas("VERTEX 66 348"));    // Volume B: full
}

// Edit mode: a tapped pad selects its column and row, and the encoder then
// edits that value with nothing held (it used to move the scene fader,
// which edited nothing: device report). Outside edit mode it is the fader.
TEST(perf_edit_mode_encoder_edits_tapped_row) {
    Rig r;
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);       // edit
    CHECK(p.edit);
    r.h.pad(12 * 1 + 1);     // column 1 (Panning), row 1: tap
    CHECK_EQ(p.column, 1);
    CHECK_EQ(p.src.row(1), 1);
    r.h.enc(-20);
    CHECK_EQ(p.src.value(1, 1), -20);
    CHECK_EQ(p.fader, 0);    // the fader did not move
    r.h.enc(5);
    CHECK_EQ(p.src.value(1, 1), -15);
    r.h.pad(12 * 2 + 1);     // row 2
    r.h.enc(7);
    CHECK_EQ(p.src.value(1, 2), 7);
    CHECK_EQ(p.src.value(1, 1), -15);
    r.h.pad(1);              // row 0 (off): nothing to edit
    r.h.enc(9);
    CHECK_EQ(p.src.value(1, 1), -15);
    CHECK_EQ(p.src.value(1, 2), 7);
    // Holding a pad still edits as before.
    r.h.push(InputEvent::kPadDown, 12 * 3 + 1);
    r.h.enc(-4);
    r.h.push(InputEvent::kPadUp, 12 * 3 + 1);
    CHECK_EQ(p.src.value(1, 3), -4);
    // Play mode: nothing held = the scene fader.
    r.h.tap(btn::Rec);
    CHECK(!p.edit);
    r.h.enc(10);
    CHECK_EQ(p.fader, 10 * PerformanceScreen::kFaderStep);
}

// Per-column scenes: a held pad + Shift+Left / Right stores only that
// column into A / B; held + Delete+Left / Right removes only that column.
TEST(perf_scene_store_and_clear_one_column) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    // Column 0 (Volume) row 1 and column 1 (Panning) row 2 latched, then a
    // full snapshot as A.
    r.h.pad(12 * 1 + 0);
    r.h.pad(12 * 2 + 1);
    r.store(0);
    CHECK_EQ(a.project.perfScene[0][0], 1);
    CHECK_EQ(a.project.perfScene[0][1], 2);
    // Hold column 1 row 3 + Shift+Left: only column 1 changes in A.
    r.h.push(InputEvent::kPadDown, 12 * 3 + 1);
    r.h.run(1);
    r.store(0);
    r.h.push(InputEvent::kPadUp, 12 * 3 + 1);
    CHECK_EQ(a.project.perfScene[0][0], 1);
    CHECK_EQ(a.project.perfScene[0][1], 3);
    r.h.run(20);
    CHECK(r.frameHas("Scene 1 (A): Sample panning 0") || r.h.disp.lastHasText("Scene 1 (A): Sample panning"));
    // Hold column 2 row 1 + Shift+Right: B gets only column 2.
    r.h.push(InputEvent::kPadDown, 12 * 1 + 2);
    r.h.run(1);
    r.store(1);
    r.h.push(InputEvent::kPadUp, 12 * 1 + 2);
    for (int s = 0; s < 12; ++s) CHECK_EQ(a.project.perfScene[1][s], s == 2 ? 1 : 0);
    // Hold the off row of column 0 + Shift+Left: column 0 is off in A.
    r.h.push(InputEvent::kPadDown, 0);
    r.h.run(1);
    r.store(0);
    r.h.push(InputEvent::kPadUp, 0);
    CHECK_EQ(a.project.perfScene[0][0], 0);
    CHECK_EQ(a.project.perfScene[0][1], 3);
    // Hold column 1 + Delete+Left: column 1 leaves A, B is untouched.
    r.h.push(InputEvent::kPadDown, 12 * 1 + 1);
    r.h.run(1);
    r.clear(0);
    r.h.push(InputEvent::kPadUp, 12 * 1 + 1);
    CHECK_EQ(a.project.perfScene[0][1], 0);
    CHECK_EQ(a.project.perfScene[1][2], 1);
    // Nothing held: Delete+Right clears all of B, as before.
    r.clear(1);
    for (int s = 0; s < 12; ++s) CHECK_EQ(a.project.perfScene[1][s], 0);
    (void)p;
}

// Scene pad LEDs: a scene-driven column lights its A and B rows, the light
// sliding with the fader; stepped FX switch at the middle; same row = full;
// other columns keep the single dim pad; edit mode shows the plain rows.
TEST(perf_scene_pad_leds) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    // Levels as the LED driver gets them (0..31 -> 0..255, Ui::updateLeds).
    const int dim = a.cfg.padDim() * 255 / 31, full = a.cfg.padFull() * 255 / 31;
    auto led = [&](int pad) {
        r.h.run(2);
        return int(r.h.leds.level[pad]);
    };
    // Column 0 (Volume): A row 1, B row 3. Column 10 (Direction, stepped):
    // A row 1, B row 2. Column 5: same row 2 in both. Column 6: no scene.
    r.h.pad(12 * 1 + 0);
    r.h.pad(12 * 1 + 10);
    r.h.pad(12 * 2 + 5);
    r.h.pad(12 * 1 + 6);
    r.store(0);
    r.h.pad(12 * 3 + 0);
    r.h.pad(12 * 2 + 10);
    r.store(1);
    a.project.perfScene[0][6] = 0;
    a.project.perfScene[1][6] = 0;
    r.fade(0);
    CHECK_EQ(led(12 * 1 + 0), full);   // A
    CHECK_EQ(led(12 * 3 + 0), dim);    // B
    CHECK_EQ(led(12 * 2 + 5), full);   // same row in both
    CHECK_EQ(led(12 * 1 + 6), dim);    // not in a scene: the selected row, dim
    r.fade(50);
    const int mid = led(12 * 1 + 0);
    CHECK(mid > dim && mid < full);
    CHECK_EQ(led(12 * 3 + 0), mid);
    r.fade(48);
    CHECK_EQ(led(12 * 1 + 10), full);  // stepped: still A
    CHECK_EQ(led(12 * 2 + 10), dim);
    r.fade(52);
    CHECK_EQ(led(12 * 1 + 10), dim);   // past the middle: B
    CHECK_EQ(led(12 * 2 + 10), full);
    r.fade(100);
    CHECK_EQ(led(12 * 1 + 0), dim);
    CHECK_EQ(led(12 * 3 + 0), full);
    // Edit mode: the plain selected rows.
    r.h.tap(btn::Rec);
    CHECK(p.edit);
    CHECK_EQ(led(12 * 1 + 0), 0);
    CHECK_EQ(led(12 * 3 + 0), dim);    // the row the B snapshot latched
}

// [I] The green row follows the scenes as the pad lights do (user report: it
// stayed on the last tapped row when switching scenes).
TEST(perf_scene_green_row_follows_the_scenes) {
    Rig r;
    App& a = r.a();
    PerformanceScreen& p = r.p();
    // Scene 1: column 0 row 1, column 1 row 2. Scene 2: column 0 row 3 only.
    r.h.pad(12 * 1 + 0);
    r.h.pad(12 * 2 + 1);
    r.store(0);
    r.h.pad(12 * 3 + 0);
    r.h.pad(12 * 0 + 1);
    r.store(1);
    r.h.pad(12 * 2 + 6);   // column 6 in no scene: the tapped row
    a.project.perfScene[0][6] = a.project.perfScene[1][6] = 0;
    r.fade(0);
    CHECK_EQ(p.shownRow(0), 1);
    CHECK_EQ(p.shownRow(1), 2);
    CHECK_EQ(p.shownRow(6), 2);
    r.fade(100);
    CHECK_EQ(p.shownRow(0), 3);
    CHECK_EQ(p.shownRow(1), 0);   // scene 2 leaves it off
    CHECK_EQ(p.src.row(0), 3);    // the tapped rows are not rewritten
    CHECK_EQ(p.shownRow(6), 2);
    // Another scene on A: the rows follow it.
    r.fade(0);
    r.h.pad(12 * 2 + 0);
    r.store(0);
    a.project.perfScene[2][0] = 1;
    p.setSceneOn(0, 2);
    CHECK_EQ(p.shownRow(0), 1);
    // Edit mode: the selected rows.
    r.h.tap(btn::Rec);
    CHECK_EQ(p.shownRow(0), p.src.row(0));
}
