// [I] The device's printf float conversions end to end: snprintf from the
// toolchain's newlib-nano with our _printf_float hook (src/hal/printf_float.cpp)
// under QEMU, against the same program built for the host with glibc.
// `make -C tests printf-m4` diffs the two outputs; any difference fails, for
// example a newlib whose private conversion state moved.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "float_format_cases.h"

#ifdef __arm__
constexpr bool kDevice = true;
#else
constexpr bool kDevice = false;   // glibc: %e / %g as the %f the device prints
#endif

int main() {
    int n = 0;
    ottest::forEachFloatCase(20000, [&n](const ottest::FloatCase& c) {
        char fmt[48], out[512];
        c.format(fmt, sizeof fmt, !kDevice);
        const int len = std::snprintf(out, sizeof out, fmt, c.v);
        uint64_t bits;
        std::memcpy(&bits, &c.v, sizeof bits);
        // Integer conversions only: the value as bits, newlib-nano has no %a.
        std::printf("%d %c %08lx%08lx %d [%s]\n", n++, c.conv, (unsigned long)(bits >> 32), (unsigned long)(bits & 0xffffffffu),
                    len, out);
    });
    // Through the stream path (vfprintf) too, not only the string one.
    std::printf("[%8.3f|%-+10.2f|%#.0f]\n", 3.14159, -2.5e-3, 100.0);
    return 0;
}
