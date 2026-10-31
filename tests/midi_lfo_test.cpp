// [I] MIDI LFO: midi_lfo.bin codec, its place next to project.mt (save,
// open, autosave), and the MIDI Instrument LFO page.
#include <cstring>

#include "test.h"
#include "ui_fakes.h"

#include "formats/file_io.h"
#include "formats/midi_lfo.h"
#include "ui/screens.h"

using namespace ot;

namespace {
MidiLfo sample() {
    MidiLfo l;
    l.target = 75;   // CC 74
    l.shape = 4;
    l.rate = 12;
    l.depth = 80;
    l.center = 40;
    l.retrig = 1;
    return l;
}
bool sameLfo(const MidiLfo& a, const MidiLfo& b) {
    return a.target == b.target && a.shape == b.shape && a.rate == b.rate && a.depth == b.depth &&
           a.center == b.center && a.retrig == b.retrig;
}
}  // namespace

TEST(midi_lfo_codec_round_trip) {
    Project p;
    CHECK(!midiLfosUsed(p));
    p.midiLfo[3] = sample();
    p.midiLfo[15].target = MidiLfo::kTargetPitchBend;
    CHECK(midiLfosUsed(p));
    uint8_t buf[mlfo::kFileSize];
    encodeMidiLfos(p, buf);
    CHECK(std::memcmp(buf, "OTML", 4) == 0);
    CHECK_EQ(buf[4], 1);
    CHECK_EQ(buf[5], 16);
    CHECK_EQ(buf[6], 6);
    Project q;
    CHECK(decodeMidiLfos(buf, sizeof(buf), q) == FmtStatus::Ok);
    for (int i = 0; i < kMidiInstrumentCount; ++i) CHECK(sameLfo(q.midiLfo[i], p.midiLfo[i]));
    // Damaged checksum: all default.
    buf[mlfo::kHeaderSize + 3 * mlfo::kRecordSize] ^= 1;
    CHECK(decodeMidiLfos(buf, sizeof(buf), q) == FmtStatus::BadMagic);
    for (const MidiLfo& l : q.midiLfo) CHECK(l.isDefault());
    // Out-of-range fields are reset one by one.
    encodeMidiLfos(p, buf);
    uint8_t* r = buf + mlfo::kHeaderSize + 3 * mlfo::kRecordSize;
    uint8_t sum = buf[mlfo::kFileSize - 1];
    sum = uint8_t(sum - r[1] + 9 - r[3] + 200);
    r[1] = 9;     // shape
    r[3] = 200;   // depth
    buf[mlfo::kFileSize - 1] = sum;
    CHECK(decodeMidiLfos(buf, sizeof(buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.midiLfo[3].shape, MidiLfo().shape);
    CHECK_EQ(q.midiLfo[3].depth, MidiLfo().depth);
    CHECK_EQ(q.midiLfo[3].target, 75);
    // A newer file with longer records keeps the known fields.
    uint8_t big[8 + 16 * 8 + 1] = {'O', 'T', 'M', 'L', 2, 16, 8, 0};
    uint8_t s2 = 0;
    for (int i = 0; i < 16; ++i) {
        uint8_t* x = big + 8 + i * 8;
        x[0] = 2; x[1] = 1; x[2] = 3; x[3] = 4; x[4] = 5; x[5] = 1; x[6] = 77; x[7] = 88;
        for (int k = 0; k < 8; ++k) s2 = uint8_t(s2 + x[k]);
    }
    big[sizeof(big) - 1] = s2;
    CHECK(decodeMidiLfos(big, sizeof(big), q) == FmtStatus::Ok);
    CHECK_EQ(q.midiLfo[7].target, 2);
    CHECK_EQ(q.midiLfo[7].retrig, 1);
}

TEST(midi_lfo_file_next_to_project_mt) {
    FakeStorage fs;
    fs.mkdir("P");
    Project p;
    // Unused: only project.mt, as stock.
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(fs.exists("P/project.mt"));
    CHECK(!fs.exists("P/midi_lfo.bin"));
    p.midiLfo[1] = sample();
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(fs.exists("P/midi_lfo.bin"));
    Project q;
    CHECK(loadProjectFile(fs, "P/project.mt", q) == FmtStatus::Ok);
    CHECK(sameLfo(q.midiLfo[1], sample()));
    // project.mt is the same bytes with or without LFOs.
    std::vector<uint8_t> withLfo = fs.files["P/project.mt"];
    p.midiLfo[1] = MidiLfo{};
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(fs.files["P/project.mt"] == withLfo);
    CHECK(!fs.exists("P/midi_lfo.bin"));   // back to default: removed
    // A project without the file loads with every LFO off, even into a used Project.
    CHECK(loadProjectFile(fs, "P/project.mt", q) == FmtStatus::Ok);
    for (const MidiLfo& l : q.midiLfo) CHECK(l.isDefault());
}

TEST(midi_lfo_survives_save_and_open) {
    Harness h;
    App& a = *h.app;
    a.project.midiLfo[4] = sample();
    a.markProjectDirty();
    CHECK(a.saveProjectAs("lfo"));
    CHECK(h.fs.exists("Projects/lfo/midi_lfo.bin"));
    CHECK(h.fs.exists("Workspace/midi_lfo.bin"));
    CHECK(a.newProject());
    CHECK(a.project.midiLfo[4].isDefault());
    CHECK(!h.fs.exists("Workspace/midi_lfo.bin"));
    CHECK(a.openProject(kProjectsDir, "lfo"));
    CHECK(sameLfo(a.project.midiLfo[4], sample()));
    CHECK(h.fs.exists("Workspace/midi_lfo.bin"));
    // Autosave after an edit rewrites the Workspace copy.
    a.project.midiLfo[4].depth = 10;
    a.markProjectDirty();
    a.autosave();
    Project q;
    CHECK(loadProjectFile(h.fs, "Workspace/project.mt", q) == FmtStatus::Ok);
    CHECK_EQ(q.midiLfo[4].depth, 10);
}

TEST(midi_lfo_instrument_page) {
    Harness h;
    App& a = *h.app;
    a.setCurrentInstrument(kFirstMidiInstrument + 1);   // MIDI 2
    h.tap(btn::InstrParams);
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument Parameters"));
    h.tap(btn::InstrParams);   // -> LFO page
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument LFO"));
    CHECK(h.disp.lastHasText("Target"));
    CHECK(h.disp.lastHasText("Retrigger"));
    CHECK(h.disp.lastHasText("Off"));
    MidiLfo& l = a.project.midiLfo[1];
    // Target (at most 10 per event, as the CC columns), Up/Down too: Off, the
    // CC slots (default 1 5 7 11 64; PC is no target), pitch bend, pressure,
    // then the other CCs. A pick reaches the LFO once the encoder rests.
    h.enc(75);
    CHECK_EQ(l.target, MidiLfo::kTargetOff);
    h.run(500);
    CHECK_EQ(l.target, 3 + 1);   // list[10] = CC 3
    for (int i = 0; i < 6; ++i) h.enc(10);
    h.enc(7);
    h.run(500);
    CHECK_EQ(l.target, 74 + 1);
    CHECK(h.disp.lastHasText("74"));
    h.tap(btn::Up);
    h.run(500);
    CHECK_EQ(l.target, 75 + 1);
    // Shape (list, F2): a tap on the selected list column wraps.
    h.tap(btn::F2);
    CHECK_EQ(l.shape, 2);
    h.tap(btn::F2);
    CHECK_EQ(l.shape, 3);
    h.tap(btn::Down);   // lists: Down moves down
    CHECK_EQ(l.shape, 4);
    // Depth (F4) and reset with Delete.
    h.tap(btn::F4);
    h.enc(-20);
    CHECK_EQ(l.depth, 30);
    h.tap(btn::Delete);
    CHECK_EQ(l.depth, 50);
    // Retrigger (F6).
    h.tap(btn::F6);
    h.tap(btn::F6);
    CHECK_EQ(l.retrig, 1);
    // On to the preset page (#13), back to the parameters page; another MIDI
    // instrument keeps the LFO page.
    h.tap(btn::InstrParams);
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument Preset"));
    h.tap(btn::InstrParams);
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument Parameters"));
    h.tap(btn::InstrParams);
    a.setCurrentInstrument(kFirstMidiInstrument + 2);
    static_cast<InstrumentScreen*>(a.ui.screen(kModInstrument))->start(0);
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument LFO"));
    CHECK(a.project.midiLfo[2].isDefault());
    // A sample instrument leaves it.
    InstrumentScreen& scr = *static_cast<InstrumentScreen*>(a.ui.screen(kModInstrument));
    CHECK_EQ(scr.mode, 4);
    a.setCurrentInstrument(0);
    scr.start(0);
    CHECK_EQ(scr.mode, 0);
}
