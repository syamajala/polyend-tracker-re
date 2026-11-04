// [I] #121 Song mode progressions (ui/progression.h).
#include "ui/progression.h"

#include <cstdio>
#include <cstring>

#include "app/text_scratch.h"
#include "formats/file_io.h"
#include "ui/notes.h"

namespace ot {
namespace prog {

namespace {

constexpr uint8_t kMajorSteps[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr uint8_t kMinorSteps[7] = {0, 2, 3, 5, 7, 8, 10};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

// "I".."VII" in either case: 1..7, 0 if not a numeral.
int numeralValue(const char* s, int n) {
    static const char* const kNum[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    for (int v = 0; v < 7; ++v)
        if (int(std::strlen(kNum[v])) == n) {
            int k = 0;
            while (k < n && lower(s[k]) == kNum[v][k]) ++k;
            if (k == n) return v + 1;
        }
    return 0;
}

struct Token {
    int accidental = 0;   // -1 b, +1 #
    int value = 0;        // 1..7
    bool upper = false;
    uint8_t quality = kMaj;
};

bool parseToken(const char* s, int n, Token& t) {
    int i = 0;
    if (i < n && (s[i] == 'b' || s[i] == '#')) t.accidental = s[i++] == 'b' ? -1 : 1;
    const int r0 = i;
    while (i < n && (s[i] == 'I' || s[i] == 'V' || s[i] == 'i' || s[i] == 'v')) ++i;
    if (i == r0) return false;
    bool up = true, low = true;
    for (int k = r0; k < i; ++k) (s[k] >= 'a' ? up : low) = false;
    if (!up && !low) return false;   // mixed case ("Iv")
    t.upper = up;
    t.value = numeralValue(s + r0, i - r0);
    if (!t.value) return false;
    const char* suf = s + i;
    const int sn = n - i;
    auto is = [&](const char* x) { return int(std::strlen(x)) == sn && std::strncmp(suf, x, size_t(sn)) == 0; };
    if (sn == 0) t.quality = up ? kMaj : kMin;
    else if (is("7")) t.quality = up ? kDom7 : kMin7;
    else if (is("maj7") && up) t.quality = kMaj7;
    else if (is("m7")) t.quality = kMin7;
    else if (is("dim")) t.quality = kDim;
    else if (is("dim7")) t.quality = kDim7;
    else if (is("m7b5")) t.quality = kHalfDim7;
    else if (is("aug") || is("+")) t.quality = kAug;
    else if (is("sus2")) t.quality = kSus2;
    else if (is("sus4")) t.quality = kSus4;
    else return false;
    return true;
}

}  // namespace

bool parse(const char* line, Progression& out) {
    out = Progression{};
    if (!line) return false;
    const char* bar = std::strchr(line, '|');
    const char* end = bar ? bar : line + std::strlen(line);
    Token tok[kMaxChords];
    int n = 0;
    for (const char* p = line; p < end;) {
        while (p < end && isSpace(*p)) ++p;
        if (p >= end) break;
        const char* q = p;
        while (q < end && !isSpace(*q)) ++q;
        if (n >= kMaxChords || q - p >= int(sizeof(Chord::numeral))) return false;
        Token t;
        if (!parseToken(p, int(q - p), t)) return false;
        std::memcpy(out.chords[n].numeral, p, size_t(q - p));
        out.chords[n].numeral[q - p] = 0;
        tok[n++] = t;
        p = q;
    }
    if (n == 0) return false;
    for (int i = 0; i < n; ++i)
        if (tok[i].value == 1 && !tok[i].upper && !tok[i].accidental) out.minor = true;
    const uint8_t* steps = out.minor ? kMinorSteps : kMajorSteps;
    for (int i = 0; i < n; ++i) {
        out.chords[i].degree = uint8_t((steps[tok[i].value - 1] + tok[i].accidental + 12) % 12);
        out.chords[i].quality = tok[i].quality;
        out.chords[i].accidental = int8_t(tok[i].accidental);
    }
    out.count = uint8_t(n);
    if (bar) {
        const char* t = bar + 1;
        while (isSpace(*t)) ++t;
        size_t len = std::strlen(t);
        while (len && (isSpace(t[len - 1]) || t[len - 1] == '\n')) --len;
        if (len >= sizeof(out.title)) len = sizeof(out.title) - 1;
        std::memcpy(out.title, t, len);
        out.title[len] = 0;
    }
    return true;
}

int tonic(const Progression& p, int root, uint8_t scale) {
    const uint16_t m = scaleMask(scale);
    const bool keyMinor = ((m >> 3) & 1) && !((m >> 4) & 1);
    int t = root % 12;
    if (p.minor == keyMinor) return t;
    // A mode of the major scale (Dorian, Mixolydian, ...): its parent major
    // key, the major scale with the same notes (D Dorian: C).
    int major = -1;
    for (int k = 0; k < 12 && major < 0; ++k)
        if (((m >> k) | (m << (12 - k))) % 4096 == 0xab5) major = (t + k) % 12;
    if (major < 0) major = keyMinor ? t + 3 : t;   // other scales: as natural minor / major
    return (p.minor ? major + 9 : major) % 12;     // the major key or its relative minor
}

int intervals(uint8_t q, int out[kMaxNotes]) {
    static const int8_t kIv[kQualityCount][kMaxNotes] = {
        {0, 4, 7, -1},  {0, 3, 7, -1},  {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 4, 7, 10}, {0, 4, 7, 11},
        {0, 3, 7, 10},  {0, 3, 6, 10},  {0, 3, 6, 9},  {0, 2, 7, -1}, {0, 5, 7, -1},
    };
    if (q >= kQualityCount) q = kMaj;
    int n = 0;
    for (int i = 0; i < kMaxNotes && kIv[q][i] >= 0; ++i) out[n++] = kIv[q][i];
    return n;
}

int chordRoot(const Progression& p, int i, int tonicPc) { return (tonicPc + p.chords[i].degree) % 12; }

void chordName(const Progression& p, int i, int tonicPc, char* out, int cap) {
    static const char* const kSuffix[kQualityCount] = {"", "m", "dim", "aug", "7", "maj7", "m7", "m7b5", "dim7", "sus2", "sus4"};
    const Chord& c = p.chords[i];
    const int root = chordRoot(p, i, tonicPc);
    const char* pc = noteSpellingAuto() && c.accidental ? pitchClassName(root, c.accidental < 0) : pitchClassName(root);
    std::snprintf(out, size_t(cap), "%s%s", pc, kSuffix[c.quality < kQualityCount ? c.quality : 0]);
}

// ---------------------------------------------------------------------------
// Files

namespace {

constexpr const char* kHeader =
    "# One progression per line, chords as numbers in the key.\n"
    "# Upper case = major, lower case = minor; after it 7, maj7, m7, dim,\n"
    "# dim7, m7b5, aug, sus2 or sus4. b or # in front = borrowed (bVII).\n"
    "# Text after | is the title.\n";

struct DefaultFile {
    const char* name;
    const char* body;
};

const DefaultFile kDefaults[] = {
    {"DnB",
     "i VI III VII\n"
     "i VII VI V\n"
     "i iv VI V\n"
     "i7 iv7 VImaj7 V7 | Liquid\n"
     "i bII | Dark two-chord\n"},
    {"Hip-hop",
     "i iv v i\n"
     "i VI VII i\n"
     "i7 iv7 | Two-chord loop\n"
     "ii7 V7 Imaj7 vi7 | Soul turnaround\n"
     "Imaj7 vi7 ii7 V7\n"},
    {"Jazz",
     "ii7 V7 Imaj7 | Two-five-one\n"
     "ii7 V7 Imaj7 VI7\n"
     "iim7b5 V7 i | Minor two-five-one\n"
     "Imaj7 VI7 ii7 V7 | Turnaround\n"
     "iii7 VI7 ii7 V7\n"},
    {"Pop",
     "I V vi IV\n"
     "vi IV I V\n"
     "I vi IV V | Fifties\n"
     "I IV vi V\n"},
};

bool hasTxt(const char* name) {
    const size_t n = std::strlen(name);
    return n > 4 && lower(name[n - 4]) == '.' && lower(name[n - 3]) == 't' && lower(name[n - 2]) == 'x' &&
           lower(name[n - 1]) == 't';
}

bool lessNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (lower(*a) != lower(*b)) return lower(*a) < lower(*b);
    return *a == 0 && *b != 0;
}

struct Names {
    int n = 0;
    char name[kMaxGroups][kGroupMax + 5];
};

}  // namespace

int defaultCount() { return int(sizeof(kDefaults) / sizeof(kDefaults[0])); }
const char* defaultName(int i) { return i >= 0 && i < defaultCount() ? kDefaults[i].name : ""; }
const char* defaultText(int i) { return i >= 0 && i < defaultCount() ? kDefaults[i].body : ""; }

bool writeDefaults(IStorage& fs) {
    if (!fs.mkdir(kDir)) return false;
    char path[64];
    char* text = textScratch();
    for (int i = 0; i < defaultCount(); ++i) {
        std::snprintf(path, sizeof(path), "%s/%s.txt", kDir, kDefaults[i].name);
        if (fs.exists(path)) continue;   // never over a file of the user's
        const int n = std::snprintf(text, kTextScratchBytes, "%s%s", kHeader, kDefaults[i].body);
        if (n <= 0 || size_t(n) >= kTextScratchBytes) return false;
        if (writeWholeFile(fs, path, reinterpret_cast<const uint8_t*>(text), uint32_t(n)) != FmtStatus::Ok) return false;
    }
    return true;
}

int addText(Library& lib, const char* group, const char* text, size_t len) {
    if (lib.groups >= kMaxGroups) return 0;
    const int g = lib.groups;
    int skipped = 0, added = 0;
    char line[160];
    for (size_t i = 0; i < len;) {
        size_t j = i;
        while (j < len && text[j] != '\n') ++j;
        size_t n = j - i;
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        std::memcpy(line, text + i, n);
        line[n] = 0;
        i = j + 1;
        const char* s = line;
        while (isSpace(*s)) ++s;
        if (!*s || (*s == '#' && !(s[1] == 'I' || s[1] == 'V' || s[1] == 'i' || s[1] == 'v'))) continue;
        Progression p;
        if (!parse(s, p) || std::strlen(s) > size_t(kLineMax) || lib.items >= kMaxItems) {
            ++skipped;
            continue;
        }
        std::memcpy(lib.line[lib.items], s, std::strlen(s) + 1);   // fits: checked above
        lib.itemGroup[lib.items++] = uint8_t(g);
        ++added;
    }
    if (added) {
        std::snprintf(lib.group[g], sizeof(lib.group[0]), "%s", group);
        lib.groups = g + 1;
    }
    return skipped;
}

bool load(IStorage& fs, Library& lib) {
    lib.reset();
    Names names;
    auto list = [&]() {
        names.n = 0;
        fs.listDir(kDir, [](const IStorage::DirEntry& e, void* c) {
            auto* nm = static_cast<Names*>(c);
            if (e.isDir || e.name[0] == '.' || !hasTxt(e.name) || nm->n >= kMaxGroups) return true;
            if (std::strlen(e.name) - 4 > size_t(kGroupMax)) return true;
            std::snprintf(nm->name[nm->n++], sizeof(nm->name[0]), "%s", e.name);
            return true;
        }, &names);
    };
    list();
    if (names.n == 0) {
        if (!writeDefaults(fs)) return false;
        list();
    }
    for (int i = 1; i < names.n; ++i)
        for (int j = i; j > 0 && lessNoCase(names.name[j], names.name[j - 1]); --j) {
            char t[sizeof(names.name[0])];
            std::memcpy(t, names.name[j], sizeof(t));
            std::memcpy(names.name[j], names.name[j - 1], sizeof(t));
            std::memcpy(names.name[j - 1], t, sizeof(t));
        }
    char* buf = textScratch();   // one file at a time
    for (int i = 0; i < names.n; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "%s/%s", kDir, names.name[i]);
        uint32_t got = 0;
        if (readWholeFile(fs, path, reinterpret_cast<uint8_t*>(buf), kTextScratchBytes - 1, &got) != FmtStatus::Ok) {
            ++lib.skipped;
            continue;
        }
        buf[got] = 0;
        char group[kGroupMax + 1];
        std::snprintf(group, sizeof(group), "%.*s", int(std::strlen(names.name[i]) - 4), names.name[i]);
        lib.skipped += addText(lib, group, buf, got);
    }
    return lib.items > 0;
}

}  // namespace prog
}  // namespace ot
