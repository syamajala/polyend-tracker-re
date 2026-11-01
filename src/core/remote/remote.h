// [I] Remote control (not in stock), text lines over the device's USB serial
// (CDC) port, which it shares with the diag log (hal/diag.h). Two modes:
//
// Panel mode: the Tracker is the front panel of the desktop simulator.
//   host -> device   "@RC HELLO"            enter panel mode / keepalive (every kKeepaliveMs)
//                    "@RC L <96 hex>"       pad LED levels 0..255, pads 0..47
//   device -> host   "@RC ON"               panel mode entered
//                    "@RC B1 <id>" / "@RC B0 <id>"            button down / up
//                    "@RC P1 <pad> <velocity>" / "@RC P0 <pad>"  pad down / up
//                    "@RC E <delta>"        encoder detents (device acceleration applied)
//
// Drive mode: a host script drives the Tracker's own app and reads it back
// (tools/tracker_remote.py; on-device testing).
//   host -> device   "@RC DRIVE"            enter drive mode / keepalive
//                    "@RC B1/B0/P1/P0/E ..." as above: input for the Tracker's app
//                    "@RC Q"                status query
//                    "@RC SHOT"             screenshot request
//                    "@RC FRAME"            the display commands of the next frame
//                                           (redrawn at once; display_tap.h)
//                    "@RC MIRROR 1" / "@RC MIRROR 0"  every frame the link has room for,
//                                           or no more (a frame already recorded is still sent)
//   device -> host   "@RC DRIVING"          drive mode entered
//                    "@RC S key=value ..."  status (App::remoteStatus)
//                    "@RC SHOT <w> <h>", then "@RC D <base64>" lines, then
//                    "@RC SHOT END <bytes> <crc32 hex>" (or "@RC SHOT FAIL"):
//                    per row, runs of [count-1, r, g, b] (a run never crosses a row)
//                    "@RC F <seq> <bytes> <cut>", then "@RC D <base64>" lines, then
//                    "@RC F END <bytes> <crc32 hex>": a frame's command stream
//                    (display_tap.h); cut = 1: the frame overflowed the buffer.
//                    A screenshot and a frame never interleave.
//
// Drive mode register commands (hardware probes without a special image;
// only images built with OT_REMOTE_REGS=1 answer anything but FAIL). Hex
// addresses and values, no 0x:
//   "@RC PEEK <addr> <n>"         n (1..8) MCU words from an allowed range
//                                 (mcuReadAllowed: GPIO, PORT, SIM, SRAM, flash)
//                -> "@RC PEEK <addr> <w0> ..." / "@RC PEEK FAIL <addr>"
//   "@RC POKE <addr> <value>"     MCU word write, only a PORTx_PCRn that switches
//                                 between disabled and GPIO (pcrWriteAllowed)
//                -> "@RC POKE OK <addr> <value>" / "@RC POKE FAIL <addr>"
//   "@RC FTRD <addr> <n>"         n (1..32) bytes of display memory (ftReadAllowed)
//                -> "@RC FTRD <addr> <hex bytes>" / "@RC FTRD FAIL <addr>"
//   "@RC FTWR <addr> <value> <b>" b (1, 2, 4)-byte display register write
//                                 (ftWriteAllowed: the register block, not the
//                                 coprocessor's FIFO pointers or reset)
//                -> "@RC FTWR OK <addr> <value>" / "@RC FTWR FAIL <addr>"
//
// Both:  host -> device "@RC BYE" ends the mode; device -> host "@RC OFF".
//
// Device lines arrive inside diag log lines (timestamp prefix), so a parser
// looks for "@RC " anywhere in the line. Panel mode forwards the panel's
// input instead of handing it to the app (the power switch stays local) and
// shows the host's pad LEDs; drive mode adds the host's input to the panel's.
// Either ends after "@RC BYE", kTimeoutMs without a keepalive, or the host
// closing the port.
//
// The device only answers while remote control is enabled (Config > USB >
// Remote control, Off after power-on). A short press of the power switch
// during either mode disables it again.
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal_api.h"

