#include "formats/perf_bank.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/perf_presets.h"

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

char* trim(char* s) {
    while (*s == ' ' || *s == '\t') ++s;
    char* e = s + std::strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

// The next whitespace-separated token of *s (advanced past it); "" at the end.
const char* token(char*& s) {
    while (*s == ' ' || *s == '\t') ++s;
    char* t = s;
    while (*s && *s != ' ' && *s != '\t') ++s;
    if (*s) *s++ = 0;
    return t;
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

// "scene 12" / "column 3": the number after `word `, 1..max; the rest of the key in *rest.
bool numbered(char* key, const char* word, int max, int& n, char*& rest) {
    const size_t w = std::strlen(word);
    for (size_t i = 0; i < w; ++i)
        if (!key[i] || (key[i] | 32) != word[i]) return false;
    if (key[w] != ' ') return false;
    char* p = key + w + 1;
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    if (end == p || v < 1 || v > max) return false;
    n = int(v);
    rest = trim(end);
    return true;
}

uint8_t typeByName(const char* s) {
    for (int id = 0; id < perf::kFxCount; ++id)
        if (eqi(s, perf::fx(uint8_t(id)).name)) return uint8_t(id);
    return 0;
}

// One row value of type `ty` as written in the file.
bool readValue(uint8_t ty, const char* s, int16_t& v) {
    int n;
    if (ty == perf::kMidiCc) {
        if (std::strcmp(s, "-") == 0) return v = 0, true;
        if (!number(s, 0, 127, n)) return false;
        v = int16_t(n + 1);
        return true;
    }
    const perf::FxDesc& d = perf::fx(ty);
    if (!number(s, d.min, d.max, n)) return false;
    v = int16_t(n);
    return true;
}

}  // namespace

size_t encodePerfBank(const PerfBank& b, char* out, size_t cap) {
    size_t n = 0;
    auto put = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        const int w = std::snprintf(out + n, cap - n, fmt, args...);
        n = w < 0 ? cap : n + size_t(w);
    };
    put("%s", "# OpenTracker scene bank\n");
    for (int c = 0; c < PerfBank::kSlots; ++c) {
        const uint8_t ty = b.type[c] < perf::kFxCount ? b.type[c] : 0;
        put("column %d = %s", c + 1, perf::fx(ty).name);
        if (ty) {
            put("%s", ":");
            for (int r = 0; r < 3; ++r) {
                const int v = b.value[c][r];
                if (ty == perf::kMidiCc && perf::ccValue(v) < 0) put("%s", " -");
                else put(" %d", ty == perf::kMidiCc ? perf::ccValue(v) : v);
            }
            if (ty == perf::kMidiCc)
                put(" @ midi %d slot %c", perf::targetMidi(b.target[c]) + 1, 'A' + perf::targetSlot(b.target[c]) % 6);
        }
        put("%s", "\n");
    }
    for (int i = 0; i < PerfBank::kScenes; ++i) {
        bool any = false;
        for (uint8_t r : b.scene[i]) any = any || r;
        if (any) {
            put("scene %d =", i + 1);
            for (uint8_t r : b.scene[i]) put(" %d", r <= 3 ? r : 0);
            put("%s", "\n");
        }
        if ((b.fxSet >> i) & 1u) {
            put("scene %d fx off =", i + 1);
            if (!b.fxOff[i]) put("%s", " none");
            for (int t = 0; t < 16; ++t)
                if ((b.fxOff[i] >> t) & 1u) put(" %d", t + 1);
            put("%s", "\n");
        }
    }
    put("a = %d\n", b.on[0] % PerfBank::kScenes + 1);
    put("b = %d\n", b.on[1] % PerfBank::kScenes + 1);
    if (n >= cap) {
        if (cap) out[0] = 0;
        return 0;
    }
    return n;
}

bool decodePerfBank(const char* text, size_t len, PerfBank& b) {
    b = PerfBank{};
    bool any = false;
    size_t i = 0;
    while (i < len) {
        char line[128];
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
        char* key = trim(line);
        char* val = trim(eq + 1);
        char* rest = nullptr;
        int n, v;
        if (numbered(key, "column", PerfBank::kSlots, n, rest) && !*rest) {
            const int c = n - 1;
            b.type[c] = 0;
            b.target[c] = 0;
            for (int16_t& x : b.value[c]) x = 0;
            char* colon = std::strchr(val, ':');
            if (colon) *colon = 0;
            const uint8_t ty = typeByName(trim(val));
            any = true;
            if (!ty) continue;
            b.type[c] = ty;
            if (!colon) continue;
            char* p = colon + 1;
            char* at = std::strchr(p, '@');
            if (at) *at++ = 0;
            for (int r = 0; r < 3; ++r) {
                const char* t = token(p);
                if (!*t) break;
                int16_t x = 0;
                if (readValue(ty, t, x)) b.value[c][r] = x;
            }
            if (at && ty == perf::kMidiCc) {
                // "midi M slot X"
                const char* w1 = token(at);
                const char* m = token(at);
                const char* w2 = token(at);
                const char* s = token(at);
                int mi;
                if (eqi(w1, "midi") && number(m, 1, 16, mi) && eqi(w2, "slot") && std::strlen(s) == 1 &&
                    (s[0] | 32) >= 'a' && (s[0] | 32) < 'a' + perf::kCcSlots)
                    b.target[c] = perf::ccTarget(mi - 1, (s[0] | 32) - 'a');
            }
        } else if (numbered(key, "scene", PerfBank::kScenes, n, rest)) {
            const int sc = n - 1;
            if (!*rest) {
                for (int c = 0; c < PerfBank::kSlots; ++c) {
                    const char* t = token(val);
                    b.scene[sc][c] = uint8_t(number(t, 0, 3, v) ? v : 0);
                }
                any = true;
            } else if (eqi(rest, "fx off")) {
                uint16_t off = 0;
                for (const char* t = token(val); *t; t = token(val))
                    if (number(t, 1, 16, v)) off = uint16_t(off | (1u << (v - 1)));
                b.fxOff[sc] = off;
                b.fxSet = uint16_t(b.fxSet | (1u << sc));
                any = true;
            }
        } else if (eqi(key, "a") || eqi(key, "b")) {
            if (number(val, 1, PerfBank::kScenes, v)) b.on[(key[0] | 32) == 'b'] = uint8_t(v - 1), any = true;
        }
    }
    return any;
}

}  // namespace ot
