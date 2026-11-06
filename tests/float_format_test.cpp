// [I] fm::formatFloat (src/core/float_format.h), the device's printf %f,
// against glibc's correctly rounded printf: flags, width and precision, ties
// (0.125, 2.5, 2.675) and carries (9.9999995). %e / %g print as %f.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "test.h"

#include "float_format.h"
#include "float_format_cases.h"

using namespace ot;

namespace {

bool append(void* ctx, const char* s, int n) {
    static_cast<std::string*>(ctx)->append(s, size_t(n));
    return true;
}

fm::FloatSpec specOf(const ottest::FloatCase& c) {
    fm::FloatSpec s;
    s.conv = c.conv;
    s.prec = c.prec;
    s.width = c.width;
    s.left = std::strchr(c.flags, '-');
    s.zero = std::strchr(c.flags, '0');
    s.plus = std::strchr(c.flags, '+');
    s.space = std::strchr(c.flags, ' ');
    s.alt = std::strchr(c.flags, '#');
    return s;
}

std::string ours(double v, const fm::FloatSpec& s) {
    std::string out;
    const int n = fm::formatFloat(v, s, append, &out);
    CHECK(n == int(out.size()));
    return out;
}

std::string ours(double v, int prec, char conv = 'f') {
    fm::FloatSpec s;
    s.conv = conv;
    s.prec = prec;
    return ours(v, s);
}

std::string glibc(const char* fmt, double v) {
    char ref[512];
    std::snprintf(ref, sizeof ref, fmt, v);
    return ref;
}

}  // namespace

TEST(float_format_matches_glibc) {
    int bad = 0;
    ottest::forEachFloatCase(1000000, [&](const ottest::FloatCase& c) {
        char fmt[48], ref[48];
        c.format(fmt, sizeof fmt);
        c.format(ref, sizeof ref, true);
        const std::string got = ours(c.v, specOf(c)), want = glibc(ref, c.v);
        if (got != want && ++bad <= 20) std::printf("  %s of %a: %s, glibc %s\n", fmt, c.v, got.c_str(), want.c_str());
    });
    CHECK(bad == 0);
}

TEST(float_format_examples) {
    CHECK(ours(double(0.1f + 0.049f * 20.0f), 2) == "1.08");   // Chorus Rate, a float parameter
    CHECK(ours(6.0, 1) == "6.0");
    CHECK(ours(2.5, 0) == "2");
    CHECK(ours(3.5, 0) == "4");
    CHECK(ours(2.675, 2) == "2.67");   // stored just below the tie
    CHECK(ours(-0.04, 1) == "-0.0");
    CHECK(ours(9.9999996, 6) == "10.000000");
    CHECK(ours(0.00001, -1, 'g') == "0.000010");   // %g prints as %f
    CHECK(ours(1.0 / 0.0, 2, 'F') == "INF");
    CHECK(ours(-std::nan(""), 2) == "-nan");
    fm::FloatSpec s;
    s.zero = true;
    s.width = 8;
    s.prec = 2;
    CHECK(ours(-3.14159, s) == "-0003.14");
    s.zero = false;
    s.left = true;
    CHECK(ours(-3.14159, s) == "-3.14   ");
}

// Past 9 decimals the digits are zeros; past 1e9 the last decimals may be.
TEST(float_format_beyond_the_exact_range) {
    CHECK(ours(0.1, 12) == glibc("%.9f", 0.1) + "000");
    CHECK(ours(1.0 / 3.0, 15) == glibc("%.9f", 1.0 / 3.0) + "000000");
    CHECK(ours(4294967296.25, 2) == "4294967296.25");   // still fits 64 bits
    CHECK(ours(1e15 + 0.5, 3) == glibc("%.3f", 1e15 + 0.5));
    CHECK(ours(1e300, 0).size() == glibc("%.0f", 1e300).size());
    CHECK(ours(1e300, 0).compare(0, 15, glibc("%.0f", 1e300), 0, 15) == 0);
    CHECK(ours(9223372036854775808.0, 1) == "9223372036854775808.0");   // 2^63, exact
    CHECK(ours(18446744073709551616.0, 0).compare(0, 15, "184467440737095") == 0);   // 2^64
    CHECK(ours(4.9e-324, 3) == "0.000");
}

TEST(float_format_stops_when_put_fails) {
    auto fail = [](void*, const char*, int) { return false; };
    fm::FloatSpec s;
    CHECK(fm::formatFloat(1.5, s, fail, nullptr) == -1);
}
