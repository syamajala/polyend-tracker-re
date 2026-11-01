// [I] #3 SFZ reader (formats/sfz.h).
#include "formats/sfz.h"

#include <cstring>

namespace ot {
namespace sfz {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool isOpChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

bool eq(const char* s, size_t n, const char* lit) {
    const size_t m = std::strlen(lit);
    if (n != m) return false;
    for (size_t i = 0; i < n; ++i)
        if (lower(s[i]) != lit[i]) return false;
    return true;
}

bool parseU32(const char* s, size_t n, uint32_t* out) {
    if (n == 0) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + uint64_t(s[i] - '0');
        if (v > 0xFFFFFFFEu) return false;
    }
    *out = uint32_t(v);
    return true;
}

// The opcodes a level (global, group, region) sets; a region starts from
// its group, which starts from the global level.
struct Level {
    uint32_t pathOff = 0;
    uint16_t pathLen = 0;
    bool hasPath = false;
    int lokey = -1, hikey = -1, key = -1, root = -1;
    uint32_t offset = 0;
    uint32_t end = kToEnd;
};

enum Header { kNone, kControl, kGlobal, kGroup, kRegion, kOther };

struct Parser {
    const char* t;
    uint32_t n;
    File& f;
    Level global, group, region;
    Header cur = kNone;

    Level& level() { return cur == kGlobal ? global : cur == kGroup ? group : region; }

    void closeRegion() {
        if (cur != kRegion) return;
        const Level& r = region;
        if (!r.hasPath || f.count >= kMaxRegions) {
            ++f.dropped;
            return;
        }
        Region& o = f.regions[f.count++];
        o.pathOff = r.pathOff;
        o.pathLen = r.pathLen;
        int lo = r.lokey, hi = r.hikey, root = r.root;
        if (r.key >= 0) {
            if (lo < 0) lo = r.key;
            if (hi < 0) hi = r.key;
            if (root < 0) root = r.key;
        }
        if (lo < 0) lo = 0;
        if (hi < 0) hi = 127;
        // SFZ's default key centre is 60; a region for one key without one
        // plays that key unpitched [I].
        if (root < 0) root = lo == hi ? lo : 60;
        o.lokey = uint8_t(lo);
        o.hikey = uint8_t(hi < lo ? lo : hi);
        o.root = uint8_t(root);
        o.offset = r.offset;
        o.end = r.end;
    }

    void header(const char* s, size_t len) {
        closeRegion();
        if (eq(s, len, "control")) cur = kControl;
        else if (eq(s, len, "global")) {
            cur = kGlobal;
            global = Level{};
            group = Level{};
        } else if (eq(s, len, "group") || eq(s, len, "master")) {
            cur = kGroup;
            group = global;
        } else if (eq(s, len, "region")) {
            cur = kRegion;
            region = group;
        } else {
            cur = kOther;   // <curve>, <effect>, <midi>, <sample>: not ours
        }
    }

    void opcode(const char* k, size_t kn, uint32_t vOff, size_t vn) {
        const char* v = t + vOff;
        if (cur == kControl) {
            if (eq(k, kn, "default_path")) {
                f.defPathOff = vOff;
                f.defPathLen = uint16_t(vn > 0xFFFF ? 0xFFFF : vn);
            }
            return;
        }
        if (cur != kGlobal && cur != kGroup && cur != kRegion) return;
        Level& l = level();
        uint32_t u = 0;
        if (eq(k, kn, "sample")) {
            l.pathOff = vOff;
            l.pathLen = uint16_t(vn > 0xFFFF ? 0xFFFF : vn);
            l.hasPath = vn > 0;
        } else if (eq(k, kn, "lokey")) {
            l.lokey = noteValue(v, vn);
        } else if (eq(k, kn, "hikey")) {
            l.hikey = noteValue(v, vn);
        } else if (eq(k, kn, "key")) {
            l.key = noteValue(v, vn);
        } else if (eq(k, kn, "pitch_keycenter")) {
            l.root = noteValue(v, vn);
        } else if (eq(k, kn, "offset")) {
            if (parseU32(v, vn, &u)) l.offset = u;
        } else if (eq(k, kn, "end")) {
            if (parseU32(v, vn, &u)) l.end = u;
        }
    }

    // The value of `sample` / `default_path` ends where the next opcode
    // ("word=") starts or at the end of the line.
    uint32_t pathEnd(uint32_t i) const {
        uint32_t e = i;
        while (e < n && t[e] != '\n' && t[e] != '\r') {
            if (isSpace(t[e])) {
                uint32_t j = e;
                while (j < n && (t[j] == ' ' || t[j] == '\t')) ++j;
                uint32_t k = j;
                while (k < n && isOpChar(t[k])) ++k;
                if (k > j && k < n && t[k] == '=') break;
                if (j < n && (t[j] == '<' || (t[j] == '/' && j + 1 < n && t[j + 1] == '/'))) break;
            }
            ++e;
        }
        while (e > i && isSpace(t[e - 1])) --e;
        return e;
    }

