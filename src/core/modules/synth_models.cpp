// Synth model list (modules/synth_models.h).
#include "modules/synth_models.h"

#include "formats/patch_pb.h"
#include "model/project.h"
#include "modules/module.h"
#include "modules/module_files.h"
#include "modules/synth_descriptor.h"

namespace ot {

namespace {
struct ListEngine {
    int32_t id;
    const char* name;
};

// The k-th module engine with an id >= 0x80: the compiled-in ones, then
// [I] the card's (modules/module_files.h, listed whether loaded or not; a
// loaded one keeps its place). {0, nullptr} past the end. [I] Off leads the
// list (offFirst), and a Tracker Mini model from the card is one of the
// first five already.
ListEngine moduleEngine(int k) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    for (int i = 0; i < h.engineCount(); ++i) {
        const mod::SynthDescriptor* d = h.engineAt(i);
        if (d->engineId < 0x80 || d->engineId == kSynthEngineOff || mod::cardEngine(d->engineId)) continue;
        if (k-- == 0) return {d->engineId, d->displayName ? d->displayName : d->name};
    }
    for (int i = 0; const mod::CardEngine* e = mod::cardEngineAt(i); ++i) {
        if (e->engineId < 0x80) continue;
        if (k-- == 0) return {e->engineId, e->displayName};
    }
    return {0, nullptr};
}

// [I] 1 when the image has the Off engine (built in): list entry 0.
int offFirst() { return mod::ModuleHost::instance().synthDescriptor(kSynthEngineOff) ? 1 : 0; }
}  // namespace

int synthModelListCount() {
    const int lead = offFirst() + kSynthModelCount;
    int n = lead;
    while (moduleEngine(n - lead).id) ++n;
    return n;
}

int32_t synthModelListEngine(int i) {
    const int off = offFirst();
    if (i < 0) return 0;
    if (i < off) return kSynthEngineOff;
    if (i < off + kSynthModelCount) return synthEngineId(SynthModel(i - off));
    return moduleEngine(i - off - kSynthModelCount).id;
}

const char* synthModelListName(int i) {
    const int off = offFirst();
    if (i >= 0 && i < off) return synthEngineDisplayName(kSynthEngineOff);
    if (i >= off && i < off + kSynthModelCount) return synthModelName(SynthModel(i - off));
    const ListEngine e = moduleEngine(i - off - kSynthModelCount);
    return e.name ? e.name : "?";
}

int synthModelListIndex(int32_t engineId) {
    const int n = synthModelListCount();
    for (int i = 0; i < n; ++i)
        if (synthModelListEngine(i) == engineId) return i;
    return -1;
}

const char* synthEngineFolder(int32_t engineId) {
    const char* n = patchEngineName(engineId);
    return n ? n : "?";
}

const char* synthEngineDisplayName(int32_t engineId) {
    const mod::SynthDescriptor* d = mod::ModuleHost::instance().synthDescriptor(engineId);
    if (d) return d->displayName ? d->displayName : d->name;
    if (const mod::CardEngine* e = mod::cardEngine(engineId)) return e->displayName;   // [I] not loaded yet
    return synthEngineFolder(engineId);
}

}  // namespace ot
