// [I] #3 SFZ reader for multisample instruments (not in stock or the
// Tracker Mini): the subset that describes one sample per key range.
//
// Read: the <control> default_path, and per region (with <global> and
// <group> defaults) sample, lokey / hikey / key, pitch_keycenter, offset and
// end. Every other header and opcode (loops, velocity layers, envelopes) is
// skipped, so a full SFZ still loads, as one-shots. Note values are numbers
// or names with c4 = 60 (sfzformat.com). `//` and `/* */` comments; a sample
// path runs to the next `opcode=` or the end of the line, so it may hold
// spaces.
//
// The parser keeps no copy of the text: a region's path is an offset into
// it, so the text must outlive the result.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ot {
namespace sfz {

constexpr int kMaxRegions = 128;
constexpr uint32_t kToEnd = 0xFFFFFFFFu;   // Region::end: play to the end of the file

struct Region {
    uint32_t pathOff = 0;    // the sample opcode's value in the text
    uint16_t pathLen = 0;
    uint8_t lokey = 0;
    uint8_t hikey = 127;
    uint8_t root = 60;       // pitch_keycenter
    uint32_t offset = 0;     // first source frame
    uint32_t end = kToEnd;   // last source frame (inclusive, as SFZ's end)
};

struct File {
    Region regions[kMaxRegions];
    int count = 0;
    int dropped = 0;          // regions past kMaxRegions, or without a sample
    uint32_t defPathOff = 0;  // <control> default_path
    uint16_t defPathLen = 0;
};

// Parses `text`; false when it holds no region with a sample.
bool parse(const char* text, uint32_t len, File& out);

// A note value: a number 0..127 or a name (c4 = 60, # / b, octave -1..9);
// -1 when it is neither.
int noteValue(const char* s, size_t n);

// The card path of region r's sample: `sfzPath`'s folder + default_path +
// sample, with `\` read as `/` and `.` / `..` resolved. False when it does
// not fit `cap` or climbs above the card's root.
bool samplePath(const char* text, const File& f, const Region& r, const char* sfzPath, char* out, size_t cap);

}  // namespace sfz
}  // namespace ot
