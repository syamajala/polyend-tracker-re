#include "model/midi_cc_names.h"

#include <cstdio>
#include <cstring>

namespace ot {

void MidiCcNames::clear() {
    std::memset(preset, 0, sizeof(preset));
    for (int i = 0; i < kMidiInstrumentCount; ++i) chart[i] = uint8_t(i);
    count = 0;
}

bool MidiCcNames::used() const {
    if (count) return true;
    for (const auto& p : preset)
        if (p[0]) return true;
    return false;
}

const char* MidiCcNames::name(int midi, int cc) const {
    if (midi < 0 || midi >= kMidiInstrumentCount || cc < 0 || cc > 127) return "";
    for (int i = 0; i < count; ++i)
        if (entry[i].chart == chart[midi] && entry[i].cc == cc) return entry[i].name;
    return "";
}

bool MidiCcNames::setName(int midi, int cc, const char* s) {
    if (midi < 0 || midi >= kMidiInstrumentCount || cc < 0 || cc > 127) return false;
    const uint8_t c = chart[midi];
    int at = -1;
    for (int i = 0; i < count; ++i)
        if (entry[i].chart == c && entry[i].cc == cc) at = i;
    if (!s || !s[0]) {
        if (at >= 0) entry[at] = entry[--count];
        return true;
    }
    if (at < 0) {
        if (count >= kEntryMax) return false;
        at = count++;
        entry[at].chart = c;
        entry[at].cc = uint8_t(cc);
    }
    std::snprintf(entry[at].name, sizeof(entry[at].name), "%s", s);
    for (char* q = entry[at].name; *q; ++q)
        if (*q < 32 || *q > 126) *q = '_';
    return true;
}

void MidiCcNames::clearChart(int midi) {
    if (midi < 0 || midi >= kMidiInstrumentCount) return;
    const uint8_t c = chart[midi];
    for (int i = 0; i < count;)
        if (entry[i].chart == c) entry[i] = entry[--count];
        else ++i;
}

bool MidiCcNames::share(int midi, const char* presetName) {
    if (midi < 0 || midi >= kMidiInstrumentCount || !presetName || !presetName[0]) return false;
    for (int j = 0; j < kMidiInstrumentCount; ++j)
        if (j != midi && std::strcmp(preset[j], presetName) == 0) {
            chart[midi] = chart[j];
            return true;
        }
    return false;
}

void MidiCcNames::unshare(int midi) {
    if (midi < 0 || midi >= kMidiInstrumentCount) return;
    bool inUse[kChartCount] = {};
    bool shared = false;
    for (int j = 0; j < kMidiInstrumentCount; ++j) {
        if (j == midi) continue;
        inUse[chart[j]] = true;
        shared = shared || chart[j] == chart[midi];
    }
    if (!shared) return;
    // 16 instruments, 16 charts: two share one, so one is free.
    for (int c = 0; c < kChartCount; ++c)
        if (!inUse[c]) {
            chart[midi] = uint8_t(c);
            clearChart(midi);   // a chart nobody used may still hold old names
            return;
        }
}

void MidiCcNames::sanitize() {
    for (auto& p : preset) {
        p[kPresetNameMax] = 0;
        for (char* q = p; *q; ++q)
            if (*q < 32 || *q > 126) *q = '_';
    }
    for (auto& c : chart)
        if (c >= kChartCount) c = 0;
    if (count > kEntryMax) count = kEntryMax;
    for (int i = 0; i < count;) {
        Entry& e = entry[i];
        e.name[kNameMax] = 0;
        for (char* q = e.name; *q; ++q)
            if (*q < 32 || *q > 126) *q = '_';
        if (e.chart >= kChartCount || e.cc > 127 || !e.name[0]) e = entry[--count];
        else ++i;
    }
}

}  // namespace ot
