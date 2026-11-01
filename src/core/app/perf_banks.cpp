// [I] #70 Performance scene banks (formats/perf_bank.h).
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app/perf_presets.h"
#include "app/text_scratch.h"
#include "formats/file_io.h"
#include "formats/perf_bank.h"
#include "sdram_bss.h"

namespace ot {

namespace {
OT_SDRAM_BSS PerfBank g_bank;

bool bankPath(char* out, size_t cap, const char* name) {
    if (!name || !name[0] || std::strlen(name) > size_t(pbank::kNameMax)) return false;
    const int n = std::snprintf(out, cap, "%s/%s%s", pbank::kDir, name, pbank::kExt);
    return n > 0 && size_t(n) < cap;
}
}  // namespace

bool App::loadPerfBank(const char* name, const perf::Source& src) {
    if (!src.valid()) return false;
    char path[64];
    if (!bankPath(path, sizeof(path), name)) return false;
    uint32_t got = 0;
    char* text = textScratch();
    if (readWholeFile(fs(), path, reinterpret_cast<uint8_t*>(text), pbank::kTextMax - 1, &got) != FmtStatus::Ok)
        return false;
    PerfBank& b = g_bank;
    if (!decodePerfBank(text, got, b)) return false;
    for (int c = 0; c < perf::kSlots; ++c) {
        src.setType(c, b.type[c]);
        src.setTarget(c, b.type[c] == perf::kMidiCc ? b.target[c] : 0);
        src.setValue(c, 0, 0);
        for (int r = 0; r < 3; ++r) src.setValue(c, r + 1, b.value[c][r]);
        src.setRow(c, 0);
    }
    // A scene drives only the columns that have a type (as a scene saved on the device).
    for (int i = 0; i < Project::kPerfScenes; ++i)
        for (int c = 0; c < perf::kSlots; ++c)
            project.perfScene[i][c] = src.type(c) && b.scene[i][c] <= 3 ? b.scene[i][c] : 0;
    std::memcpy(project.perfSceneFxOff, b.fxOff, sizeof(project.perfSceneFxOff));
    project.perfSceneFxSet = b.fxSet;
    project.perfSceneOn[0] = b.on[0];
    project.perfSceneOn[1] = b.on[1];
    markProjectDirty();
    return true;
}

bool App::savePerfBank(const char* name, const perf::Source& src) {
    if (!src.valid()) return false;
    char path[64], tmp[72];
    if (!bankPath(path, sizeof(path), name)) return false;
    PerfBank& b = g_bank;
    b = PerfBank{};
    for (int c = 0; c < perf::kSlots; ++c) {
        b.type[c] = src.type(c) < perf::kFxCount ? src.type(c) : 0;
        b.target[c] = src.target(c);
        for (int r = 0; r < 3; ++r) b.value[c][r] = src.value(c, r + 1);
    }
    std::memcpy(b.scene, project.perfScene, sizeof(b.scene));
    std::memcpy(b.fxOff, project.perfSceneFxOff, sizeof(b.fxOff));
    b.fxSet = project.perfSceneFxSet;
    b.on[0] = project.perfSceneOn[0];
    b.on[1] = project.perfSceneOn[1];
    char* text = textScratch();
    const size_t n = encodePerfBank(b, text, pbank::kTextMax);
    if (!n) return false;
    IStorage& f = fs();
    if (!f.mkdir("Performance") || !f.mkdir(pbank::kDir)) return false;
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (writeWholeFile(f, tmp, reinterpret_cast<const uint8_t*>(text), uint32_t(n)) != FmtStatus::Ok) {
        f.remove(tmp);
        return false;
    }
    if (f.exists(path) && !f.remove(path)) return false;
    return f.rename(tmp, path);
}

}  // namespace ot
