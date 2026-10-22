#include "formats/song_side.h"

#include <cstring>

namespace ot {

bool songSideUsed(const Project& p) {
    for (int i = 0; i < kSongSlots && p.playlist[i]; ++i)
        if (p.slotCount(i) > 1) return true;
    return false;
}

void encodeSongSide(const Project& p, uint8_t out[song::kFileSize]) {
    std::memset(out, 0, song::kFileSize);
    std::memcpy(out, "OTSG", 4);
    out[4] = song::kVersion;
    out[5] = uint8_t(kSongSlots);
    out[6] = uint8_t(song::kFields);
    uint8_t* f = out + song::kHeaderSize;
    uint8_t sum = 0;
    for (int i = 0; i < kSongSlots; ++i) {
        f[i] = uint8_t(p.slotCount(i));
        sum = uint8_t(sum + f[i]);
    }
    f[kSongSlots] = sum;
}

FmtStatus decodeSongSide(const uint8_t* file, size_t len, Project& p) {
    std::memset(p.playlistCount, 0, sizeof(p.playlistCount));
    if (len < song::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTSG", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t slots = file[5], fields = file[6];
    if (fields < 1) return FmtStatus::Ok;
    if (len < song::kHeaderSize + slots + 1) return FmtStatus::TooShort;
    const uint8_t* f = file + song::kHeaderSize;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < slots; ++i) sum = uint8_t(sum + f[i]);
    if (sum != f[slots]) return FmtStatus::BadMagic;
    for (uint32_t i = 0; i < slots && i < uint32_t(kSongSlots); ++i)
        p.playlistCount[i] = f[i] <= Project::kSongCountMax ? f[i] : 0;
    return FmtStatus::Ok;
}

}  // namespace ot
