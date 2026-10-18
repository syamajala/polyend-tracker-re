#include "formats/midi_lfo.h"

#include <cstring>

namespace ot {

bool midiLfosUsed(const Project& p) {
    for (const MidiLfo& l : p.midiLfo)
        if (!l.isDefault()) return true;
    return false;
}

void encodeMidiLfos(const Project& p, uint8_t out[mlfo::kFileSize]) {
    std::memset(out, 0, mlfo::kFileSize);
    std::memcpy(out, "OTML", 4);
    out[4] = mlfo::kVersion;
    out[5] = uint8_t(kMidiInstrumentCount);
    out[6] = uint8_t(mlfo::kRecordSize);
    uint8_t sum = 0;
    for (int i = 0; i < kMidiInstrumentCount; ++i) {
        const MidiLfo& l = p.midiLfo[i];
        uint8_t* r = out + mlfo::kHeaderSize + i * mlfo::kRecordSize;
        r[0] = l.target;
        r[1] = l.shape;
        r[2] = l.rate;
        r[3] = l.depth;
        r[4] = l.center;
        r[5] = l.retrig;
        for (uint32_t k = 0; k < mlfo::kRecordSize; ++k) sum = uint8_t(sum + r[k]);
    }
    out[mlfo::kFileSize - 1] = sum;
}

FmtStatus decodeMidiLfos(const uint8_t* file, size_t len, Project& p) {
    for (MidiLfo& l : p.midiLfo) l = MidiLfo{};
    if (len < mlfo::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTML", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t count = file[5], size = file[6];
    if (size < mlfo::kRecordSize) return FmtStatus::BadMagic;
    const size_t body = size_t(count) * size;
    if (len < mlfo::kHeaderSize + body + 1) return FmtStatus::TooShort;
    uint8_t sum = 0;
    for (size_t k = 0; k < body; ++k) sum = uint8_t(sum + file[mlfo::kHeaderSize + k]);
    if (sum != file[mlfo::kHeaderSize + body]) return FmtStatus::BadMagic;
    for (uint32_t i = 0; i < count && i < uint32_t(kMidiInstrumentCount); ++i) {
        const uint8_t* r = file + mlfo::kHeaderSize + i * size;
        MidiLfo& l = p.midiLfo[i];
        l.target = r[0];
        l.shape = r[1];
        l.rate = r[2];
        l.depth = r[3];
        l.center = r[4];
        l.retrig = r[5];
        l.sanitize();
    }
    return FmtStatus::Ok;
}

}  // namespace ot
