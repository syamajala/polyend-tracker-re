// [I] #70 Performance scene banks: Performance/Scenes/<name>.txt on the card,
// a small line-based text file (editable on a computer) holding a whole
// scene kit: the 12 columns (FX type, the values of rows 1..3, a MIDI CC
// column's target), the 16 scenes with their FX toggles, and the scenes on
// A / B. Import replaces the project's columns and scenes with the file's.
//
//   # OpenTracker scene bank
//   column 1 = Sample LP cutoff: -60 0 60
//   column 2 = Tape stop: 1 3 5
//   column 3 = MIDI CC: - 64 127 @ midi 2 slot A
//   column 4 = ---
//   scene 1 = 1 0 2 0 0 0 0 0 0 0 0 0      (a row 0..3 per column, 0 = not driven)
//   scene 2 fx off = 3 4 12                (the F keys the scene stores: tracks
//                                           with their FX off; "none" = all on)
//   a = 1
//   b = 2
//
// Types are the FX names of the Performance type list (any case); values
// are numbers in the type's range (a MIDI CC column: the CC value 0..127,
// "-" = off). '#' starts a comment; unknown keys are ignored and missing ones
// stay empty, so later versions can add keys.
#pragma once

#include <cstddef>
#include <cstdint>

#include "model/project.h"

namespace ot {

struct PerfBank {
    static constexpr int kSlots = Project::kPerfSceneSlots;
    static constexpr int kScenes = Project::kPerfScenes;
    uint8_t type[kSlots] = {};          // perf::Fx, 0 = empty
    int16_t value[kSlots][3] = {};      // rows 1..3, as perf::Source::value
    uint8_t target[kSlots] = {};        // perf::ccTarget, MIDI CC columns
    uint8_t scene[kScenes][kSlots] = {};
    uint16_t fxOff[kScenes] = {};       // as Project::perfSceneFxOff
    uint16_t fxSet = 0;                 // as Project::perfSceneFxSet
    uint8_t on[2] = {0, 1};             // the scenes on A / B
};

namespace pbank {
constexpr const char* kDir = "Performance/Scenes";
constexpr const char* kExt = ".txt";
constexpr int kNameMax = 24;
constexpr size_t kTextMax = 3072;   // every line at its longest: ~2.4 KB
}  // namespace pbank

// Writes the bank as text (NUL-terminated); returns its length, 0 if `cap` is too small.
size_t encodePerfBank(const PerfBank& b, char* out, size_t cap);
// Reads a bank (b reset first); false when no known key was found (not a
// bank). Out-of-range values read as 0, unknown types as empty.
bool decodePerfBank(const char* text, size_t len, PerfBank& b);

}  // namespace ot
