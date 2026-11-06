// [I] printf float cases inside float_format.h's exact range, shared by
// tests/float_format_test.cpp (fm::formatFloat against glibc) and
// tests/bench_m4/printf_float_check.cpp (the device's newlib-nano snprintf with
// our _printf_float hook, diffed against glibc by `make -C tests printf-m4`).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ottest {

struct FloatCase {
    char flags[6];   // any of "-+ #0"
    int width;       // 0: none
    int prec;        // -1: none, else 0..9
    char conv;       // f F e E g G
    double v;

    // The printf format, e.g. "%-+8.3g". asF: the %f glibc prints for it (the
    // device prints %e / %g as %f).
    void format(char* out, size_t cap, bool asF = false) const {
        char w[16] = "", p[16] = "";
        if (width) std::snprintf(w, sizeof w, "%d", width);
        if (prec >= 0) std::snprintf(p, sizeof p, ".%d", prec);
        const char c = !asF ? conv : conv >= 'a' ? 'f' : 'F';
        std::snprintf(out, cap, "%%%s%s%s%c", flags, w, p, c);
    }
};

// Calls f(const FloatCase&) for n cases (the same sequence on every host).
template <class F>
void forEachFloatCase(int n, F&& f) {
    static const double kFixed[] = {0.0, -0.0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.375, -0.04, 0.0005, 0.0015,
                                    1e-300, 4.9e-324, 99.995, 120.0, 44.1, -40.0, 9.5, 99.5, 999999.5,
                                    9.9999995, 0.000099995, 123456789.0, 999999999.4, 0.1, 1.0 / 3.0,
                                    6000.0, 0.15, 1e-5, 2.675, 1.005, 0.0000000005, 1.0 / 0.0, -1.0 / 0.0};
    static const char kConv[] = "ffffFFeEgG";
    static const char* const kFlags[] = {"", "", "", "-", "+", " ", "#", "0", "-+", "0+", "# ", "-#"};
    uint64_t x = 0x9e3779b97f4a7c15ull;
    auto next = [&x] {
        x ^= x << 13, x ^= x >> 7, x ^= x << 17;
        return x;
    };
    const int nFixed = int(sizeof kFixed / sizeof kFixed[0]);
    for (int i = 0; i < n; ++i) {
        FloatCase c{};
        const uint64_t r = next();
        c.conv = kConv[r % 10];
        std::strcpy(c.flags, kFlags[(r >> 8) % 12]);
        c.width = (r >> 16) % 4 == 0 ? int((r >> 20) % 24) : 0;
        c.prec = (r >> 28) % 6 == 0 ? -1 : int((r >> 32) % 10);
        const uint64_t s = next();
        double v;
        if (i < nFixed * 8) {
            v = kFixed[i % nFixed];
        } else if (s % 4 == 0) {
            v = double(int64_t(s >> 40) - (int64_t(1) << 23)) / 1000.0;   // UI-style decimals
        } else if (s % 4 == 1) {
            v = double(float(double(int64_t(s >> 40)) * 0.05));            // float parameters
        } else {
            // Any 53-bit mantissa, |v| in [2^-40, 2^29] (below 1e9).
            const int e = -40 + int((s >> 8) % 70);
            v = double(int64_t(next() >> 11)) * __builtin_ldexp(1.0, e - 53);
            if (s & 2) v = -v;
        }
        c.v = v;
        f(c);
    }
}

}  // namespace ottest
