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
#include "audio/engines/dx7.h"
#include "audio/synth_params.h"
#include "formats/file_io.h"
#include "formats/patch_pb.h"
#include "sdram_bss.h"

namespace ot {

namespace {
// Scratch layout (same split as the project job's patch unit): the file at
// 0, the decoded patch at 0x8000.
constexpr uint32_t kFileOff = 0;
constexpr uint32_t kPatchTmpOff = 0x8000;
static_assert(kPatchMaxFileSize + 1 <= kPatchTmpOff, "patch file fits below the decode slot");
// A DX7 bank's voices, parsed from the file at 0 (openDx7Bank).
constexpr uint32_t kDx7VoicesOff = 0x2400;
static_assert(kPatchMaxFileSize + 1 <= kDx7VoicesOff, "bank file fits below its voices");
static_assert(kDx7VoicesOff + audio::dx7::kBankVoices * audio::dx7::kVoiceBytes <= kPatchTmpOff,
              "bank voices fit below the decode slot");

SynthModel modelOf(const SynthSlotRecord& r) {
    return SynthModel(r.model < uint32_t(kSynthModelCount) ? r.model : 0);
}

void setName(SynthSlotRecord& r, const char* name) {
    std::snprintf(r.patchName, sizeof(r.patchName), "%s", name ? name : "");
}

// Macro page values M1..M6 -> the patch defaults (63 bipolar, else 0).
void resetMacros(SynthSlotRecord& r, const SynthPatch& p) {
    for (int k = 0; k < int(mt::kSynthMacroCount); ++k) r.macros[k] = audio::synthMacroDefault(p.macros[k].bipolar);
}

bool libraryPath(char* out, size_t cap, SynthModel m, const char* name) {
    if (!name || !name[0] || std::strchr(name, '/')) return false;
    std::snprintf(out, cap, "Patches/%s/%s.patch", synthModelName(m), name);
    return true;
}

// The slot's patch while a Load Patch preview plays another one (one slot
// at a time; main loop only).
OT_SDRAM_BSS SynthPatch g_previewSaved;

// The open DX7 bank's file name (openDx7Bank; main loop only). Its voices
// live in the scratch, which jobs share: every preview / load reads the
// file again.
char g_dx7BankFile[64];
int g_dx7BankCount = 0;
}  // namespace

// Patches/<model>/<name>.patch decoded into the scratch decode slot; nullptr
// when missing, damaged or of another engine.
const SynthPatch* App::readSynthLibraryPatch(int s, const char* name) {
    if (s < 0 || s >= kSynthSlotCount || !mem_.scratch || mem_.scratchBytes < kPatchTmpOff + sizeof(SynthPatch))
        return nullptr;
    if (jobBusy()) finishJob();   // the job shares the scratch (spec 05 appendix E3)
    const SynthModel m = modelOf(project.synthSlot(s));
    char path[96];
    if (!libraryPath(path, sizeof(path), m, name)) return nullptr;
    uint8_t* buf = mem_.scratch + kFileOff;
    auto* tmp = reinterpret_cast<SynthPatch*>(mem_.scratch + kPatchTmpOff);
    uint32_t got = 0;
    if (readWholeFile(fs(), path, buf, kPatchMaxFileSize + 1, &got) != FmtStatus::Ok || got > kPatchMaxFileSize)
        return nullptr;
    if (decodePatch(buf, got, tmp) != PatchStatus::kOk || tmp->engine != synthEngineId(m)) return nullptr;
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

void App::setSynthModel(int s, int model) {
    if (s < 0 || s >= kSynthSlotCount || model < 0 || model >= kSynthModelCount) return;
    if (previewSlot_ == s) endSynthPreview();
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
    patchInit(synthEngineId(modelOf(r)), &patches_[s]);
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

int App::openDx7Bank(const char* file) {
    std::snprintf(g_dx7BankFile, sizeof(g_dx7BankFile), "%s", file ? file : "");
    return readDx7Bank();
}

// g_dx7BankFile's voices into the scratch (kDx7VoicesOff); their count.
int App::readDx7Bank() {
    g_dx7BankCount = 0;
    const char* file = g_dx7BankFile;
    if (!mem_.scratch || mem_.scratchBytes < kPatchTmpOff + sizeof(SynthPatch)) return 0;
    if (!file[0] || std::strchr(file, '/')) return 0;
    if (jobBusy()) finishJob();   // the job shares the scratch
    char path[96];
    std::snprintf(path, sizeof(path), "Patches/%s/%s", synthModelName(SynthModel::DX7), file);
    uint32_t got = 0;
    // A bank dump is 4104 bytes; anything past 8 KB is no DX7 voice dump.
    if (readWholeFile(fs(), path, mem_.scratch + kFileOff, kPatchMaxFileSize + 1, &got) != FmtStatus::Ok ||
        got > kPatchMaxFileSize)
        return 0;
    auto* voices = reinterpret_cast<uint8_t(*)[audio::dx7::kVoiceBytes]>(mem_.scratch + kDx7VoicesOff);
    g_dx7BankCount = audio::dx7::parseSysex(mem_.scratch + kFileOff, got, voices, audio::dx7::kBankVoices);
    return g_dx7BankCount;
}

void App::dx7BankVoiceName(int k, char* out, int cap) const {
    if (cap <= 0) return;
    out[0] = 0;
    if (k < 0 || k >= g_dx7BankCount || !mem_.scratch) return;
    const uint8_t* nm = mem_.scratch + kDx7VoicesOff + uint32_t(k) * audio::dx7::kVoiceBytes + audio::dx7::kVoiceBytes -
                        audio::dx7::kNameLen;
    int n = 0;
    for (int i = 0; i < audio::dx7::kNameLen && n < cap - 1; ++i) {
        const char c = char(nm[i]);
        // Printable ASCII; path separators would make a bad file name on Save.
        out[n++] = (c < 0x20 || c > 0x7E || c == '/' || c == '\\') ? '_' : c;
    }
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = 0;
    if (!n) std::snprintf(out, size_t(cap), "Voice %d", k + 1);
}

const SynthPatch* App::dx7BankPatch(int s, int k) {
    if (s < 0 || s >= kSynthSlotCount || k < 0) return nullptr;
    if (modelOf(project.synthSlot(s)) != SynthModel::DX7) return nullptr;
    if (k >= readDx7Bank()) return nullptr;   // the scratch may have been used since the bank opened
    auto* tmp = reinterpret_cast<SynthPatch*>(mem_.scratch + kPatchTmpOff);
    if (!patchInit(kSynthEngineDx7, tmp) ||
        !patchSetDx7Voice(*tmp, mem_.scratch + kDx7VoicesOff + uint32_t(k) * audio::dx7::kVoiceBytes))
        return nullptr;
    return tmp;
}

bool App::previewDx7BankVoice(int s, int k) {
    const SynthPatch* p = dx7BankPatch(s, k);
    return p && previewSynthPatch(s, *p);
}

bool App::loadDx7BankVoice(int s, int k) {
    if (previewSlot_ == s) endSynthPreview();
    const SynthPatch* p = dx7BankPatch(s, k);
    if (!p) return false;
    char name[audio::dx7::kNameLen + 8];
    dx7BankVoiceName(k, name, sizeof(name));   // before the load: it reads the scratch
    loadSynthPatch(s, *p, name);
    return true;
}

bool App::synthUnavailableNotice(char* out, size_t cap) const {
    for (int s = 0; s < kSynthSlotCount; ++s) {
        const SynthSlotRecord r = project.synthSlot(s);
        const SynthModel m = modelOf(r);
        if (audio::synthEngineFor(synthEngineId(m))) continue;
        std::snprintf(out, cap, "Synth %d (%s) not available", s + 1, synthModelName(m));
        return true;
    }
    return false;
}

bool App::saveSynthLibraryPatch(int s, const char* name) {
    if (s < 0 || s >= kSynthSlotCount || !mem_.scratch || mem_.scratchBytes < kPatchMaxFileSize) return false;
    if (jobBusy()) finishJob();
    SynthSlotRecord r = project.synthSlot(s);
    const SynthModel m = modelOf(r);
    char path[96], dir[48], tmpPath[104];
    if (!libraryPath(path, sizeof(path), m, name)) return false;
    uint8_t* out = mem_.scratch + kFileOff;
    size_t n = 0;
    if (encodePatch(patches_[s], out, kPatchMaxFileSize, &n) != PatchStatus::kOk) return false;
    IStorage& f = fs();
    std::snprintf(dir, sizeof(dir), "Patches/%s", synthModelName(m));
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
