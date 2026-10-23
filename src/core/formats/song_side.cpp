#include "formats/song_side.h"

#include <cstring>

namespace ot {

bool songSideUsed(const Project& p) {
    for (int i = 0; i < kSongSlots && p.playlist[i]; ++i)
        if (p.slotCount(i) > 1 || p.slotScene(i)) return true;
    return false;
}

void encodeSongSide(const Project& p, uint8_t out[song::kFileSize]) {
    std::memset(out, 0, song::kFileSize);
    std::memcpy(out, "OTSG", 4);
    out[4] = song::kVersion;
    out[5] = uint8_t(kSongSlots);
    out[6] = uint8_t(song::kFields);
    for (int k = 0; k < song::kFields; ++k) {
        uint8_t* f = out + song::kHeaderSize + k * (kSongSlots + 1);
        uint8_t sum = 0;
        for (int i = 0; i < kSongSlots; ++i) {
            f[i] = uint8_t(k == 0 ? p.slotCount(i) : (k == 1 ? p.slotScene(i) : p.slotSceneTo(i)));
            sum = uint8_t(sum + f[i]);
        }
        f[kSongSlots] = sum;
    }
}

FmtStatus decodeSongSide(const uint8_t* file, size_t len, Project& p) {
    std::memset(p.playlistCount, 0, sizeof(p.playlistCount));
    std::memset(p.playlistScene, 0, sizeof(p.playlistScene));
    std::memset(p.playlistSceneTo, 0, sizeof(p.playlistSceneTo));
    if (len < song::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTSG", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t slots = file[5], fields = file[6];
    uint8_t* dst[song::kFields] = {p.playlistCount, p.playlistScene, p.playlistSceneTo};
    const uint8_t max[song::kFields] = {uint8_t(Project::kSongCountMax), uint8_t(Project::kPerfScenes),
                                        uint8_t(Project::kPerfScenes)};
    for (uint32_t k = 0; k < fields && k < uint32_t(song::kFields); ++k) {
        const size_t off = song::kHeaderSize + k * (slots + 1);
        if (len < off + slots + 1) return FmtStatus::TooShort;
        const uint8_t* f = file + off;
        uint8_t sum = 0;
        for (uint32_t i = 0; i < slots; ++i) sum = uint8_t(sum + f[i]);
        if (sum != f[slots]) return FmtStatus::BadMagic;
        for (uint32_t i = 0; i < slots && i < uint32_t(kSongSlots); ++i) dst[k][i] = f[i] <= max[k] ? f[i] : 0;
    }
    return FmtStatus::Ok;
}

}  // namespace ot
