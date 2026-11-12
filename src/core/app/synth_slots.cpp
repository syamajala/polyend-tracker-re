// Synth slot operations for the synth screens (2.x WP-G, docs/MINI_PLAN.md
// §3 UI; spec 21 §2 library layout, §3.3 records, §7 patches) [OT].
//
// The library is Patches/<MODEL>/<name>.patch on the card (spec 21 §2). The
// slot's patch lives in App::patches_; every change marks it dirty (the
// autosave rewrites Workspace/N.patch) and reaches the audio side through
// SynthBank::patchChanged (markSynthPatchDirty). Records go through the
// project bytes and the engine sync (markProjectDirty -> setSlotRecord).
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "modules/synth_models.h"
#include "audio/synth_params.h"
#include "formats/file_io.h"
#include "formats/patch_pb.h"
#include "modules/module.h"
#include "modules/synth_descriptor.h"
#include "sdram_bss.h"

namespace ot {

namespace {
// Scratch layout (same split as the project job's patch unit): the file at
// 0, the decoded patch at 0x8000.
constexpr uint32_t kFileOff = 0;
constexpr uint32_t kPatchTmpOff = 0x8000;
static_assert(kPatchMaxFileSize + 1 <= kPatchTmpOff, "patch file fits below the decode slot");

void setName(SynthSlotRecord& r, const char* name) {
    std::snprintf(r.patchName, sizeof(r.patchName), "%s", name ? name : "");
}

// Macro page values M1..M6 -> the patch defaults (63 bipolar, else 0).
void resetMacros(SynthSlotRecord& r, const SynthPatch& p) {
    for (int k = 0; k < int(mt::kSynthMacroCount); ++k) r.macros[k] = audio::synthMacroDefault(p.macros[k].bipolar);
}

bool libraryPath(char* out, size_t cap, int32_t engineId, const char* name) {
    if (!name || !name[0] || std::strchr(name, '/')) return false;
    std::snprintf(out, cap, "Patches/%s/%s.patch", synthEngineFolder(engineId), name);
    return true;
}

// The slot's patch while a Load Patch preview plays another one (one slot
// at a time; main loop only).
OT_SDRAM_BSS SynthPatch g_previewSaved;

// [I] The open import file (openPatchImport; main loop only): its name, the
// engine whose folder and format it is, its patch count and length. Its
// bytes live in the scratch, which jobs share: every preview / load reads
// the file again.
char g_importFile[64];
int32_t g_importEngine = 0;
int g_importCount = 0;
uint32_t g_importLen = 0;

const mod::PatchImport* importOf(int32_t engine) {
    const mod::SynthDescriptor* d = mod::ModuleHost::instance().synthDescriptor(engine);
    return d ? d->import : nullptr;
}
}  // namespace

// Patches/<model>/<name>.patch decoded into the scratch decode slot; nullptr
// when missing, damaged or of another engine.
const SynthPatch* App::readSynthLibraryPatch(int s, const char* name) {
    if (s < 0 || s >= kSynthSlotCount || !mem_.scratch || mem_.scratchBytes < kPatchTmpOff + sizeof(SynthPatch))
        return nullptr;
    if (jobBusy()) finishJob();   // the job shares the scratch (spec 05 appendix E3)
    const int32_t eng = slotEngineId(s);
    char path[96];
    if (!libraryPath(path, sizeof(path), eng, name)) return nullptr;
    uint8_t* buf = mem_.scratch + kFileOff;
    auto* tmp = reinterpret_cast<SynthPatch*>(mem_.scratch + kPatchTmpOff);
    uint32_t got = 0;
    if (readWholeFile(fs(), path, buf, kPatchMaxFileSize + 1, &got) != FmtStatus::Ok || got > kPatchMaxFileSize)
        return nullptr;
    if (decodePatch(buf, got, tmp) != PatchStatus::kOk || tmp->engine != eng) return nullptr;
    return tmp;
}

bool App::previewSynthLibraryPatch(int s, const char* name) {
    const SynthPatch* p = readSynthLibraryPatch(s, name);
    return p && previewSynthPatch(s, *p);
}

bool App::previewSynthPatch(int s, const SynthPatch& p) {
    if (previewSlot_ != s) {
        endSynthPreview();
        std::memcpy(&g_previewSaved, &patches_[s], sizeof(SynthPatch));
        previewSrc_ = patchSrc_[s];
        previewDirty_ = patchDirty_[s];
        patchDirty_[s] = false;   // the autosave never writes a previewed patch
        previewSlot_ = int8_t(s);
    }
    std::memcpy(&patches_[s], &p, sizeof(SynthPatch));
    SynthSlotRecord r = project.synthSlot(s);
    resetMacros(r, patches_[s]);
    engine.synth().setSlotRecord(s, r);   // audio side only; the project record is untouched
    engine.synth().patchChanged(s);
    return true;
}

void App::endSynthPreview() {
    const int s = previewSlot_;
    if (s < 0 || s >= kSynthSlotCount) return;
    previewSlot_ = -1;
    std::memcpy(&patches_[s], &g_previewSaved, sizeof(SynthPatch));
    patchSrc_[s] = previewSrc_;
    patchDirty_[s] = previewDirty_;
    engine.synth().setSlotRecord(s, project.synthSlot(s));
    engine.synth().patchChanged(s);
}

void App::setSynthSlotRecord(int s, const SynthSlotRecord& r) {
    if (s < 0 || s >= kSynthSlotCount) return;
    project.setSynthSlot(s, r);
    engine.synth().setSlotRecord(s, project.synthSlot(s));   // main-loop setter: applies at the next block
    markProjectDirty();
}

void App::setSynthEngine(int s, int32_t engineId) {
    if (s < 0 || s >= kSynthSlotCount) return;
    if (engineId < 0x80) {
        for (int m = 0; m < kSynthModelCount; ++m)
            if (synthEngineId(SynthModel(m)) == engineId) setSynthModel(s, m);
        return;
    }
    if (previewSlot_ == s) endSynthPreview();
    // A module engine: the record keeps its Mini model (the fallback a Mini
    // or an older OpenTracker plays); the sidecar names the engine.
    setSlotEngine(s, engineId);
    SynthSlotRecord r = project.synthSlot(s);
    setName(r, "Init");
    project.setSynthSlot(s, r);
    if (!loadSynthLibraryPatch(s, "Init")) initSynthPatch(s);
}

void App::setSynthModel(int s, int model) {
    if (s < 0 || s >= kSynthSlotCount || model < 0 || model >= kSynthModelCount) return;
    if (previewSlot_ == s) endSynthPreview();
    setSlotEngine(s, 0);
    SynthSlotRecord r = project.synthSlot(s);
    r.model = uint32_t(model);
    setName(r, "Init");
    project.setSynthSlot(s, r);
    if (!loadSynthLibraryPatch(s, "Init")) initSynthPatch(s);
}

void App::initSynthPatch(int s) {
    if (s < 0 || s >= kSynthSlotCount) return;
    if (previewSlot_ == s) endSynthPreview();
    SynthSlotRecord r = project.synthSlot(s);
    patchInit(slotEngineId(s), &patches_[s]);
    patchSrc_[s] = PatchSource::Init;
    setName(r, "Init");
    resetMacros(r, patches_[s]);
    setSynthSlotRecord(s, r);
    markSynthPatchDirty(s);
}

bool App::loadSynthLibraryPatch(int s, const char* name) {
    if (previewSlot_ == s) endSynthPreview();
    const SynthPatch* tmp = readSynthLibraryPatch(s, name);
    if (!tmp) return false;
    loadSynthPatch(s, *tmp, name);
    return true;
}

void App::loadSynthPatch(int s, const SynthPatch& p, const char* name) {
    SynthSlotRecord r = project.synthSlot(s);
    patches_[s] = p;
    patchSrc_[s] = PatchSource::Library;
    setName(r, name);
    resetMacros(r, patches_[s]);
    setSynthSlotRecord(s, r);
    markSynthPatchDirty(s);
}

int App::openPatchImport(int s, const char* file) {
    std::snprintf(g_importFile, sizeof(g_importFile), "%s", file ? file : "");
    g_importEngine = s >= 0 && s < kSynthSlotCount ? slotEngineId(s) : 0;
    return readPatchImport();
}

// g_importFile into the scratch (kFileOff); its patch count.
int App::readPatchImport() {
    g_importCount = 0;
    g_importLen = 0;
    const char* file = g_importFile;
    const mod::PatchImport* im = importOf(g_importEngine);
    if (!im || im->maxBytes > kPatchMaxFileSize) return 0;
    if (!mem_.scratch || mem_.scratchBytes < kPatchTmpOff + sizeof(SynthPatch)) return 0;
    if (!file[0] || std::strchr(file, '/')) return 0;
    if (jobBusy()) finishJob();   // the job shares the scratch
    char path[96];
    std::snprintf(path, sizeof(path), "Patches/%s/%s", synthEngineFolder(g_importEngine), file);
    uint32_t got = 0;
    if (readWholeFile(fs(), path, mem_.scratch + kFileOff, im->maxBytes + 1, &got) != FmtStatus::Ok ||
        got > im->maxBytes)
        return 0;
    const int n = im->count(mem_.scratch + kFileOff, got);
    g_importCount = n > 0 ? n : 0;
    g_importLen = got;
    return g_importCount;
}

void App::importPatchName(int k, char* out, int cap) const {
    if (cap <= 0) return;
    out[0] = 0;
    const mod::PatchImport* im = importOf(g_importEngine);
    if (!im || k < 0 || k >= g_importCount || !mem_.scratch) return;
    im->name(mem_.scratch + kFileOff, g_importLen, k, out, cap);
    out[cap - 1] = 0;
    int n = 0;
    for (; out[n]; ++n) {
        const char c = out[n];
        // Printable ASCII; path separators would make a bad file name on Save.
        if (c < 0x20 || c > 0x7E || c == '/' || c == '\\') out[n] = '_';
    }
    if (!n) std::snprintf(out, size_t(cap), "Voice %d", k + 1);
}

const SynthPatch* App::importPatch(int s, int k) {
    if (s < 0 || s >= kSynthSlotCount || k < 0) return nullptr;
    const int32_t eng = slotEngineId(s);
    const mod::PatchImport* im = importOf(eng);
    if (!im || eng != g_importEngine) return nullptr;
    if (k >= readPatchImport()) return nullptr;   // the scratch may have been used since the file opened
    auto* tmp = reinterpret_cast<SynthPatch*>(mem_.scratch + kPatchTmpOff);
    if (!patchInit(eng, tmp) || !im->toPatch(mem_.scratch + kFileOff, g_importLen, k, *tmp)) return nullptr;
    return tmp;
}

bool App::previewImportPatch(int s, int k) {
    const SynthPatch* p = importPatch(s, k);
    return p && previewSynthPatch(s, *p);
}

bool App::loadImportPatch(int s, int k) {
    if (previewSlot_ == s) endSynthPreview();
    const SynthPatch* p = importPatch(s, k);
    if (!p) return false;
    char name[40];
    importPatchName(k, name, sizeof(name));   // before the load: it reads the scratch
    loadSynthPatch(s, *p, name);
    return true;
}

bool App::synthUnavailableNotice(char* out, size_t cap) const {
    for (int s = 0; s < kSynthSlotCount; ++s) {
        const int32_t eng = slotEngineId(s);
        if (eng && audio::synthEngineFor(eng)) continue;
        if (const char* why = cardSlotState(s))
            std::snprintf(out, cap, "Synth %d (%s): %s", s + 1, synthEngineDisplayName(eng), why);
        else
            std::snprintf(out, cap, "Synth %d (%s) not available", s + 1, synthEngineDisplayName(eng));
        return true;
    }
    return false;
}

// ---- [I] #21 card synths ------------------------------------------------------

const char* App::cardSlotState(int s) const {
    const int32_t eng = slotEngineId(s);
    const mod::CardEngine* e = mod::cardEngine(eng);
    if (!e || mod::cardEngineLoaded(eng)) return nullptr;
    if (seq.isPlaying() || seq.isArmed()) return "Loads at Stop";
    int32_t engines[kSynthSlotCount];
    slotEngines(engines);
    if (std::memcmp(cardTried_, engines, sizeof(cardTried_)) != 0) return "Loading";
    const mod::OtmLoadReport& r = moduleLoad_;
    if (r.noRoom) return "No card memory";
    const mod::OtmLoadReport::Item& it = r.items[e->file];
    if (it.refused) return "Module refused";
    if (it.error == mod::OtmError::TooBig) return "Card memory full";
    return mod::otmErrorText(it.error);
}

void App::slotEngines(int32_t* out) const {
    for (int s = 0; s < kSynthSlotCount; ++s) out[s] = slotEngineId(s);
}

void App::serviceCardSynths(uint32_t now) {
    int32_t engines[kSynthSlotCount];
    slotEngines(engines);
    const bool synthsTried = std::memcmp(cardTried_, engines, sizeof(cardTried_)) == 0;
    const bool fxWaiting = cardFxWaiting(now);   // [I] the card effects that are on changed
    if (synthsTried && !fxWaiting) {
        cardSince_ = 0;
        return;
    }
    if (!synthsTried && (!cardSince_ || std::memcmp(cardWaiting_, engines, sizeof(cardWaiting_)) != 0)) {
        std::memcpy(cardWaiting_, engines, sizeof(cardWaiting_));
        cardSince_ = now ? now : 1;
        return;
    }
    if (!cardSince_) {
        cardSince_ = now ? now : 1;
        return;
    }
    if (now - cardSince_ < kCardSettleMs || seq.isPlaying() || seq.isArmed() || jobBusy()) return;
    cardSince_ = 0;
    syncCardSynths(true);
}

void App::holdSynthEngines() {
    // No card engine runs from here on: the next block drops the synth
    // voices (a block is 2.9 ms); when no block runs on its own, this does.
    audio::SynthBank& bank = engine.synth();
    bank.holdEngines(true);
    const uint32_t t0 = millis();
    while (!bank.enginesHeld())
        if (!hal_.audio || !hal_.audio->blocksOnTheirOwn() || millis() - t0 > 50) {
            bank.dropEngines();
            break;
        }
}

void App::rescanCardSynths() {
    if (!mem_.scratch) return;
    if (previewSlot_ >= 0) endSynthPreview();
    holdSynthEngines();
    engine.setMasterInserts(nullptr, 0);   // [I] card effects leave too
    mod::rescanCardSynths(fs(), mem_.scratch, mem_.scratchBytes);
    engine.synth().holdEngines(false);
    bank.setModuleHold(0);
    bindMasterInserts();
    // Nothing tried yet: the project load (or a model change) loads anew.
    std::memset(cardTried_, 0, sizeof(cardTried_));
    for (int s = 0; s < kSynthSlotCount; ++s) engine.synth().patchChanged(s);
}

bool App::syncCardSynths(bool initPatches) {
    // The slots' engines: card synths load, card and built-in synths share
    // the fast block (modules/module_files.h).
    // [I] Z presets (upstream #171) will add the engines their presets name.
    int32_t engines[kSynthSlotCount];
    slotEngines(engines);
    std::memcpy(cardTried_, engines, sizeof(cardTried_));
    // [I] With the card effects that are on, waiting to load or have project
    // data (app/master_fx.cpp).
    int fx[mod::kMaxCardFx];
    int nFx = wantedCardFx(fx);
    markCardFxTried(fx, nFx);
    if (!mod::cardSynthsDiffer(engines, kSynthSlotCount, fx, nFx)) {
        settleCardFx();
        return true;
    }
    // The scratch is free: serviceCardSynths waits for the job to end, the
    // project load calls this between its units.
    if (!mem_.scratch) return false;
    if (previewSlot_ >= 0) endSynthPreview();
    audio::SynthBank& bank = engine.synth();
    holdSynthEngines();
    beginCardFx(fx, &nFx);
    const bool ok =
        mod::loadCardSynths(fs(), engines, kSynthSlotCount, mem_.scratch, mem_.scratchBytes, fx, nFx);
    ++cardLoads_;   // [I] #159
    bank.holdEngines(false);
    const bool again = endCardFx(fx, nFx);
    for (int s = 0; s < kSynthSlotCount; ++s) {
        const int32_t eng = slotEngineId(s);
        // A slot that took its card engine before it loaded: its Init patch now.
        if (initPatches && patches_[s].engine != eng && audio::synthEngineFor(eng)) {
            SynthSlotRecord r = project.synthSlot(s);
            setName(r, "Init");
            project.setSynthSlot(s, r);
            if (!loadSynthLibraryPatch(s, "Init")) initSynthPatch(s);
        }
        engine.synth().patchChanged(s);
    }
    if (!ok && initPatches) {
        char t[64];
        if (synthUnavailableNotice(t, sizeof(t))) ui.notify(t, 4000);
    }
    ui.dirty();
    // [I] Card effects Open loaded for their data that are off leave again.
    if (again) syncCardSynths(initPatches);
    return ok;
}

bool App::saveSynthLibraryPatch(int s, const char* name) {
    if (s < 0 || s >= kSynthSlotCount || !mem_.scratch || mem_.scratchBytes < kPatchMaxFileSize) return false;
    if (jobBusy()) finishJob();
    SynthSlotRecord r = project.synthSlot(s);
    const int32_t eng = slotEngineId(s);
    char path[96], dir[48], tmpPath[104];
    if (!libraryPath(path, sizeof(path), eng, name)) return false;
    uint8_t* out = mem_.scratch + kFileOff;
    size_t n = 0;
    if (encodePatch(patches_[s], out, kPatchMaxFileSize, &n) != PatchStatus::kOk) return false;
    IStorage& f = fs();
    std::snprintf(dir, sizeof(dir), "Patches/%s", synthEngineFolder(eng));
    if (!f.mkdir("Patches") || !f.mkdir(dir)) return false;
    std::snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    if (writeWholeFile(f, tmpPath, out, uint32_t(n)) != FmtStatus::Ok) {
        f.remove(tmpPath);
        return false;
    }
    if (f.exists(path) && !f.remove(path)) return false;
    if (!f.rename(tmpPath, path)) return false;
    setName(r, name);
    setSynthSlotRecord(s, r);
    return true;
}

}  // namespace ot
