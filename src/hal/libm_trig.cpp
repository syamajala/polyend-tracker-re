// [I] sinf / cosf / tanf on the device: fm::sinF / cosF / tanF (src/core/fmath.h)
// replace newlib's. Theirs reduce huge arguments exactly (__kernel_rem_pio2f and
// its 2/pi table, ~4.4 KB of flash with the kernels); no caller comes near
// (the largest argument is ~640, buildClick), and ours are within 2 ulp (tan: 4)
// of glibc below 2^17. A strong definition here keeps libm.a's members out of
// the link (the Makefile checks it).
#include "fmath.h"

extern "C" {
float sinf(float x) { return ot::fm::sinF(x); }
float cosf(float x) { return ot::fm::cosF(x); }
float tanf(float x) { return ot::fm::tanF(x); }
}