namespace ot {
namespace remote {

class DisplayTap;

constexpr uint32_t kKeepaliveMs = 500;
constexpr uint32_t kTimeoutMs = 2000;
constexpr int kPads = 48;
constexpr int kLineMax = 128;   // longest line: "@RC L " + 96 hex digits
constexpr int kShotChunk = 72;  // screenshot bytes per "@RC D" line (96 base64 chars)
constexpr int kShotMaxW = 800;   // the shared row (display_row.h) holds at least this many

struct Msg {
    enum Kind : uint8_t {
        kNone, kHello, kBye, kOn, kOff, kButtonDown, kButtonUp, kPadDown, kPadUp, kEncoder, kLeds,
        kDrive, kQuery, kShot, kFrame, kMirrorOn, kMirrorOff, kPeek, kPoke, kFtRead, kFtWrite,
    };
    Kind kind = kNone;
    uint8_t id = 0;
    uint8_t velocity = 0;
    int8_t delta = 0;
    uint8_t levels[kPads] = {};
    uint32_t addr = 0, value = 0;   // register commands
    uint8_t count = 0;              // PEEK words, FTRD bytes, FTWR width
};

// Register command limits and address rules (the header comment).
constexpr int kPeekMaxWords = 8;
constexpr int kFtReadMaxBytes = 32;
// MCU words [addr, addr + 4 * words): word-aligned and inside one allowed
// range (reads of a peripheral whose clock is off fault the K66, so only
// GPIO, PORT, SIM, SRAM and flash).
bool mcuReadAllowed(uint32_t addr, uint32_t words);
// A PORTx_PCRn write from `cur` to `next`: only the MUX field changes, between
// 0 (disabled) and 1 (GPIO), and the pin's GPIO direction is input
// (pinOutput false), so a probe can never make a pin drive.
bool pcrWriteAllowed(uint32_t addr, uint32_t cur, uint32_t next, bool pinOutput);
// Display memory reads anywhere in the FT81x map (0 .. 0x3FFFFF).
bool ftReadAllowed(uint32_t addr, uint32_t len);
// Display register writes: aligned, inside the register block 0x302000 ..
// 0x302FFF, not REG_CPURESET or the coprocessor's REG_CMD_READ / WRITE / DL
// (the driver owns those).
bool ftWriteAllowed(uint32_t addr, uint8_t bytes);

// One line (without the line end) -> message; false for anything else.
bool parse(const char* line, Msg& m);
// Input event -> "@RC ..." (no line end); 0 for events with no remote form.
int formatEvent(const InputEvent& ev, char* out, int cap);
int formatLeds(const uint8_t levels[kPads], char* out, int cap);
// Base64 of n bytes into out (NUL-terminated); returns the length.
int base64(const uint8_t* in, int n, char* out);

// Splits a byte stream into lines (CR / LF ends, over-long lines dropped).
class LineReader {
public:
    // Feeds bytes; calls fn(ctx, line) for each complete line.
    void feed(const char* data, size_t n, void (*fn)(void*, const char*), void* ctx);

private:
    char buf_[kLineMax + 1] = {};
    int len_ = 0;
    bool overflow_ = false;
};

// Device side: wraps the panel input and pad LEDs. Feed received bytes with
// rx(), call tick() every main-loop pass; the App gets `input` / `leds`.
class DeviceBridge {
public:
    struct Io {
        void (*send)(void* ctx, const char* line) = nullptr;   // one line, no line end
        void (*changed)(void* ctx, bool active) = nullptr;     // a remote mode on / off
        // Room for a few more lines (diag ring); null = always. Screenshots
        // only send while there is room, so nothing is overwritten unsent.
        bool (*room)(void* ctx) = nullptr;
        // MCU register access for "@RC PEEK" / "@RC POKE" (OT_REMOTE_REGS
        // images); null = refused. The bridge checks the rules above first;
        // poke gets the pin's current PCR and GPIO direction from pcrState.
        bool (*peek)(void* ctx, uint32_t addr, uint32_t* value) = nullptr;
        bool (*poke)(void* ctx, uint32_t addr, uint32_t value) = nullptr;
        bool (*pcrState)(void* ctx, uint32_t pcrAddr, uint32_t* pcr, bool* output) = nullptr;
        void* ctx = nullptr;
    };
    DeviceBridge(IInput& panel, ILeds& pads, const Io& io);

    void rx(const char* data, size_t n, uint32_t now);
    // hostOpen: the host holds the serial port (DTR). Times out a silent host;
    // streams a requested screenshot while there is room.
    void tick(uint32_t now, bool hostOpen);
    bool active() const { return mode_ == kPanel; }    // panel mode
    bool driving() const { return mode_ == kDrive; }   // drive mode
    // Config > USB > Remote control. Disabling ends either mode.
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

