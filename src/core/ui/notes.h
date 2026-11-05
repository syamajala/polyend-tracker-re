// Note naming, scales and the pad -> note map (spec 05 §4.1, spec 06 §1.1).
#pragma once

#include <cstdint>

namespace ot {

constexpr int kScaleCount = 39;

// Note text, at most 3 chars + NUL: "C5", "C#5" (no separator, stock); specials "FAD", "CUT", "OFF"; empty "---".
// Note 0 = C0, so note 60 = C5 (spec 06 §1.1). Black keys follow the current spelling ("Db5" with flats).
void noteName(int8_t note, char out[4]);

// [I] Config > General > Note names (not in stock): how black keys are spelled.
enum NoteSpelling : uint8_t { kSpellSharps = 0, kSpellFlats = 1, kSpellAuto = 2 };
constexpr int kNoteSpellingCount = 3;
// Auto: the spelling that names the scale's notes with the fewest repeated
// letters (C Mixolydian: Bb, not A#); on a tie, flats for flat keys.
bool keyUsesFlats(int root, uint8_t scale);
// Sets the spelling noteName() and pitchClassName() use; root/scale are the project's (Auto only).
// Cheap to call every UI pass: it recomputes only when an argument changed.
void setNoteSpelling(uint8_t spelling, int root, uint8_t scale);
bool noteSpellingFlats();
bool noteSpellingAuto();
// Pitch class text without octave: "C", "C#" / "Db", ... (current spelling).
const char* pitchClassName(int note);
// The same in a given spelling.
const char* pitchClassName(int note, bool flats);
// [I] Auto spelling by letter, for chord names: pitch class `pc` written on a
// letter (0 = C .. 6 = B), so F# major's 7th is E# ("E#" for pc 5 on E).
// nullptr when that needs a double sharp or flat.
const char* letterName(int letter, int pc);
// The letter Auto spells the project root with (A minor: 5 = A; Gb is F#: 3).
int keyLetter(int root, uint8_t scale);

uint16_t scaleMask(uint8_t scale);
const char* scaleName(uint8_t scale);   // stock names (spec 16 §B.2)
bool isInScale(int note, int root, uint8_t scale);
// Root note option 0..36 = C1..C4; the root pitch class is root % 12.

// [I] Chord hints: the chord on `note` that fits the scale, built by stacking
// every other scale note (root, 3rd, 5th; C major: D -> D F A). False when the
// note is outside the scale or the scale has no thirds to stack (Chromatic).
struct ScaleChord {
    uint8_t pc[3];     // pitch classes: root, 3rd, 5th
    uint8_t degree;    // 0-based scale degree of the root
    uint8_t degrees;   // notes in the scale (Roman numerals only for 7)
    int8_t letter[3];  // Auto spelling in 7-note scales: each note's letter (0 = C), else -1
};
// With Auto note names a 7-note scale is spelled one letter per degree from
// the root's (F# major: F# G# A# B C# D# E#), so the chord's notes are too.
bool scaleTriad(int note, int root, uint8_t scale, ScaleChord& out);
// Note j (0 root, 1 3rd, 2 5th) of a chord: by its letter, else the name table.
const char* chordToneName(const ScaleChord& c, int j);
// "Dm  ii", "Bdim  vii", "Caug": chord name and, in 7-note scales, the Roman
// numeral; unusual stacks list their notes ("D F# B").
void scaleTriadName(const ScaleChord& c, char* out, int cap);
// [I] The alternate major / minor chord on the same root: a major triad's 3rd
// lowered to minor, a minor triad's raised to major (A minor: Em -> E, the
// major V of harmonic minor; C major: F -> Fm). Its notes keep their letters
// (E G# B). Just the other quality, not a chord picked from another mode.
// False for dim and aug.
bool alternateTriad(const ScaleChord& in, ScaleChord& out);

// Pad -> note table (spec 05 §4.1). Physical pad index = row*12 + col, row 0 = top.
class PadNotes {
public:
    // [I] a pad that plays nothing (a gap of the piano layout).
    static constexpr uint8_t kNoNote = 0xFF;
    // piano: Config > Pad layout "Piano" (layout is then unused): per row
    // pair the white keys along the lower row (C D E F G A B C D E F G) and
    // the black keys above them, each over the white key above it (C# over
    // D; gaps over C and F); the upper pair an octave above the lower one, which starts at the
    // root note's octave.
    void build(uint8_t scale, uint8_t layout, uint8_t root, bool piano = false);
    // raw: chromatic pad + 24 (spec 05 §4.1 getNote raw).
    uint8_t note(uint8_t physicalPad, bool raw = false) const;
    // Physical pad producing `note`, or -1.
    int padsForNote(int note, uint8_t* pads, int cap) const;
    // [I] the piano layout's key lights (levels 0..31, physical pads):
    // the scale's root brightest, white keys `dim`, black keys dimmer, keys
    // outside the scale barely lit, gaps dark (lv untouched there). False
    // (lv untouched) when the table isn't the piano layout. The levels are
    // built with the table (build) and again only for a new dim / full.
    bool pianoLeds(uint8_t* lv, uint8_t dim, uint8_t full);
    bool piano() const { return piano_; }
    // Times the key-light levels were built (tests).
    uint32_t ledBuilds() const { return ledBuilds_; }

private:
    void buildLeds();
    uint8_t notes_[48] = {};   // logical (bottom-origin) index; kNoNote = gap
    uint8_t scale_ = 0xFF, layout_ = 0xFF, root_ = 0xFF;
    bool piano_ = false;
    // Cached key lights (physical pads; kNoNote = gap) for ledDim_ / ledFull_.
    uint8_t leds_[48] = {};
    uint8_t ledDim_ = 0, ledFull_ = 0;
    uint32_t ledBuilds_ = 0;
};

inline int logicalPad(int physical) { return (3 - physical / 12) * 12 + physical % 12; }
inline int physicalPad(int logical) { return (3 - logical / 12) * 12 + logical % 12; }

}  // namespace ot
