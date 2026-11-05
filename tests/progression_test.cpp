// [I] #121 Song mode progressions: the text format (ui/progression.h), the
// files with their built-in defaults, App::insertProgression and the Song
// screen's Insert list.
#include <cstdio>
#include <cstdlib>
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
    const prog::Tonic t = prog::tonic(p, root, scale);
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

// "F#" -> pitch class and letter; "D##" too (theory_cases.inc).
prog::Tonic spelled(const char* name) {
    static const int kPc[7] = {9, 11, 0, 2, 4, 5, 7};   // A..G
    prog::Tonic t;
    t.letter = (name[0] - 'A' + 5) % 7;
    t.pc = kPc[name[0] - 'A'];
    for (const char* c = name + 1; *c; ++c) t.pc += *c == '#' ? 1 : *c == 'b' ? -1 : 0;
    t.pc = (t.pc % 12 + 12) % 12;
    return t;
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
    // Minor two-five-one, a lowered root (bVII), sus.
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
    // A mode plays progressions as its parallel major or minor key (by its
    // 3rd): the same kind on the root, the other kind in the relative key.
    setNoteSpelling(kSpellSharps, 0, 0);
    prog::Progression p;
    CHECK(prog::parse("i IV", p));   // the Dorian vamp: IV as written, major
    CHECK_STR(names(p, kRootC + 2, kDorian), "Dm G");
    CHECK(prog::parse("i VI", p));   // VI of D minor: Bb, outside D Dorian
    CHECK_STR(names(p, kRootC + 2, kDorian), "Dm A#");
    CHECK(prog::parse("I V vi IV", p));   // D Dorian counts as D minor: F major
    CHECK_STR(names(p, kRootC + 2, kDorian), "F C Dm A#");
    CHECK_STR(names(p, kRootC + 4, kPhrygian), "G D Em C");
    CHECK_STR(names(p, kRootC + 7, kMixolydian), "G D Em C");   // major kind: on G
    CHECK(prog::parse("i VI III VII", p));
    CHECK_STR(names(p, kRootC + 7, kMixolydian), "Em C G D");
    // Other scales the same way: Harmonic minor counts as minor.
    CHECK(prog::parse("I V vi IV", p));
    CHECK_STR(names(p, kRootA, kHarmMinor), "C G Am F");
    CHECK(prog::parse("i iv V", p));
    CHECK_STR(names(p, kRootA, kHarmMinor), "Am Dm E");
    CHECK(prog::scaleIsMinor(kDorian));
    CHECK(!prog::scaleIsMinor(kMixolydian));
    CHECK(!prog::scaleIsMinor(0));   // Chromatic: major
}

TEST(progression_key_from_chords) {
    // The key whose own chords a line uses most, not one chord's case.
    setNoteSpelling(kSpellSharps, 0, 0);
    prog::Progression p;
    CHECK(prog::parse("iv V", p));   // no i, still minor (V as in harmonic minor)
    CHECK(p.minor);
    CHECK(!p.keyGiven);
    CHECK_STR(names(p, kRootA, kMinor), "Dm E");
    CHECK_STR(names(p, kRootC, kMajor), "Dm E");   // in A minor, the relative minor
    CHECK(prog::parse("iv VII III VI", p));
    CHECK_STR(names(p, kRootA, kMinor), "Dm G C F");
    CHECK(prog::parse("V i", p));
    CHECK(p.minor);
    CHECK(prog::parse("I bVII IV", p));
    CHECK(!p.minor);
    // A tie goes by the I / i chord, else major.
    CHECK(prog::parse("i IV", p));
    CHECK(p.minor);
    CHECK(prog::parse("I iv", p));
    CHECK(!p.minor);
    CHECK(prog::parse("Isus4 V", p));
    CHECK(!p.minor);
    // The leading-tone chord: on the raised 7th in minor, on the 7th in major.
    CHECK(prog::parse("viidim i", p));
    CHECK_STR(names(p, kRootA, kMinor), "G#dim Am");
    CHECK(prog::parse("viidim I", p));
    CHECK_STR(names(p, kRootC, kMajor), "Bdim C");
    CHECK(prog::parse("VII i", p));   // VII stays on the natural 7th
    CHECK_STR(names(p, kRootA, kMinor), "G Am");
    // The key said in front wins.
    CHECK(prog::parse("major: iv V", p));
    CHECK(!p.minor);
    CHECK(p.keyGiven);
    CHECK_STR(names(p, kRootC, kMajor), "Fm G");
    CHECK_STR(names(p, kRootA, kMinor), "Fm G");   // in C, A minor's relative major
    CHECK(prog::parse("  Minor:I iv | Picardy", p));
    CHECK(p.minor);
    CHECK_EQ(p.count, 2);
    CHECK_STR(p.chords[0].numeral, "I");
    CHECK_STR(names(p, kRootA, kMinor), "A Dm");
    CHECK(!prog::parse("minor:", p));
    CHECK(!prog::parse("dorian: i IV", p));
}

