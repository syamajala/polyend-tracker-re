// [I] USB audio on the device (#97). See usb_audio.h.
#include "usb_audio.h"

#include <cstring>

#include "dma_mem.h"
#include "usb_desc.h"
#include "usb/usb_audio_in.h"
#include "usb/usb_audio_out.h"

#if defined(OT_USB_AUDIO_LAST)

namespace {
// SRAM .bss, zero-initialised (no constructor): the USB interrupt and the
// audio block both touch it (sdram_bss.h rules).
ot::usbaudio::UsbAudioIn g_in;
ot::usbaudio::UsbAudioOut g_out;
int16_t g_block[2 * ot::usbaudio::kBlockFrames];
}  // namespace

// The hooks usb_dev.c calls (declared in third_party/teensy3/usb_dev.h; the
// Teensy implementation, usb_audio.cpp, is not built). Buffers the USB DMA
// reads or writes live in `.dma` (SRAM_U, uncached, zeroed at boot).
extern "C" {
OT_DMA_MEM uint16_t usb_audio_receive_buffer[AUDIO_RX_SIZE / 2];
OT_DMA_MEM uint16_t usb_audio_transmit_buffer[AUDIO_TX_SIZE / 2];
OT_DMA_MEM uint32_t usb_audio_sync_feedback;   // 10.14 frames per ms, read by the USB DMA (3 bytes)
uint8_t usb_audio_receive_setting;
uint8_t usb_audio_transmit_setting;

// USB interrupt: an isochronous OUT packet arrived (len bytes, 4 per frame).
void usb_audio_receive_callback(unsigned int len) {
    if (!usb_audio_receive_setting) return;
    g_in.receive(reinterpret_cast<const int16_t*>(usb_audio_receive_buffer), len / 4);
}

// USB interrupt: the next IN packet (#97 step 2), 43..45 frames at the
// codec's pace (core/usb/usb_audio_out.h).
unsigned int usb_audio_transmit_callback(void) {
    static_assert(AUDIO_TX_SIZE / 4 == ot::usbaudio::kOutMaxPacket, "IN packet size");
    return g_out.packet(reinterpret_cast<int16_t*>(usb_audio_transmit_buffer)) * 4;
}

// No Feature Unit in our descriptor (usb_desc.c): nothing to get or set.
int usb_audio_get_feature(void*, uint8_t*, uint32_t*) { return 0; }
int usb_audio_set_feature(void*, uint8_t*) { return 0; }
}

namespace ot {
namespace hal {

const int16_t* usbAudioIn(void*) {
    const int16_t* b = g_in.block(g_block);
    usb_audio_sync_feedback = g_in.feedback();   // one aligned word store
    return b;
}

int16_t* usbAudioOutBuffer(void*) { return g_out.buffer(); }
void usbAudioOutCommit(void*) { g_out.commit(); }

bool usbAudioAvailable() { return true; }

UsbAudioStats usbAudioStats() {
    return UsbAudioStats{g_in.packets(), g_in.underruns(), g_in.dropped(), g_in.fill(), g_in.feedback(),
                         g_in.playing(), usb_audio_receive_setting != 0};
}

UsbAudioOutStats usbAudioOutStats() {
    return UsbAudioOutStats{g_out.packets(), g_out.underruns(), g_out.overflows(), g_out.fill(), g_out.step(),
                            g_out.playing()};
}

}  // namespace hal
}  // namespace ot

#else  // no USB audio function in this image

namespace ot {
namespace hal {

const int16_t* usbAudioIn(void*) { return nullptr; }
int16_t* usbAudioOutBuffer(void*) { return nullptr; }
void usbAudioOutCommit(void*) {}
bool usbAudioAvailable() { return false; }
UsbAudioStats usbAudioStats() { return UsbAudioStats{}; }
UsbAudioOutStats usbAudioOutStats() { return UsbAudioOutStats{}; }

}  // namespace hal
}  // namespace ot

#endif
