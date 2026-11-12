// [I] Instrument library: a sample instrument saved to Instruments/ (or a
// folder in it) as a stock .pti, with its sample and every parameter, and
// loaded back into a slot from the Instrument Effects page (F6 Load, F7 Save).
#include <cstring>
#include <vector>

#include "test.h"
#include "ui_fakes.h"

#include "formats/wav.h"
#include "ui/screens.h"

using namespace ot;

namespace {

void addWav(Harness& h, const char* path, uint32_t frames) {
    std::vector<int16_t> s(frames);
    for (uint32_t i = 0; i < frames; ++i) s[i] = int16_t((i % 100) * 300 - 15000);
    std::vector<uint8_t> out(frames * 2 + 64);
    out.resize(wavEncode(s.data(), frames, 1, out.data(), out.size()));
    h.fs.files[path] = out;
}

InstrumentScreen& inst(Harness& h) { return *static_cast<InstrumentScreen*>(h.app->ui.screen(kModInstrument)); }

// Slot 1: tone.wav, Volume 30, Tune +5, named "Lead".
void setUp(Harness& h) {
    App& a = *h.app;
    CHECK(a.newProject());
    addWav(h, "tone.wav", 20000);
    CHECK(a.importSample("tone.wav", 0));
    a.instruments[0].volume = 30;
    a.instruments[0].tune = 5;
    a.renameInstrument(0, "Lead");
    a.markInstrumentDirty(0);
}

}  // namespace

TEST(instrument_library_save_and_load_keep_the_parameters) {
    Harness h;
    App& a = *h.app;
    setUp(h);
    CHECK(a.saveLibraryInstrument(0, App::kInstrumentLibDir, "My Lead") == nullptr);
    CHECK(h.fs.files.count("Instruments/My Lead.pti") == 1);
    // Into slot 3: the sample and the parameters come with it.
    CHECK(a.loadLibraryInstrument(2, "Instruments/My Lead.pti") == nullptr);
    CHECK(a.instrPresent[2]);
    CHECK_EQ(a.bank.frames(2), a.bank.frames(0));
    CHECK_EQ(int(a.instruments[2].volume), 30);
    CHECK_EQ(int(a.instruments[2].tune), 5);
    CHECK_STR(a.instruments[2].name, "Lead");
    // A folder in Instruments/ is made when saved into.
    CHECK(a.saveLibraryInstrument(0, "Instruments/Leads", "Bright") == nullptr);
    CHECK(h.fs.files.count("Instruments/Leads/Bright.pti") == 1);
}

TEST(instrument_library_refuses_what_does_not_fit) {
    Harness h;
    App& a = *h.app;
    CHECK(a.newProject());
    CHECK(a.saveLibraryInstrument(5, App::kInstrumentLibDir, "Empty") != nullptr);   // empty slot
    CHECK(a.loadLibraryInstrument(0, "Instruments/missing.pti") != nullptr);
    CHECK(!a.instrPresent[0]);
    // A .pti larger than the free sample memory: refused, the slot keeps its sample.
    addWav(h, "tone.wav", 20000);
    CHECK(a.importSample("tone.wav", 0));
    std::vector<uint8_t> big(392 + a.bank.capacityBytes() * 2, 0);
    h.fs.files["Instruments/huge.pti"] = big;
    CHECK_STR(a.loadLibraryInstrument(0, "Instruments/huge.pti"), "Not enough sample memory");
    CHECK(a.instrPresent[0]);
    CHECK_EQ(a.bank.frames(0), 20000u);
}

TEST(instrument_effects_page_saves_and_lists_folders) {
    Harness h;
    App& a = *h.app;
    setUp(h);
    h.fs.dirs.insert("Instruments");
    h.fs.dirs.insert("Instruments/Drums");
    a.setCurrentInstrument(0);
    h.tap(btn::InstrParams);
    h.run(50);
    InstrumentScreen& s = inst(h);
    h.tap(btn::F8);   // More: the Effects page
    CHECK(s.effects);
    CHECK_STR(a.ui.bar.label[5].top, "Load");
    CHECK_STR(a.ui.bar.label[6].top, "Save");
    // F7 Save: the keyboard with the instrument's name; OK saves.
    h.tap(btn::F7);
    CHECK(a.ui.kb.isOpen());
    h.tap(btn::F8);   // the keyboard's Save
    h.run(50);
    CHECK(h.fs.files.count("Instruments/Lead.pti") == 1);
    // Saving the same name again asks first.
    h.tap(btn::F7);
    h.tap(btn::F8);   // the keyboard's Save
    h.run(50);
    CHECK(a.ui.askOpen());
    h.tap(btn::F8);   // Replace
    h.run(50);
    // F6 Load: the folder first, then the instrument.
    h.tap(btn::F6);
    CHECK(s.libList);
    CHECK_EQ(s.libCount, 2);
    CHECK_STR(a.ui.bar.label[7].top, "Open");
    h.tap(btn::F8);   // into Drums/ (empty)
    CHECK_STR(s.libDir, "Instruments/Drums");
    CHECK_EQ(s.libCount, 0);
    h.tap(btn::Left);   // back up, on the folder it came from
    CHECK_STR(s.libDir, "Instruments");
    CHECK_EQ(s.libCursor, 0);
    h.tap(btn::Down);
    CHECK_STR(a.ui.bar.label[7].top, "Load");
    a.setCurrentInstrument(4);
    h.tap(btn::F8);
    CHECK(!s.libList);
    CHECK(a.instrPresent[4]);
    CHECK_EQ(int(a.instruments[4].volume), 30);
    // Left at Instruments/ closes the list.
    h.tap(btn::F6);
    h.tap(btn::Left);
    CHECK(!s.libList);
}
