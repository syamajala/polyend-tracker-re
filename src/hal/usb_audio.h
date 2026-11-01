// [I] USB audio on the device (#97): the K66 side of the USB audio function
// (third_party/teensy3 usb_desc.h, OT_USB_AUDIO=1; interfaces 4..6, iso IN 8,
// iso OUT 9, feedback 10). usb_dev.c calls this driver's C hooks from the
// USB interrupt; the stream goes through core/usb/usb_audio_in.h.
//
// Computer -> Tracker: usbAudioIn() is HalRefs::usbIn, called once per audio
// block; it also refreshes the feedback value the computer paces by.
// Tracker -> computer (#97 step 2): usbAudioOutBuffer() / usbAudioOutCommit()
// are HalRefs::usbSend, called by the engine once per live block with the
// limiter output; the IN endpoint sends it through core/usb/usb_audio_out.h.
// OT_USB_AUDIO=0: usbAudioIn() and usbAudioOutBuffer() return nullptr (no USB
// audio in that image).
#pragma once

#include <cstdint>

namespace ot {
namespace hal {

// The computer's stream for the next block (128 interleaved frames) or
// nullptr. Audio-block context only.
const int16_t* usbAudioIn(void* ctx);
// Where to write this block's send (128 interleaved frames), or nullptr
// (the computer isn't recording); usbAudioOutCommit() once written.
// Audio-block context only.
int16_t* usbAudioOutBuffer(void* ctx);
void usbAudioOutCommit(void* ctx);
// True in an image with the USB audio function.
bool usbAudioAvailable();

// Diagnostics: packets received, under-runs, frames dropped, ring fill and
// the feedback value (10.14).
struct UsbAudioStats {
    uint32_t packets, underruns, dropped, fill, feedback;
    bool playing, streaming;
};
UsbAudioStats usbAudioStats();

// Tracker -> computer: packets sent, under-runs, blocks dropped, ring fill
// and the packet step (10.14 frames per ms).
struct UsbAudioOutStats {
    uint32_t packets, underruns, overflows, fill, step;
    bool playing;
};
UsbAudioOutStats usbAudioOutStats();

}  // namespace hal
}  // namespace ot
