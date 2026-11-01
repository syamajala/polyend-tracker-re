// [I] #3 Loading an .sfz as a multisample instrument (model/multisample.h):
// the regions' samples are probed for the Sample Loader's panel, then the
// chosen zones are imported one after another into the free sample memory,
// each on its slice point, as one mono Slice instrument with a zone map.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/app.h"
#include "formats/file_io.h"
#include "formats/sfz.h"
#include "formats/wav.h"

namespace ot {

namespace {

// The SFZ text and its parse live in the App's scratch buffer (no job
// runs while the Loader probes or loads: finishJob first).
struct SfzScratch {
    sfz::File file;
    char text[1];   // the rest of the scratch
};

const char* fileNameOf(const char* p) {
    const char* s = std::strrchr(p, '/');
    return s ? s + 1 : p;
}

// The source range of region r in a file of `total` frames, as import markers.
void regionMarkers(const sfz::Region& r, uint32_t total, WavImportOptions& o) {
    o.stereo = false;   // v1: multisamples load mono
    o.startMarker = 0;
    o.endMarker = 65535;
    if (total == 0) return;
    if (r.offset > 0) o.startMarker = uint16_t(r.offset >= total ? 65535 : uint64_t(r.offset) * 65535u / total);
    if (r.end != sfz::kToEnd && uint64_t(r.end) + 1 < total) {
        const uint64_t e = (uint64_t(r.end + 1) * 65535u + total - 1) / total;   // ceil: keep the last frame
        o.endMarker = uint16_t(e > 65535 ? 65535 : e);
    }
    if (o.endMarker < o.startMarker) o.endMarker = o.startMarker;
}

bool regionInfo(IStorage& fs, const char* path, WavInfo& info) {
    IStorage::Handle h = fs.open(path, IStorage::kRead);
    if (h < 0) return false;
    StorageReader rd(fs, h);
    const bool ok = wavParseHeader(rd, info) && wavIsSupported(info);
    fs.close(h);
    return ok;
}

}  // namespace

App::SfzParse App::readSfz(const char* sfzPath) {
    SfzParse r;
    finishJob();   // the scratch buffer is ours
    if (!sfzPath || !mem_.scratch || mem_.scratchBytes < sizeof(SfzScratch) + 1024) return r;
    auto* s = new (mem_.scratch) SfzScratch;
    const uint32_t cap = mem_.scratchBytes - uint32_t(offsetof(SfzScratch, text)) - 1;
    uint32_t got = 0;
    if (readWholeFile(fs(), sfzPath, reinterpret_cast<uint8_t*>(s->text), cap, &got) != FmtStatus::Ok) return r;
    s->text[got] = 0;
    if (!sfz::parse(s->text, got, s->file)) return r;
    r.file = &s->file;
    r.text = s->text;
    return r;
}

int App::multisampleProbe(const char* sfzPath, ms::Candidate* out, int cap) {
    const SfzParse p = readSfz(sfzPath);
    if (!p.file) return -1;
    IStorage& f = fs();
    int n = 0;
    char path[340];
    for (int i = 0; i < p.file->count && n < cap; ++i) {
        const sfz::Region& r = p.file->regions[i];
        if (!sfz::samplePath(p.text, *p.file, r, sfzPath, path, sizeof(path))) continue;
        WavInfo info;
        if (!regionInfo(f, path, info)) continue;
        WavImportOptions o;
        regionMarkers(r, wavSourceFrames(info), o);
        const uint32_t frames = wavPlanImport(info, o).outFrames;
        if (frames == 0) continue;
        out[n++] = ms::Candidate{r.root, uint8_t(i), frames};
    }
    return ms::uniqueByRoot(out, n);
}

uint32_t App::multisampleFreeFrames(int slot) const {
    // The slot's current sample is replaced: its memory counts as free.
    uint64_t f = bank.freeSamples();
    if (slot >= 0 && slot < kSampleInstrumentCount && bank.loaded(slot) && !bank.partial(slot))
        f += uint64_t(bank.frames(slot)) * (bank.stereo(slot) ? 2u : 1u);
    return f > 0xFFFFFFFFu ? 0xFFFFFFFFu : uint32_t(f);
}

bool App::loadMultisample(const char* sfzPath, int slot, const ms::Candidate* cand, int n, const ms::Options& opt) {
    if (slot < 0 || slot >= kSampleInstrumentCount || n <= 0) return false;
    ms::Plan plan;
    ms::plan(cand, n, opt, plan);
    if (plan.count == 0 || plan.tooMany) return false;
    if (uint64_t(plan.totalFrames) + 64 > multisampleFreeFrames(slot)) {
        ui.notify("Not enough sample memory");
        return false;
    }
    const SfzParse p = readSfz(sfzPath);
    if (!p.file) {
        ui.notify("Can't read the SFZ");
        return false;
    }
    IStorage& f = fs();
    allVoicesOff();   // bank moves invalidate voice sample pointers
    streamUnbind(slot);
    bank.remove(slot);
    refreshStreamSources();
    instrPresent[slot] = false;
    multiClear(slot);
    int16_t* dst = bank.tail();
    const size_t freeS = bank.freeSamples();
    std::memset(dst, 0, size_t(plan.totalFrames) * sizeof(int16_t));
    char path[340];
    int failed = 0;
    for (int z = 0; z < plan.count; ++z) {
        ui.progress("Loading multisample", z * 100 / plan.count);
        const uint8_t ri = cand[plan.cand[z]].region;
        if (ri >= p.file->count) {
            ++failed;
            continue;
        }
        const sfz::Region& r = p.file->regions[ri];
        WavInfo info;
        if (!sfz::samplePath(p.text, *p.file, r, sfzPath, path, sizeof(path)) || !regionInfo(f, path, info)) {
            ++failed;
            continue;
        }
        WavImportOptions o;
        const uint32_t total = wavSourceFrames(info);
        regionMarkers(r, total, o);
        // Max length: read only what's kept (plus a frame of marker rounding).
        if (plan.frames[z] < cand[plan.cand[z]].frames && total > 0) {
            const uint32_t s0 = uint32_t(uint64_t(total) * o.startMarker / 65535u);
            const uint64_t want = uint64_t(plan.frames[z]) * info.sampleRate / 44100u + 2;
            const uint64_t e = ((uint64_t(s0) + want) * 65535u + total - 1) / total;
            if (e < o.endMarker) o.endMarker = uint16_t(e);
        }
        int16_t* at = dst + plan.start[z];
        WavImportResult res;
        if (wavImportFile(f, path, o, at, freeS - plan.start[z], res) != FmtStatus::Ok) {
            ++failed;
            std::memset(at, 0, size_t(plan.frames[z]) * sizeof(int16_t));
            continue;
        }
        // Up to the next zone: the zone, then silence (the import may have
        // written a few frames more, or fewer than planned).
        const uint32_t keep = res.frames < plan.frames[z] ? res.frames : plan.frames[z];
        const uint32_t next = z + 1 < plan.count ? plan.start[z + 1] : plan.totalFrames;
        if (keep < cand[plan.cand[z]].frames) {   // cut by Max length: fade out
            const uint32_t fade = keep < ms::kFadeFrames ? keep : ms::kFadeFrames;
            for (uint32_t i = 0; i < fade; ++i) {
                int16_t& s = at[keep - fade + i];
                s = int16_t(int32_t(s) * int32_t(fade - i) / int32_t(fade));
            }
        }
        const uint32_t clearTo = next - plan.start[z];
        const uint32_t written = res.frames > clearTo ? clearTo : res.frames;
        if (written > keep) std::memset(at + keep, 0, size_t(written - keep) * sizeof(int16_t));
    }
    ui.progressDone();
    if (failed == plan.count || !bank.commitTail(slot, plan.totalFrames, false)) {
        markInstrumentDirty(slot, true);
        ui.notify("Load failed");
        return false;
    }
    Instrument ins;
    ins.setDefault();
    const char* bn = fileNameOf(sfzPath);
    size_t nl = std::strlen(bn);
    if (const char* dot = std::strrchr(bn, '.')) nl = size_t(dot - bn);
    if (nl > size_t(kInstrumentNameLen)) nl = kInstrumentNameLen;
    std::memcpy(ins.name, bn, nl);
    ins.name[nl] = 0;
    ins.playMode = uint8_t(PlayMode::Slice);
    ins.sliceCount = uint8_t(plan.count);
    for (int z = 0; z < plan.count; ++z) ins.slices[z] = plan.point[z];
    ins.lengthFrames = bank.frames(slot);
    ins.stereo = false;
    instruments[slot] = ins;
    instrPresent[slot] = true;
    MultiMap& m = multiMap(slot);
    m.on = 1;
    m.count = uint8_t(plan.count);
    for (int z = 0; z < plan.count; ++z) m.root[z] = plan.root[z];
    refreshStreamSources();
    markInstrumentDirty(slot, true);
    markProjectDirty();   // multisample.bin
    if (failed) {
        char msg[48];
        std::snprintf(msg, sizeof(msg), "%d of %d samples missing", failed, plan.count);
        ui.notify(msg);
    }
    return true;
}

}  // namespace ot