TEST(progression_builtins_keep_their_key) {
    // Every built-in line reads in the key it was written for: minor exactly
    // when it has a plain i chord.
    for (int f = 0; f < prog::defaultCount(); ++f) {
        const char* t = prog::defaultText(f);
        while (*t) {
            const char* e = std::strchr(t, '\n');
            const std::string line(t, e ? size_t(e - t) : std::strlen(t));
            t = e ? e + 1 : t + line.size();
            prog::Progression p;
            CHECK(prog::parse(line.c_str(), p));
            const std::string pad = " " + line.substr(0, line.find('|')) + " ";
            const bool wantMinor = pad.find(" i ") != std::string::npos || pad.find(" i7 ") != std::string::npos;
            if (p.minor != wantMinor) std::printf("  %s\n", line.c_str());
            CHECK_EQ(p.minor, wantMinor);
        }
    }
}

TEST(progression_theory_cases) {
    // Rows checked against Tonal by tools/theory_check (names and notes).
    setNoteSpelling(kSpellAuto, kRootC, kMajor);
    int rows = 0;
    auto check = [&](const char* tonicName, const char* key, const char* line, const char* wantNames,
                     const char* wantNotes) {
        ++rows;
        prog::Progression p;
        CHECK(prog::parse(line, p));
        CHECK_EQ(p.minor, std::strcmp(key, "minor") == 0);
        const prog::Tonic t = spelled(tonicName);
        std::string got, notes = wantNotes;
        for (int i = 0; i < p.count; ++i) {
            char n[16];
            prog::chordName(p, i, t, n, sizeof(n));
            got += (i ? " " : "") + std::string(n);
            // The chord's notes as pitch classes against the spelled notes.
            const size_t bar = notes.find('|');
            std::string want = notes.substr(0, bar);
            notes = bar == std::string::npos ? "" : notes.substr(bar + 1);
            int iv[prog::kMaxNotes];
            const int k = prog::intervals(p.chords[i].quality, iv);
            std::string have, exp;
            for (int j = 0; j < k; ++j) have += std::to_string((prog::chordRoot(p, i, t) + iv[j]) % 12) + " ";
            for (size_t s = 0; s < want.size();) {
                while (s < want.size() && want[s] == ' ') ++s;
                size_t e = want.find(' ', s);
                if (e == std::string::npos) e = want.size();
                if (e > s) exp += std::to_string(spelled(want.substr(s, e - s).c_str()).pc) + " ";
                s = e;
            }
            if (have != exp) std::printf("  %s: chord %d notes %s, want %s\n", line, i + 1, have.c_str(), exp.c_str());
            CHECK(have == exp);
        }
        if (got != wantNames) std::printf("  %s %s: %s, want %s\n", tonicName, line, got.c_str(), wantNames);
        CHECK(got == wantNames);
    };
#define PROG_CASE(tonic, key, line, n, notes) check(tonic, key, line, n, notes);
#define MODE_CASE(...)
#define HINT_CASE(...)
#include "theory_cases.inc"
#undef PROG_CASE
#undef MODE_CASE
#undef HINT_CASE
    CHECK(rows >= 15);
    setNoteSpelling(kSpellSharps, 0, 0);
}

