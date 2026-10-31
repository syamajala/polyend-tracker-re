#include "formats/midi_names.h"

#include <cstring>

namespace ot {

uint32_t midiNamesSize(const MidiCcNames& n) { return mnames::kFixedSize + n.count * mnames::kEntrySize + 1; }

void encodeMidiNames(const MidiCcNames& n, uint8_t* out) {
    const uint32_t size = midiNamesSize(n);
    std::memset(out, 0, size);
    std::memcpy(out, "OTMN", 4);
    out[4] = mnames::kVersion;
    out[5] = uint8_t(kMidiInstrumentCount);
    out[6] = uint8_t(mnames::kPresetSize);
    uint8_t* w = out + mnames::kHeaderSize;
    for (const auto& p : n.preset) {
        std::strncpy(reinterpret_cast<char*>(w), p, mnames::kPresetSize - 1);
        w += mnames::kPresetSize;
    }
    for (uint8_t c : n.chart) *w++ = c;
    *w++ = uint8_t(n.count & 0xFF);
    *w++ = uint8_t(n.count >> 8);
    for (int i = 0; i < n.count; ++i) {
        *w++ = n.entry[i].chart;
        *w++ = n.entry[i].cc;
        std::strncpy(reinterpret_cast<char*>(w), n.entry[i].name, MidiCcNames::kNameMax);
        w += MidiCcNames::kNameMax + 1;
    }
    uint8_t sum = 0;
    for (uint8_t* q = out + mnames::kHeaderSize; q < w; ++q) sum = uint8_t(sum + *q);
    *w = sum;
}

FmtStatus decodeMidiNames(const uint8_t* file, size_t len, MidiCcNames& n) {
    n.clear();
    if (len < mnames::kFixedSize + 1) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTMN", 4) != 0 || file[4] != mnames::kVersion || file[5] != kMidiInstrumentCount ||
        file[6] != mnames::kPresetSize)
        return FmtStatus::BadMagic;
    const uint8_t* r = file + mnames::kHeaderSize + kMidiInstrumentCount * (mnames::kPresetSize + 1);
    const uint32_t count = uint32_t(r[0]) | uint32_t(r[1]) << 8;
    if (count > uint32_t(MidiCcNames::kEntryMax)) return FmtStatus::BadMagic;
    const size_t size = mnames::kFixedSize + count * mnames::kEntrySize + 1;
    if (len < size) return FmtStatus::TooShort;
    uint8_t sum = 0;
    for (size_t k = mnames::kHeaderSize; k + 1 < size; ++k) sum = uint8_t(sum + file[k]);
    if (sum != file[size - 1]) return FmtStatus::BadMagic;
    r = file + mnames::kHeaderSize;
    for (auto& p : n.preset) {
        std::memcpy(p, r, mnames::kPresetSize - 1);
        p[mnames::kPresetSize - 1] = 0;
        r += mnames::kPresetSize;
    }
    for (uint8_t& c : n.chart) c = *r++;
    r += 2;
    n.count = uint16_t(count);
    for (uint32_t i = 0; i < count; ++i) {
        MidiCcNames::Entry& e = n.entry[i];
        e.chart = *r++;
        e.cc = *r++;
        std::memcpy(e.name, r, MidiCcNames::kNameMax);
        e.name[MidiCcNames::kNameMax] = 0;
        r += MidiCcNames::kNameMax + 1;
    }
    n.sanitize();
    return FmtStatus::Ok;
}

}  // namespace ot
