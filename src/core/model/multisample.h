// [I] #3 Multisample instruments (not in stock or the Tracker Mini).
//
// A multisample is one sample instrument whose sample holds several note
// samples ("zones") back to back, one per slice. The .pti stores it as a
// plain Slice instrument, so stock firmware plays each zone as a slice; the
// zones' root notes and the Multi play mode live in the project's
// multisample.bin (formats/multisample_side.h).
//
// Play: a note picks the zone whose root is nearest (a tie takes the lower
// one) and plays it repitched by note - root.
//
// Every zone starts exactly on its slice point: the loader pads the gap
// before each zone with silence (at most sampleFrames / 65535 + 1 frames,
// ~2 ms on a 6 MB sample), so the slice table alone gives exact zone
// starts, on stock too.
#pragma once

#include <cstdint>

#include "model/instrument.h"

namespace ot {

constexpr int kMultiMaxZones = kSliceCount;   // one zone per slice

// The zones of one instrument slot. Zone i spans slice i.
struct MultiMap {
    uint8_t on;                     // the Multi play mode (playMode stays Slice)
    uint8_t count;                  // zones = the instrument's sliceCount when valid
    uint8_t root[kMultiMaxZones];   // ascending
};

// One per sample instrument slot, in the SDRAM on the device. Main loop
// writes; the audio block reads it at note-on only (multiZone: a binary
// search, at most 7 byte reads), like the sample data it describes.
extern MultiMap gMultiMaps[kSampleInstrumentCount];

inline MultiMap& multiMap(int slot) { return gMultiMaps[slot]; }
inline bool multiActive(const MultiMap& m, const Instrument& in) {
    return in.playMode == uint8_t(PlayMode::Slice) && m.on && m.count > 0 && m.count == in.sliceCount;
}
inline void multiClear(int slot) {
    MultiMap& m = gMultiMaps[slot];
    m.on = 0;
    m.count = 0;
}

// The zone for a note: nearest root, the lower on a tie.
inline int multiZone(const MultiMap& m, int note) {
    int lo = 0, hi = m.count - 1;
    if (hi <= 0) return 0;
    while (lo < hi) {   // first zone with root >= note
        const int mid = (lo + hi) / 2;
        if (m.root[mid] < note) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0 && note - m.root[lo - 1] <= m.root[lo] - note) return lo - 1;
    return lo;
}

// ---- loading: choosing the zones ------------------------------------------

namespace ms {

constexpr int kMaxCandidates = 128;   // = sfz::kMaxRegions

// One note sample the SFZ offers (the first region per root).
struct Candidate {
    uint8_t root;
    uint8_t region;     // index into sfz::File::regions
    uint32_t frames;    // 44.1 kHz mono frames the region gives
};

// The Sample Loader panel's choices.
constexpr int kEveryCount = 6;
constexpr uint8_t kEvery[kEveryCount] = {1, 2, 3, 4, 6, 12};
constexpr int kLengthCount = 9;   // index 0 = the whole sample
constexpr uint16_t kLengthMs[kLengthCount] = {0, 8000, 6000, 4000, 3000, 2000, 1500, 1000, 500};
constexpr uint32_t kFadeFrames = 441;   // 10 ms fade where Max length cuts a zone

struct Options {
    uint8_t lo = 36, hi = 84;   // note range (MIDI)
    uint8_t every = 2;          // index into kEvery
    uint8_t length = 0;         // index into kLengthMs
};

struct Plan {
    int count = 0;
    bool tooMany = false;               // more than kMultiMaxZones targets (cut)
    uint8_t cand[kMultiMaxZones] = {};  // index into the candidates
    uint8_t root[kMultiMaxZones] = {};
    uint32_t frames[kMultiMaxZones] = {};   // after Max length
    uint32_t start[kMultiMaxZones] = {};    // frame of zone i in the packed sample
    uint16_t point[kMultiMaxZones] = {};    // its slice point
    uint32_t totalFrames = 0;               // the packed sample, padding included
};

// Sorts candidates by root and drops repeated roots (the first one stays).
int uniqueByRoot(Candidate* c, int n);

// Zones for `o`: targets lo, lo + every, ... <= hi, each the nearest
// candidate (lower on a tie), repeats dropped; then the layout.
void plan(const Candidate* c, int n, const Options& o, Plan& p);

// Packs zone lengths: start[i] lands exactly on point[i]
// (start = len * point / 65535, as Voice::computePoints rounds).
void layout(Plan& p);

// The defaults: the pack's notes inside C2..C6 (MIDI 36..84; the pack's own
// range when it lies outside), every 3rd note, and the longest Max length
// whose sample takes at most `budgetFrames`; every 6th / 12th note when even
// 0.5 s does not fit. False when nothing fits.
bool defaults(const Candidate* c, int n, uint32_t budgetFrames, Options& o);

}  // namespace ms
}  // namespace ot
