// [I] #13 MIDI instrument presets (formats/midi_preset.h).
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "formats/file_io.h"
#include "formats/midi_preset.h"
#include "sdram_bss.h"

namespace ot {

namespace {
OT_SDRAM_BSS char g_presetText[mpreset::kTextMax];
OT_SDRAM_BSS MidiPreset g_preset;   // 1.3 KB of CC names: not on the stack

bool presetPath(char* out, size_t cap, const char* name) {
    if (!name || !name[0] || std::strlen(name) > size_t(mpreset::kNameMax)) return false;
    const int n = std::snprintf(out, cap, "%s/%s%s", mpreset::kDir, name, mpreset::kExt);
    return n > 0 && size_t(n) < cap;
}
}  // namespace

bool App::loadMidiPreset(int midi, const char* name) {
    if (midi < 0 || midi >= kMidiInstrumentCount) return false;
    char path[64];
    if (!presetPath(path, sizeof(path), name)) return false;
    uint32_t got = 0;
    if (readWholeFile(fs(), path, reinterpret_cast<uint8_t*>(g_presetText), mpreset::kTextMax - 1, &got) !=
        FmtStatus::Ok)
        return false;
    MidiPreset& p = g_preset;
    if (!decodeMidiPreset(g_presetText, got, p)) return false;
    MidiInstrument m = project.midiInstrument(midi);
    m.velocity = p.velocity;
    m.extra[0] = p.offset;
    std::memcpy(m.cc, p.cc, sizeof(m.cc));
    project.setMidiInstrument(midi, m);
    project.midiLfo[midi] = p.lfo;
    project.midiLfo[midi].sanitize();
    // The CC names: shared with the instruments that have this preset (the
    // file's names replace theirs), else a chart of this instrument's own.
    MidiCcNames& n = midiNames;
    if (!n.share(midi, name)) n.unshare(midi);
    n.clearChart(midi);
    bool full = false;
    for (int c = 0; c < 128; ++c)
        if (p.name[c][0] && !n.setName(midi, c, p.name[c])) full = true;
    std::snprintf(n.preset[midi], sizeof(n.preset[midi]), "%s", name);
    markProjectDirty();
    return !full;
}

bool App::saveMidiPreset(int midi, const char* name) {
    if (midi < 0 || midi >= kMidiInstrumentCount) return false;
    char path[64], tmp[72];
    if (!presetPath(path, sizeof(path), name)) return false;
    const MidiInstrument m = project.midiInstrument(midi);
    MidiPreset& p = g_preset;
    p.reset();
    p.velocity = m.velocity;
    p.offset = m.extra[0] > 100 ? 100 : m.extra[0];
    std::memcpy(p.cc, m.cc, sizeof(p.cc));
    p.lfo = project.midiLfo[midi];
    for (int c = 0; c < 128; ++c) std::snprintf(p.name[c], sizeof(p.name[c]), "%s", midiNames.name(midi, c));
    const size_t n = encodeMidiPreset(p, g_presetText, mpreset::kTextMax);
    if (!n) return false;
    IStorage& f = fs();
    if (!f.mkdir(mpreset::kDir)) return false;
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (writeWholeFile(f, tmp, reinterpret_cast<const uint8_t*>(g_presetText), uint32_t(n)) != FmtStatus::Ok) {
        f.remove(tmp);
        return false;
    }
    if (f.exists(path) && !f.remove(path)) return false;
    if (!f.rename(tmp, path)) return false;
    std::snprintf(midiNames.preset[midi], sizeof(midiNames.preset[midi]), "%s", name);
    markProjectDirty();
    return true;
}

}  // namespace ot
