// [I] printf %f / %e / %g on the device. newlib-nano's printf hands every float
// conversion to _printf_float, which libc_nano.a defines weak (its own version
// needs dtoa + mprec, ~12 KB, linked only with -u _printf_float). This strong
// definition replaces it with fm::formatFloat (src/core/float_format.h), so
// "%.2f" prints the number for our code and for modules alike (%e / %g print
// as %f, see float_format.h).
//
// The arguments follow newlib's private nano-vfprintf_local.h; only the leading
// fields of its per-conversion state are read. tests/bench_m4/printf_float_check.cpp runs
// the toolchain's own snprintf with this hook under QEMU and diffs it with
// glibc (`make -C tests printf-m4`), so a newlib that moved them fails there.
#include <cstdarg>
#include <cstdio>

#include "float_format.h"

// newlib-nano's state for one conversion (nano-vfprintf_local.h); the fields
// after `code` are not used here.
struct _prt_data_t {
    int flags;
    int prec;    // -1: none
    int dprec;
    int width;   // >= 0; a '-' or negative '*' width sets kLeft
    int size;
    int ret;
    char code;   // e f g E F G
};

namespace {

// nano-vfprintf_local.h flag bits: "#-0+ " in order.
constexpr int kAlt = 0x01, kLeft = 0x02, kZero = 0x04, kPlus = 0x08, kSpace = 0x10;

struct Sink {
    struct _reent* data;
    FILE* fp;
    int (*pfunc)(struct _reent*, FILE*, const char*, size_t);
};

bool put(void* ctx, const char* s, int n) {
    Sink* k = static_cast<Sink*>(ctx);
    return k->pfunc(k->data, k->fp, s, size_t(n)) != EOF;
}

}  // namespace

extern "C" int _printf_float(struct _reent* data, _prt_data_t* pd, FILE* fp,
                             int (*pfunc)(struct _reent*, FILE*, const char*, size_t), va_list* ap) {
    // long double is double on ARM EABI, so the LONGDBL flag reads the same.
    const double v = va_arg(*ap, double);
    ot::fm::FloatSpec s;
    s.conv = pd->code;
    s.prec = pd->prec;
    s.width = pd->width;
    s.alt = pd->flags & kAlt;
    s.left = pd->flags & kLeft;
    s.zero = pd->flags & kZero;
    s.plus = pd->flags & kPlus;
    s.space = pd->flags & kSpace;
    Sink k{data, fp, pfunc};
    return ot::fm::formatFloat(v, s, put, &k);
}
