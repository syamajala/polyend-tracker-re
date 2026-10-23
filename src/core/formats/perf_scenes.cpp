#include "formats/perf_scenes.h"

#include <cstring>

namespace ot {

bool perfScenesUsed(const Project& p) {
    for (const auto& sc : p.perfScene)
        for (uint8_t r : sc)
            if (r) return true;
    for (uint8_t t : p.perfExtType)
        if (t) return true;
    return p.perfSceneOn[0] != 0 || p.perfSceneOn[1] != 1;
}

bool perfFaderUsed(const Project& p) {
    for (int ab = 0; ab < 2; ++ab)
        for (int s = 0; s < Project::kPerfSceneSlots; ++s)
            if (p.sceneOn(ab)[s]) return true;
    return false;
}

namespace {
uint8_t sum8(const uint8_t* b, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; ++i) s = uint8_t(s + b[i]);
    return s;
}
}  // namespace

void encodePerfScenes(const Project& p, uint8_t out[pscn::kFileSize]) {
    std::memset(out, 0, pscn::kFileSize);
    std::memcpy(out, "OTPS", 4);
    out[4] = pscn::kVersion;
    out[5] = uint8_t(Project::kPerfScenes);
    out[6] = uint8_t(Project::kPerfSceneSlots);
    uint8_t* r = out + pscn::kHeaderSize;
    const size_t rows = size_t(Project::kPerfScenes) * Project::kPerfSceneSlots;
    std::memcpy(r, p.perfScene, rows);
    r[rows] = sum8(r, rows);
    uint8_t* e = out + pscn::kScenesEnd;
    e[0] = uint8_t(Project::kPerfSceneSlots);
    std::memcpy(e + 1, p.perfExtType, Project::kPerfSceneSlots);
    e[1 + Project::kPerfSceneSlots] = sum8(e + 1, Project::kPerfSceneSlots);
    uint8_t* ab = out + pscn::kExtEnd;
    ab[0] = p.perfSceneOn[0];
    ab[1] = p.perfSceneOn[1];
    ab[2] = sum8(ab, 2);
}

FmtStatus decodePerfScenes(const uint8_t* file, size_t len, Project& p) {
    std::memset(p.perfScene, 0, sizeof(p.perfScene));
    std::memset(p.perfExtType, 0, sizeof(p.perfExtType));
    p.perfSceneOn[0] = 0;
    p.perfSceneOn[1] = 1;
    if (len < pscn::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTPS", 4) != 0 || file[4] < 3) return FmtStatus::BadMagic;
    const uint32_t scenes = file[5], slots = file[6];
    const size_t body = size_t(scenes) * slots;
    const size_t e = pscn::kHeaderSize + body + 1;   // the FX types block
    if (len < e + 1) return FmtStatus::TooShort;
    if (sum8(file + pscn::kHeaderSize, body) != file[pscn::kHeaderSize + body]) return FmtStatus::BadMagic;
    for (uint32_t sc = 0; sc < scenes && sc < uint32_t(Project::kPerfScenes); ++sc)
        for (uint32_t s = 0; s < slots && s < uint32_t(Project::kPerfSceneSlots); ++s) {
            const uint8_t r = file[pscn::kHeaderSize + sc * slots + s];
            p.perfScene[sc][s] = r <= 3 ? r : 0;
        }
    // The OpenTracker-only FX types (checked by perf::Source::validate).
    const uint32_t n = file[e];
    if (len < e + 1 + n + 1 || sum8(file + e + 1, n) != file[e + 1 + n]) return FmtStatus::Ok;
    for (uint32_t k = 0; k < n && k < uint32_t(Project::kPerfSceneSlots); ++k) p.perfExtType[k] = file[e + 1 + k];
    // The scenes on A and B.
    const size_t ab = e + 1 + n + 1;
    if (len >= ab + 3 && sum8(file + ab, 2) == file[ab + 2]) {
        if (file[ab] < Project::kPerfScenes) p.perfSceneOn[0] = file[ab];
        if (file[ab + 1] < Project::kPerfScenes) p.perfSceneOn[1] = file[ab + 1];
    }
    return FmtStatus::Ok;
}

}  // namespace ot
