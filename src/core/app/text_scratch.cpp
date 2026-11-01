#include "app/text_scratch.h"

#include "formats/midi_preset.h"
#include "formats/perf_bank.h"
#include "sdram_bss.h"

namespace ot {
static_assert(mpreset::kTextMax <= kTextScratchBytes && pbank::kTextMax <= kTextScratchBytes,
              "the text files fit the scratch");
namespace {
OT_SDRAM_BSS char g_text[kTextScratchBytes];
}
char* textScratch() { return g_text; }
}  // namespace ot
