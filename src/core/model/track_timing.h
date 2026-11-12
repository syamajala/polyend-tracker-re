// [I] Per-track timing (#43 speed, #83 groove): value tables shared by the
// Pattern screen (names page F3 / F4, track header tags) and the sequencer.
#pragma once

#include <cstdint>

namespace ot {
namespace timing {

// Speed: a step of the track lasts 8 / kSpeedEighths master steps.
constexpr int kSpeedCount = 7;
constexpr int kSpeedDefault = 4;   // 1x
inline constexpr const char* kSpeedNames[kSpeedCount] = {"1/8x", "1/4x", "1/2x", "3/4x", "1x", "1.5x", "2x"};
inline constexpr uint8_t kSpeedEighths[kSpeedCount] = {1, 2, 4, 6, 8, 12, 16};

// Groove: 0 = Global (follow the Swing FX, stock timing); any other
// replaces it for the track (Straight: no swing).
constexpr int kGrooveCount = 9;
inline constexpr const char* kGrooveNames[kGrooveCount] = {"Global",   "Straight", "Swing 54", "Swing 58",  "Swing 62",
                                                           "Swing 66", "Swing 71", "Shuffle",  "Laid back"};
// Track header tags: empty for the default.
inline constexpr const char* kGrooveTags[kGrooveCount] = {"", "Str", "S54", "S58", "S62", "S66", "S71", "Shf", "LB"};

// Step lengths of a groove (50 = one straight step), repeating every 8 steps
// from step 0; a row sums to 400, so a cycle keeps 8 steps' length. Swing
// NN (MPC style): even steps NN, odd 100 - NN. Shuffle: swung 8ths. Laid
// back: steps 4 and 12 (beats 2 and 4) a tenth of a step late. Row 0
// (Global) is unused: Global follows the Swing FX.
inline constexpr uint8_t kGrooveSteps[kGrooveCount][8] = {
    {50, 50, 50, 50, 50, 50, 50, 50}, {50, 50, 50, 50, 50, 50, 50, 50}, {54, 46, 54, 46, 54, 46, 54, 46},
    {58, 42, 58, 42, 58, 42, 58, 42}, {62, 38, 62, 38, 62, 38, 62, 38}, {66, 34, 66, 34, 66, 34, 66, 34},
    {71, 29, 71, 29, 71, 29, 71, 29}, {62, 62, 38, 38, 62, 62, 38, 38}, {50, 50, 50, 55, 45, 50, 50, 50},
};
inline constexpr int grooveLength(int groove, int step) { return kGrooveSteps[groove][step & 7]; }

}  // namespace timing
}  // namespace ot
