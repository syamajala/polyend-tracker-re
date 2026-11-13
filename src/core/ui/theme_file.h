// [I] Theme files from the card: Tracker CE's JSON colour themes
// (/Themes/<name>.json, the format of CE's web theme editor). The stock
// themes stay in flash and always work; a file only replaces the active table
// while it is chosen.
//
//   {
//     "name": "Monokai",
//     "base": "Original",                       (Original / Monochrome / Anniversary)
//     "background": "#272822",
//     "topBar": { "text": "#f8f8f2", "background": "#1e1f1c", ... },
//     ...
//   }
//
// Each CE key colours the OpenTracker slots it maps to (ce_theme_map.inc,
// written by tools/ce_theme_map.py); keys a file leaves out keep the base
// theme's colours. Unknown keys and bad colours are skipped and counted.
// Colours: "#RRGGBB" or "#RGB". Only a file that cannot be opened fails.
#pragma once

#include <cstdint>

#include "hal_api.h"
#include "ui/theme.h"

namespace ot {

constexpr const char* kThemesDir = "Themes";
constexpr uint32_t kThemeFileMax = 16384;   // bytes read; the rest is ignored (truncated)
constexpr int kThemeListMax = 32;           // files listed in the Display theme picker
constexpr int kThemeNameMax = 20;           // file basename, printable ASCII

const char* themeBaseName(uint8_t base);   // "Original" / "Monochrome" / "Anniversary"

struct ThemeParse {
    uint8_t base = 0;          // stock table the file starts from
    uint16_t colours = 0;      // CE keys applied
    uint16_t badKeys = 0;      // unknown keys and bad colours (skipped)
    bool truncated = false;    // longer than kThemeFileMax
    bool guarded = false;      // unreadable text: colours restored from the base
};

// Parses a whole JSON text into `slots` (kThemeSlotCount words, always all written).
void parseTheme(const char* text, uint32_t len, uint32_t* slots, ThemeParse* out);
// Themes/<name>.json -> slots. False (slots untouched) when the name is not
// valid or the file does not open.
bool loadThemeFile(IStorage& fs, const char* name, uint32_t* slots, ThemeParse* out);

// 1..kThemeNameMax printable ASCII characters, none of / \ :.
bool themeNameValid(const char* name);
// Basenames of Themes/*.json (extension in any case), sorted case-insensitively;
// at most `max`, the first ones in that order. Returns the count.
int listThemeFiles(IStorage& fs, char (*names)[kThemeNameMax + 1], int max);

// Writes Themes/<name>.json in CE's format and key order: every key that has
// an OpenTracker colour, read from `slots`. Never overwrites.
enum class ThemeWrite : uint8_t { kOk = 0, kExists, kFailed };
ThemeWrite writeThemeFile(IStorage& fs, const char* name, const uint32_t* slots, uint8_t base);

}  // namespace ot
