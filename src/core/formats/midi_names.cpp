#include "formats/midi_names.h"

#include <cstring>

namespace ot {

bool midiNamesUsed(const Project& p) {
    for (const MidiPresetInfo& m : p.midiPreset)
        if (!m.isDefault()) return true;
    return false;
}

void encodeMidiNames(const Project& p, uint8_t out[mnames::kFileSize]) {
    std::memset(out, 0, mnames::kFileSize);
    std::memcpy(out, "OTMN", 4);
    out[4] = mnames::kVersion;
    out[5] = uint8_t(kMidiInstrumentCount);
    out[6] = uint8_t(mnames::kRecordSize);
    uint8_t sum = 0;
    for (int i = 0; i < kMidiInstrumentCount; ++i) {
        const MidiPresetInfo& m = p.midiPreset[i];
        uint8_t* r = out + mnames::kHeaderSize + i * mnames::kRecordSize;
        std::strncpy(reinterpret_cast<char*>(r), m.name, mnames::kNameSize - 1);
        for (int k = 0; k < 6; ++k)
            std::strncpy(reinterpret_cast<char*>(r + mnames::kNameSize + k * mnames::kLabelSize), m.label[k],
                         mnames::kLabelSize - 1);
        for (uint32_t k = 0; k < mnames::kRecordSize; ++k) sum = uint8_t(sum + r[k]);
    }
    out[mnames::kFileSize - 1] = sum;
}

FmtStatus decodeMidiNames(const uint8_t* file, size_t len, Project& p) {
    for (MidiPresetInfo& m : p.midiPreset) m = MidiPresetInfo{};
    if (len < mnames::kHeaderSize) return FmtStatus::TooShort;
    if (std::memcmp(file, "OTMN", 4) != 0 || file[4] == 0) return FmtStatus::BadMagic;
    const uint32_t count = file[5], size = file[6];
    if (size < mnames::kRecordSize) return FmtStatus::BadMagic;
    const size_t body = size_t(count) * size;
    if (len < mnames::kHeaderSize + body + 1) return FmtStatus::TooShort;
    uint8_t sum = 0;
    for (size_t k = 0; k < body; ++k) sum = uint8_t(sum + file[mnames::kHeaderSize + k]);
    if (sum != file[mnames::kHeaderSize + body]) return FmtStatus::BadMagic;
    for (uint32_t i = 0; i < count && i < uint32_t(kMidiInstrumentCount); ++i) {
        const uint8_t* r = file + mnames::kHeaderSize + i * size;
        MidiPresetInfo& m = p.midiPreset[i];
        std::memcpy(m.name, r, mnames::kNameSize - 1);
        for (int k = 0; k < 6; ++k)
            std::memcpy(m.label[k], r + mnames::kNameSize + k * mnames::kLabelSize, mnames::kLabelSize - 1);
        m.sanitize();
    }
    return FmtStatus::Ok;
}

}  // namespace ot
