// Synth help files (modules/synth_help.h): a line scanner over the Markdown
// subset. No allocation; the caller owns the text and the line buffer.
#include "modules/synth_help.h"

#include <cstring>

namespace ot {
namespace synthhelp {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// The current entry while its device line is collected.
struct Scan {
    Visit visit;
    void* ctx;
    char* buf;
    int cap;
    int n = 0;              // bytes in buf
    bool open = false;      // an entry is pending
    bool collecting = false;
    Entry e{};

    void append(const char* s, int len) {
        while (len > 0 && isSpace(*s)) ++s, --len;
        while (len > 0 && isSpace(s[len - 1])) --len;
        if (len == 0) return;
        if (n > 0 && n < cap - 1) buf[n++] = ' ';
        const int k = len < cap - 1 - n ? len : cap - 1 - n;
        if (k > 0) std::memcpy(buf + n, s, size_t(k)), n += k;
    }
    void flush() {
        if (!open) return;
        buf[n] = '\0';
        e.line = buf;
        e.lineLen = n;
        visit(ctx, e);
        open = collecting = false;
        n = 0;
    }
    void start(Level level, const char* name, int nameLen, int lineNo) {
        flush();
        while (nameLen > 0 && isSpace(name[nameLen - 1])) --nameLen;
        e = Entry{level, name, nameLen, buf, 0, lineNo};
        open = collecting = true;
    }
};

// "- " at column 0: a choice item.
bool isItem(const char* s, int len) { return len >= 2 && s[0] == '-' && s[1] == ' '; }
bool isFence(const char* s, int len) { return len >= 3 && std::strncmp(s, "```", 3) == 0; }
// A block that is not a paragraph (quote, table, image, HTML, other lists).
bool isOtherBlock(const char* s, int len) {
    if (len == 0) return false;
    const char c = s[0];
    if (c == '>' || c == '|' || c == '!' || c == '<' || isSpace(c)) return true;
    if ((c == '*' || c == '+') && len >= 2 && s[1] == ' ') return true;
    int i = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') ++i;
    return i > 0 && i + 1 < len && (s[i] == '.' || s[i] == ')') && s[i + 1] == ' ';
}

bool same(const char* a, int aLen, const char* b) {
    return int(std::strlen(b)) == aLen && std::strncmp(a, b, size_t(aLen)) == 0;
}

}  // namespace

void walk(const char* text, uint32_t len, char* buf, int cap, Visit visit, void* ctx) {
    if (!buf || cap < 1) return;
    Scan sc;
    sc.visit = visit;
    sc.ctx = ctx;
    sc.buf = buf;
    sc.cap = cap;
    bool fence = false, docsOnly = false;
    int lineNo = 0;
    for (uint32_t p = 0; p < len;) {
        const char* s = text + p;
        uint32_t q = p;
        while (q < len && text[q] != '\n') ++q;
        int n = int(q - p);
        p = q + 1;
        ++lineNo;
        if (n > 0 && s[n - 1] == '\r') --n;

        if (fence) {
            if (isFence(s, n)) fence = false;
            continue;
        }
        if (isFence(s, n)) {
            sc.flush();
            fence = true;
            continue;
        }
        int h = 0;
        while (h < n && s[h] == '#') ++h;
        if (h > 0 && h < n && s[h] == ' ') {
            sc.flush();
            docsOnly = h > kSetting;
            if (!docsOnly) sc.start(Level(h), s + h + 1, n - h - 1, lineNo);
            continue;
        }
        if (docsOnly) continue;
        int b = 0;
        while (b < n && isSpace(s[b])) ++b;
        if (b == n) {   // blank: ends a started line
            if (sc.collecting && sc.n > 0) sc.flush();
            continue;
        }
        if (isItem(s, n)) {
            sc.start(kChoice, s, 0, lineNo);
            sc.e.nameLen = 0;
            sc.append(s + 2, n - 2);
            continue;
        }
        if (!sc.collecting) continue;   // a later paragraph: docs only
        if (sc.n == 0 && isOtherBlock(s, n)) {
            sc.collecting = false;   // the first block is not a paragraph: no device line
            continue;
        }
        sc.append(s, n);
    }
    sc.flush();
}

int splitChoice(const char* item, int len) {
    for (int i = 0; i + 1 < len; ++i)
        if (item[i] == ':' && item[i + 1] == ' ') return i;
    return -1;
}

namespace {

struct Find {
    const char* section;
    const char* setting;
    const char* choice;
    char* out;
    int cap;
    int result = 0;
    bool done = false;
    bool inSection = false, inSetting = false;

    void copy(const char* s, int len) {
        while (len > 0 && isSpace(*s)) ++s, --len;
        const int k = len < cap - 1 ? len : cap - 1;
        std::memmove(out, s, size_t(k));
        out[k] = '\0';
        result = k;
        done = true;
    }
};

void findVisit(void* ctx, const Entry& e) {
    Find& f = *static_cast<Find*>(ctx);
    if (f.done) return;
    switch (e.level) {
        case kEngine:
            f.inSection = f.inSetting = false;
            break;
        case kSection:
            f.inSection = f.section && same(e.name, e.nameLen, f.section);
            f.inSetting = false;
            if (f.inSection && !f.setting) f.copy(e.line, e.lineLen);
            break;
        case kSetting:
            f.inSetting = f.inSection && f.setting && same(e.name, e.nameLen, f.setting);
            if (f.inSetting && !f.choice) f.copy(e.line, e.lineLen);
            break;
        case kChoice:
            if (!f.inSetting || !f.choice) break;
            if (!*f.choice) {
                f.copy(e.line, e.lineLen);
            } else {
                const int k = splitChoice(e.line, e.lineLen);
                if (k >= 0 && same(e.line, k, f.choice)) f.copy(e.line + k + 2, e.lineLen - k - 2);
            }
            break;
    }
}

}  // namespace

int find(const char* text, uint32_t len, const char* section, const char* setting, const char* choice,
         char* out, int cap) {
    if (!out || cap < 1) return 0;
    out[0] = '\0';
    Find f{section, setting, choice, out, cap};
    char line[kMaxLine];
    walk(text, len, line, kMaxLine, findVisit, &f);
    return f.result;
}

}  // namespace synthhelp
}  // namespace ot
