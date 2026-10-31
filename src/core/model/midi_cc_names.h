// [I] #13 MIDI CC names: each MIDI instrument's preset name and its CC chart,
// a name per CC number ("74 Cutoff") shown for the CC slots A..F, in the CC
// lists and as the MIDI LFO target. Instruments that load the same preset
// share one chart: the same synth on several channels has one set of names.
//
// Not in Project (that is held twice, the live copy in SRAM): App keeps the
// one copy in SDRAM, saved as the project folder's midi_names.bin
// (formats/midi_names.h). Names live in one pool for all charts.
#pragma once

#include <cstdint>

#include "model/constants.h"

namespace ot {

struct MidiCcNames {
    static constexpr int kPresetNameMax = 24;
    static constexpr int kNameMax = 9;       // fits a bottom-bar label
    static constexpr int kEntryMax = 400;    // all charts together (~8 synths of 50 names)
    static constexpr int kChartCount = kMidiInstrumentCount;

    struct Entry {
        uint8_t chart;
        uint8_t cc;   // 0..127
        char name[kNameMax + 1];
    };

    char preset[kMidiInstrumentCount][kPresetNameMax + 1];
    uint8_t chart[kMidiInstrumentCount];   // MIDI instrument -> chart 0..15
    Entry entry[kEntryMax];
    uint16_t count;

    // No names, no presets, instrument i on chart i.
    void clear();
    bool used() const;
    // CC `cc`'s name in MIDI instrument `midi`'s chart; "" when unnamed (or
    // not a CC 0..127).
    const char* name(int midi, int cc) const;
    // "" removes the name. False when the pool is full.
    bool setName(int midi, int cc, const char* s);
    // Removes every name of `midi`'s chart (and so of the instruments sharing it).
    void clearChart(int midi);
    // Points `midi` at the chart of another instrument with preset `name`;
    // false when there is none.
    bool share(int midi, const char* presetName);
    // Gives `midi` an unused chart of its own when it shares one.
    void unshare(int midi);
    // Keeps printable ASCII, terminates the strings, drops bad entries.
    void sanitize();
};

}  // namespace ot
