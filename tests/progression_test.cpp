// [I] #121 Song mode progressions: the text format (ui/progression.h), the
// files with their built-in defaults, App::insertProgression and the Song
// screen's Insert list.
#include <cstring>
#include <string>

#include "test.h"
#include "ui_fakes.h"

#include "app/project_fields.h"
#include "ui/notes.h"
#include "ui/progression.h"
#include "ui/screens.h"

using namespace ot;

namespace {
constexpr uint8_t kMinor = 1, kMajor = 2, kDorian = 3, kPhrygian = 7, kMixolydian = 9, kHarmMinor = 11;   // scaleName order (spec 16 §B.2)
constexpr uint8_t kRootA = 9, kRootC = 0;   // root options C1.. (pitch class = option % 12)

// The chord names in a key ("Am F C G"); valid until the next call.
const char* names(const prog::Progression& p, int root, uint8_t scale) {
    static std::string out;
    out.clear();
    const int t = prog::tonic(p, root, scale);
    for (int i = 0; i < p.count; ++i) {
        char n[16];
        prog::chordName(p, i, t, n, sizeof(n));
        out += (i ? " " : "") + std::string(n);
    }
    return out.c_str();
}

void setKey(App& a, uint8_t root, uint8_t scale) {
    a.project.setU8(pf::kRootNote, root);
    a.project.setU8(pf::kScale, scale);
}

void putFile(Harness& h, const char* path, const char* text) {
    h.fs.files[path] = std::vector<uint8_t>(text, text + std::strlen(text));
}

SongScreen& song(Harness& h) { return *static_cast<SongScreen*>(h.app->ui.screen(kModSong)); }
}  // namespace

TEST(progression_parse) {
    setNoteSpelling(kSpellSharps, 0, 0);
    prog::Progression p;
    CHECK(prog::parse("i VI III VII", p));
    CHECK(p.minor);
    CHECK_EQ(p.count, 4);
    CHECK_EQ(p.chords[1].degree, 8);   // VI on the natural minor scale
    CHECK_EQ(p.chords[0].quality, prog::kMin);
    CHECK_EQ(p.chords[3].quality, prog::kMaj);
    CHECK_STR(names(p, kRootA, kMinor), "Am F C G");
    // Sevenths, a major key, a title.
    CHECK(prog::parse("ii7 V7 Imaj7 | Two-five-one", p));
    CHECK(!p.minor);
    CHECK_STR(p.title, "Two-five-one");
    CHECK_STR(names(p, kRootC, kMajor), "Dm7 G7 Cmaj7");
    CHECK_STR(p.chords[2].numeral, "Imaj7");
    // Minor two-five-one, borrowed chords, sus.
    CHECK(prog::parse("iim7b5 V7 i", p));
    CHECK_STR(names(p, kRootA, kMinor), "Bm7b5 E7 Am");
    CHECK(prog::parse("I bVII IV Isus4", p));
    CHECK_STR(names(p, kRootC, kMajor), "C A# F Csus4");
    // Not progressions: unknown words, mixed case, a bad suffix, too many chords.
    CHECK(!prog::parse("", p));
    CHECK(!prog::parse("Am F C G", p));
    CHECK(!prog::parse("I Iv V", p));
    CHECK(!prog::parse("I V9", p));
    CHECK(!prog::parse("I ii iii IV V vi vii I ii", p));
    CHECK(!prog::parse("| only a title", p));
}

TEST(progression_relative_key) {
    // A progression in the other key than the project's plays in the relative key.
    setNoteSpelling(kSpellSharps, 0, 0);
    prog::Progression p;
    CHECK(prog::parse("I V vi IV", p));
    CHECK_STR(names(p, kRootA, kMinor), "C G Am F");
    CHECK(prog::parse("i iv v i", p));
    CHECK_STR(names(p, kRootC, kMajor), "Am Dm Em Am");
    CHECK_STR(names(p, kRootA, kMinor), "Am Dm Em Am");
    // Chromatic counts as major.
    CHECK(prog::parse("I IV V", p));
    CHECK_STR(names(p, kRootC + 2, 0), "D G A");
}

TEST(progression_modes) {
    setNoteSpelling(kSpellSharps, 0, 0);
    prog::Progression p;
    // In the mode's own key quality the progression stays on its root (the Dorian vamp).
    CHECK(prog::parse("i IV", p));
    CHECK_STR(names(p, kRootC + 2, kDorian), "Dm G");
    // The other quality plays in the parent major key or its relative minor,
    // so its chords use the mode's notes.
    CHECK(prog::parse("I V vi IV", p));
    CHECK_STR(names(p, kRootC + 2, kDorian), "C G Am F");
    CHECK_STR(names(p, kRootC + 4, kPhrygian), "C G Am F");
    CHECK(prog::parse("i VI III VII", p));
    CHECK_STR(names(p, kRootC + 7, kMixolydian), "Am F C G");
    // Not a mode of the major scale: as natural minor.
    CHECK(prog::parse("I V vi IV", p));
    CHECK_STR(names(p, kRootA, kHarmMinor), "C G Am F");
}

