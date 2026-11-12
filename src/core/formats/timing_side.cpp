#include "formats/timing_side.h"

#include <cstdio>
#include <cstring>

#include "formats/file_io.h"

namespace ot {

namespace {
bool patternDefault(const TrackTiming* p) {
    for (int t = 0; t < kTrackCount; ++t)
        if (!p[t].isDefault()) return false;
    return true;
}

bool sidePath(char* out, size_t cap, const char* projectPath) {
    const size_t n = std::strlen(projectPath), k = std::strlen("/project.mt");
    if (n < k || std::strcmp(projectPath + n - k, "/project.mt") != 0) return false;
    const int w = std::snprintf(out, cap, "%.*s/%s", int(n - k), projectPath, tmside::kFileName);
    return w > 0 && size_t(w) < cap;
}
}  // namespace

bool timingUsed(const TrackTiming* store) {
    for (int n = 0; n < kPatternCount; ++n)
        if (!patternDefault(store + n * kTrackCount)) return true;
    return false;
}

uint32_t encodeTiming(const TrackTiming* store, uint8_t* out) {
    std::memcpy(out, "OTTM", 4);
    out[4] = tmside::kVersion;
    out[7] = 0;
    uint32_t o = tmside::kHeaderSize, count = 0;
    uint8_t sum = 0;
    for (int n = 0; n < kPatternCount; ++n) {
        const TrackTiming* p = store + n * kTrackCount;
        if (patternDefault(p)) continue;
        out[o++] = uint8_t(n + 1);
        for (int t = 0; t < kTrackCount; ++t) {
            out[o++] = p[t].speed;
            out[o++] = p[t].groove;
        }
        ++count;
    }
    for (uint32_t i = tmside::kHeaderSize; i < o; ++i) sum = uint8_t(sum + out[i]);
    out[5] = uint8_t(count);
    out[6] = uint8_t(count >> 8);
    out[o++] = sum;
    return o;
}

FmtStatus decodeTiming(const uint8_t* file, size_t len, TrackTiming* store) {
    for (int i = 0; i < kPatternCount * kTrackCount; ++i) store[i] = TrackTiming{};
    if (len < tmside::kHeaderSize + 1) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTTM", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t count = uint32_t(file[5]) | uint32_t(file[6]) << 8;
    const size_t end = tmside::kHeaderSize + size_t(count) * tmside::kRecordSize;
    if (count > uint32_t(kPatternCount) || len < end + 1) return FmtStatus::TooShort;
    uint8_t sum = 0;
    for (size_t i = tmside::kHeaderSize; i < end; ++i) sum = uint8_t(sum + file[i]);
    if (sum != file[end]) return FmtStatus::BadMagic;
    for (uint32_t r = 0; r < count; ++r) {
        const uint8_t* rec = file + tmside::kHeaderSize + r * tmside::kRecordSize;
        const int n = rec[0];
        if (n < 1 || n > kPatternCount) continue;
        TrackTiming* p = store + (n - 1) * kTrackCount;
        for (int t = 0; t < kTrackCount; ++t) {
            const uint8_t sp = rec[1 + 2 * t], gr = rec[2 + 2 * t];
            p[t].speed = sp < timing::kSpeedCount ? sp : uint8_t(timing::kSpeedDefault);
            p[t].groove = gr < timing::kGrooveCount ? gr : 0;
        }
    }
    return FmtStatus::Ok;
}

FmtStatus loadTimingFile(IStorage& fs, const char* projectPath, TrackTiming* store, uint8_t* buf) {
    for (int i = 0; i < kPatternCount * kTrackCount; ++i) store[i] = TrackTiming{};
    char sp[352];
    if (!sidePath(sp, sizeof(sp), projectPath) || !fs.exists(sp)) return FmtStatus::Ok;
    uint32_t got = 0;
    const FmtStatus st = readWholeFile(fs, sp, buf, tmside::kMaxFileSize, &got);
    if (st != FmtStatus::Ok) return st;
    return decodeTiming(buf, got, store);
}

FmtStatus saveTimingFile(IStorage& fs, const char* projectPath, const TrackTiming* store, uint8_t* buf) {
    char sp[352];
    if (!sidePath(sp, sizeof(sp), projectPath)) return FmtStatus::Ok;
    if (!timingUsed(store)) return fs.exists(sp) && !fs.remove(sp) ? FmtStatus::IoError : FmtStatus::Ok;
    return writeWholeFile(fs, sp, buf, encodeTiming(store, buf));
}

FmtStatus copyTimingFile(IStorage& fs, const char* srcProjectPath, const char* dstProjectPath, uint8_t* buf) {
    char sp[352], dp[352];
    if (!sidePath(sp, sizeof(sp), srcProjectPath) || !sidePath(dp, sizeof(dp), dstProjectPath)) return FmtStatus::Ok;
    if (!fs.exists(sp)) return fs.exists(dp) && !fs.remove(dp) ? FmtStatus::IoError : FmtStatus::Ok;
    uint32_t got = 0;
    const FmtStatus st = readWholeFile(fs, sp, buf, tmside::kMaxFileSize, &got);
    return st != FmtStatus::Ok ? st : writeWholeFile(fs, dp, buf, got);
}

}  // namespace ot
