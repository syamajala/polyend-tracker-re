// [I] #13 MIDI presets: the MidiPresets/<name>.txt text format, the
// midi_names.bin side file, App load / save and the MIDI Instrument Preset page.
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
    std::strcpy(p.label[0], "Cutoff");
    std::strcpy(p.label[1], "Resonance");
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
    for (int i = 0; i < 6; ++i)
        if (std::strcmp(a.label[i], b.label[i]) != 0) return false;
    const MidiLfo &x = a.lfo, &y = b.lfo;
    return x.target == y.target && x.shape == y.shape && x.rate == y.rate && x.depth == y.depth &&
           x.center == y.center && x.retrig == y.retrig;
}
std::string text(const char* s) { return s; }
}  // namespace

TEST(midi_preset_text_round_trip) {
    char buf[mpreset::kTextMax];
    const size_t n = encodeMidiPreset(sample(), buf, sizeof(buf));
    CHECK(n > 0);
    const std::string t(buf, n);
    CHECK(t.find("cc A = 43 Cutoff\n") != std::string::npos);
    CHECK(t.find("cc F = PC\n") != std::string::npos);
    CHECK(t.find("lfo target = CC 74\n") != std::string::npos);
    CHECK(t.find("channel") == std::string::npos);   // a preset has no channel
    MidiPreset q;
    CHECK(decodeMidiPreset(buf, n, q));
    CHECK(same(q, sample()));
    // Too small a buffer: nothing.
    CHECK_EQ(encodeMidiPreset(sample(), buf, 20), 0u);
}

TEST(midi_preset_text_is_forgiving) {
    // Comments, case, CRLF, spaces, unknown keys; missing keys keep defaults.
    const std::string t = text(
        "# my synth\r\n"
        "VELOCITY=90\r\n"
        "  cc b =  PAT   Poly AT \r\n"
        "cc c = 200 too big\n"   // out of range: ignored
        "colour = blue\n"        // unknown: ignored
        "lfo target = pb\n"
        "lfo rate = 99\n");      // out of range: default
    MidiPreset p;
    CHECK(decodeMidiPreset(t.data(), t.size(), p));
    CHECK_EQ(p.velocity, 90);
    CHECK_EQ(p.cc[1], 130);
    CHECK(std::strcmp(p.label[1], "Poly AT") == 0);
    CHECK_EQ(p.cc[2], MidiPreset{}.cc[2]);
    CHECK_EQ(p.lfo.target, MidiLfo::kTargetPitchBend);
    CHECK_EQ(p.lfo.rate, MidiLfo{}.rate);
    // Labels longer than the bar cut at kLabelMax.
    const std::string l = text("cc A = 1 AVeryLongLabelName\n");
    CHECK(decodeMidiPreset(l.data(), l.size(), p));
    CHECK_EQ(std::strlen(p.label[0]), size_t(MidiPresetInfo::kLabelMax));
    // Not a preset.
    const std::string junk = text("hello\nworld = 1\n");
    CHECK(!decodeMidiPreset(junk.data(), junk.size(), p));
}

TEST(midi_names_file_next_to_project_mt) {
    FakeStorage fs;
    fs.mkdir("P");
    Project p;
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(!fs.exists("P/midi_names.bin"));   // unused: stock file set
    std::strcpy(p.midiPreset[3].name, "Cycles");
    std::strcpy(p.midiPreset[3].label[2], "Decay");
    CHECK(saveProjectFile(fs, "P/project.mt", p) == FmtStatus::Ok);
    CHECK(fs.exists("P/midi_names.bin"));
    Project q;
    CHECK(loadProjectFile(fs, "P/project.mt", q) == FmtStatus::Ok);
    CHECK(std::strcmp(q.midiPreset[3].name, "Cycles") == 0);
    CHECK(std::strcmp(q.midiPreset[3].label[2], "Decay") == 0);
    // A damaged file leaves every name empty.
    fs.files["P/midi_names.bin"][20] ^= 1;
    CHECK(loadProjectFile(fs, "P/project.mt", q) == FmtStatus::Ok);
    for (const MidiPresetInfo& m : q.midiPreset) CHECK(m.isDefault());
}