TEST(progression_auto_spelling) {
    prog::Progression p;
    CHECK(prog::parse("I bVII IV", p));
    CHECK(prog::parse("i bII", p));
    // Auto: a b chord in flats, a # chord in sharps, the rest in the key's spelling.
    setNoteSpelling(kSpellAuto, kRootA, kMinor);
    CHECK_STR(names(p, kRootA, kMinor), "Am Bb");
    CHECK(prog::parse("I bVII IV #IV", p));
    setNoteSpelling(kSpellAuto, kRootC, kMajor);
    CHECK_STR(names(p, kRootC, kMajor), "C Bb F F#");
    CHECK(prog::parse("i iv v", p));
    setNoteSpelling(kSpellAuto, kRootC, kMinor);
    CHECK_STR(names(p, kRootC, kMinor), "Cm Fm Gm");
    CHECK(prog::parse("i VI", p));
    CHECK_STR(names(p, kRootC, kMinor), "Cm Ab");
    // A chosen spelling wins.
    CHECK(prog::parse("I bVII", p));
    setNoteSpelling(kSpellSharps, kRootC, kMajor);
    CHECK_STR(names(p, kRootC, kMajor), "C A#");
    setNoteSpelling(kSpellFlats, kRootC, kMajor);
    CHECK(prog::parse("I #IV", p));
    CHECK_STR(names(p, kRootC, kMajor), "C Gb");
    setNoteSpelling(kSpellSharps, 0, 0);
}

TEST(progression_defaults_written_and_read) {
    Harness h;
    prog::Library& lib = *new prog::Library;
    CHECK(prog::load(h.fs, lib));
    for (int i = 0; i < prog::defaultCount(); ++i) {
        const std::string path = std::string("Progressions/") + prog::defaultName(i) + ".txt";
        CHECK(h.fs.exists(path.c_str()));
    }
    CHECK_EQ(lib.groups, prog::defaultCount());
    CHECK_EQ(lib.skipped, 0);
    CHECK(lib.items >= 15);
    CHECK_STR(lib.group[0], "DnB");   // sorted by name
    // Every built-in line reads.
    for (int i = 0; i < lib.items; ++i) {
        prog::Progression p;
        CHECK(prog::parse(lib.line[i], p));
    }
    delete &lib;
}

TEST(progression_user_files) {
    // A folder with a file of the user's: no defaults, bad lines counted,
    // comments and blank lines ignored.
    Harness h;
    h.fs.mkdir("Progressions");
    putFile(h, "Progressions/Mine.txt", "# my loops\n\ni VI III VII\nnot chords\nI V vi IV | Pop\r\n");
    putFile(h, "Progressions/readme.md", "I V");
    prog::Library& lib = *new prog::Library;
    CHECK(prog::load(h.fs, lib));
    CHECK(!h.fs.exists("Progressions/Jazz.txt"));
    CHECK_EQ(lib.groups, 1);
    CHECK_EQ(lib.items, 2);
    CHECK_EQ(lib.skipped, 1);
    prog::Progression p;
    CHECK(prog::parse(lib.line[1], p));
    CHECK_STR(p.title, "Pop");
    // A user's file named like a default is never overwritten.
    Harness h2;
    h2.fs.mkdir("Progressions");
    putFile(h2, "Progressions/Jazz.txt", "x\n");
    CHECK(prog::writeDefaults(h2.fs));
    CHECK_EQ(h2.fs.files["Progressions/Jazz.txt"].size(), 2u);
    CHECK(h2.fs.exists("Progressions/Pop.txt"));
    delete &lib;
}

