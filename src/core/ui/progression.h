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
// sus2 or sus4; b or # in front moves the root a semitone (bVII). A line
// with a lower-case i is in a minor key (numbers on the natural minor
// scale), any other in a major key; a progression in the other key than the
// project's plays in its relative key (I V vi IV in A minor: C G Am F).
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
    uint8_t quality = kMaj;
    char numeral[10] = {};   // as written ("bVII", "ii7")
};

struct Progression {
    uint8_t count = 0;
    bool minor = false;      // numbers on the natural minor scale
    Chord chords[kMaxChords];
    char title[kLineMax] = {};
};

// One line ("ii7 V7 Imaj7 | Two-five-one"); false when it isn't a progression.
bool parse(const char* line, Progression& out);
// The progression's tonic pitch class in a project key (root option, scale).
int tonic(const Progression& p, int root, uint8_t scale);
// Semitones above the chord root; returns the note count (3 or 4).
int intervals(uint8_t quality, int out[kMaxNotes]);
// "Am", "G7", "Bm7b5" (pitch class names in the current spelling).
void chordName(int rootPc, uint8_t quality, char* out, int cap);
// Chord i of p in a key: its root pitch class and name.
int chordRoot(const Progression& p, int i, int tonicPc);

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
