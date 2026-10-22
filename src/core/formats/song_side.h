// [I] song.bin: the song slots' OpenTracker-only settings (Project::
// playlistCount), next to project.mt. Stock firmware and the Tracker Mini
// never read it, so project.mt stays byte-compatible: they play every slot
// once.
//
// Layout: "OTSG", version, slot count, field count, 0, then one block per
// field: the field's values for every slot, then their 8-bit sum. A reader
// takes the slots and fields it knows; later versions append fields (a
// slot's scene, its fader ramp) after the ones before.
//   field 0: repeat count (0 / 1 = once .. Project::kSongCountMax)
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/project.h"

namespace ot {

namespace song {
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeaderSize = 8;
constexpr int kFields = 1;
constexpr uint32_t kFileSize = kHeaderSize + kFields * (kSongSlots + 1);
constexpr const char* kFileName = "song.bin";
}  // namespace song

// True when a slot repeats (only then is a file written).
bool songSideUsed(const Project& p);
void encodeSongSide(const Project& p, uint8_t out[song::kFileSize]);
// Bad magic / size / checksum: every slot plays once, BadMagic or TooShort.
FmtStatus decodeSongSide(const uint8_t* file, size_t len, Project& p);

}  // namespace ot
