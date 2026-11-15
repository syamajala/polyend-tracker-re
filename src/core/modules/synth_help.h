// Synth help files: the Edit Patch help box text of a synth engine, one
// Markdown file per engine next to its module code (modules/<dir>/help/<NAME>.md
// in the module repository, Help/Synths/<NAME>.md on the card; NAME = the
// descriptor's name). The same file is the engine's page in the docs.
//
// The device reads a strict subset; everything else is for the docs only:
//   # B-Type                 the engine (displayName, else name)
//   ## Channel               a section (Section::longName)
//   ### Distortion           a setting of that section (ParamUi::label)
//   - Pure: a clean square   a choice of an enum setting (its enum name), or
//   - low is ..., high ...   for a number setting, one item: the low/high tip
//   #### Try this            docs only, up to the next #, ## or ### heading
// The device line of a heading is the paragraph right after it (source lines
// joined with spaces); later paragraphs, indented lines, fenced code and the
// #### parts are skipped. A choice item may continue on indented lines.
// Device lines carry no inline markup and fit the help box (kHelpLineW).
#pragma once

#include <cstdint>

#include "ui/gfx.h"

namespace ot {
namespace synthhelp {

// The help box: each line is "<name> <text>" in kHelpFont, at most kHelpLineW
// pixels wide (checked by tests/synth_help_test.cpp).
constexpr uint8_t kHelpFont = kFont5;
constexpr int kHelpLineW = 760;
constexpr int kHelpNameGap = 6;   // px between the name and the text
constexpr int kMaxLine = 256;     // find(): longer device lines are cut
constexpr uint32_t kMaxFile = 16384;   // a help file's bytes (the screen reads no more)

enum Level : uint8_t { kEngine = 1, kSection = 2, kSetting = 3, kChoice = 4 };

// One entry of a help file, in file order. name: the heading text (empty for
// kChoice); line: the device line (kChoice: the whole item, see splitChoice;
// empty when none), valid during the callback only.
struct Entry {
    Level level;
    const char* name;
    int nameLen;
    const char* line;
    int lineLen;
    int lineNo;   // 1-based source line of the heading or item
};
using Visit = void (*)(void* ctx, const Entry& e);

// Walks the entries of `text`. buf (cap bytes) holds each joined device line;
// a longer one is cut at cap - 1.
void walk(const char* text, uint32_t len, char* buf, int cap, Visit visit, void* ctx);

// The length of the choice name in a choice item ("Pure: a clean square"
// -> 4), -1 without ": ".
int splitChoice(const char* item, int len);

// The device line for a place in the file, copied to out (NUL-terminated);
// returns its length, 0 when the file has none. setting / choice nullptr =
// the section's / setting's own line; choice "" = the setting's tip (the
// whole first item, for a number setting).
int find(const char* text, uint32_t len, const char* section, const char* setting, const char* choice,
         char* out, int cap);

}  // namespace synthhelp
}  // namespace ot
