// [I] perf_scenes.bin: the Performance scene bank of a project
// (Project::perfScene, perfSceneOn) and the OpenTracker-only FX types of
// its presets (perfExtType), next to project.mt. OpenTracker only: stock
// firmware and the Tracker Mini never read it, so project.mt stays
// byte-compatible.
//
// Layout (version 3): "OTPS", version, scene count, slots per scene, 0,
// then three blocks, each followed by the 8-bit sum of its bytes:
//   the rows (scene-major, 0..3 each);
//   a slot count and the OpenTracker-only FX types;
//   the scenes on A and B (bank indices).
// Older versions (scenes A / B only) are not read: no such projects exist.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/project.h"

namespace ot {

namespace pscn {
constexpr uint8_t kVersion = 3;
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kScenesEnd = kHeaderSize + Project::kPerfScenes * Project::kPerfSceneSlots + 1;
constexpr uint32_t kExtEnd = kScenesEnd + 1 + Project::kPerfSceneSlots + 1;
constexpr uint32_t kFileSize = kExtEnd + 2 + 1;
constexpr const char* kFileName = "perf_scenes.bin";
}  // namespace pscn

// True when a scene holds any row, a slot an OpenTracker-only FX type, or
// A / B point elsewhere than scenes 1 / 2 (only then is a file written).
bool perfScenesUsed(const Project& p);
// True when the scene on A or B holds a row (the crossfader has something to do).
bool perfFaderUsed(const Project& p);
void encodePerfScenes(const Project& p, uint8_t out[pscn::kFileSize]);
// Bad magic / size / checksum or an older version: the bank empty, A / B on
// scenes 1 / 2; BadMagic or TooShort.
FmtStatus decodePerfScenes(const uint8_t* file, size_t len, Project& p);

}  // namespace ot
