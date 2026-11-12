// [I] Instrument library: sample instruments saved to Instruments/ on the
// card as stock .pti files and loaded back (Instrument Effects page, F6 Load
// / F7 Save). The Workspace already keeps every slot as a complete .pti, so
// Save flushes the slot there and copies the file.
#include <cstdio>

#include "app/app.h"
#include "app/project_io.h"

namespace ot {

const char* App::saveLibraryInstrument(int slot, const char* dir, const char* name) {
    if (slot < 0 || slot >= kSampleInstrumentCount || !instrPresent[slot]) return "The instrument is empty";
    if (!name || !name[0]) return "The name is empty";
    // The slot's .pti in the Workspace, complete: a streamed instrument is
    // copied from its project file first (issue #184).
    autosave(streamCopyPending(slot) ? StreamCopies::kAll : StreamCopies::kNone);
    if (streamCopyPending(slot)) return "Instrument could not be saved";
    char src[64];
    std::snprintf(src, sizeof src, "%s/instruments/instrument_%02d.pti", kWorkspace, slot + 1);
    IStorage& f = fs();
    if (!f.exists(src)) return "Instrument could not be saved";
    f.mkdir(kInstrumentLibDir);
    if (dir && dir[0]) f.mkdir(dir);
    char dst[300];
    std::snprintf(dst, sizeof dst, "%s/%s.pti", dir && dir[0] ? dir : kInstrumentLibDir, name);
    return copyFile(src, dst) ? nullptr : "Instrument could not be saved";
}

const char* App::loadLibraryInstrument(int slot, const char* path) {
    if (slot < 0 || slot >= kSampleInstrumentCount) return "Not a sample instrument";
    // The slot's own sample goes first: its room counts.
    const uint32_t need = sampleBytesNeeded(path);
    if (!need) return "Not an instrument file";
    if (need > bank.freeSamples() * 2u + bank.samples(slot) * 2u) return "Not enough sample memory";
    return importSample(path, slot) ? nullptr : "Instrument could not be loaded";
}

}  // namespace ot
