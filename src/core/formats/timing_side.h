// [I] #43 / #83 timing.bin: the patterns' per-track Speed and Groove
// (Pattern::timing, App::storedTiming), next to project.mt. OpenTracker
// only: stock firmware and the Tracker Mini never read it and play every
// track at 1x with the Swing FX.
//
// Layout: "OTTM", version, record count (2 bytes, little endian), 0, then
// one record per pattern with any track off the defaults (pattern number,
// then speed and groove of each of the 16 tracks), then the 8-bit sum of
// every byte after the header. Written only while a record exists.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/constants.h"
#include "model/pattern.h"

namespace ot {

class IStorage;

namespace tmside {
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kRecordSize = 1 + 2 * kTrackCount;
constexpr uint32_t kMaxFileSize = kHeaderSize + kPatternCount * kRecordSize + 1;
constexpr const char* kFileName = "timing.bin";
}  // namespace tmside

// `store`: kPatternCount * kTrackCount entries, pattern n at [(n-1) * kTrackCount].
bool timingUsed(const TrackTiming* store);
// Returns the bytes written (at most tmside::kMaxFileSize).
uint32_t encodeTiming(const TrackTiming* store, uint8_t* out);
// Resets the store first; bad magic / size / checksum leaves it default.
// Out-of-range values read as the defaults.
FmtStatus decodeTiming(const uint8_t* file, size_t len, TrackTiming* store);

// The file next to a ".../project.mt" path: load (a missing file resets the
// store), save (removed when every pattern is default), copy (or remove)
// between two project folders. `buf` holds tmside::kMaxFileSize bytes.
FmtStatus loadTimingFile(IStorage& fs, const char* projectPath, TrackTiming* store, uint8_t* buf);
FmtStatus saveTimingFile(IStorage& fs, const char* projectPath, const TrackTiming* store, uint8_t* buf);
FmtStatus copyTimingFile(IStorage& fs, const char* srcProjectPath, const char* dstProjectPath, uint8_t* buf);

}  // namespace ot