TEST(progression_mode_cases) {
    // Progressions in a project scale (Tonal-checked rows): names with Auto
    // spelling and which chords leave the scale.
    struct S { const char* name; uint8_t scale; };
    static const S kScales[] = {{"dorian", kDorian}, {"phrygian", kPhrygian}, {"mixolydian", kMixolydian},
                                {"lydian", 4}, {"locrian", 6}, {"harmonic minor", kHarmMinor}};
    int rows = 0;
    auto check = [&](const char* rootName, const char* scaleName, const char* key, const char* line,
                     const char* wantNames, const char* wantOutside) {
        ++rows;
        uint8_t sc = 0xFF;
        for (const S& s : kScales)
            if (std::strcmp(s.name, scaleName) == 0) sc = s.scale;
        CHECK(sc != 0xFF);
        const int root = spelled(rootName).pc;
        setNoteSpelling(kSpellAuto, root, sc);
        prog::Progression p;
        CHECK(prog::parse(line, p));
        CHECK_EQ(p.minor, std::strcmp(key, "minor") == 0);
        const std::string got = names(p, root, sc);
        const prog::Tonic t = prog::tonic(p, root, sc);
        std::string out;
        for (int i = 0; i < p.count; ++i) {
            int iv[prog::kMaxNotes];
            const int k = prog::intervals(p.chords[i].quality, iv);
            bool inside = true;
            for (int j = 0; j < k; ++j) inside = inside && isInScale(prog::chordRoot(p, i, t) + iv[j], root, sc);
            if (!inside) {
                char n[16];
                prog::chordName(p, i, t, n, sizeof(n));
                out += (out.empty() ? "" : " ") + std::string(n);
            }
        }
        if (got != wantNames || out != wantOutside)
            std::printf("  %s %s %s: %s (outside: %s), want %s (outside: %s)\n", rootName, scaleName, line, got.c_str(),
                        out.c_str(), wantNames, wantOutside);
        CHECK(got == wantNames);
        CHECK(out == wantOutside);
    };
#define PROG_CASE(...)
#define MODE_CASE(root, scale, key, line, n, outside) check(root, scale, key, line, n, outside);
#define HINT_CASE(...)
#include "theory_cases.inc"
#undef PROG_CASE
#undef MODE_CASE
#undef HINT_CASE
    CHECK(rows >= 10);
    setNoteSpelling(kSpellSharps, 0, 0);
}

