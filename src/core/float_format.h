// [I] printf's %f without newlib's float printf (dtoa + mprec, ~12 KB of
// flash, #122). The firmware plugs this into newlib-nano's weak _printf_float
// hook (src/hal/printf_float.cpp), so snprintf("%.2f") works on the device as
// in the simulator, for our code and for modules alike.
//
// %f / %F print the exact binary value rounded half to even, as glibc and
// newlib do, for |v| < 1e9 with up to 9 decimals (tests/float_format_test.cpp);
// further decimals print as zeros. %e / %g (unused in the firmware) print as
// %f with the same precision.
#pragma once

namespace ot {
namespace fm {

struct FloatSpec {
    char conv = 'f';     // f F (e E g G print as f F)
    int prec = -1;       // -1: the default 6
    int width = 0;
    bool left = false;   // '-'
    bool zero = false;   // '0'
    bool plus = false;   // '+'
    bool space = false;  // ' '
    bool alt = false;    // '#'
};

// Writes the conversion through put (false = stop). Returns the number of
// characters, or -1 when put failed.
using FloatPut = bool (*)(void* ctx, const char* s, int n);
int formatFloat(double v, const FloatSpec& spec, FloatPut put, void* ctx);

}  // namespace fm
}  // namespace ot
