// [I] #28 Performance MIDI CC column: a column that sends a MIDI
// instrument's CC slot A..F, set up in the FX type list, saved with the
// presets, blended by scenes one column at a time.
#include <cstdio>
#include <memory>
#include <vector>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"
#include "app/project_fields.h"
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
        App& a = *h.app;
        a.cfg.midiOut = 1;   // USB
        a.applySequencerSettings();
        // MIDI 3 (channel 3): slot C on CC 74, named; MIDI 5: slot A on CC 71.
        MidiInstrument m = a.project.midiInstrument(2);
        m.cc[2] = 74;
        a.project.setMidiInstrument(2, m);
        a.midiNames.setName(2, 74, "Cutoff");
        std::snprintf(a.midiNames.preset[2], sizeof(a.midiNames.preset[2]), "S-1");
        m = a.project.midiInstrument(4);
        m.cc[0] = 71;
        a.project.setMidiInstrument(4, m);
    }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void setRow(int pad, int value) {
        h.push(InputEvent::kPadDown, uint8_t(pad));
        for (; value > 100; value -= 100) h.enc(100);
        h.enc(int8_t(value));
        h.push(InputEvent::kPadUp, uint8_t(pad));
    }
    // Edit mode: the edit column becomes MIDI CC on MIDI instrument `midi`, slot `slot`.
    void pickCc(int midi, int slot) {
        h.tap(btn::Down);
        h.enc(int8_t(perf::kMidiCc - p().typeCursor));
        h.tap(btn::Right);
        h.enc(int8_t(midi - p().midiCursor_));
        h.tap(btn::Right);
        h.enc(int8_t(slot - p().slotCursor_));
        h.tap(btn::F8);
    }
    // CC values sent on `status` for `cc`.
    std::vector<int> sent(int status, int cc) const {
        std::vector<int> v;
        const auto& b = h.midi.sent;
        for (size_t i = 0; i + 2 < b.size(); ++i)
            if (b[i] == status && b[i + 1] == cc) v.push_back(b[i + 2]);
        return v;
    }
};

}  // namespace

TEST(perf_midi_cc_type_list_sets_the_target) {
    Rig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.h.tap(btn::Down);
    r.h.enc(int8_t(perf::kMidiCc - p.typeCursor));
    r.h.tap(btn::Right);   // MIDI CC: Right moves to the instrument list
    CHECK(p.typeList);
    CHECK_EQ(p.typeFocus_, 1);
    r.h.enc(2);
    r.h.tap(btn::Right);
    r.h.enc(2);
    r.h.tap(btn::Right);   // stays on the slot list
    CHECK_EQ(p.typeFocus_, 2);
    r.h.tap(btn::F8);
    CHECK(!p.typeList);
    CHECK_EQ(p.src.type(0), perf::kMidiCc);
    CHECK_EQ(p.src.target(0), perf::ccTarget(2, 2));
    CHECK_EQ(a.project.u8(pf::kPerfTypes), 0);   // stock byte: empty
    CHECK_EQ(a.project.perfExtType[0], perf::kMidiCc);
    char nm[32];
    p.columnName(0, nm, sizeof(nm));
    CHECK_STR(nm, "MIDI 3 Cutoff");
    // Another type: Right closes the list unchanged, as before.
    r.h.tap(btn::Right);   // column 1
    const uint8_t was = p.src.type(1);
    r.h.tap(btn::Down);
    r.h.enc(int8_t(perf::kDjFilter - p.typeCursor));
    r.h.tap(btn::Right);
    CHECK(!p.typeList);
    CHECK_EQ(p.src.type(1), was);
    // Reopened on a MIDI CC column: on its target; F7 leaves it.
    r.h.tap(btn::Left);
    r.h.tap(btn::Down);
    CHECK_EQ(p.midiCursor_, 2);
    CHECK_EQ(p.slotCursor_, 2);
    r.h.tap(btn::Right);
    r.h.enc(1);
    r.h.tap(btn::F7);
    CHECK_EQ(p.src.target(0), perf::ccTarget(2, 2));
}

TEST(perf_midi_cc_pads_send) {
    Rig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    r.h.tap(btn::Rec);
    r.pickCc(2, 2);
    r.setRow(12, 1);     // row 1: CC 0
    r.setRow(24, 41);    // row 2: CC 40
    r.setRow(36, 128);   // row 3: CC 127
    r.h.tap(btn::Rec);
    r.h.midi.sent.clear();
    r.h.pad(24);
    CHECK(r.sent(0xB2, 74) == std::vector<int>{40});
    r.h.pad(24);   // a press always sends, even the same value
    CHECK_EQ(r.sent(0xB2, 74).size(), 2u);
    r.h.pad(12);
    r.h.pad(36);
    CHECK(r.sent(0xB2, 74) == (std::vector<int>{40, 40, 0, 127}));
    r.h.pad(0);    // off: nothing, the synth keeps 127
    CHECK_EQ(r.sent(0xB2, 74).size(), 4u);
    // The fader with no scene on the column: no repeats.
    r.h.enc(3);
    CHECK_EQ(r.sent(0xB2, 74).size(), 4u);
    r.h.midi.sent.clear();
    r.h.tap(btn::Pattern);   // leaving sends nothing
    CHECK(r.sent(0xB2, 74).empty());
}

