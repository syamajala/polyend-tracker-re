// [I] usb_audio.bin: a project's USB audio settings (#97; Project::usbLevel,
// usbMute, recUsb), next to project.mt. Stock firmware and the Tracker Mini
// never read it, so project.mt stays byte-compatible: there USB audio
// doesn't exist and the Sample Recorder's source reads Line in.
//
// Layout: "OTUA", version, field count, 0, 0, the fields, then their 8-bit
// sum. A reader takes the fields it knows; later versions append fields.
//   field 0: Global Mixer USB level (TRK index 0..100)
//   field 1: Global Mixer USB mute (0 / 1)
//   field 2: the Sample Recorder's source is USB (0 / 1)
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/project.h"

namespace ot {

namespace usbside {
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHeaderSize = 8;
constexpr int kFields = 3;
constexpr uint32_t kFileSize = kHeaderSize + kFields + 1;
constexpr const char* kFileName = "usb_audio.bin";
}  // namespace usbside

// True when a setting differs from the new-project default (only then is a
// file written).
bool usbAudioSideUsed(const Project& p);
void encodeUsbAudioSide(const Project& p, uint8_t out[usbside::kFileSize]);
// Bad magic / size / checksum: the defaults, BadMagic or TooShort.
FmtStatus decodeUsbAudioSide(const uint8_t* file, size_t len, Project& p);

}  // namespace ot
