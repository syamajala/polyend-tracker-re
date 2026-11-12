// A module's settings page (docs/PLAN_3X_M2.md M-C): shown in the Master key
// cycle after the Global Mixer. Up to 8 rows, one above each F-key: F1-F8
// select a row, the encoder and Up / Down change it. Module-facing: no
// drawing code; the core draws value bars and the bottom-bar labels.
#pragma once

#include <cstdint>
#include <cstring>

// [I] fork #159: cores whose IModulePage has the perf* calls and PerfRows
// (modules build against cores with and without them).
#define OT_MODULE_PERF_ROWS 1

namespace ot {
namespace mod {

struct ModuleRow {
    char label[16];    // bottom-bar label ("Drive")
    char value[16];    // bottom-bar value ("30", "On")
    float frac;        // bar position 0..1 (bipolar: -1..1)
    bool bipolar;
};

class IModulePage {
public:
    virtual const char* title() const = 0;   // "Tape" (title bar: "Master Tape")
    virtual int rows() const = 0;            // 1..8
    virtual void row(int i, ModuleRow& out) = 0;
    virtual void adjust(int i, int detents) = 0;   // main loop; mark the module's data dirty
    // [I] Performance FX columns (fork #159): a column plays row i (1 ..
    // rows() - 1; row 0 is the on row) at a value lo .. hi of perfRange
    // while its pad or scene does. The effect sounds while any row plays;
    // the played value is never saved (row() and the project data keep the
    // page's settings). Main loop. A page without them isn't playable.
    virtual bool perfRange(int i, int& lo, int& hi) const {   // false: row i can't be played
        (void)i;
        lo = hi = 0;
        return false;
    }
    virtual void perfText(int i, int v, char* out, int cap) {   // v as row() would show it
        (void)i;
        if (cap > 0) out[0] = 0;
        (void)v;
    }
    virtual void perfPlay(int i, int v) {   // v < 0: row i is released
        (void)i;
        (void)v;
    }
};

// [I] fork #159: the rows a page's Performance columns play (value + 1 per
// row, 0 = not played), for the pages' perfPlay.
template <int N>
struct PerfRows {
    uint8_t v[N] = {};
    bool any() const {
        for (uint8_t x : v)
            if (x) return true;
        return false;
    }
    // Row i plays val (< 0: released), clamped to lo .. hi; false = unchanged.
    bool set(int i, int val, int lo, int hi) {
        if (i < 1 || i >= N) return false;
        const uint8_t n = val < 0 ? 0 : uint8_t((val < lo ? lo : (val > hi ? hi : val)) + 1);
        if (v[i] == n) return false;
        v[i] = n;
        return true;
    }
    // Field f of row i takes the played value.
    void over(int i, uint8_t& f) const {
        if (i >= 1 && i < N && v[i]) f = uint8_t(v[i] - 1);
    }
};

// [I] An effect's page starts with its on / mode row: value "Off" = off
// ("On" / "Off"; BBD "I" / "II" / "Off"), the first step up turns it on
// (the FX bank, fork #21).
inline bool pageOn(IModulePage* p) {
    if (!p) return false;
    ModuleRow r{};
    p->row(0, r);
    return std::strcmp(r.value, "Off") != 0;
}
inline void pageTurn(IModulePage* p, bool on) {
    if (p && pageOn(p) != on) p->adjust(0, on ? 1 : -100);
}

}  // namespace mod
}  // namespace ot
