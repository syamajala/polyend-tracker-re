// Replays coprocessor streams (otf2cmd output) on Bridgetek's FT81x emulator
// (bt8xxemu.dll, FT813 like the Tracker; Windows only, runs under Wine) and
// reports what each frame built in RAM_DL.
//
//   replay.exe FONTS.bin LIST.txt
//
// FONTS.bin goes to RAM_G 0x06CC00 (the driver's font image). LIST.txt has
// one "NAME FILE.cmd" per line; each stream goes through the command FIFO
// and prints "NAME SENT_WORDS REG_CMD_DL_BYTES ok|FAULT|TIMEOUT". After a
// fault the coprocessor is reset as Ft81xDisplay::recoverCoprocessor() does
// and the next frame is replayed.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "bt8xxemu.h"

namespace {

BT8XXEMU_Emulator* E;

int graphics(BT8XXEMU_Emulator*, void*, int, const argb8888*, uint32_t, uint32_t, BT8XXEMU_FrameFlags) { return 1; }
void logcb(BT8XXEMU_Emulator*, void*, BT8XXEMU_LogType t, const char* m) {
    if (t != BT8XXEMU_LogMessage) std::fprintf(stderr, "[emu %d] %s\n", int(t), m);
}

void addr(uint32_t a, bool wr) {
    BT8XXEMU_transfer(E, uint8_t(((a >> 16) & 0x3F) | (wr ? 0x80 : 0)));
    BT8XXEMU_transfer(E, uint8_t(a >> 8));
    BT8XXEMU_transfer(E, uint8_t(a));
}
uint32_t rd32(uint32_t a) {
    BT8XXEMU_chipSelect(E, 1);
    addr(a, false);
    BT8XXEMU_transfer(E, 0);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(BT8XXEMU_transfer(E, 0)) << (8 * i);
    BT8XXEMU_chipSelect(E, 0);
    return v;
}
void wrBytes(uint32_t a, const uint8_t* p, size_t n) {
    BT8XXEMU_chipSelect(E, 1);
    addr(a, true);
    for (size_t i = 0; i < n; ++i) BT8XXEMU_transfer(E, p[i]);
    BT8XXEMU_chipSelect(E, 0);
}
void wr32(uint32_t a, uint32_t v) {
    const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    wrBytes(a, b, 4);
}

constexpr uint32_t kRamCmd = 0x308000, kRegCpuReset = 0x302020, kRegCmdRead = 0x3020F8, kRegCmdWrite = 0x3020FC,
                   kRegCmdDl = 0x302100, kFontBase = 0x06CC00;

enum Result { kOk, kFault, kTimeout };

Result waitIdle() {
    for (int i = 0; i < 2000000; ++i) {
        const uint32_t r = rd32(kRegCmdRead) & 0xFFF;
        if (r == 0xFFF) return kFault;
        if (r == (rd32(kRegCmdWrite) & 0xFFF)) return kOk;
    }
    return kTimeout;
}

Result send(const std::vector<uint32_t>& w) {
    size_t i = 0;
    while (i < w.size()) {
        const uint32_t rp = rd32(kRegCmdRead) & 0xFFF, wp = rd32(kRegCmdWrite) & 0xFFF;
        if (rp == 0xFFF) return kFault;
        uint32_t room = 4092 - ((wp - rp) & 0xFFF), pos = wp;
        for (int k = 0; room >= 4 && i < w.size() && k < 256; ++k) {
            wr32(kRamCmd + pos, w[i++]);
            pos = (pos + 4) & 0xFFF;
            room -= 4;
        }
        wr32(kRegCmdWrite, pos);
    }
    return waitIdle();
}

void recover() {   // Ft81xDisplay::recoverCoprocessor (programmer guide §5.6)
    const uint8_t one = 1, zero = 0;
    wrBytes(kRegCpuReset, &one, 1);
    wr32(kRegCmdRead, 0);
    wr32(kRegCmdWrite, 0);
    wrBytes(kRegCpuReset, &zero, 1);
}

std::vector<uint8_t> load(const std::string& p) {
    std::vector<uint8_t> b;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        int c;
        while ((c = std::fgetc(f)) != EOF) b.push_back(uint8_t(c));
        std::fclose(f);
    }
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: replay.exe FONTS.bin LIST.txt\n");
        return 2;
    }
    BT8XXEMU_EmulatorParameters p;
    BT8XXEMU_defaults(BT8XXEMU_VERSION_API, &p, BT8XXEMU_EmulatorFT813);
    p.Graphics = graphics;
    p.Log = logcb;
    BT8XXEMU_run(BT8XXEMU_VERSION_API, &E, &p);
    const std::vector<uint8_t> fonts = load(argv[1]);
    if (fonts.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    for (size_t o = 0; o < fonts.size(); o += 4096)
        wrBytes(kFontBase + uint32_t(o), fonts.data() + o, fonts.size() - o < 4096 ? fonts.size() - o : 4096);
    FILE* list = std::fopen(argv[2], "r");
    if (!list) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    char name[256], path[1024];
    int frames = 0;
    while (std::fscanf(list, "%255s %1023s", name, path) == 2) {
        const std::vector<uint8_t> b = load(path);
        std::vector<uint32_t> w(b.size() / 4);
        for (size_t i = 0; i < w.size(); ++i)
            w[i] = uint32_t(b[4 * i]) | uint32_t(b[4 * i + 1]) << 8 | uint32_t(b[4 * i + 2]) << 16 |
                   uint32_t(b[4 * i + 3]) << 24;
        const Result r = w.empty() ? kTimeout : send(w);
        const uint32_t dl = rd32(kRegCmdDl) & 0x1FFF;
        std::printf("%s %zu %u %s\n", name, w.size(), unsigned(dl), r == kOk ? "ok" : (r == kFault ? "FAULT" : "TIMEOUT"));
        std::fflush(stdout);
        if (r != kOk) recover();
        ++frames;
    }
    std::fclose(list);
    BT8XXEMU_destroy(E);
    return frames ? 0 : 2;
}
