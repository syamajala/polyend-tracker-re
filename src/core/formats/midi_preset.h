// [I] #13 MIDI instrument presets: MidiPresets/<name>.txt on the card, a small
// line-based text file (editable on a computer), loaded into whichever MIDI
// instrument is open. A preset describes the synth, not a sound or a channel:
// no program change and no channel (the instrument slot is the channel).
//
//   # OpenTracker MIDI preset
//   velocity = 127
//   offset = 0
//   slot A = 74               (CC A..F: CC number 0..127, PC, AT or PAT)
//   ...
//   cc 74 = Cutoff            (the synth's CC chart: a name per CC)
//   ...
//   lfo target = slot A       (off, slot A..F, PB or AT; CC n = the slot with CC n)
//   lfo shape = 2             (0..4: Rev saw, Saw, Triangle, Square, Random)
//   lfo rate = 8              (0..22: 128 steps .. 1/8 step)
//   lfo depth = 32            (0..64: the swing either side of the center)
//   lfo center = 64           (0..127)
//   lfo retrigger = 0         (0 Free, 1 Note)
//
// '#' starts a comment; keys are case-insensitive; unknown keys are ignored
// and missing ones keep the defaults, so later versions can add keys.
#pragma once

#include <cstddef>
#include <cstdint>

#include "model/instrument.h"
#include "model/midi_cc_names.h"

namespace ot {

struct MidiPreset {
    uint8_t velocity = 127;
    uint8_t offset = 0;                            // MidiInstrument::extra[0], 0..100
    uint8_t cc[6] = {1, 5, 7, 11, 64, 128};        // as MidiInstrument::cc (128 PC, 129 AT, 130 PAT)
    char name[128][MidiCcNames::kNameMax + 1] = {};   // CC 0..127, "" = unnamed
    MidiLfo lfo;

    // The defaults, in place (no 1.3 KB temporary on the stack).
    void reset() {
        velocity = 127;
        offset = 0;
        const uint8_t d[6] = {1, 5, 7, 11, 64, 128};
        for (int i = 0; i < 6; ++i) cc[i] = d[i];
        for (auto& n : name) n[0] = 0;
        lfo = MidiLfo{};
    }
};

namespace mpreset {
constexpr const char* kDir = "MidiPresets";
constexpr const char* kExt = ".txt";
constexpr int kNameMax = MidiCcNames::kPresetNameMax;
constexpr size_t kTextMax = 3072;   // all 128 CCs named at the longest: ~2.7 KB
}  // namespace mpreset

// Writes the preset as text (NUL-terminated); returns its length, 0 if `cap` is too small.
size_t encodeMidiPreset(const MidiPreset& p, char* out, size_t cap);
// Reads a preset; false when no known key was found (not a preset).
bool decodeMidiPreset(const char* text, size_t len, MidiPreset& p);

}  // namespace ot
