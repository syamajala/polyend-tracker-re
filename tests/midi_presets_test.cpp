// [I] #13 MIDI presets: the MidiPresets/<name>.txt text format, the
// midi_names.bin side file (CC charts), App load / save and the MIDI
// Instrument Preset page.
#include <cstring>
#include <string>

#include "test.h"
#include "ui_fakes.h"

#include "formats/file_io.h"
#include "formats/midi_names.h"
#include "formats/midi_preset.h"
#include "ui/screens.h"

using namespace ot;

namespace {
MidiPreset sample() {
    MidiPreset p;
    p.velocity = 100;
    p.offset = 12;
    const uint8_t cc[6] = {43, 44, 16, 7, 64, 128};
    std::memcpy(p.cc, cc, 6);
    std::strcpy(p.name[43], "Cutoff");
    std::strcpy(p.name[44], "Resonance");
    std::strcpy(p.name[103], "Chop Ovt");   // a named CC on no slot
    p.lfo.target = 75;   // CC 74
    p.lfo.shape = 4;
    p.lfo.rate = 12;
    p.lfo.depth = 80;
    p.lfo.center = 40;
    p.lfo.retrig = 1;
    return p;
}
bool same(const MidiPreset& a, const MidiPreset& b) {
    if (a.velocity != b.velocity || a.offset != b.offset || std::memcmp(a.cc, b.cc, 6) != 0) return false;
    for (int i = 0; i < 128; ++i)
        if (std::strcmp(a.name[i], b.name[i]) != 0) return false;
    const MidiLfo &x = a.lfo, &y = b.lfo;
    return x.target == y.target && x.shape == y.shape && x.rate == y.rate && x.depth == y.depth &&
           x.center == y.center && x.retrig == y.retrig;
}
std::string text(const char* s) { return s; }
void writeText(FakeStorage& fs, const char* path, const std::string& t) {
    CHECK(writeWholeFile(fs, path, reinterpret_cast<const uint8_t*>(t.data()), uint32_t(t.size())) == FmtStatus::Ok);
}
}  // namespace

TEST(midi_preset_text_round_trip) {
    char buf[mpreset::kTextMax];
    const size_t n = encodeMidiPreset(sample(), buf, sizeof(buf));
    CHECK(n > 0);
    const std::string t(buf, n);
    CHECK(t.find("slot A = 43\n") != std::string::npos);
    CHECK(t.find("slot F = PC\n") != std::string::npos);
    CHECK(t.find("cc 43 = Cutoff\n") != std::string::npos);
    CHECK(t.find("cc 103 = Chop Ovt\n") != std::string::npos);
    CHECK(t.find("lfo target = CC 74\n") != std::string::npos);
    CHECK(t.find("channel") == std::string::npos);   // a preset has no channel
    MidiPreset q;
    CHECK(decodeMidiPreset(buf, n, q));
    CHECK(same(q, sample()));
    // Every CC named, at the longest: still fits.
    MidiPreset all;
    for (int c = 0; c < 128; ++c) std::snprintf(all.name[c], sizeof(all.name[c]), "Name%05d", c & 0x7F);
    CHECK(encodeMidiPreset(all, buf, sizeof(buf)) > 0);
    // Too small a buffer: nothing.
    CHECK_EQ(encodeMidiPreset(sample(), buf, 20), 0u);
}

TEST(midi_preset_text_is_forgiving) {
    // Comments, case, CRLF, spaces, unknown keys; missing keys keep defaults.
    const std::string t = text(
        "# my synth\r\n"
        "VELOCITY=90\r\n"
        "  slot b =  PAT \r\n"
        "slot c = 200\n"         // out of range: ignored
        "CC 71 =  Reso \n"
        "cc 128 = Nope\n"        // not a CC: ignored
        "colour = blue\n"        // unknown: ignored
        "lfo target = pb\n"
        "lfo rate = 99\n");      // out of range: default
    MidiPreset p;
    CHECK(decodeMidiPreset(t.data(), t.size(), p));
    CHECK_EQ(p.velocity, 90);
    CHECK_EQ(p.cc[1], 130);
    CHECK_EQ(p.cc[2], MidiPreset{}.cc[2]);
    CHECK_STR(p.name[71], "Reso");
    CHECK_EQ(p.lfo.target, MidiLfo::kTargetPitchBend);
    CHECK_EQ(p.lfo.rate, MidiLfo{}.rate);
    // Names longer than a bar label cut at kNameMax.
    const std::string l = text("cc 1 = AVeryLongLabelName\n");
    CHECK(decodeMidiPreset(l.data(), l.size(), p));
    CHECK_EQ(std::strlen(p.name[1]), size_t(MidiCcNames::kNameMax));
    // Not a preset.
    const std::string junk = text("hello\nworld = 1\n");
    CHECK(!decodeMidiPreset(junk.data(), junk.size(), p));
}

