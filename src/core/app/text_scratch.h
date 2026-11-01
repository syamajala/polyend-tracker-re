// [I] One SDRAM buffer for the small text files the UI reads or writes
// whole within one call: #13 MIDI presets, #70 scene banks.
#pragma once

#include <cstddef>

namespace ot {
constexpr size_t kTextScratchBytes = 3072;
char* textScratch();
}  // namespace ot