TEST(progression_auto_spelling) {
    prog::Progression p;
    // Auto: roots on the numeral's letter from the key's tonic.
    CHECK(prog::parse("i bII", p));
    setNoteSpelling(kSpellAuto, kRootA, kMinor);
    CHECK_STR(names(p, kRootA, kMinor), "Am Bb");
    CHECK(prog::parse("I V vi IV", p));   // relative major: letters from C
    CHECK_STR(names(p, kRootA, kMinor), "C G Am F");
    CHECK(prog::parse("I bVII IV #IV", p));
    setNoteSpelling(kSpellAuto, kRootC, kMajor);
    CHECK_STR(names(p, kRootC, kMajor), "C Bb F F#");
    CHECK(prog::parse("i VI", p));
    setNoteSpelling(kSpellAuto, kRootC, kMinor);
    CHECK_STR(names(p, kRootC, kMinor), "Cm Ab");
    // F# major: the 7th is E#, not F.
    CHECK(prog::parse("Imaj7 viidim", p));
    setNoteSpelling(kSpellAuto, kRootC + 6, kMajor);
    CHECK_STR(names(p, kRootC + 6, kMajor), "F#maj7 E#dim");
    // A double sharp falls back to the key's table (#vii in F#: E## -> F#).
    CHECK(prog::parse("I #vii", p));
    CHECK_STR(names(p, kRootC + 6, kMajor), "F# F#m");
    // D Dorian (spelled like D minor, flats): F C Dm Bb.
    CHECK(prog::parse("I V vi IV", p));
    setNoteSpelling(kSpellAuto, kRootC + 2, kDorian);
    CHECK_STR(names(p, kRootC + 2, kDorian), "F C Dm Bb");
    // A chosen spelling wins: the name table (sounding pitch).
    CHECK(prog::parse("I bVII", p));
    setNoteSpelling(kSpellSharps, kRootC, kMajor);
    CHECK_STR(names(p, kRootC, kMajor), "C A#");
    setNoteSpelling(kSpellFlats, kRootC, kMajor);
    CHECK(prog::parse("I #IV", p));
    CHECK_STR(names(p, kRootC, kMajor), "C Gb");
    CHECK(prog::parse("Imaj7 viidim", p));
    setNoteSpelling(kSpellSharps, kRootC + 6, kMajor);
    CHECK_STR(names(p, kRootC + 6, kMajor), "F#maj7 Fdim");
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
    // The cursor ends on the last added slot; the title shows its pattern name.
    CHECK_EQ(s.cursor, 3);
    h.run(50);
    CHECK(h.disp.lastHasText("G VII"));
    h.tap(btn::Up);
    h.run(50);
    CHECK(h.disp.lastHasText("C III"));
    h.tap(btn::Down);
    // The next Insert follows the progression instead of splitting it (device
    // test: a Wide chorus landed between the verse's first and second chord).
    h.tap(btn::Insert);
    h.tap(btn::F6);
    h.tap(btn::F6);   // Wide
    h.tap(btn::F8);
    CHECK_STR(a.names.get(a.project.playlist[3]), "G VII");
    CHECK_STR(a.names.get(a.project.playlist[4]), "Am i Wide");
    CHECK_STR(a.names.get(a.project.playlist[7]), "G VII Wide");
    CHECK_EQ(s.cursor, 7);
}

namespace {
// Chord i of p voiced after the previous chords, as "57 60 64".
std::string voiced(const prog::Progression& p, int root, uint8_t scale, uint8_t v, int upto) {
    const prog::Tonic t = prog::tonic(p, root, scale);
    int prev[prog::kMaxVoiced], k = 0, notes[prog::kMaxVoiced];
    std::string out;
    for (int i = 0; i <= upto; ++i) {
        const int c = prog::voice(p, i, t, v, prev, k, notes);
        out.clear();
        for (int j = 0; j < c; ++j) {
            out += (j ? " " : "") + std::to_string(notes[j]);
            prev[j] = notes[j];
        }
        k = c;
    }
    return out;
}
}  // namespace

TEST(progression_voicings) {
    setNoteSpelling(kSpellSharps, 0, 0);
    prog::Progression p;
    CHECK(prog::parse("i VI III VII", p));   // Am F C G in A minor
    // Smooth: Am A4 C5 E5, F A4 C5 F5 (A, C stay), C G4 C5 E5, G G4 B4 D5.
    CHECK(voiced(p, kRootA, kMinor, prog::kSmooth, 0) == "57 60 64");
    CHECK(voiced(p, kRootA, kMinor, prog::kSmooth, 1) == "57 60 65");
    CHECK(voiced(p, kRootA, kMinor, prog::kSmooth, 2) == "55 60 64");
    CHECK(voiced(p, kRootA, kMinor, prog::kSmooth, 3) == "55 59 62");
    // Block: root position, root C4..B4 (F4 A4 C5).
    CHECK(voiced(p, kRootA, kMinor, prog::kBlock, 1) == "53 57 60");
    // Wide: root C3..B3, 5th, 3rd an octave up (A3 E4 C5); a 7th adds it under the 3rd.
    CHECK(voiced(p, kRootA, kMinor, prog::kWide, 0) == "45 52 60");
    // Bass: the root on its own track, the chord above Smooth.
    CHECK(voiced(p, kRootA, kMinor, prog::kBass, 0) == "45 57 60 64");
    CHECK(voiced(p, kRootA, kMinor, prog::kBass, 1) == "41 57 60 65");
    CHECK(prog::parse("ii7 V7 Imaj7", p));
    CHECK(voiced(p, kRootC, kMajor, prog::kWide, 0) == "38 45 48 53");   // D3 A3 C4 F4
    CHECK(voiced(p, kRootC, kMajor, prog::kBass, 2) == "36 52 55 59 60");   // C3, then Cmaj7 above
    // Smooth stays in range over a long progression: lowest note G3..D5.
    CHECK(prog::parse("I IV vii iii vi ii V I", p));
    for (int i = 0; i < p.count; ++i) {
        const std::string s = voiced(p, kRootC, kMajor, prog::kSmooth, i);
        const int low = std::atoi(s.c_str());
        CHECK(low >= 43 && low <= 62);
    }
    CHECK_STR(prog::voicingWord(prog::kSmooth), "");
    CHECK_STR(prog::voicingWord(prog::kWide), "Wide");
}