// Two MIDI CC columns are two parameters: both stay on, each sends.
TEST(perf_midi_cc_columns_are_independent) {
    Rig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.pickCc(2, 2);
    r.setRow(12, 11);
    r.h.tap(btn::Right);
    r.pickCc(4, 0);
    r.setRow(13, 21);
    r.h.tap(btn::Right);
    r.pickCc(2, 2);      // the same target as column 0
    r.setRow(14, 31);
    r.h.tap(btn::Rec);
    r.h.midi.sent.clear();
    r.h.pad(12);
    r.h.pad(13);
    CHECK_EQ(p.src.row(0), 1);
    CHECK_EQ(p.src.row(1), 1);
    CHECK(r.sent(0xB2, 74) == std::vector<int>{10});
    CHECK(r.sent(0xB4, 71) == std::vector<int>{20});
    r.h.pad(14);         // same target: column 0 goes off
    CHECK_EQ(p.src.row(0), 0);
    CHECK_EQ(p.src.row(1), 1);
    CHECK(r.sent(0xB2, 74) == (std::vector<int>{10, 30}));
}

// Scenes blend each MIDI CC column on its own; a scene without the column
// holds the other's value.
TEST(perf_midi_cc_scenes_blend_per_column) {
    Rig r;
    App& a = *r.h.app;
    a.cfg.perfPresets = 1;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.pickCc(2, 2);
    r.setRow(12, 1);     // CC 0
    r.setRow(36, 101);   // CC 100
    r.h.tap(btn::Right);
    r.pickCc(4, 0);
    r.setRow(13, 61);    // CC 60
    r.h.tap(btn::Rec);
    r.h.pad(12);
    r.h.pad(13);
    r.h.down(btn::Shift);
    r.h.tap(btn::Left);    // scene A: column 0 row 1, column 1 row 1
    r.h.up(btn::Shift);
    r.h.pad(1);            // column 1 off
    r.h.pad(36);
    r.h.down(btn::Shift);
    r.h.tap(btn::Right);   // scene B: column 0 row 3 only
    r.h.up(btn::Shift);
    r.h.enc(int8_t(-100 / PerformanceScreen::kFaderStep));
    CHECK_EQ(p.fader, 0);
    r.h.midi.sent.clear();
    r.h.enc(int8_t(50 / PerformanceScreen::kFaderStep));
    const auto v = r.sent(0xB2, 74);
    CHECK(!v.empty() && v.back() == 50);
    for (size_t i = 1; i < v.size(); ++i) CHECK(v[i] > v[i - 1]);   // each change once, rising
    const auto w = r.sent(0xB4, 71);
    CHECK(w.empty() || w.back() == 60);   // only in A: held, not faded out
    CHECK_EQ(perf::ccValue(p.cur[1]), 60);
    r.h.enc(int8_t(50 / PerformanceScreen::kFaderStep));
    const auto v2 = r.sent(0xB2, 74);
    CHECK(!v2.empty() && v2.back() == 100);
    CHECK_EQ(perf::ccValue(p.cur[1]), 60);
}

TEST(perf_midi_cc_targets_saved) {
    // Project presets: perf_scenes.bin v5.
    Project p;
    p.perfExtType[3] = perf::kMidiCc;
    p.perfExtArg[3] = perf::ccTarget(15, 5);
    CHECK(perfScenesUsed(p));
    uint8_t buf[pscn::kFileSize];
    encodePerfScenes(p, buf);
    Project q;
    CHECK(decodePerfScenes(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.perfExtType[3], perf::kMidiCc);
    CHECK_EQ(q.perfExtArg[3], perf::ccTarget(15, 5));
    // A v4 file: targets on instrument 1, slot A.
    buf[4] = 4;
    CHECK(decodePerfScenes(buf, pscn::kFxEnd, q) == FmtStatus::Ok);
    CHECK_EQ(q.perfExtType[3], perf::kMidiCc);
    CHECK_EQ(q.perfExtArg[3], 0);
    // Global presets: the Config extension record.
    FakeNvStore nv;
    Config c;
    c.perfExt[1] = perf::kMidiCc;
    c.perfArg[1] = perf::ccTarget(4, 3);
    CHECK(c.save(nv));
    Config d;
    CHECK(d.load(nv));
    CHECK_EQ(d.perfExt[1], perf::kMidiCc);
    CHECK_EQ(d.perfArg[1], perf::ccTarget(4, 3));
    // A bad target is dropped by the source check.
    d.perfArg[1] = perf::ccTarget(4, 7);
    perf::globalSource(d).validate();
    CHECK_EQ(d.perfArg[1], 0);
}

TEST(perf_midi_cc_values) {
    char v[16];
    perf::valueText(perf::kMidiCc, 1, v, sizeof(v));
    CHECK_STR(v, "0");
    perf::valueText(perf::kMidiCc, 128, v, sizeof(v));
    CHECK_STR(v, "127");
    CHECK_EQ(perf::ccToValue(perf::kMidiCc, 0), 1);     // CC in: the value itself
    CHECK_EQ(perf::ccToValue(perf::kMidiCc, 127), 128);
    CHECK_EQ(perf::ccValue(0), -1);
}
