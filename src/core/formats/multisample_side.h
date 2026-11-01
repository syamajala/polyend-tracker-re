// [I] #3 multisample.bin: the zones of a project's multisample instruments
// (model/multisample.h gMultiMaps), next to project.mt. OpenTracker only:
// stock firmware and the Tracker Mini never read it and play those
// instruments as the Slice instruments their .pti files describe.
//
// Layout: "OTMS", version, record count, 0, 0, then per multisample slot
// (slot, on, zone count, the zones' roots), then the 8-bit sum of every
// byte after the header. Written only while a slot holds zones.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/multisample.h"

namespace ot {

class IStorage;

namespace msside {
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kMaxFileSize = kHeaderSize + kSampleInstrumentCount * (3 + kMultiMaxZones) + 1;
constexpr const char* kFileName = "multisample.bin";
}  // namespace msside

bool multiMapsUsed();
// Returns the bytes written (at most msside::kMaxFileSize).
uint32_t encodeMultiMaps(uint8_t* out);
// Clears every map first; bad magic / size / checksum leaves them clear.
FmtStatus decodeMultiMaps(const uint8_t* file, size_t len);

// The file next to a ".../project.mt" path: load (a missing file clears the
// maps), save (removed when no slot holds zones).
FmtStatus loadMultiMapsFile(IStorage& fs, const char* projectPath);
FmtStatus saveMultiMapsFile(IStorage& fs, const char* projectPath);
// Copies (or removes) the file from one project folder to another; the maps stay.
FmtStatus copyMultiMapsFile(IStorage& fs, const char* srcProjectPath, const char* dstProjectPath);

}  // namespace ot
