// [I] perf_scenes.bin: the Performance scenes A / B of a project
// (Project::perfScene), next to project.mt. OpenTracker only: stock firmware
// and the Tracker Mini never read it, so project.mt stays byte-compatible.
//
// Layout: "OTPS", version, scene count, slots per scene, 0, the rows
// (scene-major, 0..3 each), then the 8-bit sum of the rows. A reader takes
// the scenes and slots it knows. Version 2 appends the project presets'
// OpenTracker-only FX types (Project::perfExtType): a slot count, the types,
// their 8-bit sum; version 1 readers stop before it.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/project.h"

namespace ot {

namespace pscn {
constexpr uint8_t kVersion = 2;
constexpr uint32_t kHeaderSize = 8;
constexpr uint32_t kScenesEnd = kHeaderSize + Project::kPerfScenes * Project::kPerfSceneSlots + 1;   // v1 size
constexpr uint32_t kFileSize = kScenesEnd + 1 + Project::kPerfSceneSlots + 1;
constexpr const char* kFileName = "perf_scenes.bin";
}  // namespace pscn

// True when a scene holds any row or a slot an OpenTracker-only FX type
// (only then is a file written).
bool perfScenesUsed(const Project& p);
void encodePerfScenes(const Project& p, uint8_t out[pscn::kFileSize]);
// Bad magic / size / checksum: both scenes empty, BadMagic or TooShort.
FmtStatus decodePerfScenes(const uint8_t* file, size_t len, Project& p);

}  // namespace ot