TEST(progression_insert_names_patterns) {
    Harness h;
    App& a = *h.app;
    setKey(a, kRootA, kMinor);
    a.setCurrentInstrument(0);
    prog::Progression p;
    CHECK(prog::parse("i VI III VII", p));
    // New project: slot 1 holds the empty pattern 1, which is filled.
    const char* why = nullptr;
    CHECK_EQ(a.insertProgression(p, 0, &why), 0);
    CHECK_EQ(a.project.playlist[0], 1);
    CHECK_EQ(a.project.playlist[3], 4);
    CHECK_EQ(a.project.playlist[4], 0);
    CHECK_STR(a.names.get(1), "Am i");
    CHECK_STR(a.names.get(2), "F VI");
    CHECK_STR(a.names.get(4), "G VII");
    // Am on step 1 of tracks 1..3 (A4 C5 E5), the current instrument.
    const Track& t0 = a.storedStock(1).tracks[0];
    CHECK_EQ(t0.steps[0].note, 57);
    CHECK_EQ(a.storedStock(1).tracks[1].steps[0].note, 60);
    CHECK_EQ(a.storedStock(1).tracks[2].steps[0].note, 64);
    CHECK_EQ(a.storedStock(1).tracks[3].steps[0].note, kNoteEmpty);
    CHECK_EQ(t0.steps[0].instrument, 0);
    CHECK_EQ(a.pattern().tracks[0].steps[0].note, 57);   // pattern 1 is the open one
    // A second progression reuses the named chords; a repeated new chord is one pattern.
    CHECK(prog::parse("i iv iv VI", p));
    CHECK_EQ(a.insertProgression(p, 3, &why), 4);
    CHECK_EQ(a.project.playlist[4], 1);   // Am
    CHECK_EQ(a.project.playlist[5], 5);   // Dm, new
    CHECK_EQ(a.project.playlist[6], 5);
    CHECK_EQ(a.project.playlist[7], 2);   // F
    CHECK_STR(a.names.get(5), "Dm iv");
    CHECK(!a.patternHasData(6));
    // A pattern the user named "C" counts as C.
    a.names.set(9, "C");
    CHECK(prog::parse("i III", p));
    CHECK_EQ(a.insertProgression(p, 0, &why), 1);
    CHECK_EQ(a.project.playlist[1], 1);
    CHECK_EQ(a.project.playlist[2], 3);   // "C III" comes first
    a.names.set(3, "");
    CHECK_EQ(a.insertProgression(p, 0, &why), 1);
    CHECK_EQ(a.project.playlist[2], 9);
    CHECK_EQ(a.project.playlist[3], 1);   // the later slots moved down
    CHECK_EQ(a.project.playlist[4], 3);
}

TEST(progression_insert_refuses_when_full) {
    Harness h;
    App& a = *h.app;
    setKey(a, kRootC, kMajor);
    prog::Progression p;
    CHECK(prog::parse("I V vi IV", p));
    for (int s = 0; s < kSongSlots; ++s) a.project.playlist[s] = 1;
    a.pattern().tracks[0].steps[0].note = 48;   // pattern 1 has data: no slot to fill
    a.patternEdited();
    const char* why = nullptr;
    CHECK_EQ(a.insertProgression(p, 0, &why), -1);
    CHECK_STR(why, "Song is full");
    // Every pattern named: no new ones, nothing changed.
    Harness h2;
    App& b = *h2.app;
    setKey(b, kRootC, kMajor);
    for (int n = 1; n <= kPatternCount; ++n) b.names.set(n, "x");
    CHECK_EQ(b.insertProgression(p, 0, &why), -1);
    CHECK_STR(why, "No empty patterns");
    CHECK_EQ(b.project.playlist[1], 0);
}

TEST(progression_song_screen_insert) {
    Harness h;
    App& a = *h.app;
    setKey(a, kRootA, kMinor);
    h.tap(btn::Song);
    SongScreen& s = song(h);
    h.tap(btn::Insert);   // not in edit mode: only a notice
    CHECK(!s.progOpen);
    h.tap(btn::Rec);
    h.tap(btn::Insert);
    CHECK(s.progOpen);
    CHECK(h.fs.exists("Progressions/Pop.txt"));
    h.run(50);
    CHECK(h.disp.lastHasText("Progression"));
    CHECK(h.disp.lastHasText("DnB"));
    CHECK(h.disp.lastHasText("Am  F  C  G"));
    CHECK(h.disp.lastHasText("i VI III VII"));
    // The cursor starts on the first progression (DnB: i VI III VII), skips headings.
    h.tap(btn::Down);
    h.tap(btn::Down);
    h.tap(btn::Up);
    h.tap(btn::Up);
    h.tap(btn::F7);   // Cancel
    CHECK(!s.progOpen);
    CHECK_EQ(a.project.playlist[1], 0);
    h.tap(btn::Insert);
    h.tap(btn::F8);   // Insert
    CHECK(!s.progOpen);
    CHECK_STR(a.names.get(2), "F VI");
    CHECK_EQ(a.project.playlist[3], 4);
    CHECK_EQ(s.cursor, 0);
    // The title shows the cursor slot's pattern name.
    h.run(50);
    CHECK(h.disp.lastHasText("Am i"));
    h.tap(btn::Down);
    h.run(50);
    CHECK(h.disp.lastHasText("F VI"));
}