    // Drive mode, for the App (App::serviceRemote).
    bool takeQuery() {
        const bool q = query_;
        query_ = false;
        return q;
    }
    void sendLine(const char* line) {
        if (io_.send) io_.send(io_.ctx, line);
    }
    void setDisplay(IDisplay* d) { display_ = d; }
    // Display command export (display_tap.h); null: "@RC FRAME" is refused.
    void setTap(DisplayTap* t) { tap_ = t; }
    DisplayTap* tap() const { return tap_; }
    // A frame was asked for: the App redraws now (a still screen draws no frames).
    bool takeRedraw() {
        const bool r = redraw_;
        redraw_ = false;
        return r;
    }
    bool mirroring() const { return mirror_; }
    bool shotActive() const { return shotRow_ >= 0; }
    // A screenshot gives up after this long without reading a row (display busy / failing).
    static constexpr uint32_t kShotStallMs = 2000;

    IInput& input() { return in_; }
    ILeds& leds() { return leds_; }

private:
    enum Mode : uint8_t { kOff, kPanel, kDrive };

    class Input final : public IInput {
    public:
        explicit Input(DeviceBridge& b) : b_(b) {}
        bool poll(InputEvent& ev) override;

    private:
        DeviceBridge& b_;
    };
    class Leds final : public ILeds {
    public:
        explicit Leds(DeviceBridge& b) : b_(b) {}
        uint8_t padCount() const override { return b_.pads_.padCount(); }
        void setPad(uint8_t pad, uint8_t r, uint8_t g, uint8_t b) override;
        void clear() override;
        void show() override;

    private:
        DeviceBridge& b_;
    };

    void onLine(const char* line);
    void onRegister(const Msg& m);   // PEEK / POKE / FTRD / FTWR, drive mode
    void setMode(Mode m);
    // True: the event goes to the host (or is a dropped repeat), not to the app.
    bool forward(const InputEvent& ev);
    void inject(const Msg& m);
    void pushInjected(InputEvent::Type t, uint8_t id, uint8_t vel = 0, int8_t delta = 0);
    bool room() const { return !io_.room || io_.room(io_.ctx); }
    void serviceShot();
    void shotLine(int n);   // one "@RC D" line of the first n pending bytes
    void serviceFrame();
    void dataLine(const uint8_t* p, int n);

    IInput& panel_;
    ILeds& pads_;
    Io io_;
    Input in_{*this};
    Leds leds_{*this};
    LineReader reader_;
    Mode mode_ = kOff;
    bool enabled_ = false;
    uint32_t now_ = 0, lastHello_ = 0;
    uint64_t buttonsDown_ = 0, padsDown_ = 0;     // panel state, always tracked
    uint64_t localButtons_ = 0, localPads_ = 0;   // held when panel mode began: their release stays local
    uint64_t sentButtons_ = 0, sentPads_ = 0;     // pressed on the host
    uint32_t appColor_[kPads] = {};               // the app's own LED frame, shown again after panel mode

    // Drive mode: the host's input for the app, and what it holds.
    static constexpr int kInjected = 32;
    InputEvent inj_[kInjected];
    uint8_t injHead_ = 0, injTail_ = 0;
    uint64_t injButtons_ = 0, injPads_ = 0;
    bool query_ = false;

    // Screenshot streaming (drive mode): one row at a time from the display.
    IDisplay* display_ = nullptr;
    int shotRow_ = -1;    // next row to read, -1 = idle
    uint32_t shotLastOk_ = 0;   // now_ of the last row read (or the start): retry until kShotStallMs
    int shotW_ = 0, shotH_ = 0;
    uint32_t shotCrc_ = 0, shotBytes_ = 0;
    uint8_t shotBuf_[kShotMaxW * 4 + kShotChunk] = {};   // one row's runs + a partial line
    int shotLen_ = 0;

    // Frame streaming (drive mode): the tap's buffer, kShotChunk bytes a line.
    DisplayTap* tap_ = nullptr;
    bool redraw_ = false, mirror_ = false;
    int64_t frameSent_ = -1;   // bytes of the ready frame sent, -1 = header not sent
    uint32_t frameCrc_ = 0;
};

}  // namespace remote
}  // namespace ot
