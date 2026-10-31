// [I] #13 MIDI instrument presets: MidiPresets/<name>.txt on the card, a small
// line-based text file (editable on a computer), loaded into whichever MIDI
// instrument is open. A preset describes the synth, not a sound or a channel:
// no program change and no channel (the instrument slot is the channel).
//
//   # OpenTracker MIDI preset
//   velocity = 127
//   offset = 0
//   cc A = 43 Cutoff          (CC number 0..127, PC, AT or PAT, then the label)
//   ...
//   lfo target = CC 74        (off, CC n, PB or AT)
//   lfo shape = 2             (0..4: Rev saw, Saw, Triangle, Square, Random)
//   lfo rate = 8              (0..22: 128 steps .. 1/8 step)
//   lfo depth = 50            (0..100)
//   lfo center = 64           (0..127)
//   lfo retrigger = 0         (0 Free, 1 Note)
//
// '#' starts a comment; keys are case-insensitive; unknown keys are ignored
// and missing ones keep the defaults, so later versions can add keys.
#pragma once

#include <cstddef>
#include <cstdint>

#include "model/instrument.h"

namespace ot {

struct MidiPreset {
    uint8_t velocity = 127;
    uint8_t offset = 0;                            // MidiInstrument::extra[0], 0..100
    uint8_t cc[6] = {1, 5, 7, 11, 64, 128};        // as MidiInstrument::cc (128 PC, 129 AT, 130 PAT)
    char label[6][MidiPresetInfo::kLabelMax + 1] = {};
    MidiLfo lfo;
};

namespace mpreset {
constexpr const char* kDir = "MidiPresets";
constexpr const char* kExt = ".txt";
constexpr int kNameMax = MidiPresetInfo::kNameMax;
constexpr size_t kTextMax = 1024;   // the longest file written, with room to spare
}  // namespace mpreset

// Writes the preset as text (NUL-terminated); returns its length, 0 if `cap` is too small.
size_t encodeMidiPreset(const MidiPreset& p, char* out, size_t cap);
// Reads a preset; false when no known key was found (not a preset).
bool decodeMidiPreset(const char* text, size_t len, MidiPreset& p);

}  // namespace ot