TEST(midi_cc_names_charts) {
    static MidiCcNames n;
    n.clear();
    CHECK(!n.used());
    CHECK(n.setName(3, 74, "Cutoff"));
    CHECK_STR(n.name(3, 74), "Cutoff");
    CHECK_STR(n.name(4, 74), "");   // charts are per instrument
    CHECK_STR(n.name(3, 129), "");  // only CCs 0..127 have names
    // Same preset: the chart is shared, so a rename shows on both.
    std::strcpy(n.preset[3], "S-1");
    CHECK(n.share(4, "S-1"));
    CHECK_STR(n.name(4, 74), "Cutoff");
    CHECK(n.setName(4, 74, "Filter"));
    CHECK_STR(n.name(3, 74), "Filter");
    // Unsharing gives an empty chart of its own; the other keeps its names.
    n.unshare(4);
    CHECK_STR(n.name(4, 74), "");
    CHECK_STR(n.name(3, 74), "Filter");
    CHECK(!n.share(5, "Missing"));
    // "" removes; the pool has a limit.
    CHECK(n.setName(3, 74, ""));
    CHECK_STR(n.name(3, 74), "");
    n.clear();
    for (int i = 0; i < MidiCcNames::kEntryMax; ++i) CHECK(n.setName(i / 128, i % 128, "x"));
    CHECK(!n.setName(15, 0, "y"));
    CHECK(n.setName(0, 0, "z"));   // renaming needs no room
}

TEST(midi_names_file_next_to_project_mt) {
    FakeStorage fs;
    fs.mkdir("P");
    Project p;
    static MidiCcNames n, q;
    n.clear();
    CHECK(saveProjectFile(fs, "P/project.mt", p, &n) == FmtStatus::Ok);
    CHECK(!fs.exists("P/midi_names.bin"));   // unused: stock file set
    std::strcpy(n.preset[3], "Cycles");
    std::strcpy(n.preset[4], "Cycles");
    n.chart[4] = 3;
    CHECK(n.setName(3, 16, "Decay"));
    CHECK(saveProjectFile(fs, "P/project.mt", p, &n) == FmtStatus::Ok);
    CHECK(fs.exists("P/midi_names.bin"));
    Project r;
    CHECK(loadProjectFile(fs, "P/project.mt", r, nullptr, &q) == FmtStatus::Ok);
    CHECK_STR(q.preset[3], "Cycles");
    CHECK_STR(q.name(4, 16), "Decay");   // still shared
    CHECK_EQ(q.chart[4], 3);
    // No names pointer: the file is left alone.
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(fs.exists("P/midi_names.bin"));
    // A damaged file leaves every name empty.
    fs.files["P/midi_names.bin"][20] ^= 1;
    CHECK(loadProjectFile(fs, "P/project.mt", r, nullptr, &q) == FmtStatus::Ok);
    CHECK(!q.used());
}

TEST(midi_preset_save_and_load_into_any_instrument) {
    Harness h;
    App& a = *h.app;
    MidiInstrument m = a.project.midiInstrument(2);
    m.velocity = 99;
    m.cc[0] = 74;
    a.project.setMidiInstrument(2, m);
    CHECK(a.midiNames.setName(2, 74, "Cutoff"));
    CHECK(a.midiNames.setName(2, 103, "Chop Ovt"));
    a.project.midiLfo[2].target = 75;
    CHECK(a.saveMidiPreset(2, "Cycles"));
    CHECK(h.fs.exists("MidiPresets/Cycles.txt"));
    CHECK_STR(a.midiNames.preset[2], "Cycles");
    // Loaded into another instrument (another channel): everything but the
    // channel, and the two share one chart.
    const uint8_t ch = a.project.midiInstrument(5).channel;
    CHECK(a.loadMidiPreset(5, "Cycles"));
    const MidiInstrument n = a.project.midiInstrument(5);
    CHECK_EQ(n.velocity, 99);
    CHECK_EQ(n.cc[0], 74);
    CHECK_EQ(n.channel, ch);
    CHECK_EQ(a.project.midiLfo[5].target, 75);
    CHECK_STR(a.midiNames.preset[5], "Cycles");
    CHECK_STR(a.midiNames.name(5, 103), "Chop Ovt");
    CHECK_EQ(a.midiNames.chart[5], a.midiNames.chart[2]);
    CHECK(!a.loadMidiPreset(5, "Missing"));
    CHECK(!a.saveMidiPreset(5, ""));
    // A preset written on a computer replaces the chart; another preset
    // takes the instrument off the shared one.
    writeText(h.fs, "MidiPresets/S-1.txt", "slot A = 74\ncc 74 = Cutoff\ncc 71 = Reso\n");
    CHECK(a.loadMidiPreset(5, "S-1"));
    CHECK_STR(a.midiNames.name(5, 71), "Reso");
    CHECK_STR(a.midiNames.name(5, 103), "");
    CHECK_STR(a.midiNames.name(2, 103), "Chop Ovt");
    // Names go with the project.
    CHECK(a.saveProjectAs("mp"));
    CHECK(a.newProject());
    CHECK(!a.midiNames.used());
    CHECK(a.openProject(kProjectsDir, "mp"));
    CHECK_STR(a.midiNames.name(5, 71), "Reso");
    CHECK_STR(a.midiNames.name(2, 103), "Chop Ovt");
    CHECK_STR(a.midiNames.preset[5], "S-1");
}

