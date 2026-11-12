// [I] #43 / #83 per-track timing outside the sequencer: timing.bin, the
// pattern store (save, reopen, undo, preload) and the Pattern screen's
// names page (F3 Speed / F4 Groove of the cursor track). The clock itself
// is tested in sequencer_test.cpp.
#include "test.h"
#include "mem_storage.h"
#include "ui_fakes.h"

#include "app/pattern_ops.h"
#include "formats/timing_side.h"
#include "model/track_timing.h"
#include "ui/screens.h"

using namespace ot;

namespace {
PatternScreen& pe(Harness& h) { return *static_cast<PatternScreen*>(h.app->ui.screen(kModPattern)); }
constexpr uint8_t k2x = 6, k3q = 3, kSwing58 = 3, kStraight = 1;
}  // namespace

TEST(timing_side_round_trip) {
    static TrackTiming store[kPatternCount * kTrackCount];
    for (auto& t : store) t = TrackTiming{};
    CHECK(!timingUsed(store));
    MemStorage fs;
    static uint8_t buf[tmside::kMaxFileSize];
    CHECK(saveTimingFile(fs, "P/project.mt", store, buf) == FmtStatus::Ok);
    CHECK(!fs.exists("P/timing.bin"));   // nothing to write
    store[(5 - 1) * kTrackCount + 2] = TrackTiming{k2x, kSwing58};
    store[(255 - 1) * kTrackCount + 15] = TrackTiming{timing::kSpeedDefault, kStraight};
    CHECK(timingUsed(store));
    CHECK(saveTimingFile(fs, "P/project.mt", store, buf) == FmtStatus::Ok);
    CHECK(fs.exists("P/timing.bin"));
    CHECK(copyTimingFile(fs, "P/project.mt", "Q/project.mt", buf) == FmtStatus::Ok);
    for (auto& t : store) t = TrackTiming{k3q, 2};
    CHECK(loadTimingFile(fs, "Q/project.mt", store, buf) == FmtStatus::Ok);
    CHECK_EQ(store[(5 - 1) * kTrackCount + 2].speed, k2x);
    CHECK_EQ(store[(5 - 1) * kTrackCount + 2].groove, kSwing58);
    CHECK_EQ(store[(255 - 1) * kTrackCount + 15].groove, kStraight);
    CHECK(store[0].isDefault());
    CHECK(store[(5 - 1) * kTrackCount + 3].isDefault());
    // Two records: header + 2 x 33 + sum.
    const uint32_t n = encodeTiming(store, buf);
    CHECK_EQ(n, tmside::kHeaderSize + 2 * tmside::kRecordSize + 1);
    // A damaged sum leaves every pattern default.
    buf[tmside::kHeaderSize + 3] ^= 1;
    CHECK(decodeTiming(buf, n, store) == FmtStatus::BadMagic);
    CHECK(!timingUsed(store));
    // Back to default: the file goes.
    CHECK(saveTimingFile(fs, "P/project.mt", store, buf) == FmtStatus::Ok);
    CHECK(!fs.exists("P/timing.bin"));
}

TEST(timing_saves_and_reopens_with_the_project) {
    Harness h;
    App& a = *h.app;
    CHECK(a.newProject());
    a.pattern().timing[3] = TrackTiming{k2x, kSwing58};
    a.patternEdited();
    a.selectPattern(2);
    a.pattern().timing[0] = TrackTiming{k3q, 0};
    a.patternEdited();
    CHECK(a.saveProjectAs("Timing"));
    CHECK(h.fs.exists("Projects/Timing/timing.bin"));
    CHECK(a.newProject());
    CHECK(a.pattern().timing[0].isDefault());
    CHECK(!h.fs.exists("Workspace/timing.bin"));
    CHECK(a.openProject("Projects", "Timing"));
    CHECK_EQ(int(a.patternNumber()), 2);
    CHECK_EQ(a.pattern().timing[0].speed, k3q);
    const TrackTiming* p1 = a.storedTiming(1);
    CHECK(p1 != nullptr);
    if (!p1) return;
    CHECK_EQ(p1[3].speed, k2x);
    CHECK_EQ(p1[3].groove, kSwing58);
    CHECK(p1[0].isDefault());
    a.selectPattern(1);
    CHECK_EQ(a.pattern().timing[3].speed, k2x);
}

TEST(timing_plain_project_writes_no_side_file) {
    Harness h;
    App& a = *h.app;
    CHECK(a.newProject());
    CHECK(a.saveProjectAs("Plain"));
    CHECK(h.fs.exists("Projects/Plain/project.mt"));
    CHECK(!h.fs.exists("Projects/Plain/timing.bin"));
}

TEST(timing_names_page_keys_and_undo) {
    Harness h;
    App& a = *h.app;
    CHECK(a.newProject());
    PatternScreen& p = pe(h);
    p.track = 2;
    h.tap(btn::F8);
    h.tap(btn::F8);   // names page: F3 Speed, F4 Groove
    CHECK_EQ(p.page, 2);
    h.push(InputEvent::kButtonDown, btn::F3);
    h.enc(1);
    h.enc(1);
    h.push(InputEvent::kButtonUp, btn::F3);
    CHECK_EQ(a.pattern().timing[2].speed, k2x);   // 1x -> 1.5x -> 2x
    h.push(InputEvent::kButtonDown, btn::F4);
    h.enc(1);
    h.push(InputEvent::kButtonDown, btn::Right);   // next track, F4 still held
    h.push(InputEvent::kButtonUp, btn::Right);
    h.enc(1);
    h.enc(1);
    h.push(InputEvent::kButtonUp, btn::F4);
    CHECK_EQ(a.pattern().timing[2].groove, kStraight);
    CHECK_EQ(p.track, 3);
    CHECK_EQ(a.pattern().timing[3].groove, 2);   // Swing 54
    CHECK(a.pattern().timing[3].speed == timing::kSpeedDefault);
    CHECK(a.undoEdit());
    CHECK_EQ(a.pattern().timing[3].groove, kStraight);
    // The wheel clamps at the ends.
    h.push(InputEvent::kButtonDown, btn::F3);
    for (int i = 0; i < 10; ++i) h.enc(-1);
    h.push(InputEvent::kButtonUp, btn::F3);
    CHECK_EQ(a.pattern().timing[3].speed, 0);   // 1/8x
}

TEST(timing_copies_with_the_pattern) {
    // Copy pattern / Paste pattern (spec 06 §1.11) carry the timing.
    Harness h;
    App& a = *h.app;
    CHECK(a.newProject());
    a.pattern().timing[5] = TrackTiming{k3q, kSwing58};
    SelRect all;
    all.s0 = 0;
    all.s1 = 31;
    all.t0 = 0;
    all.t1 = kTrackCount - 1;
    a.copyToClipboard(all);
    a.selectPattern(3);
    CHECK(a.pattern().timing[5].isDefault());
    pops::paste(a.pattern(), a.clipboard(), a.clipRect(), 0, 0, ColMode::Pattern, a.currentInstrument());
    CHECK_EQ(a.pattern().timing[5].speed, k3q);
    CHECK_EQ(a.pattern().timing[5].groove, kSwing58);
}