TEST(midi_preset_save_and_load_into_any_instrument) {
    Harness h;
    App& a = *h.app;
    MidiInstrument m = a.project.midiInstrument(2);
    m.velocity = 99;
    m.cc[0] = 74;
    a.project.setMidiInstrument(2, m);
    std::strcpy(a.project.midiPreset[2].label[0], "Cutoff");
    a.project.midiLfo[2].target = 75;
    CHECK(a.saveMidiPreset(2, "Cycles"));
    CHECK(h.fs.exists("MidiPresets/Cycles.txt"));
    CHECK(std::strcmp(a.project.midiPreset[2].name, "Cycles") == 0);
    // Loaded into another instrument (another channel): everything but the channel.
    const uint8_t ch = a.project.midiInstrument(5).channel;
    CHECK(a.loadMidiPreset(5, "Cycles"));
    const MidiInstrument n = a.project.midiInstrument(5);
    CHECK_EQ(n.velocity, 99);
    CHECK_EQ(n.cc[0], 74);
    CHECK_EQ(n.channel, ch);
    CHECK_EQ(a.project.midiLfo[5].target, 75);
    CHECK(std::strcmp(a.project.midiPreset[5].name, "Cycles") == 0);
    CHECK(std::strcmp(a.project.midiPreset[5].label[0], "Cutoff") == 0);
    CHECK(!a.loadMidiPreset(5, "Missing"));
    CHECK(!a.saveMidiPreset(5, ""));
    // Names and labels go with the project.
    CHECK(a.saveProjectAs("mp"));
    CHECK(a.newProject());
    CHECK(a.project.midiPreset[5].isDefault());
    CHECK(a.openProject(kProjectsDir, "mp"));
    CHECK(std::strcmp(a.project.midiPreset[5].label[0], "Cutoff") == 0);
}

TEST(midi_preset_page) {
    Harness h;
    App& a = *h.app;
    a.setCurrentInstrument(kFirstMidiInstrument + 2);   // MIDI 3
    h.tap(btn::InstrParams);
    h.tap(btn::InstrParams);   // LFO
    h.tap(btn::InstrParams);   // Preset
    h.run(40);
    CHECK(h.disp.lastHasText("MIDI Instrument Preset"));
    CHECK(h.disp.lastHasText("(none)"));
    // Label slot B: Down, F6, type, OK (Harness::type types lowercase).
    h.tap(btn::Down);
    h.tap(btn::F6);
    CHECK(a.ui.kb.isOpen());
    h.type("reso");
    h.tap(btn::F8);
    CHECK_STR(a.project.midiPreset[2].label[1], "reso");
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
    // The parameters page shows the label instead of "CC B".
    h.tap(btn::InstrParams);
    h.run(40);
    CHECK(h.disp.lastHasText("reso"));
    CHECK(!h.disp.lastHasText("CC B"));
    // Load it into MIDI 7 from the list.
    a.setCurrentInstrument(kFirstMidiInstrument + 6);
    InstrumentScreen& scr = *static_cast<InstrumentScreen*>(a.ui.screen(kModInstrument));
    scr.start(0);
    h.tap(btn::InstrParams);
    h.tap(btn::InstrParams);
    CHECK_EQ(scr.mode, 5);
    h.tap(btn::F1);
    CHECK(scr.presetList);
    CHECK_EQ(scr.presetCount, 1);
    h.tap(btn::F8);
    CHECK(!scr.presetList);
    CHECK(std::strcmp(a.project.midiPreset[6].name, "juno") == 0);
    CHECK(std::strcmp(a.project.midiPreset[6].label[1], "reso") == 0);
    // Delete clears the selected slot's label.
    scr.presetSlot = 1;
    h.tap(btn::Delete);
    CHECK_EQ(a.project.midiPreset[6].label[1][0], 0);
}
