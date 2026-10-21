#include "formats/perf_scenes.h"

#include <cstring>

namespace ot {

bool perfScenesUsed(const Project& p) {
    for (const auto& sc : p.perfScene)
        for (uint8_t r : sc)
            if (r) return true;
    for (uint8_t t : p.perfExtType)
        if (t) return true;
    return false;
}

void encodePerfScenes(const Project& p, uint8_t out[pscn::kFileSize]) {
    std::memset(out, 0, pscn::kFileSize);
    std::memcpy(out, "OTPS", 4);
    out[4] = pscn::kVersion;
    out[5] = uint8_t(Project::kPerfScenes);
    out[6] = uint8_t(Project::kPerfSceneSlots);
    uint8_t sum = 0;
    uint8_t* r = out + pscn::kHeaderSize;
    for (const auto& sc : p.perfScene)
        for (uint8_t v : sc) {
            *r++ = v;
            sum = uint8_t(sum + v);
        }
    out[pscn::kScenesEnd - 1] = sum;
    uint8_t* e = out + pscn::kScenesEnd;
    *e++ = uint8_t(Project::kPerfSceneSlots);
    sum = 0;
    for (uint8_t t : p.perfExtType) {
        *e++ = t;
        sum = uint8_t(sum + t);
    }
    *e = sum;
}

FmtStatus decodePerfScenes(const uint8_t* file, size_t len, Project& p) {
    std::memset(p.perfScene, 0, sizeof(p.perfScene));
    std::memset(p.perfExtType, 0, sizeof(p.perfExtType));
    if (len < pscn::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTPS", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t scenes = file[5], slots = file[6];
    const size_t body = size_t(scenes) * slots;
    if (len < pscn::kHeaderSize + body + 1) return FmtStatus::TooShort;
    uint8_t sum = 0;
    for (size_t k = 0; k < body; ++k) sum = uint8_t(sum + file[pscn::kHeaderSize + k]);
    if (sum != file[pscn::kHeaderSize + body]) return FmtStatus::BadMagic;
    for (uint32_t sc = 0; sc < scenes && sc < uint32_t(Project::kPerfScenes); ++sc)
        for (uint32_t s = 0; s < slots && s < uint32_t(Project::kPerfSceneSlots); ++s) {
            const uint8_t r = file[pscn::kHeaderSize + sc * slots + s];
            p.perfScene[sc][s] = r <= 3 ? r : 0;
        }
    // v2: the OpenTracker-only FX types (checked by perf::Source::validate).
    const size_t e = pscn::kHeaderSize + body + 1;
    if (file[4] >= 2 && len >= e + 1) {
        const uint32_t n = file[e];
        if (len >= e + 1 + n + 1) {
            uint8_t esum = 0;
            for (uint32_t k = 0; k < n; ++k) esum = uint8_t(esum + file[e + 1 + k]);
            if (esum == file[e + 1 + n])
                for (uint32_t k = 0; k < n && k < uint32_t(Project::kPerfSceneSlots); ++k) p.perfExtType[k] = file[e + 1 + k];
        }
    }
    return FmtStatus::Ok;
}

}  // namespace ot
