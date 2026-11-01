#include "formats/usb_audio_side.h"

#include <cstring>

namespace ot {

bool usbAudioSideUsed(const Project& p) {
    return p.usbLevel != Project::kUsbLevelDefault || p.usbMute != 1 || p.recUsb != 0;
}

void encodeUsbAudioSide(const Project& p, uint8_t out[usbside::kFileSize]) {
    std::memset(out, 0, usbside::kFileSize);
    std::memcpy(out, "OTUA", 4);
    out[4] = usbside::kVersion;
    out[5] = uint8_t(usbside::kFields);
    uint8_t* f = out + usbside::kHeaderSize;
    f[0] = p.usbLevel;
    f[1] = p.usbMute ? 1 : 0;
    f[2] = p.recUsb ? 1 : 0;
    uint8_t sum = 0;
    for (int k = 0; k < usbside::kFields; ++k) sum = uint8_t(sum + f[k]);
    f[usbside::kFields] = sum;
}

FmtStatus decodeUsbAudioSide(const uint8_t* file, size_t len, Project& p) {
    p.resetUsbAudio();
    if (len < usbside::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTUA", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t fields = file[5];
    if (len < usbside::kHeaderSize + fields + 1) return FmtStatus::TooShort;
    const uint8_t* f = file + usbside::kHeaderSize;
    uint8_t sum = 0;
    for (uint32_t k = 0; k < fields; ++k) sum = uint8_t(sum + f[k]);
    if (sum != f[fields]) return FmtStatus::BadMagic;
    if (fields > 0) p.usbLevel = f[0] <= 100 ? f[0] : Project::kUsbLevelDefault;
    if (fields > 1) p.usbMute = f[1] ? 1 : 0;
    if (fields > 2) p.recUsb = f[2] ? 1 : 0;
    return FmtStatus::Ok;
}

}  // namespace ot
