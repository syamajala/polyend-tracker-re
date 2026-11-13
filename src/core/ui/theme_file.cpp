// [I] CE JSON theme files (ui/theme_file.h): reader, lister, exporter.
#include "ui/theme_file.h"

#include <cstdio>
#include <cstring>

namespace ot {

namespace {

#include "ui/ce_theme_map.inc"

const char* const kBaseNames[kThemeCount] = {"Original", "Monochrome", "Anniversary"};

// Unreadable-text guard: (text, its background) slot pairs. Equal colours
// restore both from the base theme.
const uint16_t kReadable[][2] = {{slot::kList + 4, slot::kList + 3},
                                 {slot::kBottomLabel, slot::kStrip + 1},
                                 {slot::kTitle, slot::kTitle + 1}};

char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
bool sameCi(const char* a, const char* b) {
    while (*a && lower(*a) == lower(*b)) ++a, ++b;
    return lower(*a) == lower(*b);
}
int cmpCi(const char* a, const char* b) {
    while (*a && lower(*a) == lower(*b)) ++a, ++b;
    const int d = int(uint8_t(lower(*a))) - int(uint8_t(lower(*b)));
    return d ? d : std::strcmp(a, b);
}
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// "#RRGGBB" or "#RGB".
bool parseColour(const char* v, uint32_t* out) {
    if (*v++ != '#') return false;
    const int n = int(std::strlen(v));
    if (n != 6 && n != 3) return false;
    uint32_t c = 0;
    for (int i = 0; i < n; ++i) {
        const int d = hexDigit(v[i]);
        if (d < 0) return false;
        c = (c << (n == 3 ? 8 : 4)) | uint32_t(n == 3 ? d * 17 : d);
    }
    *out = c & 0xFFFFFFu;
    return true;
}

// CE key index of (dotted group path, leaf), or -1.
int ceKey(const char* group, const char* leaf) {
    int g = -1, l = -1;
    for (int i = 0; i < int(sizeof(kCeGroups) / sizeof(kCeGroups[0])) && g < 0; ++i)
        if (!std::strcmp(group, kCeGroups[i])) g = i;
    for (int i = 0; i < int(sizeof(kCeLeaves) / sizeof(kCeLeaves[0])) && l < 0; ++i)
        if (!std::strcmp(leaf, kCeLeaves[i])) l = i;
    if (g < 0 || l < 0) return -1;
    for (int k = 0; k < kCeKeyCount; ++k)
        if (kCeKeys[k][0] == g && kCeKeys[k][1] == l) return k;
    return -1;
}

void copyBase(uint32_t* slots, uint8_t base) { std::memcpy(slots, theme(base).slots, sizeof(uint32_t) * kThemeSlotCount); }

// Streaming JSON reader over byte chunks (no file buffer): objects nest the
// key path, string values are colours (or "base"); numbers, literals and
// arrays are skipped.
struct Parser {
    static constexpr int kDepthMax = 4, kPathMax = 63, kStrMax = 47;
    uint32_t* slots;
    ThemeParse* r;
    uint32_t colour[kCeKeyCount];
    uint8_t have[(kCeKeyCount + 7) / 8] = {};
    char path[kPathMax + 1] = {};   // dotted object path below the root
    uint8_t pathLen[kDepthMax + 1] = {};
    int depth = -1;                 // -1: before the root object
    int arrays = 0;                 // open [ ] (their contents are skipped)
    char str[kStrMax + 1];
    int sn = 0;
    bool inStr = false, esc = false, strOver = false;
    char key[kStrMax + 1] = {};
    bool haveKey = false, keyOver = false, colon = false;
    bool deep = false;              // nested deeper than kDepthMax: skip the object
    int deepDepth = 0;

    Parser(uint32_t* s, ThemeParse* p) : slots(s), r(p) { *r = ThemeParse{}; }