TEST(midi_preset_page) {
    Harness h;
    App& a = *h.app;
    a.setCurrentInstrument(kFirstMidiInstrument + 2);   // MIDI 3
    MidiInstrument m = a.project.midiInstrument(2);
    m.cc[1] = 71;
    a.project.setMidiInstrument(2, m);
    h.tap(btn::InstrParams);
    h.tap(btn::InstrParams);   // LFO
    h.tap(btn::InstrParams);   // Preset
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument Preset"));
    CHECK(h.disp.lastHasText("(none)"));
    InstrumentScreen& scr = *static_cast<InstrumentScreen*>(a.ui.screen(kModInstrument));
    CHECK_EQ(scr.presetCc, 1);   // opens on CC A's CC
    // Name CC 71 (slot B): encoder to it, F6, type, OK (Harness::type types lowercase).
    for (int i = 0; i < 7; ++i) h.enc(10);
    CHECK_EQ(scr.presetCc, 71);
    h.tap(btn::F6);
    CHECK(a.ui.kb.isOpen());
    h.type("reso");
    h.tap(btn::F8);
    CHECK_STR(a.midiNames.name(2, 71), "reso");
    h.run(40);
    CHECK(h.disp.lastHasText("reso"));
    // Save with no name: Save As.
    h.tap(btn::F2);
    CHECK(a.ui.kb.isOpen());
    h.type("juno");
    h.tap(btn::F8);
    CHECK(h.fs.exists("MidiPresets/juno.txt") || h.fs.exists("MidiPresets/juno.TXT"));
    h.run(40);
    CHECK(h.disp.lastHasText("juno"));
    // The parameters page shows the name instead of "CC B".
    h.tap(btn::InstrParams);
    h.run(40);
    CHECK(h.disp.lastHasText("reso"));
    CHECK(!h.disp.lastHasText("CC B"));
    // Load it into MIDI 7 from the list.
    a.setCurrentInstrument(kFirstMidiInstrument + 6);
    scr.start(0);
    h.tap(btn::InstrParams);
    h.tap(btn::InstrParams);
    CHECK_EQ(scr.mode, 5);
    h.tap(btn::F1);
    CHECK(scr.presetList);
    CHECK_EQ(scr.presetCount, 1);
    h.tap(btn::F8);
    CHECK(!scr.presetList);
    CHECK_STR(a.midiNames.preset[6], "juno");
    CHECK_STR(a.midiNames.name(6, 71), "reso");
    // Delete clears the selected CC's name (shared: MIDI 3 too).
    scr.presetCc = 71;
    h.tap(btn::Delete);
    CHECK_STR(a.midiNames.name(6, 71), "");
    CHECK_STR(a.midiNames.name(2, 71), "");
}

TEST(midi_lfo_target_slots_first_and_settles) {
    Harness h;
    App& a = *h.app;
    MidiInstrument m = a.project.midiInstrument(2);
    const uint8_t cc[6] = {74, 71, 24, 75, 92, 91};
    std::memcpy(m.cc, cc, 6);
    a.project.setMidiInstrument(2, m);
    CHECK(a.midiNames.setName(2, 74, "Cutoff"));
    CHECK(a.midiNames.setName(2, 71, "Reso"));
    a.setCurrentInstrument(kFirstMidiInstrument + 2);
    h.tap(btn::InstrParams);
    h.tap(btn::InstrParams);   // LFO
    InstrumentScreen& scr = *static_cast<InstrumentScreen*>(a.ui.screen(kModInstrument));
    CHECK_EQ(scr.mode, 4);
    scr.column = 0;
    // One click past Off is slot A, shown by its name; two is slot B.
    h.enc(1);
    h.run(20);
    CHECK(h.disp.lastHasText("Cutoff"));
    CHECK_EQ(a.project.midiLfo[2].target, MidiLfo::kTargetOff);   // not sent while scrolling
    h.enc(1);
    h.run(100);
    CHECK(h.disp.lastHasText("Reso"));
    CHECK_EQ(a.project.midiLfo[2].target, MidiLfo::kTargetOff);
    h.run(500);   // the encoder rests: Reso (CC 71) reaches the LFO
    CHECK_EQ(a.project.midiLfo[2].target, 71 + 1);
    // Past the six slots: pitch bend, channel pressure, then CC 0.
    for (int i = 0; i < 5; ++i) h.enc(1);
    h.run(20);
    CHECK(h.disp.lastHasText("Pitch bnd"));
    CHECK_EQ(a.project.midiLfo[2].target, 71 + 1);
    // Leaving the page hands the pick over at once.
    h.tap(btn::InstrParams);
    CHECK_EQ(a.project.midiLfo[2].target, MidiLfo::kTargetPitchBend);
}
