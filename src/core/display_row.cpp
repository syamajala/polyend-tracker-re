#include "display_row.h"

#include "sdram_bss.h"

namespace ot {
namespace {
OT_SDRAM_BSS uint32_t g_row[kDisplayRowPixels];
}
uint32_t* displayRow() { return g_row; }
}  // namespace ot
