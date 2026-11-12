// [I] Memory use for the Config Memory page: what the card memory holds
// (the card modules loaded, and the ones wanted that didn't fit) and what
// the sample memory holds. Read-only, main loop.
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "modules/module.h"
#include "modules/module_files.h"

namespace ot {

namespace {

using Row = App::MemoryUse::Row;
using State = App::MemoryUse::State;

Row* addRow(App::MemoryUse& u) {
    if (u.nCard >= App::MemoryUse::kMaxRows) return nullptr;
    Row& r = u.card[u.nCard++];
    r = Row{};
    return &r;
}

// The file name without ".otm" (a module that is neither a synth nor an effect).
void fileName(const char* file, char* out, int cap) {
    std::snprintf(out, size_t(cap), "%s", file);
    if (char* dot = std::strrchr(out, '.')) *dot = 0;
}

}  // namespace

void App::memoryUse(MemoryUse& u) const {
    u = MemoryUse{};
    const mod::OtmLoadReport& r = moduleLoad_;
    u.cardUsed = r.used;
    u.cardCap = r.cap;
    int32_t engines[kSynthSlotCount];
    slotEngines(engines);
    // Slots naming card engine e: "synth, slot 1 2" (synths count from 1).
    auto slotsOf = [&engines](int file, char* out, int cap) {
        int n = std::snprintf(out, size_t(cap), "synth");
        bool first = true;
        for (int s = 0; s < kSynthSlotCount; ++s) {
            const mod::CardEngine* e = mod::cardEngine(engines[s]);
            if (!e || e->file != file || n >= cap) continue;
            n += std::snprintf(out + n, size_t(cap - n), first ? ", slot %d" : " %d", s + 1);
            first = false;
        }
    };
    // Loaded: synths, effects and the modules the boot loaded for good.
    for (int i = 0; i < r.n; ++i) {
        const mod::OtmLoadReport::Item& it = r.items[i];
        if (!it.loaded) continue;
        Row* row = addRow(u);
        if (!row) break;
        row->bytes = it.poolBytes;
        row->state = State::kLoaded;
        fileName(it.file, row->name, sizeof row->name);
        if (it.synth) {
            for (int k = 0; k < mod::cardEngineCount(); ++k)
                if (mod::cardEngineAt(k)->file == i) {
                    std::snprintf(row->name, sizeof row->name, "%s", mod::cardEngineAt(k)->displayName);
                    break;
                }
            slotsOf(i, row->what, sizeof row->what);
        } else if (it.fx) {
            for (int k = 0; k < mod::cardFxCount(); ++k)
                if (mod::cardFxAt(k)->file == i)
                    std::snprintf(row->name, sizeof row->name, "%s", mod::cardFxAt(k)->name);
            std::snprintf(row->what, sizeof row->what, "effect");
        } else {
            std::snprintf(row->what, sizeof row->what, "module");
        }
    }
    // Wanted, not loaded: a slot's card synth (once per module) ...
    bool listed[mod::OtmLoadReport::kMax] = {};
    for (int s = 0; s < kSynthSlotCount; ++s) {
        const mod::CardEngine* e = mod::cardEngine(engines[s]);
        const char* why = cardSlotState(s);
        if (!e || !why || listed[e->file]) continue;
        listed[e->file] = true;
        Row* row = addRow(u);
        if (!row) break;
        std::snprintf(row->name, sizeof row->name, "%s", e->displayName);
        slotsOf(e->file, row->what, sizeof row->what);
        row->bytes = r.items[e->file].needBytes;
        row->state = std::strcmp(why, "Loads at Stop") == 0 || std::strcmp(why, "Loading") == 0 ? State::kWaiting
                     : std::strstr(why, "memory")                                               ? State::kNoRoom
                                                                                                : State::kFailed;
    }
    // ... and a card effect turned on (or played) that isn't in.
    for (int k = 0; k < masterFxCount(); ++k) {
        MasterFx fx;
        if (!masterFx(k, fx) || fx.module || fx.card < 0 || (!fx.pending && fx.error == FxError::kNone)) continue;
        Row* row = addRow(u);
        if (!row) break;
        std::snprintf(row->name, sizeof row->name, "%s", fx.name);
        std::snprintf(row->what, sizeof row->what, "effect");
        row->bytes = r.items[mod::cardFxAt(fx.card)->file].needBytes;
        row->state = fx.error == FxError::kModuleMemory ? State::kNoRoom
                     : fx.error != FxError::kNone       ? State::kFailed
                                                        : State::kWaiting;
    }
    for (int i = 0; i < audio::SampleBank::kSlots; ++i) u.samples += bank.loaded(i);
    u.sampleBytes = bank.usedBytes();
    u.holdBytes = uint32_t(bank.moduleHold() * 2u);
    u.reservedBytes = uint32_t(bank.reserved() * 2u);
    u.sampleCap = bank.capacityBytes() + u.holdBytes;
}

void App::cardFullText(char* out, int cap) const {
    const mod::OtmLoadReport& r = moduleLoad_;
    int big = -1;
    for (int i = 0; i < r.n; ++i)
        if (r.items[i].loaded && (big < 0 || r.items[i].poolBytes > r.items[big].poolBytes)) big = i;
    if (big < 0) {
        std::snprintf(out, size_t(cap), "Card memory full");
        return;
    }
    MemoryUse::Row row{};
    fileName(r.items[big].file, row.name, sizeof row.name);
    for (int k = 0; k < mod::cardEngineCount(); ++k)
        if (mod::cardEngineAt(k)->file == big) std::snprintf(row.name, sizeof row.name, "%s", mod::cardEngineAt(k)->displayName);
    for (int k = 0; k < mod::cardFxCount(); ++k)
        if (mod::cardFxAt(k)->file == big) std::snprintf(row.name, sizeof row.name, "%s", mod::cardFxAt(k)->name);
    const uint32_t b = r.items[big].poolBytes;
    std::snprintf(out, size_t(cap), "Card memory full (%s %lu.%lu KB)", row.name, static_cast<unsigned long>(b / 1024),
                  static_cast<unsigned long>((b % 1024) * 10 / 1024));
}

}  // namespace ot
