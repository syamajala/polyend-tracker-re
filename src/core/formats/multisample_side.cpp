// [I] #3 multisample.bin codec (formats/multisample_side.h).
#include "formats/multisample_side.h"

#include <cstdio>
#include <cstring>

#include "formats/file_io.h"
#include "hal_api.h"
#include "sdram_bss.h"

namespace ot {

bool multiMapsUsed() {
    for (const MultiMap& m : gMultiMaps)
        if (m.count > 0) return true;
    return false;
}

uint32_t encodeMultiMaps(uint8_t* out) {
    std::memcpy(out, "OTMS", 4);
    out[4] = msside::kVersion;
    out[6] = out[7] = 0;
    uint32_t o = msside::kHeaderSize;
    uint8_t n = 0;
    for (int s = 0; s < kSampleInstrumentCount; ++s) {
        const MultiMap& m = gMultiMaps[s];
        if (m.count == 0) continue;
        const uint8_t c = m.count > kMultiMaxZones ? uint8_t(kMultiMaxZones) : m.count;
        out[o++] = uint8_t(s);
        out[o++] = m.on ? 1 : 0;
        out[o++] = c;
        std::memcpy(out + o, m.root, c);
        o += c;
        ++n;
    }
    out[5] = n;
    uint8_t sum = 0;
    for (uint32_t i = msside::kHeaderSize; i < o; ++i) sum = uint8_t(sum + out[i]);
    out[o++] = sum;
    return o;
}

FmtStatus decodeMultiMaps(const uint8_t* file, size_t len) {
    for (int s = 0; s < kSampleInstrumentCount; ++s) multiClear(s);
    if (!file || len < msside::kHeaderSize + 1) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTMS", 4) != 0 || file[4] != msside::kVersion) return FmtStatus::BadMagic;
    uint8_t sum = 0;
    for (size_t i = msside::kHeaderSize; i + 1 < len; ++i) sum = uint8_t(sum + file[i]);
    if (sum != file[len - 1]) return FmtStatus::BadMagic;
    const size_t end = len - 1;
    size_t o = msside::kHeaderSize;
    for (int r = 0; r < file[5]; ++r) {
        if (o + 3 > end) return FmtStatus::TooShort;
        const uint8_t slot = file[o], on = file[o + 1], c = file[o + 2];
        o += 3;
        if (o + c > end) return FmtStatus::TooShort;
        if (slot < kSampleInstrumentCount && c > 0 && c <= kMultiMaxZones) {
            MultiMap& m = gMultiMaps[slot];
            m.on = on ? 1 : 0;
            m.count = c;
            std::memcpy(m.root, file + o, c);
        }
        o += c;
    }
    return FmtStatus::Ok;
}

namespace {
bool sidePath(char* out, size_t cap, const char* projectPath) {
    const size_t n = std::strlen(projectPath), k = std::strlen("/project.mt");
    if (n < k || std::strcmp(projectPath + n - k, "/project.mt") != 0) return false;
    const int w = std::snprintf(out, cap, "%.*s/%s", int(n - k), projectPath, msside::kFileName);
    return w > 0 && size_t(w) < cap;
}
OT_SDRAM_BSS uint8_t g_buf[msside::kMaxFileSize];   // main loop only
}  // namespace

FmtStatus loadMultiMapsFile(IStorage& fs, const char* projectPath) {
    for (int s = 0; s < kSampleInstrumentCount; ++s) multiClear(s);
    char sp[352];
    if (!sidePath(sp, sizeof(sp), projectPath) || !fs.exists(sp)) return FmtStatus::Ok;
    uint32_t got = 0;
    const FmtStatus st = readWholeFile(fs, sp, g_buf, sizeof(g_buf), &got);
    if (st != FmtStatus::Ok) return st;
    return decodeMultiMaps(g_buf, got);
}

FmtStatus saveMultiMapsFile(IStorage& fs, const char* projectPath) {
    char sp[352];
    if (!sidePath(sp, sizeof(sp), projectPath)) return FmtStatus::Ok;
    if (!multiMapsUsed()) return fs.exists(sp) && !fs.remove(sp) ? FmtStatus::IoError : FmtStatus::Ok;
    const uint32_t n = encodeMultiMaps(g_buf);
    return writeWholeFile(fs, sp, g_buf, n);
}

FmtStatus copyMultiMapsFile(IStorage& fs, const char* srcProjectPath, const char* dstProjectPath) {
    char sp[352], dp[352];
    if (!sidePath(sp, sizeof(sp), srcProjectPath) || !sidePath(dp, sizeof(dp), dstProjectPath)) return FmtStatus::Ok;
    if (!fs.exists(sp)) return fs.exists(dp) && !fs.remove(dp) ? FmtStatus::IoError : FmtStatus::Ok;
    uint32_t got = 0;
    const FmtStatus st = readWholeFile(fs, sp, g_buf, sizeof(g_buf), &got);
    return st != FmtStatus::Ok ? st : writeWholeFile(fs, dp, g_buf, got);
}

}  // namespace ot
