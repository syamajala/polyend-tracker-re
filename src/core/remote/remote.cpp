#include "remote/remote.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "display_row.h"
#include "formats/crc32.h"
#include "remote/display_tap.h"
#include "ui/buttons.h"

static_assert(ot::remote::kShotMaxW <= ot::kDisplayRowPixels, "a shot row fits the shared display row");

namespace ot {
namespace remote {

namespace {
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Whole-number arguments: false on missing or trailing garbage.
bool ints(const char* s, int* v, int n) {
    for (int i = 0; i < n; ++i) {
        char* end = nullptr;
        const long x = std::strtol(s, &end, 10);
        if (end == s) return false;
        v[i] = int(x);
        s = end;
    }
    while (*s == ' ') ++s;
    return *s == 0;
}
// Hex arguments (no 0x), each up to 32 bits: false on missing, overlong or
// trailing garbage.
bool hexes(const char* s, uint32_t* v, int n) {
    for (int i = 0; i < n; ++i) {
        while (*s == ' ') ++s;
        int digits = 0;
        uint32_t x = 0;
        for (int d; (d = hexDigit(*s)) >= 0; ++s, ++digits) x = (x << 4) | uint32_t(d);
        if (digits == 0 || digits > 8) return false;
        v[i] = x;
    }
    while (*s == ' ') ++s;
    return *s == 0;
}
}  // namespace

// K66 memory map (reference manual ch. 4): the ranges a PEEK may read.
bool mcuReadAllowed(uint32_t addr, uint32_t words) {
    if (words == 0 || words > uint32_t(kPeekMaxWords) || (addr & 3u)) return false;
    const uint64_t end = uint64_t(addr) + 4u * words;
    struct Range { uint32_t lo, hi; };   // [lo, hi)
    static constexpr Range kAllowed[] = {
        {0x00000000u, 0x00200000u},   // program flash, 2 MB
        {0x1FFF0000u, 0x20030000u},   // SRAM_L + SRAM_U, 256 KB
        {0x40047000u, 0x40049000u},   // SIM
        {0x40049000u, 0x4004E000u},   // PORTA .. PORTE (clocks on: SIM_SCGC5, Teensy start-up)
        {0x400FF000u, 0x400FF140u},   // GPIOA .. GPIOE
    };
    for (const Range& r : kAllowed)
        if (addr >= r.lo && end <= r.hi) return true;
    return false;
}

bool pcrWriteAllowed(uint32_t addr, uint32_t cur, uint32_t next, bool pinOutput) {
    // PORTx_PCRn: 0x40049000 + 0x1000 * port + 4 * pin, port A..E, pin 0..31.
    if (addr < 0x40049000u || addr >= 0x4004E000u || (addr & 3u) || (addr & 0xFFFu) >= 0x80u) return false;
    constexpr uint32_t kMux = 7u << 8;
    if ((cur & ~kMux) != (next & ~kMux)) return false;   // only the MUX field changes
    const uint32_t from = (cur & kMux) >> 8, to = (next & kMux) >> 8;
    if (from > 1 || to > 1) return false;                // disabled <-> GPIO only
    return !pinOutput;                                   // a GPIO input, never a driver
}

bool ftReadAllowed(uint32_t addr, uint32_t len) {
    return len > 0 && len <= uint32_t(kFtReadMaxBytes) && uint64_t(addr) + len <= 0x400000u;
}

bool ftWriteAllowed(uint32_t addr, uint8_t bytes) {
    if ((bytes != 1 && bytes != 2 && bytes != 4) || (addr & (bytes - 1u))) return false;
    if (addr < 0x302000u || addr + bytes > 0x303000u) return false;
    // REG_CPURESET 0x302020; REG_CMD_READ / WRITE / DL 0x3020F8 .. 0x302103.
    const auto hits = [&](uint32_t lo, uint32_t hi) { return addr < hi && addr + bytes > lo; };
    return !hits(0x302020u, 0x302024u) && !hits(0x3020F8u, 0x302104u);
}

bool parse(const char* line, Msg& m) {
    const char* p = std::strstr(line, "@RC ");
    if (!p) return false;
    p += 4;
    m = Msg{};
    int v[2] = {};
    if (!std::strcmp(p, "HELLO")) m.kind = Msg::kHello;
    else if (!std::strcmp(p, "BYE")) m.kind = Msg::kBye;
    else if (!std::strcmp(p, "ON")) m.kind = Msg::kOn;
    else if (!std::strcmp(p, "OFF")) m.kind = Msg::kOff;
    else if (!std::strcmp(p, "DRIVE")) m.kind = Msg::kDrive;
    else if (!std::strcmp(p, "Q")) m.kind = Msg::kQuery;
    else if (!std::strcmp(p, "SHOT")) m.kind = Msg::kShot;
    else if (!std::strcmp(p, "FRAME")) m.kind = Msg::kFrame;
    else if (!std::strcmp(p, "MIRROR 1")) m.kind = Msg::kMirrorOn;
    else if (!std::strcmp(p, "MIRROR 0")) m.kind = Msg::kMirrorOff;
    else if (!std::strncmp(p, "B1 ", 3) || !std::strncmp(p, "B0 ", 3)) {
        if (!ints(p + 3, v, 1) || v[0] < 0 || v[0] > 63) return false;
        m.kind = p[1] == '1' ? Msg::kButtonDown : Msg::kButtonUp;
        m.id = uint8_t(v[0]);
    } else if (!std::strncmp(p, "P1 ", 3)) {
        if (!ints(p + 3, v, 2) || v[0] < 0 || v[0] >= kPads || v[1] < 0 || v[1] > 127) return false;
        m.kind = Msg::kPadDown;
        m.id = uint8_t(v[0]);
        m.velocity = uint8_t(v[1]);
    } else if (!std::strncmp(p, "P0 ", 3)) {
        if (!ints(p + 3, v, 1) || v[0] < 0 || v[0] >= kPads) return false;
        m.kind = Msg::kPadUp;
        m.id = uint8_t(v[0]);
    } else if (!std::strncmp(p, "E ", 2)) {
        if (!ints(p + 2, v, 1) || v[0] < -127 || v[0] > 127 || v[0] == 0) return false;
        m.kind = Msg::kEncoder;
        m.delta = int8_t(v[0]);
    } else if (!std::strncmp(p, "L ", 2)) {
        const char* h = p + 2;
        for (int i = 0; i < kPads; ++i) {
            const int a = hexDigit(h[2 * i]), b = a < 0 ? -1 : hexDigit(h[2 * i + 1]);
            if (a < 0 || b < 0) return false;
            m.levels[i] = uint8_t(a * 16 + b);
        }
        if (h[2 * kPads] != 0) return false;
        m.kind = Msg::kLeds;
    } else if (!std::strncmp(p, "PEEK ", 5) || !std::strncmp(p, "FTRD ", 5)) {
        uint32_t a[2];
        if (!hexes(p + 5, a, 2) || a[1] == 0 || a[1] > 255) return false;
        m.kind = p[0] == 'P' ? Msg::kPeek : Msg::kFtRead;
        m.addr = a[0];
        m.count = uint8_t(a[1]);
    } else if (!std::strncmp(p, "POKE ", 5)) {
        uint32_t a[2];
        if (!hexes(p + 5, a, 2)) return false;
        m.kind = Msg::kPoke;
        m.addr = a[0];
        m.value = a[1];
    } else if (!std::strncmp(p, "FTWR ", 5)) {
        uint32_t a[3];
        if (!hexes(p + 5, a, 3) || a[2] > 4) return false;
        m.kind = Msg::kFtWrite;
        m.addr = a[0];
        m.value = a[1];
        m.count = uint8_t(a[2]);
    } else {
        return false;
    }
    return true;
}

int formatEvent(const InputEvent& ev, char* out, int cap) {
    int n = 0;
    switch (ev.type) {
        case InputEvent::kButtonDown: n = std::snprintf(out, size_t(cap), "@RC B1 %u", unsigned(ev.id)); break;
        case InputEvent::kButtonUp: n = std::snprintf(out, size_t(cap), "@RC B0 %u", unsigned(ev.id)); break;
        case InputEvent::kPadDown:
            n = std::snprintf(out, size_t(cap), "@RC P1 %u %u", unsigned(ev.id), unsigned(ev.velocity));
            break;
        case InputEvent::kPadUp: n = std::snprintf(out, size_t(cap), "@RC P0 %u", unsigned(ev.id)); break;
        case InputEvent::kEncoder:
            if (ev.delta == 0) return 0;
            n = std::snprintf(out, size_t(cap), "@RC E %d", int(ev.delta));
            break;
        default: return 0;
    }
    return n > 0 && n < cap ? n : 0;
}

int formatLeds(const uint8_t levels[kPads], char* out, int cap) {
    static const char kHex[] = "0123456789ABCDEF";
    if (cap < 6 + 2 * kPads + 1) return 0;
    std::memcpy(out, "@RC L ", 6);
    for (int i = 0; i < kPads; ++i) {
        out[6 + 2 * i] = kHex[levels[i] >> 4];
        out[7 + 2 * i] = kHex[levels[i] & 15];
    }
    out[6 + 2 * kPads] = 0;
    return 6 + 2 * kPads;
}

int base64(const uint8_t* in, int n, char* out) {
    static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < n ? uint32_t(in[i + 1]) << 8 : 0u) |
                           (i + 2 < n ? uint32_t(in[i + 2]) : 0u);
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        out[o++] = i + 1 < n ? kB64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? kB64[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

void LineReader::feed(const char* data, size_t n, void (*fn)(void*, const char*), void* ctx) {
    for (size_t i = 0; i < n; ++i) {
        const char c = data[i];
        if (c == '\n' || c == '\r') {
            if (len_ && !overflow_) {
                buf_[len_] = 0;
                fn(ctx, buf_);
            }
            len_ = 0;
            overflow_ = false;
        } else if (len_ < kLineMax) {
            buf_[len_++] = c;
        } else {
            overflow_ = true;
        }
    }
}

// ---------------------------------------------------------------------------
// DeviceBridge

DeviceBridge::DeviceBridge(IInput& panel, ILeds& pads, const Io& io) : panel_(panel), pads_(pads), io_(io) {}

void DeviceBridge::rx(const char* data, size_t n, uint32_t now) {
    now_ = now;
    reader_.feed(data, n, [](void* self, const char* l) { static_cast<DeviceBridge*>(self)->onLine(l); }, this);
}

void DeviceBridge::onLine(const char* line) {
    Msg m;
    if (!parse(line, m)) return;
    switch (m.kind) {
        case Msg::kHello:
        case Msg::kDrive:
            if (!enabled_) break;
            lastHello_ = now_;
            setMode(m.kind == Msg::kHello ? kPanel : kDrive);
            break;
        case Msg::kBye: setMode(kOff); break;
        case Msg::kLeds:
            if (mode_ != kPanel) break;
            for (int i = 0; i < kPads && i < pads_.padCount(); ++i)
                pads_.setPad(uint8_t(i), m.levels[i], m.levels[i], m.levels[i]);
            pads_.show();
            break;
        case Msg::kButtonDown:
        case Msg::kButtonUp:
        case Msg::kPadDown:
        case Msg::kPadUp:
        case Msg::kEncoder:
            if (mode_ == kDrive) inject(m);
            break;
        case Msg::kQuery:
            if (mode_ == kDrive) query_ = true;
            break;
        case Msg::kShot:
            if (mode_ != kDrive) break;
            if (!display_ || display_->width() > kShotMaxW) {
                sendLine("@RC SHOT FAIL");
                break;
            }
            shotW_ = display_->width();
            shotH_ = display_->height();
            shotRow_ = 0;
            shotLastOk_ = now_;
            shotLen_ = 0;
            shotBytes_ = 0;
            shotCrc_ = crc32Init();
            {
                char h[40];
                std::snprintf(h, sizeof h, "@RC SHOT %d %d", shotW_, shotH_);
                sendLine(h);
            }
            break;
        case Msg::kFrame:
        case Msg::kMirrorOn:
            if (mode_ != kDrive) break;
            if (!tap_) {
                sendLine("@RC F FAIL");
                break;
            }
            if (m.kind == Msg::kMirrorOn) mirror_ = true;
            tap_->request();
            redraw_ = true;
            break;
        case Msg::kMirrorOff:
            mirror_ = false;
            if (tap_ && !tap_->ready()) tap_->release();   // a frame not drawn yet: dropped
            break;
        case Msg::kPeek:
        case Msg::kPoke:
        case Msg::kFtRead:
        case Msg::kFtWrite:
            if (mode_ == kDrive) onRegister(m);
            break;
        default: break;
    }
}

void DeviceBridge::onRegister(const Msg& m) {
    char line[kLineMax + 32];
    int n = 0;
    const auto fail = [&](const char* what) {
        std::snprintf(line, sizeof line, "@RC %s FAIL %08lX", what, static_cast<unsigned long>(m.addr));
        sendLine(line);
    };
    switch (m.kind) {
        case Msg::kPeek: {
            if (!io_.peek || !mcuReadAllowed(m.addr, m.count)) return fail("PEEK");
            n = std::snprintf(line, sizeof line, "@RC PEEK %08lX", static_cast<unsigned long>(m.addr));
            for (uint32_t i = 0; i < m.count; ++i) {
                uint32_t v = 0;
                if (!io_.peek(io_.ctx, m.addr + 4 * i, &v)) return fail("PEEK");
                n += std::snprintf(line + n, sizeof line - size_t(n), " %08lX", static_cast<unsigned long>(v));
            }
            break;
        }
        case Msg::kPoke: {
            uint32_t pcr = 0;
            bool output = true;
            if (!io_.poke || !io_.pcrState || !io_.pcrState(io_.ctx, m.addr, &pcr, &output) ||
                !pcrWriteAllowed(m.addr, pcr, m.value, output) || !io_.poke(io_.ctx, m.addr, m.value))
                return fail("POKE");
            std::snprintf(line, sizeof line, "@RC POKE OK %08lX %08lX", static_cast<unsigned long>(m.addr),
                          static_cast<unsigned long>(m.value));
            break;
        }
        case Msg::kFtRead: {
            uint8_t b[kFtReadMaxBytes];
            if (!display_ || !ftReadAllowed(m.addr, m.count) || !display_->debugRead(m.addr, b, m.count))
                return fail("FTRD");
            n = std::snprintf(line, sizeof line, "@RC FTRD %06lX ", static_cast<unsigned long>(m.addr));
            for (uint32_t i = 0; i < m.count; ++i) n += std::snprintf(line + n, sizeof line - size_t(n), "%02X", b[i]);
            break;
        }
        case Msg::kFtWrite:
            if (!display_ || !ftWriteAllowed(m.addr, m.count) || !display_->debugWrite(m.addr, m.value, m.count))
                return fail("FTWR");
            std::snprintf(line, sizeof line, "@RC FTWR OK %06lX %08lX", static_cast<unsigned long>(m.addr),
                          static_cast<unsigned long>(m.value));
            break;
        default: return;
    }
    sendLine(line);
}

void DeviceBridge::tick(uint32_t now, bool hostOpen) {
    now_ = now;
    if (mode_ != kOff && (!hostOpen || now - lastHello_ > kTimeoutMs)) setMode(kOff);
    // A started frame finishes before a screenshot starts, and the other way round.
    if (frameSent_ < 0 && shotRow_ >= 0) serviceShot();
    else serviceFrame();
}

void DeviceBridge::setEnabled(bool on) {
    enabled_ = on;
    if (!on) setMode(kOff);
}

void DeviceBridge::setMode(Mode m) {
    if (m == mode_) return;
    const Mode old = mode_;
    mode_ = m;
    if (old == kPanel) {
        // The host releases whatever it still holds for us (on "@RC OFF");
        // keys still down here are not the app's: their releases are dropped.
        localButtons_ = localPads_ = 0;
        sentButtons_ = sentPads_ = 0;
        for (int i = 0; i < kPads && i < pads_.padCount(); ++i) {
            const uint32_t c = appColor_[i];
            pads_.setPad(uint8_t(i), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c));
        }
        pads_.show();
    } else if (old == kDrive) {
        // Whatever the host still holds is released to the app.
        for (uint8_t i = 0; i < 64; ++i) {
            if ((injButtons_ >> i) & 1) pushInjected(InputEvent::kButtonUp, i);
            if ((injPads_ >> i) & 1) pushInjected(InputEvent::kPadUp, i);
        }
        injButtons_ = injPads_ = 0;
        query_ = false;
        shotRow_ = -1;
        mirror_ = redraw_ = false;
        frameSent_ = -1;
        if (tap_) tap_->release();
    }
    if (m == kPanel) {
        // Keys the app saw go down are released to the app, not the host.
        localButtons_ = buttonsDown_;
        localPads_ = padsDown_;
        sentButtons_ = sentPads_ = 0;
        pads_.clear();
        pads_.show();
    }
    sendLine(m == kPanel ? "@RC ON" : m == kDrive ? "@RC DRIVING" : "@RC OFF");
    if (io_.changed && (old == kOff || m == kOff)) io_.changed(io_.ctx, m != kOff);
}

void DeviceBridge::pushInjected(InputEvent::Type t, uint8_t id, uint8_t vel, int8_t delta) {
    if (uint8_t(injHead_ - injTail_) >= kInjected) return;   // full: dropped
    InputEvent& e = inj_[injHead_ % kInjected];
    e = InputEvent{};
    e.type = t;
    e.id = id;
    e.velocity = vel;
    e.delta = delta;
    e.timeMs = now_;
    ++injHead_;
}

void DeviceBridge::inject(const Msg& m) {
    const uint64_t bit = 1ull << (m.id & 63);
    switch (m.kind) {
        case Msg::kButtonDown:
            if (m.id == btn::Power) return;   // the host never powers the Tracker off
            injButtons_ |= bit;
            pushInjected(InputEvent::kButtonDown, m.id);
            break;
        case Msg::kButtonUp:
            if (!(injButtons_ & bit)) return;
            injButtons_ &= ~bit;
            pushInjected(InputEvent::kButtonUp, m.id);
            break;
        case Msg::kPadDown:
            injPads_ |= bit;
            pushInjected(InputEvent::kPadDown, m.id, m.velocity ? m.velocity : 100);
            break;
        case Msg::kPadUp:
            if (!(injPads_ & bit)) return;
            injPads_ &= ~bit;
            pushInjected(InputEvent::kPadUp, m.id);
            break;
        case Msg::kEncoder: pushInjected(InputEvent::kEncoder, 0, 0, m.delta); break;
        default: break;
    }
}

void DeviceBridge::shotLine(int n) {
    dataLine(shotBuf_, n);
    std::memmove(shotBuf_, shotBuf_ + n, size_t(shotLen_ - n));
    shotLen_ -= n;
}

void DeviceBridge::serviceShot() {
    while (shotRow_ >= 0 && room()) {
        if (shotLen_ >= kShotChunk) {
            shotLine(kShotChunk);
            continue;
        }
        if (shotRow_ < shotH_) {
            // One row: runs of equal pixels, [count-1, r, g, b]. The readback
            // needs the display idle (a frame upload may still be running on
            // its bus): wait for the next pass, and retry a failed read until
            // nothing has worked for kShotStallMs.
            const bool ready = display_ && display_->frameReady();
            uint32_t* const px = displayRow();
            if (!ready || !display_->readPixels(0, uint16_t(shotRow_), uint16_t(shotW_), 1, px)) {
                if (now_ - shotLastOk_ > kShotStallMs) {
                    sendLine("@RC SHOT FAIL");
                    shotRow_ = -1;
                }
                return;
            }
            shotLastOk_ = now_;
            uint8_t* o = shotBuf_ + shotLen_;
            for (int x = 0; x < shotW_;) {
                const uint32_t c = px[x] & 0xFFFFFFu;
                int n = 1;
                while (x + n < shotW_ && n < 256 && (px[x + n] & 0xFFFFFFu) == c) ++n;
                *o++ = uint8_t(n - 1);
                *o++ = uint8_t(c >> 16);
                *o++ = uint8_t(c >> 8);
                *o++ = uint8_t(c);
                x += n;
            }
            const int added = int(o - (shotBuf_ + shotLen_));
            shotCrc_ = crc32Update(shotCrc_, shotBuf_ + shotLen_, size_t(added));
            shotBytes_ += uint32_t(added);
            shotLen_ += added;
            ++shotRow_;
            continue;
        }
        if (shotLen_ > 0) {
            shotLine(shotLen_);
            continue;
        }
        char e[64];
        std::snprintf(e, sizeof e, "@RC SHOT END %lu %08lX", static_cast<unsigned long>(shotBytes_),
                      static_cast<unsigned long>(crc32Final(shotCrc_)));
        sendLine(e);
        shotRow_ = -1;
    }
}

void DeviceBridge::dataLine(const uint8_t* p, int n) {
    char line[16 + (kShotChunk + 2) / 3 * 4];
    std::memcpy(line, "@RC D ", 6);
    base64(p, n, line + 6);
    sendLine(line);
}

void DeviceBridge::serviceFrame() {
    if (!tap_ || !tap_->ready()) return;
    const uint32_t bytes = tap_->frameBytes();
    while (room()) {
        if (frameSent_ < 0) {
            char h[48];
            std::snprintf(h, sizeof h, "@RC F %lu %lu %d", static_cast<unsigned long>(tap_->frameSeq()),
                          static_cast<unsigned long>(bytes), tap_->truncated() ? 1 : 0);
            sendLine(h);
            frameSent_ = 0;
            frameCrc_ = crc32Init();
            continue;
        }
        if (frameSent_ < int64_t(bytes)) {
            const uint8_t* p = tap_->frame() + frameSent_;
            const int n = bytes - uint32_t(frameSent_) < uint32_t(kShotChunk) ? int(bytes - uint32_t(frameSent_))
                                                                                : kShotChunk;
            frameCrc_ = crc32Update(frameCrc_, p, size_t(n));
            dataLine(p, n);
            frameSent_ += n;
            continue;
        }
        char e[64];
        std::snprintf(e, sizeof e, "@RC F END %lu %08lX", static_cast<unsigned long>(bytes),
                      static_cast<unsigned long>(crc32Final(frameCrc_)));
        sendLine(e);
        frameSent_ = -1;
        tap_->release();
        if (mirror_) tap_->request();
        return;
    }
}

bool DeviceBridge::forward(const InputEvent& ev) {
    const uint64_t bit = 1ull << (ev.id & 63);
    const bool button = ev.type == InputEvent::kButtonDown || ev.type == InputEvent::kButtonUp;
    const bool pad = ev.type == InputEvent::kPadDown || ev.type == InputEvent::kPadUp;
    const bool down = ev.type == InputEvent::kButtonDown || ev.type == InputEvent::kPadDown;
    uint64_t& held = button ? buttonsDown_ : padsDown_;
    const bool repeat = down && (held & bit);   // driver auto-repeat: the host makes its own
    if (button || pad) held = down ? (held | bit) : (held & ~bit);
    if (mode_ == kOff) return false;
    if (button && ev.id == btn::Power) {
        // Power stays local; a press also hands the Tracker back (the app's
        // power popup closes again on the release).
        if (down && !repeat) setEnabled(false);
        return false;
    }
    if (mode_ == kDrive) return false;   // the panel keeps working next to the host
    uint64_t& local = button ? localButtons_ : localPads_;
    if ((button || pad) && !down && (local & bit)) {
        local &= ~bit;
        return false;   // pressed before panel mode: the app gets its release
    }
    if (repeat) return true;
    uint64_t& sent = button ? sentButtons_ : sentPads_;
    if (button || pad) {
        if (!down && !(sent & bit)) return true;   // never sent down (pressed while off)
        sent = down ? (sent | bit) : (sent & ~bit);
    }
    char line[32];
    if (formatEvent(ev, line, sizeof line)) sendLine(line);
    return true;
}

bool DeviceBridge::Input::poll(InputEvent& ev) {
    // The host's input first (drive mode), then the panel.
    if (b_.injTail_ != b_.injHead_) {
        ev = b_.inj_[b_.injTail_ % kInjected];
        ++b_.injTail_;
        return true;
    }
    while (b_.panel_.poll(ev))
        if (!b_.forward(ev)) return true;
    return false;
}

void DeviceBridge::Leds::setPad(uint8_t pad, uint8_t r, uint8_t g, uint8_t b) {
    if (pad < kPads) b_.appColor_[pad] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    if (b_.mode_ != kPanel) b_.pads_.setPad(pad, r, g, b);
}

void DeviceBridge::Leds::clear() {
    std::memset(b_.appColor_, 0, sizeof(b_.appColor_));
    if (b_.mode_ != kPanel) b_.pads_.clear();
}

void DeviceBridge::Leds::show() {
    if (b_.mode_ != kPanel) b_.pads_.show();
}

}  // namespace remote
}  // namespace ot
