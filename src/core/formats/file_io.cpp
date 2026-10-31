// Storage helpers, spec 02 §0, §1.
#include "formats/file_io.h"

#include <cstdio>
#include <cstring>

#include "formats/bytes.h"
#include "formats/midi_lfo.h"
#include "formats/midi_names.h"
#include "formats/perf_scenes.h"
#include "formats/song_side.h"
#include "formats/patterns_metadata.h"
#include "sdram_bss.h"

namespace ot {

namespace {
// Shared staging buffer sized for the largest fixed-size file
// (patternsMetadata, 0x3210). Not re-entrant: storage calls run on the main
// loop only.
// SDRAM on the device (OP-5; SD transfers go direct or bounce, sdhc.cpp).
OT_SDRAM_BSS uint8_t g_io[pamd::kFileSize];
static_assert(sizeof(g_io) >= mtp::kFileSize && sizeof(g_io) >= mt::kFileSize &&
                  sizeof(g_io) >= recents::kFileSize && sizeof(g_io) >= song::kFileSize &&
                  sizeof(g_io) >= mnames::kFileSize,
              "staging buffer too small");

bool fmt(char*, size_t cap, int n) { return n >= 0 && size_t(n) < cap; }

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
constexpr bool kLittleEndian = true;
#else
constexpr bool kLittleEndian = false;
#endif
}  // namespace

FmtStatus readWholeFile(IStorage& fs, const char* path, uint8_t* buf, uint32_t requested, uint32_t* bytesRead) {
    if (bytesRead) *bytesRead = 0;
    std::memset(buf, 0, requested);
    IStorage::Handle h = fs.open(path, IStorage::kRead);
    if (h < 0) return fs.exists(path) ? FmtStatus::IoError : FmtStatus::NotFound;
    int32_t size = fs.size(h);
    if (size < 0) {
        fs.close(h);
        return FmtStatus::IoError;
    }
    uint32_t want = uint32_t(size) < requested ? uint32_t(size) : requested;
    uint32_t done = 0;
    while (done < want) {
        uint32_t c = want - done < kIoChunk ? want - done : kIoChunk;
        int32_t r = fs.read(h, buf + done, c);
        if (r <= 0) {
            fs.close(h);
            return FmtStatus::IoError;
        }
        done += uint32_t(r);
    }
    fs.close(h);
    if (bytesRead) *bytesRead = done;
    return FmtStatus::Ok;
}

FmtStatus writeWholeFile(IStorage& fs, const char* path, const uint8_t* buf, uint32_t len) {
    if (fs.exists(path) && !fs.remove(path)) return FmtStatus::IoError;
    IStorage::Handle h = fs.open(path, IStorage::kWrite);
    if (h < 0) return FmtStatus::IoError;
    uint32_t done = 0;
    while (done < len) {
        uint32_t c = len - done < kIoChunk ? len - done : kIoChunk;
        if (fs.write(h, buf + done, c) != int32_t(c)) {
            fs.close(h);
            return FmtStatus::IoError;
        }
        done += c;
    }
    fs.close(h);
    return FmtStatus::Ok;
}

bool projectFilePath(char* out, size_t cap, const char* base) {
    return fmt(out, cap, std::snprintf(out, cap, "%s/project.mt", base));
}
bool patternFilePath(char* out, size_t cap, const char* base, int n) {
    return fmt(out, cap, std::snprintf(out, cap, "%s/patterns/pattern_%02d.mtp", base, n));
}
bool patternNamesPath(char* out, size_t cap, const char* base) {
    return fmt(out, cap, std::snprintf(out, cap, "%s/patterns/patternsMetadata", base));
}
bool workspaceInstrumentPath(char* out, size_t cap, const char* base, int n) {
    return fmt(out, cap, std::snprintf(out, cap, "%s/instruments/instrument_%02d.pti", base, n));
}
bool savedInstrumentPath(char* out, size_t cap, const char* base, int n, const char* name) {
    return fmt(out, cap, std::snprintf(out, cap, "%s/instruments/%d %s.pti", base, n, name));
}

// [I] An OpenTracker side file `name` next to a ".../project.mt" path; false for other paths.
static bool sideFilePath(char* out, size_t cap, const char* projectPath, const char* name) {
    const size_t n = std::strlen(projectPath), k = std::strlen("/project.mt");
    if (n < k || std::strcmp(projectPath + n - k, "/project.mt") != 0) return false;
    return fmt(out, cap, std::snprintf(out, cap, "%.*s/%s", int(n - k), projectPath, name));
}

// [I] Writes side file `name` (encoded by `encode` into g_io) when `used`,
// else removes it.
template <class Encode>
static FmtStatus saveSideFile(IStorage& fs, const char* projectPath, const char* name, bool used, Encode encode,
                              uint32_t size) {
    char sp[352];
    if (!sideFilePath(sp, sizeof(sp), projectPath, name)) return FmtStatus::Ok;
    if (!used) return fs.exists(sp) && !fs.remove(sp) ? FmtStatus::IoError : FmtStatus::Ok;
    encode(g_io);
    return writeWholeFile(fs, sp, g_io, size);
}

FmtStatus loadProjectFile(IStorage& fs, const char* path, Project& p, ProjectLoadInfo* info, MidiCcNames* names) {
    uint32_t got = 0;
    FmtStatus st = readWholeFile(fs, path, g_io, mt::kFileSize, &got);
    if (st != FmtStatus::Ok) return st;
    st = decodeProject(g_io, got, p, info);
    if (st != FmtStatus::Ok) return st;
    // [I] OpenTracker side files: a missing or damaged one leaves its data default.
    for (MidiLfo& l : p.midiLfo) l = MidiLfo{};
    std::memset(p.perfScene, 0, sizeof(p.perfScene));
    p.perfSceneOn[0] = 0;
    p.perfSceneOn[1] = 1;
    std::memset(p.perfExtType, 0, sizeof(p.perfExtType));
    std::memset(p.playlistCount, 0, sizeof(p.playlistCount));
    char sp[352];
    if (sideFilePath(sp, sizeof(sp), path, mlfo::kFileName) && fs.exists(sp) &&
        readWholeFile(fs, sp, g_io, mlfo::kFileSize, &got) == FmtStatus::Ok)
        decodeMidiLfos(g_io, got, p);
    if (names) {
        names->clear();
        if (sideFilePath(sp, sizeof(sp), path, mnames::kFileName) && fs.exists(sp) &&
            readWholeFile(fs, sp, g_io, mnames::kFileSize, &got) == FmtStatus::Ok)
            decodeMidiNames(g_io, got, *names);
    }
    if (sideFilePath(sp, sizeof(sp), path, pscn::kFileName) && fs.exists(sp) &&
        readWholeFile(fs, sp, g_io, pscn::kFileSize, &got) == FmtStatus::Ok)
        decodePerfScenes(g_io, got, p);
    if (sideFilePath(sp, sizeof(sp), path, song::kFileName) && fs.exists(sp) &&
        readWholeFile(fs, sp, g_io, song::kFileSize, &got) == FmtStatus::Ok)
        decodeSongSide(g_io, got, p);
    return FmtStatus::Ok;
}

FmtStatus saveProjectFile(IStorage& fs, const char* path, const Project& p, const MidiCcNames* names) {
    encodeProject(p, g_io);
    const FmtStatus st = writeWholeFile(fs, path, g_io, mt::kFileSize);
    if (st != FmtStatus::Ok) return st;
    // [I] OpenTracker side files (midi_lfo.bin, midi_names.bin, perf_scenes.bin, song.bin), each only
    // while it holds something, so other projects keep exactly the stock file set.
    const FmtStatus side = saveSideFile(fs, path, mlfo::kFileName, midiLfosUsed(p),
                                  [&](uint8_t* out) { encodeMidiLfos(p, out); }, mlfo::kFileSize);
    const FmtStatus nms = !names ? FmtStatus::Ok
                                 : saveSideFile(fs, path, mnames::kFileName, names->used(),
                                                [&](uint8_t* out) { encodeMidiNames(*names, out); },
                                                midiNamesSize(*names));
    const FmtStatus scn = saveSideFile(fs, path, pscn::kFileName, perfScenesUsed(p),
                                       [&](uint8_t* out) { encodePerfScenes(p, out); }, pscn::kFileSize);
    const FmtStatus sng = saveSideFile(fs, path, song::kFileName, songSideUsed(p),
                                       [&](uint8_t* out) { encodeSongSide(p, out); }, song::kFileSize);
    return side != FmtStatus::Ok ? side : nms != FmtStatus::Ok ? nms : scn != FmtStatus::Ok ? scn : sng;
}

FmtStatus copyMidiNamesFile(IStorage& fs, const char* srcProjectPath, const char* dstProjectPath) {
    char sp[352], dp[352];
    if (!sideFilePath(sp, sizeof(sp), srcProjectPath, mnames::kFileName) ||
        !sideFilePath(dp, sizeof(dp), dstProjectPath, mnames::kFileName))
        return FmtStatus::Ok;
    if (!fs.exists(sp)) return fs.exists(dp) && !fs.remove(dp) ? FmtStatus::IoError : FmtStatus::Ok;
    uint32_t got = 0;
    const FmtStatus st = readWholeFile(fs, sp, g_io, mnames::kFileSize, &got);
    return st != FmtStatus::Ok ? st : writeWholeFile(fs, dp, g_io, got);
}

// 2.x: pattern files are the Tracker Mini 16-track layout (spec 21 §4):
// any stock / legacy size loads (upgraded per §4.2) and saves as 0x3030.
static_assert(sizeof(g_io) >= mtp::kMiniFileSize, "g_io holds a 16-track pattern");
FmtStatus loadPatternFile(IStorage& fs, const char* path, PatternStock& p, const PatternUpgradeContext* ctx,
                          PatternLoadInfo* info) {
    uint32_t got = 0;
    FmtStatus st = readWholeFile(fs, path, g_io, mtp::kMiniFileSize, &got);
    if (st != FmtStatus::Ok) return st;
    static Track ext[kExtTrackCount];   // tracks 12..15 dropped (stock store)
    decodePattern16(g_io, got, p, ext, ctx, info);
    return FmtStatus::Ok;
}
FmtStatus loadPatternFile(IStorage& fs, const char* path, Pattern& p, const PatternUpgradeContext* ctx,
                          PatternLoadInfo* info) {
    uint32_t got = 0;
    FmtStatus st = readWholeFile(fs, path, g_io, mtp::kMiniFileSize, &got);
    if (st != FmtStatus::Ok) return st;
    decodePattern16(g_io, got, p, ctx, info);
    return FmtStatus::Ok;
}
FmtStatus savePatternFile(IStorage& fs, const char* path, const PatternStock& p) {
    const size_t n = encodePattern16(p, nullptr, g_io);
    return writeWholeFile(fs, path, g_io, uint32_t(n));
}
FmtStatus savePatternFile(IStorage& fs, const char* path, const Pattern& p) {
    const size_t n = encodePattern16(p, g_io);
    return writeWholeFile(fs, path, g_io, uint32_t(n));
}

FmtStatus loadPatternNamesFile(IStorage& fs, const char* path, PatternNames& n) {
    uint32_t got = 0;
    FmtStatus st = readWholeFile(fs, path, g_io, pamd::kFileSize, &got);
    if (st != FmtStatus::Ok) {
        n.clear();
        return st;
    }
    return decodePatternNames(g_io, got, n);
}

FmtStatus savePatternNamesFile(IStorage& fs, const char* path, const PatternNames& n) {
    encodePatternNames(n, g_io);
    return writeWholeFile(fs, path, g_io, pamd::kFileSize);
}

FmtStatus loadRecentsFile(IStorage& fs, const char* path, Recents& r) {
    uint32_t got = 0;
    FmtStatus st = readWholeFile(fs, path, g_io, recents::kFileSize, &got);
    if (st != FmtStatus::Ok) {
        r.clear();
        return st;
    }
    decodeRecents(g_io, got, r);
    return FmtStatus::Ok;
}

FmtStatus saveRecentsFile(IStorage& fs, const char* path, const Recents& r) {
    encodeRecents(r, g_io);
    return writeWholeFile(fs, path, g_io, recents::kFileSize);
}

FmtStatus loadInstrumentFile(IStorage& fs, const char* path, Instrument& ins, int16_t* samples, size_t capSamples,
                             bool stereoEnabled, PtiSampleLayout* layoutOut) {
    IStorage::Handle h = fs.open(path, IStorage::kRead);
    if (h < 0) return FmtStatus::NotFound;
    int32_t size = fs.size(h);
    uint8_t hdr[pti::kHeaderBlockSize] = {};
    int32_t got = size > 0 ? fs.read(h, hdr, sizeof(hdr)) : 0;
    if (size < 0 || got < 0) {
        fs.close(h);
        return FmtStatus::IoError;
    }
    PtiLoadInfo info;
    FmtStatus st = decodePtiHeader(hdr, size_t(got), ins, &info);
    if (st != FmtStatus::Ok) {
        fs.close(h);
        return st;
    }
    uint32_t payload = uint32_t(size) > info.dataOffset ? uint32_t(size) - info.dataOffset : 0;
    PtiSampleLayout lay = resolvePtiSample(ins.lengthFrames, payload, stereoEnabled);
    const size_t count = size_t(lay.frames) * (lay.useStereo ? 2 : 1);
    if (count > capSamples) {
        fs.close(h);
        return FmtStatus::NoSpace;
    }
    if (count) {
        if (!fs.seek(h, info.dataOffset)) {
            fs.close(h);
            return FmtStatus::IoError;
        }
        uint8_t* raw = reinterpret_cast<uint8_t*>(samples);
        size_t want = count * 2, done = 0;
        while (done < want) {
            uint32_t c = want - done < kIoChunk ? uint32_t(want - done) : kIoChunk;
            int32_t r = fs.read(h, raw + done, c);
            if (r <= 0) break;
            done += size_t(r);
        }
        if (done < want) std::memset(raw + done, 0, want - done);
        // Convert LE bytes to native int16 in place (nothing to do on a
        // little-endian CPU: the bytes are the samples).
        if (!kLittleEndian)
            for (size_t i = 0; i < count; ++i) samples[i] = rdI16(raw + 2 * i);
    }
    fs.close(h);
    ins.lengthFrames = lay.frames;
    ins.stereo = lay.useStereo;
    if (layoutOut) *layoutOut = lay;
    return FmtStatus::Ok;
}

FmtStatus saveInstrumentFile(IStorage& fs, const char* path, const Instrument& ins, const int16_t* samples) {
    if (fs.exists(path) && !fs.remove(path)) return FmtStatus::IoError;
    IStorage::Handle h = fs.open(path, IStorage::kWrite);
    if (h < 0) return FmtStatus::IoError;
    uint8_t hdr[pti::kHeaderBlockSize];
    encodePtiHeader(ins, hdr);
    bool ok = fs.write(h, hdr, sizeof(hdr)) == int32_t(sizeof(hdr));
    const size_t count = size_t(ins.lengthFrames) * (ins.stereo ? 2 : 1);
    if (kLittleEndian) {
        // The int16 samples are the little-endian file bytes: write them
        // straight from memory (stock: 0x800-byte chunks, spec 02 §1.1; the
        // chunking is not visible in the file).
        const uint8_t* b = reinterpret_cast<const uint8_t*>(samples);
        for (size_t done = 0, want = count * 2; ok && samples && done < want;) {
            const uint32_t c = want - done < kIoChunk ? uint32_t(want - done) : kIoChunk;
            ok = fs.write(h, b + done, c) == int32_t(c);
            done += c;
        }
    } else {
        uint8_t buf[0x800];
        for (size_t i = 0; ok && samples && i < count;) {
            size_t n = 0;
            for (; n < sizeof(buf) / 2 && i < count; ++n, ++i) wrU16(buf + 2 * n, uint16_t(samples[i]));
            ok = fs.write(h, buf, uint32_t(2 * n)) == int32_t(2 * n);
        }
    }
    fs.close(h);
    return ok ? FmtStatus::Ok : FmtStatus::IoError;
}

FmtStatus saveInstrumentHeader(IStorage& fs, const char* path, const Instrument& ins) {
    IStorage::Handle h = fs.open(path, IStorage::kReadWrite);
    if (h < 0) return FmtStatus::IoError;
    uint8_t hdr[pti::kHeaderBlockSize];
    encodePtiHeader(ins, hdr);
    bool ok = fs.seek(h, 0) && fs.write(h, hdr, sizeof(hdr)) == int32_t(sizeof(hdr));
    fs.close(h);
    return ok ? FmtStatus::Ok : FmtStatus::IoError;
}

}  // namespace ot
