// [I] #121 Song mode progressions: chord progressions written as scale
// numbers ("i VI III VII"), read from Progressions/<Group>.txt on the card,
// turned into chords in the project key.
//
//   # Jazz.txt: one progression per line
//   ii7 V7 Imaj7
//   iim7b5 V7 i
//   i VI III VII | Jungle pads
//
// Upper case = a major chord, lower case = minor; after it 7 (a dominant
// 7th on upper case, a minor 7th on lower), maj7, m7, dim, dim7, m7b5, aug,
// sus2 or sus4; b or # in front lowers or raises the root a semitone (bVII).
// The chord is always built as written, also when it has notes outside the
// key (V in minor is major).
//
// Key: "major:" or "minor:" in front says it; otherwise the key whose own
// chords the line uses most (minor counts V, V7 and vii dim as its own, as in
// harmonic minor), on a tie the I / i chord's case, else major. Numbers are
// on that key's scale (natural minor: VI in A minor is F); in minor, vii dim,
// dim7 and m7b5 sit on the raised 7th (G#dim in A minor).
//
// In the project: a scale counts as minor when it has a minor 3rd and no
// major 3rd (Minor, Dorian, Phrygian, Harm Min, ...), otherwise as major. A
// progression of the same kind plays on the project root, the other kind in
// the relative key (I V vi IV: C G Am F in A minor, F C Dm Bb in D Dorian).
// So a mode plays progressions as its parallel major or minor key; chords on
// the mode's own note leave its scale (i VI in D Dorian: Dm Bb).
//
// Names: with Auto note names the chord root is spelled on the numeral's
// letter counted from the key's tonic (vii in F# major is E#, bVII in C is
// Bb); with Sharps / Flats every name follows that table.
// Text after '|' is the title; a line starting with '#' (not #I..) is a comment.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hal_api.h"

namespace ot {
namespace prog {

constexpr const char* kDir = "Progressions";
constexpr int kMaxChords = 8;
constexpr int kMaxItems = 48;      // progressions in the list
constexpr int kMaxGroups = 8;      // files
constexpr int kLineMax = 56;       // a stored line (chords and title)
constexpr int kGroupMax = 20;      // a group (file) name
constexpr int kMaxNotes = 4;

enum Quality : uint8_t { kMaj, kMin, kDim, kAug, kDom7, kMaj7, kMin7, kHalfDim7, kDim7, kSus2, kSus4, kQualityCount };

struct Chord {
    uint8_t degree = 0;    // semitones above the key's tonic, 0..11
    uint8_t step = 0;      // the numeral - 1 (I = 0 .. VII = 6): its letter above the tonic's
    uint8_t quality = kMaj;
    int8_t accidental = 0;   // -1 b, +1 #, as written
    char numeral[10] = {};   // as written ("bVII", "ii7")
};

struct Progression {
    uint8_t count = 0;
    bool minor = false;      // numbers on the natural minor scale
    bool keyGiven = false;   // "major:" / "minor:" in front
    Chord chords[kMaxChords];
    char title[kLineMax] = {};
};

// One line ("ii7 V7 Imaj7 | Two-five-one"); false when it isn't a progression.
bool parse(const char* line, Progression& out);
// The key a progression plays in: its tonic's pitch class and, for Auto
// spelling, the tonic's letter (0 = C .. 6 = B; -1: none, the name tables).
struct Tonic {
    int pc = 0;
    int letter = -1;
};
// The progression's tonic in a project key (root option, scale).
Tonic tonic(const Progression& p, int root, uint8_t scale);
// True when the project scale counts as minor (a minor 3rd, no major 3rd).
bool scaleIsMinor(uint8_t scale);
// Semitones above the chord root; returns the note count (3 or 4).
int intervals(uint8_t quality, int out[kMaxNotes]);
// Chord i of p in a key: its root pitch class.
int chordRoot(const Progression& p, int i, const Tonic& t);
// Its name: "Am", "G7", "Bm7b5" (see Names above).
void chordName(const Progression& p, int i, const Tonic& t, char* out, int cap);

// How Insert stacks each chord's notes, one note per track:
//   Smooth  each chord in the inversion that moves least from the one before
//           (the first in root position, root C4..B4)
//   Block   root position, root C4..B4
//   Wide    root C3..B3, then the 5th (and 7th), the 3rd an octave up
//   Bass    the root an octave below (C3..B3) on its own track, then Smooth
enum Voicing : uint8_t { kSmooth, kBlock, kWide, kBass, kVoicingCount };
constexpr int kMaxVoiced = kMaxNotes + 1;   // Bass: a 7th on 5 tracks
const char* voicingName(uint8_t v);
// The word a pattern name ends with ("Am i Wide"); "" for Smooth.
const char* voicingWord(uint8_t v);
// Chord i's notes (MIDI, low to high = track order) after the previous
// chord's (prev, prevCount; 0 for the first); returns the note count.
int voice(const Progression& p, int i, const Tonic& t, uint8_t v, const int* prev, int prevCount,
          int out[kMaxVoiced]);

// The list: every progression of every file, files sorted by name.
struct Library {
    int groups = 0, items = 0, skipped = 0;
    char group[kMaxGroups][kGroupMax + 1] = {};
    uint8_t itemGroup[kMaxItems] = {};
    char line[kMaxItems][kLineMax + 1] = {};
    void reset() { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }   // in place (~3 KB: no temporary)
};
// Reads Progressions/*.txt; writes the built-in files first when the folder
// has none. False when nothing could be read.
bool load(IStorage& fs, Library& lib);
// The built-in files (name, text), i = 0 .. defaultCount() - 1.
int defaultCount();
const char* defaultName(int i);
const char* defaultText(int i);
bool writeDefaults(IStorage& fs);
// Adds the lines of one file's text under a new group; returns the lines skipped.
int addText(Library& lib, const char* group, const char* text, size_t len);

}  // namespace prog
}  // namespace ot