    void run() {
        uint32_t i = 0;
        while (i < n) {
            const char c = t[i];
            if (isSpace(c)) {
                ++i;
            } else if (c == '/' && i + 1 < n && t[i + 1] == '/') {
                while (i < n && t[i] != '\n') ++i;
            } else if (c == '/' && i + 1 < n && t[i + 1] == '*') {
                i += 2;
                while (i + 1 < n && !(t[i] == '*' && t[i + 1] == '/')) ++i;
                i += 2;
            } else if (c == '<') {
                uint32_t e = i + 1;
                while (e < n && t[e] != '>' && t[e] != '\n') ++e;
                header(t + i + 1, e - i - 1);
                i = e < n ? e + 1 : n;
            } else if (c == '#') {   // #define / #include: not supported
                while (i < n && t[i] != '\n') ++i;
            } else if (isOpChar(c)) {
                uint32_t k = i;
                while (k < n && isOpChar(t[k])) ++k;
                if (k >= n || t[k] != '=') {   // a stray word
                    i = k + 1;
                    continue;
                }
                const char* key = t + i;
                const size_t kn = k - i;
                uint32_t v = k + 1;
                uint32_t ve;
                if (eq(key, kn, "sample") || eq(key, kn, "default_path")) {
                    ve = pathEnd(v);
                } else {
                    ve = v;
                    while (ve < n && !isSpace(t[ve])) ++ve;
                }
                opcode(key, kn, v, ve - v);
                i = ve;
            } else {
                ++i;
            }
        }
        closeRegion();
    }
};

}  // namespace

int noteValue(const char* s, size_t n) {
    while (n > 0 && isSpace(*s)) ++s, --n;
    while (n > 0 && isSpace(s[n - 1])) --n;
    if (n == 0) return -1;
    if (s[0] >= '0' && s[0] <= '9') {
        uint32_t v = 0;
        return parseU32(s, n, &v) && v <= 127 ? int(v) : -1;
    }
    static const int8_t kIdx[7] = {9, 11, 0, 2, 4, 5, 7};   // a b c d e f g
    const char l = lower(s[0]);
    if (l < 'a' || l > 'g') return -1;
    int note = kIdx[l - 'a'];
    size_t i = 1;
    if (i < n && s[i] == '#') ++note, ++i;
    else if (i < n && s[i] == 'b') --note, ++i;
    bool neg = false;
    if (i < n && s[i] == '-') neg = true, ++i;
    if (i + 1 != n || s[i] < '0' || s[i] > '9') return -1;
    const int oct = (neg ? -1 : 1) * (s[i] - '0');
    const int v = (oct + 1) * 12 + note;   // c4 = 60, c-1 = 0
    return v >= 0 && v <= 127 ? v : -1;
}

bool parse(const char* text, uint32_t len, File& out) {
    out.count = 0;
    out.dropped = 0;
    out.defPathOff = 0;
    out.defPathLen = 0;
    if (!text) return false;
    Parser p{text, len, out, {}, {}, {}};
    p.run();
    return out.count > 0;
}

bool samplePath(const char* text, const File& f, const Region& r, const char* sfzPath, char* out, size_t cap) {
    if (!out || cap < 2) return false;
    // Segments go onto `out` one by one; ".." drops the last one.
    size_t o = 0;
    const bool abs = sfzPath && sfzPath[0] == '/';
    if (abs) out[o++] = '/';
    const size_t root = o;
    out[o] = 0;
    auto push = [&](const char* s, size_t n) -> bool {
        size_t i = 0;
        while (i < n) {
            while (i < n && (s[i] == '/' || s[i] == '\\')) ++i;
            size_t e = i;
            while (e < n && s[e] != '/' && s[e] != '\\') ++e;
            const size_t sl = e - i;
            if (sl == 0) break;
            if (sl == 1 && s[i] == '.') {
                // this folder
            } else if (sl == 2 && s[i] == '.' && s[i + 1] == '.') {
                if (o == root) return false;   // above the card's root
                while (o > root && out[o - 1] != '/') --o;
                if (o > root) --o;   // the separator
                out[o] = 0;
            } else {
                const bool sep = o > root;
                if (o + (sep ? 1 : 0) + sl + 1 > cap) return false;
                if (sep) out[o++] = '/';
                std::memcpy(out + o, s + i, sl);
                o += sl;
                out[o] = 0;
            }
            i = e;
        }
        return true;
    };
    if (sfzPath) {
        const char* slash = std::strrchr(sfzPath, '/');
        if (slash && !push(sfzPath, size_t(slash - sfzPath))) return false;
    }
    if (f.defPathLen && !push(text + f.defPathOff, f.defPathLen)) return false;
    if (!push(text + r.pathOff, r.pathLen)) return false;
    return o > root;
}

}  // namespace sfz
}  // namespace ot