TEST(progression_insert_voicing_names) {
    Harness h;
    App& a = *h.app;
    setKey(a, kRootA, kMinor);
    prog::Progression p;
    CHECK(prog::parse("i VI III VII", p));
    const char* why = nullptr;
    CHECK_EQ(a.insertProgression(p, 0, &why), 0);   // Smooth: plain names
    CHECK_STR(a.names.get(2), "F VI");
    CHECK_EQ(a.storedStock(2).tracks[2].steps[0].note, 65);   // F5 on top: led from Am
    // The same chords Wide: new patterns with the word, the Smooth ones untouched.
    CHECK_EQ(a.insertProgression(p, 3, &why, prog::kWide), 4);
    CHECK_EQ(a.project.playlist[4], 5);
    CHECK_STR(a.names.get(5), "Am i Wide");
    CHECK_STR(a.names.get(8), "G VII Wide");
    CHECK_EQ(a.storedStock(5).tracks[0].steps[0].note, 45);
    // Smooth again reuses the Smooth patterns, Wide again the Wide ones.
    CHECK_EQ(a.insertProgression(p, 7, &why), 8);
    CHECK_EQ(a.project.playlist[8], 1);
    CHECK_EQ(a.project.playlist[11], 4);
    CHECK_EQ(a.insertProgression(p, 11, &why, prog::kWide), 12);
    CHECK_EQ(a.project.playlist[12], 5);
    CHECK(!a.patternHasData(9));
    // Bass: one more track; a user's pattern named "Dm" counts as Smooth only.
    a.names.set(20, "Dm");
    CHECK(prog::parse("iv V7", p));
    CHECK_EQ(a.insertProgression(p, 15, &why, prog::kBass), 16);
    CHECK(a.project.playlist[16] != 20);
    CHECK_STR(a.names.get(a.project.playlist[17]), "E7 V7 Bass");
    CHECK_EQ(a.storedStock(a.project.playlist[17]).tracks[0].steps[0].note, 40);   // E3
    CHECK(a.storedStock(a.project.playlist[17]).tracks[4].steps[0].note >= 0);   // 5 tracks
    CHECK_EQ(a.insertProgression(p, 17, &why), 18);
    CHECK_EQ(a.project.playlist[18], 20);
}

TEST(progression_song_screen_voicing_key) {
    Harness h;
    App& a = *h.app;
    setKey(a, kRootA, kMinor);
    h.tap(btn::Song);
    SongScreen& s = song(h);
    h.tap(btn::Rec);
    h.tap(btn::Insert);
    h.run(50);
    CHECK(h.disp.lastHasText("Smooth"));
    h.tap(btn::F6);
    CHECK_EQ(s.progVoicing, prog::kBlock);
    h.tap(btn::F6);
    h.run(50);
    CHECK(h.disp.lastHasText("Wide"));
    CHECK(h.disp.lastHasText("Wide: Am A3 E4 C5 > F F3 C4 A4"));
    h.tap(btn::F8);
    CHECK_STR(a.names.get(1), "Am i Wide");
    // Kept for the next Insert.
    h.tap(btn::Insert);
    CHECK_EQ(s.progVoicing, prog::kWide);
}
