// [I] #3 Multisample zone selection and layout (model/multisample.h).
#include "model/multisample.h"

#include "sdram_bss.h"

namespace ot {

OT_SDRAM_BSS MultiMap gMultiMaps[kSampleInstrumentCount];

namespace ms {

int uniqueByRoot(Candidate* c, int n) {
    for (int i = 1; i < n; ++i) {   // insertion sort, stable: the first per root stays first
        const Candidate x = c[i];
        int j = i - 1;
        while (j >= 0 && c[j].root > x.root) {
            c[j + 1] = c[j];
            --j;
        }
        c[j + 1] = x;
    }
    int m = 0;
    for (int i = 0; i < n; ++i)
        if (m == 0 || c[i].root != c[m - 1].root) c[m++] = c[i];
    return m;
}

namespace {
int nearest(const Candidate* c, int n, int note) {
    int best = 0;
    for (int i = 1; i < n; ++i) {
        const int d = c[i].root > note ? c[i].root - note : note - c[i].root;
        const int bd = c[best].root > note ? c[best].root - note : note - c[best].root;
        if (d < bd) best = i;   // equal distance keeps the lower root (sorted)
    }
    return best;
}
}  // namespace

void layout(Plan& p) {
    uint64_t sum = 0;
    for (int i = 0; i < p.count; ++i) sum += p.frames[i];
    // Room for the padding: each zone start moves up by less than
    // len / 65535 + 1 frames; grow until that estimate holds.
    uint64_t len = sum + 1;
    for (int k = 0; k < 4; ++k) len = sum + uint64_t(p.count) * (len / 65535 + 2) + 1;
    uint64_t cur = 0;
    for (int i = 0; i < p.count; ++i) {
        // Smallest point whose frame (len * pt / 65535, floored) is >= cur.
        uint64_t pt = (cur * 65535 + len - 1) / len;
        if (pt > 65535) pt = 65535;
        p.point[i] = uint16_t(pt);
        p.start[i] = uint32_t(len * pt / 65535);
        cur = p.start[i] + uint64_t(p.frames[i]);
    }
    p.totalFrames = uint32_t(len > cur ? len : cur);
}

void plan(const Candidate* c, int n, const Options& o, Plan& p) {
    p = Plan{};
    if (n <= 0) return;
    const uint32_t cap = kLengthMs[o.length % kLengthCount] ? uint32_t(kLengthMs[o.length % kLengthCount]) * 441u / 10u : 0u;
    const int step = kEvery[o.every % kEveryCount];
    int last = -1;
    for (int t = o.lo; t <= o.hi; t += step) {
        const int k = nearest(c, n, t);
        if (k == last) continue;
        if (p.count == kMultiMaxZones) {
            p.tooMany = true;
            break;
        }
        last = k;
        p.cand[p.count] = uint8_t(k);
        p.root[p.count] = c[k].root;
        p.frames[p.count] = cap && c[k].frames > cap ? cap : c[k].frames;
        if (p.frames[p.count] == 0) p.frames[p.count] = 1;
        ++p.count;
    }
    layout(p);
}

bool defaults(const Candidate* c, int n, uint32_t budgetFrames, Options& o) {
    if (n <= 0) return false;
    int lo = c[0].root < 36 ? 36 : c[0].root;
    int hi = c[n - 1].root > 84 ? 84 : c[n - 1].root;
    if (lo > hi) {   // the pack lies outside C2..C6: its own lowest 4 octaves
        lo = c[0].root;
        hi = c[n - 1].root < lo + 48 ? c[n - 1].root : lo + 48;
    }
    o.lo = uint8_t(lo);
    o.hi = uint8_t(hi);
    Plan p;
    static const uint8_t kTryEvery[3] = {2, 4, 5};   // 3rd, 6th, 12th note
    for (uint8_t e : kTryEvery) {
        o.every = e;
        for (uint8_t l = 0; l < kLengthCount; ++l) {
            o.length = l;
            plan(c, n, o, p);
            if (!p.tooMany && p.totalFrames <= budgetFrames) return true;
        }
    }
    o.every = 2;
    o.length = 0;
    return false;
}

}  // namespace ms
}  // namespace ot
