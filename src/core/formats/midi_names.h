// [I] #13 midi_names.bin: each MIDI instrument's preset name and CC slot labels
// (model/instrument.h MidiPresetInfo), next to project.mt. OpenTracker only:
// stock firmware and the Tracker Mini never read it, so project.mt stays
// byte-compatible.
//
// Layout: "OTMN", version, record count, record size, 0, the records (name
// [25], then labels A..F [10 each], zero-padded), then the 8-bit sum of the
// record bytes. Newer versions may append fields to a record (larger record
// size) or records; a reader takes the fields it knows.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/project.h"

namespace ot {

namespace mnames {
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kNameSize = MidiPresetInfo::kNameMax + 1;
constexpr uint32_t kLabelSize = MidiPresetInfo::kLabelMax + 1;
constexpr uint32_t kRecordSize = kNameSize + 6 * kLabelSize;
constexpr uint32_t kFileSize = kHeaderSize + kMidiInstrumentCount * kRecordSize + 1;
constexpr const char* kFileName = "midi_names.bin";
}  // namespace mnames

// True when any instrument has a preset name or a label (only then is a file written).
bool midiNamesUsed(const Project& p);
void encodeMidiNames(const Project& p, uint8_t out[mnames::kFileSize]);
// Bad magic / size / checksum: every name and label empty, BadMagic or TooShort.
FmtStatus decodeMidiNames(const uint8_t* file, size_t len, Project& p);

}  // namespace ot
