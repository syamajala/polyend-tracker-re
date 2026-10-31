// [I] #13 midi_names.bin: each MIDI instrument's preset name and CC chart
// (model/midi_cc_names.h), next to project.mt. OpenTracker only: stock
// firmware and the Tracker Mini never read it, so project.mt stays
// byte-compatible.
//
// Layout (version 2): "OTMN", version, instrument count (16), preset name
// size (25), 0; the preset names (16 x 25, zero-padded); the chart of each
// instrument (16 bytes); the entry count (u16 LE); the entries (chart, CC,
// name [10] zero-padded); then the 8-bit sum of every byte after the header.
// Version 1 (slot labels) is not read: there are no such projects yet.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/midi_cc_names.h"

namespace ot {

namespace mnames {
constexpr uint8_t kVersion = 2;
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kPresetSize = MidiCcNames::kPresetNameMax + 1;
constexpr uint32_t kEntrySize = 2 + MidiCcNames::kNameMax + 1;
constexpr uint32_t kFixedSize = kHeaderSize + kMidiInstrumentCount * (kPresetSize + 1) + 2;
constexpr uint32_t kFileSize = kFixedSize + MidiCcNames::kEntryMax * kEntrySize + 1;   // the largest
constexpr const char* kFileName = "midi_names.bin";
}  // namespace mnames

// Bytes encodeMidiNames() writes for `n`.
uint32_t midiNamesSize(const MidiCcNames& n);
void encodeMidiNames(const MidiCcNames& n, uint8_t* out);
// Bad magic / version / size / checksum: `n` cleared, BadMagic or TooShort.
FmtStatus decodeMidiNames(const uint8_t* file, size_t len, MidiCcNames& n);

}  // namespace ot
