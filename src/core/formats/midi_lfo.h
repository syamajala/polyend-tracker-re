// [I] midi_lfo.bin: the MIDI LFO settings of a project (model/instrument.h
// MidiLfo), next to project.mt. OpenTracker only: stock firmware and the
// Tracker Mini never read it, so project.mt stays byte-compatible.
//
// Layout: "OTML", version, record count, record size, 0, the records
// (target, shape, rate, depth, center, retrig; target as MidiLfo: Off, slot
// A..F, pitch bend, pressure), then the 8-bit sum of the
// record bytes. Newer versions may append fields to a record (larger record
// size) or records; a reader takes the fields it knows.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/project.h"

namespace ot {

namespace mlfo {
constexpr uint8_t kVersion = 2;   // 2: target = slot A..F, depth 0..64 (1: CC number, %; not read)
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kRecordSize = 6;
constexpr uint32_t kFileSize = kHeaderSize + kMidiInstrumentCount * kRecordSize + 1;
constexpr const char* kFileName = "midi_lfo.bin";
}  // namespace mlfo

// True when any LFO differs from the default (only then is a file written).
bool midiLfosUsed(const Project& p);
void encodeMidiLfos(const Project& p, uint8_t out[mlfo::kFileSize]);
// Bad magic / size / checksum: every LFO default, BadMagic or TooShort.
FmtStatus decodeMidiLfos(const uint8_t* file, size_t len, Project& p);

}  // namespace ot
