#include "formats/midi_preset.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ot {

namespace {

bool eqi(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = char(x + 32);
        if (y >= 'A' && y <= 'Z') y = char(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
}

// A whole decimal number in [lo, hi]; false otherwise.
bool number(const char* s, int lo, int hi, int& v) {
    if (!*s) return false;
    char* end = nullptr;
    const long n = std::strtol(s, &end, 10);
    if (end == s || *end || n < lo || n > hi) return false;
    v = int(n);
    return true;
}

void ccToken(uint8_t cc, char* out, size_t cap) {
    if (cc == 128) std::snprintf(out, cap, "PC");
    else if (cc == 129) std::snprintf(out, cap, "AT");
    else if (cc == 130) std::snprintf(out, cap, "PAT");
    else std::snprintf(out, cap, "%d", cc > 127 ? 127 : cc);
}

bool ccFromToken(const char* s, uint8_t& cc) {
    int n;
    if (eqi(s, "PC")) cc = 128;
    else if (eqi(s, "AT")) cc = 129;
    else if (eqi(s, "PAT")) cc = 130;
    else if (number(s, 0, 127, n)) cc = uint8_t(n);
    else return false;
    return true;
}

}  // namespace

size_t encodeMidiPreset(const MidiPreset& p, char* out, size_t cap) {
    size_t n = 0;
    auto put = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        const int w = std::snprintf(out + n, cap - n, fmt, args...);
        n = w < 0 ? cap : n + size_t(w);
    };
    put("# OpenTracker MIDI preset\n");
    put("velocity = %d\n", p.velocity);
    put("offset = %d\n", p.offset);
    for (int i = 0; i < 6; ++i) {
        char t[8];
        ccToken(p.cc[i], t, sizeof(t));
        if (p.label[i][0]) put("cc %c = %s %s\n", 'A' + i, t, p.label[i]);
        else put("cc %c = %s\n", 'A' + i, t);
    }
    const MidiLfo& l = p.lfo;
    if (l.target == MidiLfo::kTargetPitchBend) put("lfo target = PB\n");
    else if (l.target == MidiLfo::kTargetPressure) put("lfo target = AT\n");
    else if (l.active()) put("lfo target = CC %d\n", l.target - 1);
    else put("lfo target = off\n");
    put("lfo shape = %d\n", l.shape);
    put("lfo rate = %d\n", l.rate);
    put("lfo depth = %d\n", l.depth);
    put("lfo center = %d\n", l.center);
    put("lfo retrigger = %d\n", l.retrig);
    if (n >= cap) {
        if (cap) out[0] = 0;
        return 0;
    }
    return n;
}

bool decodeMidiPreset(const char* text, size_t len, MidiPreset& p) {
    p = MidiPreset{};
    bool any = false;
    size_t i = 0;
    while (i < len) {
        char line[96];
        size_t k = 0;
        while (i < len && text[i] != '\n') {
            if (k + 1 < sizeof(line) && text[i] != '\r') line[k++] = text[i];
            ++i;
        }
        ++i;
        line[k] = 0;
        if (char* h = std::strchr(line, '#')) *h = 0;
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        // Trim the key and the value.
        auto trim = [](char* s) {
            while (*s == ' ' || *s == '\t') ++s;
            char* e = s + std::strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
            return s;
        };
        char* key = trim(line);
        char* val = trim(eq + 1);
        int v;
        if (eqi(key, "velocity")) {
            if (number(val, 0, 127, v)) p.velocity = uint8_t(v), any = true;
        } else if (eqi(key, "offset")) {
            if (number(val, 0, 100, v)) p.offset = uint8_t(v), any = true;
        } else if (std::strlen(key) == 4 && (key[0] | 32) == 'c' && (key[1] | 32) == 'c' && key[2] == ' ' &&
                   (key[3] | 32) >= 'a' && (key[3] | 32) <= 'f') {
            const int slot = (key[3] | 32) - 'a';
            char* sp = std::strchr(val, ' ');
            const char* label = "";
            if (sp) {
                *sp = 0;
                label = trim(sp + 1);
            }
            if (ccFromToken(val, p.cc[slot])) {
                std::snprintf(p.label[slot], sizeof(p.label[slot]), "%s", label);
                any = true;
            }
        } else if (eqi(key, "lfo target")) {
            if (eqi(val, "off")) p.lfo.target = MidiLfo::kTargetOff, any = true;
            else if (eqi(val, "PB")) p.lfo.target = MidiLfo::kTargetPitchBend, any = true;
            else if (eqi(val, "AT")) p.lfo.target = MidiLfo::kTargetPressure, any = true;
            else if ((val[0] | 32) == 'c' && (val[1] | 32) == 'c' && number(trim(val + 2), 0, 127, v))
                p.lfo.target = uint8_t(v + 1), any = true;
        } else if (eqi(key, "lfo shape")) {
            if (number(val, 0, MidiLfo::kShapeCount - 1, v)) p.lfo.shape = uint8_t(v), any = true;
        } else if (eqi(key, "lfo rate")) {
            if (number(val, 0, MidiLfo::kRateCount - 1, v)) p.lfo.rate = uint8_t(v), any = true;
        } else if (eqi(key, "lfo depth")) {
            if (number(val, 0, 100, v)) p.lfo.depth = uint8_t(v), any = true;
        } else if (eqi(key, "lfo center")) {
            if (number(val, 0, 127, v)) p.lfo.center = uint8_t(v), any = true;
        } else if (eqi(key, "lfo retrigger")) {
            if (number(val, 0, MidiLfo::kRetrigCount - 1, v)) p.lfo.retrig = uint8_t(v), any = true;
        }
    }
    for (auto& l : p.label)
        for (char* c = l; *c; ++c)
            if (*c < 32 || *c > 126) *c = '_';
    return any;
}

}  // namespace ot
