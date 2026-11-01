// [I] One row of display readback (0x00RRGGBB words), shared by the BMP
// screenshot (App::screenshot) and the remote screenshot stream
// (remote::DeviceBridge::serviceShot). Each reads a row into it and is done
// with it before returning, so they never hold it at the same time. Main
// loop only; SDRAM on the device.
#pragma once

#include <cstdint>

namespace ot {
constexpr int kDisplayRowPixels = 800;
uint32_t* displayRow();
}  // namespace ot