    void feed(const char* p, uint32_t len) {
        for (uint32_t i = 0; i < len; ++i) step(p[i]);
    }
    void step(char c) {
        if (inStr) {
            if (!esc && c == '\\') {
                esc = true;
                return;
            }
            if (!esc && c == '"') {
                inStr = false;
                str[sn] = 0;
                string();
                return;
            }
            esc = false;   // an escaped character is kept as is (\" -> ")
            if (sn < kStrMax) str[sn++] = c;
            else strOver = true;
            return;
        }
        switch (c) {
            case '"':
                inStr = true;
                esc = strOver = false;
                sn = 0;
                break;
            case '{': open(); break;
            case '}': close(); break;
            case '[': ++arrays; break;
            case ']':
                if (arrays) --arrays;
                break;
            case ':': colon = haveKey; break;
            case ',':
                if (!arrays) haveKey = colon = false;
                break;
            default: break;   // whitespace, numbers, true / false / null
        }
    }
    void open() {
        if (arrays) return;
        if (deep || depth >= kDepthMax) {
            deep = true;
            ++deepDepth;
            return;
        }
        if (depth >= 0) {
            // Nest the key: path "a" + ".b".
            const int at = pathLen[depth];
            const int need = (at ? 1 : 0) + int(std::strlen(key));
            if (!haveKey || keyOver || at + need > kPathMax) {
                deep = true;
                ++deepDepth;
                haveKey = colon = false;
                return;
            }
            if (at) path[at] = '.';
            std::memcpy(path + at + (at ? 1 : 0), key, std::strlen(key) + 1);
            pathLen[depth + 1] = uint8_t(at + need);
        }
        ++depth;
        haveKey = colon = false;
    }
    void close() {
        if (arrays) return;
        if (deep) {
            if (--deepDepth == 0) deep = false;
            haveKey = colon = false;
            return;
        }
        if (depth > 0) path[pathLen[depth - 1]] = 0;
        if (depth >= 0) --depth;
        haveKey = colon = false;
    }
    void string() {
        if (arrays || deep || depth < 0) return;
        if (!haveKey) {
            std::memcpy(key, str, size_t(sn) + 1);
            keyOver = strOver;
            haveKey = true;
            colon = false;
            return;
        }
        if (!colon) return;
        value();
        haveKey = colon = false;
    }
    void value() {
        if (depth == 0 && !std::strcmp(key, "name")) return;
        if (depth == 0 && !std::strcmp(key, "base")) {
            for (uint8_t b = 0; b < kThemeCount; ++b)
                if (sameCi(str, kBaseNames[b])) r->base = b;
            return;
        }
        const int k = keyOver ? -1 : ceKey(path, key);
        uint32_t c = 0;
        if (k < 0 || strOver || !parseColour(str, &c)) {
            ++r->badKeys;
            return;
        }
        colour[k] = c;
        have[k / 8] = uint8_t(have[k / 8] | (1u << (k % 8)));
    }
    void finish(bool truncated) {
        r->truncated = truncated;
        copyBase(slots, r->base);
        for (const CeMapEntry& e : kCeMap) {
            if (!(have[e.key / 8] & (1u << (e.key % 8)))) continue;
            for (int i = 0; i < e.count; ++i) slots[kCeSlots[e.first + i]] = colour[e.key];
        }
        for (int k = 0; k < kCeKeyCount; ++k)
            if (have[k / 8] & (1u << (k % 8))) ++r->colours;
        // CE draws flat (no fades or value-bar gradients), bar lines on, and
        // the pattern band only on the playing row.
        slots[slot::kFlagBottomFade] = 0;
        slots[slot::kFlagValueBarGrad] = 0;
        slots[slot::kFlagCursorRowGrad] = 1;
        slots[slot::kFlagBarLines] = 1;
        slots[slot::kFlagBandPlayingOnly] = 1;
        const uint32_t* base = theme(r->base).slots;
        for (const auto& g : kReadable) {
            if (slots[g[0]] != slots[g[1]]) continue;
            r->guarded = true;
            slots[g[0]] = base[g[0]];
            slots[g[1]] = base[g[1]];
        }
    }
};

void themePath(char* out, size_t cap, const char* name) { std::snprintf(out, cap, "%s/%s.json", kThemesDir, name); }

struct ListCtx {
    char (*names)[kThemeNameMax + 1];
    int max;
    int n;
};
bool listFn(const IStorage::DirEntry& e, void* p) {
    ListCtx& x = *static_cast<ListCtx*>(p);
    if (e.isDir) return true;
    const size_t len = std::strlen(e.name);
    if (len < 6 || len - 5 > size_t(kThemeNameMax) || !sameCi(e.name + len - 5, ".json")) return true;
    char base[kThemeNameMax + 1];
    std::memcpy(base, e.name, len - 5);
    base[len - 5] = 0;
    if (!themeNameValid(base)) return true;
    // Sorted insertion; when full, the last one in sort order drops out.
    int i = x.n < x.max ? x.n++ : x.max;
    if (i == x.max && (x.max == 0 || cmpCi(base, x.names[x.max - 1]) >= 0)) return true;
    if (i == x.max) --i;
    while (i > 0 && cmpCi(x.names[i - 1], base) > 0) {
        std::memcpy(x.names[i], x.names[i - 1], sizeof(x.names[0]));
        --i;
    }
    std::memcpy(x.names[i], base, sizeof(base));
    return true;
}

// Buffered text output to an open file.
struct Writer {
    IStorage& fs;
    IStorage::Handle h;
    char buf[256];
    int n = 0;
    bool ok = true;
    void flush() {
        if (n && fs.write(h, buf, uint32_t(n)) != n) ok = false;
        n = 0;
    }
    void put(const char* s) {
        for (; *s; ++s) {
            if (n == int(sizeof(buf))) flush();
            buf[n++] = *s;
        }
    }
    void indent(int d) {
        put("\n");
        for (int i = 0; i < d; ++i) put("  ");
    }
};

// Component i of a dotted group path: its start and length (false: none).
bool component(const char* g, int i, const char** at, int* len) {
    if (!*g) return false;
    for (; i > 0; --i) {
        g = std::strchr(g, '.');
        if (!g++) return false;
    }
    const char* e = std::strchr(g, '.');
    *at = g;
    *len = e ? int(e - g) : int(std::strlen(g));
    return true;
}

}  // namespace

const char* themeBaseName(uint8_t base) { return kBaseNames[base < kThemeCount ? base : 0]; }

void parseTheme(const char* text, uint32_t len, uint32_t* slots, ThemeParse* out) {
    Parser p(slots, out);
    const bool cut = len > kThemeFileMax;
    p.feed(text, cut ? kThemeFileMax : len);
    p.finish(cut);
}

bool themeNameValid(const char* name) {
    int n = 0;
    for (; name[n]; ++n) {
        const char c = name[n];
        if (c < 0x20 || c > 0x7E || c == '/' || c == '\\' || c == ':' || n >= kThemeNameMax) return false;
    }
    return n > 0;
}

bool loadThemeFile(IStorage& fs, const char* name, uint32_t* slots, ThemeParse* out) {
    if (!themeNameValid(name)) return false;
    char path[48];
    themePath(path, sizeof(path), name);
    const IStorage::Handle h = fs.open(path, IStorage::kRead);
    if (h < 0) return false;
    const int32_t size = fs.size(h);
    Parser p(slots, out);
    char chunk[512];
    uint32_t total = 0;
    int32_t got;
    while (total < kThemeFileMax) {
        const uint32_t want = kThemeFileMax - total < sizeof(chunk) ? kThemeFileMax - total : uint32_t(sizeof(chunk));
        if ((got = fs.read(h, chunk, want)) <= 0) break;
        p.feed(chunk, uint32_t(got));
        total += uint32_t(got);
    }
    fs.close(h);
    p.finish(size > int32_t(kThemeFileMax));
    return true;
}

int listThemeFiles(IStorage& fs, char (*names)[kThemeNameMax + 1], int max) {
    ListCtx x{names, max, 0};
    fs.listDir(kThemesDir, listFn, &x);
    return x.n;
}

ThemeWrite writeThemeFile(IStorage& fs, const char* name, const uint32_t* slots, uint8_t base) {
    if (!themeNameValid(name) || base >= kThemeCount) return ThemeWrite::kFailed;
    char path[48];
    themePath(path, sizeof(path), name);
    if (fs.exists(path)) return ThemeWrite::kExists;
    fs.mkdir(kThemesDir);
    const IStorage::Handle h = fs.open(path, IStorage::kWrite);
    if (h < 0) return ThemeWrite::kFailed;
    Writer w{fs, h, {}};
    char l[48];
    w.put("{");
    w.indent(1);
    w.put("\"name\": \"");
    for (const char* c = name; *c; ++c) {   // JSON string: escape " and backslash
        if (*c == '"' || *c == '\\') w.put("\\");
        const char one[2] = {*c, 0};
        w.put(one);
    }
    std::snprintf(l, sizeof(l), "\",\n  \"base\": \"%s\"", kBaseNames[base]);
    w.put(l);
    // Keys in CE order: the open groups close down to the next key's group
    // path, then the rest of its path opens.
    constexpr int kDepthMax = 4;
    bool first[kDepthMax + 1] = {false, true, true, true, true};
    const char* open = "";
    int depth = 0;
    auto item = [&] {
        if (!first[depth]) w.put(",");
        first[depth] = false;
        w.indent(depth + 1);
    };
    for (int k = 0; k < kCeKeyCount; ++k) {
        if (kCeExport[k] == 0xFFFF) continue;
        const char* g = kCeGroups[kCeKeys[k][0]];
        const char *pa, *pb;
        int la, lb, common = 0;
        while (common < depth && component(open, common, &pa, &la) && component(g, common, &pb, &lb) && la == lb &&
               !std::strncmp(pa, pb, size_t(la)))
            ++common;
        for (; depth > common; --depth) {
            w.indent(depth);
            w.put("}");
        }
        while (depth < kDepthMax && component(g, depth, &pb, &lb)) {
            item();
            std::snprintf(l, sizeof(l), "\"%.*s\": {", lb, pb);
            w.put(l);
            first[++depth] = true;
        }
        item();
        std::snprintf(l, sizeof(l), "\"%s\": \"#%06lx\"", kCeLeaves[kCeKeys[k][1]],
                      static_cast<unsigned long>(slots[kCeExport[k]] & 0xFFFFFFu));
        w.put(l);
        open = g;
    }
    for (; depth > 0; --depth) {
        w.indent(depth);
        w.put("}");
    }
    w.put("\n}\n");
    w.flush();
    fs.close(h);
    return w.ok ? ThemeWrite::kOk : ThemeWrite::kFailed;
}

}  // namespace ot
