// OpenTracker simulator -- the machine: host HAL objects + the App's large
// buffers (SDRAM on the device) + ot::App (or the renderer demo). Used by
// the simulator's front end (main.cpp) and by programs that embed it.
#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/storage_stream_reader.h"
#include "host_hal.h"
#include "host_midi.h"
#include "host_storage.h"
#include "remote/display_tap.h"
#include "remote/remote.h"
#include "sdl_audio.h"
#include "sim_nvstore.h"
#include "sim_usb_storage.h"
#include "sw_display.h"

namespace sim {

// The HAL-relevant part of the front end's options.
struct MachineOptions {
    std::string sd;            // card root folder
    std::string eepromPath;    // config EEPROM file; empty = "<sd>.eeprom" next to the SD folder
    bool lineIn = false, noAudio = false, noMidi = false, demo = false;
    bool romFonts = false;     // never bind the custom fonts (OP-6 checks)
    bool busyDisplay = true;   // FT81x frame pacing model
    std::string wav;           // headless: record the rendered audio
    float lineInTone = 0.0f;   // headless: synthetic line input (Hz), 0 = none
    int audioBuffer = 0;       // SDL device buffer in frames (0 = SdlAudio default)
    std::string audioOut;      // SDL output device name (empty = system default)
    bool usbStorage = true;    // false: HalRefs::usbStorage null (USB Storage mode "not available")
    bool serve = false;        // [I] act as a Tracker for tools/tracker_remote.py (pseudo-terminal)
    bool mirror = false;       // [I] --mirror: no app, the display shows a Tracker's frames
    bool frames = false;       // headless --frames: record display commands for "budget" (#71)
    std::string usbIn;         // [I] #97 capture device as USB audio in ("default")
    std::string usbOut;        // [I] #97 step 2 output device for the USB send ("default")
    float usbInTone = 0.0f;    // [I] headless: synthetic USB audio in (Hz), 0 = none
};

struct Machine {
    SimClock clock;
    SwDisplay display;
    SimInput input{clock};
    SimLeds leds;
    HostStorage storage;
    ot::StorageStreamReader streamReader{storage};   // SD streaming: host reader over the card folder
    SimUsbStorage usbStorage{storage};
    HostMidi midi;
    SdlAudio sdlAudio;
    ManualAudio manualAudio;
    // Synthetic line input: 0.25 s silence, then 0.4 s tone bursts every 0.6 s
    // (L at hz, R a fifth above: distinct stereo lanes).
    struct ToneIn {
        float hz = 440.0f;
        bool used = false;  // --line-in-tone given
        bool off = false;   // script 'line-in off': silence, the clock keeps running
        uint64_t n = 0;
        int16_t buf[128 * 2] = {};
        const int16_t* block() {
            for (int i = 0; i < 128; ++i, ++n) {
                const double t = double(n) / 44100.0;
                const double ph = std::fmod(t - 0.25, 0.6);
                const double env = off || t < 0.25 || ph > 0.4 ? 0.0 : std::sin(3.14159265 * ph / 0.4);
                // Distinct lanes for the 2.x stereo recorder: R a fifth above, softer.
                buf[2 * i] = int16_t(12000.0 * env * std::sin(2.0 * 3.14159265 * hz * t));
                buf[2 * i + 1] = int16_t(9000.0 * env * std::sin(2.0 * 3.14159265 * 1.5 * hz * t));
            }
            return buf;
        }
    } tone, usbTone;   // usbTone: [I] --usb-in-tone (#97)
    // [I] USB audio out (#97 step 2): the engine's send while "the computer
    // records" (--usb-out, or the script's usb-out on); its peak for
    // expect-usb-out.
    struct UsbOut {
        bool recording = false;
        SdlAudio* sdl = nullptr;   // --usb-out: also played there
        int16_t buf[128 * 2] = {};
        int peak = 0;
    } usbOut;
    ot::IAudioOut* audio = nullptr;
    bool demo = false;
    bool mirror = false;   // [I] --mirror: no app, the display shows the Tracker's frames

    // App and its large buffers (SDRAM on the device).
    std::unique_ptr<ot::App> app;
    std::vector<int16_t> sampleBank, fxMemory;
    std::unique_ptr<ot::PatternStock[]> patterns;
    std::unique_ptr<ot::Pattern[]> undo, fileUndo, clipboard, seqBuffers;
    std::vector<ot::Track> extTracks;
    std::vector<uint8_t> extMeta;
    uint8_t undoNums[20] = {}, fileUndoNums[10] = {};
    std::vector<uint8_t> scratch;
    std::unique_ptr<ot::PerfPatternMemory> perfPatterns;
    SimNvStore nvstore;   // config EEPROM stand-in (sim: "<sd>.eeprom" next to the SD folder)
    // [I] --serve: the device side of remote control on a pseudo-terminal.
    std::unique_ptr<ot::remote::DeviceBridge> bridge;
    std::unique_ptr<ot::remote::DisplayTap> tap;   // "@RC FRAME" (display command export), --frames
    std::vector<uint8_t> tapBuf;
    int serveFd = -1;
    bool openServe();
    void serviceServe();

    // demo state
    std::string demoLog[8];
    int demoEnc = 0;
    uint32_t demoLastFrame = 0;

    bool start(const MachineOptions& o, bool headless);
    void step();
    uint8_t appliedBrightness = 0xFF;
    void shutdown();

    // --- demo: exercise the renderer and echo inputs -----------------------
    void demoStep();
    void demoFrame(uint32_t now);
};

}  // namespace sim
