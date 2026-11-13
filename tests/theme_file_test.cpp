// [I] CE JSON theme files from the card (/Themes, ui/theme_file.h): the stock
// tables stay bit-identical, the CE key map, the streaming JSON reader, the
// lister and the JSON exporter.
#include "test.h"
#include "mem_storage.h"

#include <string>

#include "formats/crc32.h"
#include "ui/theme.h"
#include "ui/theme_file.h"

using namespace ot;

namespace {

uint32_t gSlots[kThemeSlotCount];

ThemeParse parse(const std::string& s, uint32_t* slots = gSlots) {
    ThemeParse r;
    parseTheme(s.data(), uint32_t(s.size()), slots, &r);
    return r;
}

void putFile(MemStorage& fs, const std::string& path, const std::string& text) {
    fs.files[path] = std::vector<uint8_t>(text.begin(), text.end());
}

std::string fileText(MemStorage& fs, const std::string& path) {
    const auto& v = fs.files[path];
    return std::string(v.begin(), v.end());
}

// The colour slots (flags aside) equal the stock table's.
bool coloursAs(const uint32_t* s, uint8_t stock) {
    for (int i = 0; i < kThemeSlotCount; ++i) {
        if (i >= slot::kFlagBottomFade && i <= slot::kFlagBarLines) continue;
        if (i == slot::kFlagBandPlayingOnly) continue;
        if (s[i] != theme(stock).slots[i]) return false;
    }
    return true;
}

// A CE-shaped theme (keys from the CE editor's format, our own colours).
const char* const kSample = R"({
  "name": "Sample",
  "base": "Original",
  "background": "#102030",
  "topBar": { "text": "#eeeeee", "background": "#203040", "frame": "#405060", "text2": "#aaaaaa" },
  "labels": {
    "active": { "text": "#fafafa", "background": "#102030", "frame": "#3388ff", "text2": "#ffffff" },
    "inactive": { "text": "#667788", "background": "#102030", "frame": "#203040", "text2": "#667788" }
  },
  "patternTracker": { "note": "#11cc99", "playhead": "#335500", "trackEnd": "#ccaa00" },
  "waveform": { "audio": "#22aa99", "progress": { "text": "#ffffff", "cursor": "#ff5500" } },
  "accents": { "red": "#dd3322", "green": "#88aa00" },
  "miniMenu": { "text": "#123456" }
}
)";

}  // namespace

TEST(stock_tables_bit_identical) {
    // CRC-32 of the 277 little-endian stock words of each table (spec 20
    // §1.4), taken before theme files existed: the stock themes never change.
    const uint32_t want[kThemeCount] = {0x32CBDEE3u, 0xE9D68DB5u, 0x5CE44B85u};
    for (uint8_t t = 0; t < kThemeCount; ++t) {
        uint8_t b[277 * 4];
        for (int i = 0; i < 277; ++i)
            for (int k = 0; k < 4; ++k) b[4 * i + k] = uint8_t(theme(t).slots[i] >> (8 * k));
        CHECK_EQ(crc32(b, sizeof(b)), want[t]);
    }
    CHECK_STR(theme(0).name, "Original");
    CHECK_STR(theme(1).name, "Monochrome");
    CHECK_STR(theme(2).name, "Anniversary");
    CHECK_STR(themeBaseName(1), "Monochrome");
    CHECK_STR(themeBaseName(9), "Original");
}

TEST(ce_keys_colour_their_slots) {
    ThemeParse r = parse(kSample);
    CHECK_EQ(r.base, 0);
    CHECK_EQ(r.badKeys, 0);
    CHECK_EQ(r.colours, 22);   // miniMenu.text counts too (no slot)
    CHECK(!r.guarded);
    CHECK(!r.truncated);
    // background: the screen, Config's list panels, the role's other slots.
    for (int s : {0, 86, 142, 196}) CHECK_EQ(gSlots[s], 0x102030u);
    // topBar: the stock title set 1..4, the hairline, the Notes popup title.
    CHECK_EQ(gSlots[slot::kTitle], 0xEEEEEEu);
    CHECK_EQ(gSlots[slot::kTitle + 1], 0x203040u);
    CHECK_EQ(gSlots[slot::kTitle + 2], 0x405060u);
    CHECK_EQ(gSlots[slot::kTitleHairline], 0x405060u);
    CHECK_EQ(gSlots[slot::kFullPopupText], 0xEEEEEEu);
    CHECK_EQ(gSlots[slot::kFullPopupBand], 0x203040u);
    // labels: Config's list text (fork slots) and selection outline.
    CHECK_EQ(gSlots[slot::kListText], 0x667788u);
    CHECK_EQ(gSlots[slot::kListTextSel], 0xFAFAFAu);
    CHECK_EQ(gSlots[slot::kList], 0x3388FFu);
    CHECK_EQ(gSlots[slot::kGrey], 0x667788u);   // the dim role
    // Pattern grid: note text, the playing-row band, the wrap line.
    CHECK_EQ(gSlots[slot::kGrid + 2], 0x11CC99u);
    CHECK_EQ(gSlots[slot::kGridCursorRow], 0x335500u);
    CHECK_EQ(gSlots[slot::kGrid + 20], 0xCCAA00u);
    // Nested two deep.
    CHECK_EQ(gSlots[slot::kWave], 0x22AA99u);
    CHECK_EQ(gSlots[slot::kWaveOverlay + 2], 0xFF5500u);
    // Accents: the accent role, then the exact slot.
    CHECK_EQ(gSlots[slot::kFocusFrame], 0xDD3322u);
    CHECK_EQ(gSlots[slot::kSolidRect], 0xDD3322u);
    CHECK_EQ(gSlots[slot::kGreen], 0x88AA00u);
    // Keys the file leaves out keep the base's colours.
    CHECK_EQ(gSlots[slot::kGrid + 3], theme(0).slots[slot::kGrid + 3]);
    CHECK_EQ(gSlots[slot::kSongList], theme(0).slots[slot::kSongList]);
    // CE's flat look and playing-only band.
    CHECK(!gSlots[slot::kFlagBottomFade]);
    CHECK(!gSlots[slot::kFlagValueBarGrad]);
    CHECK(gSlots[slot::kFlagCursorRowGrad]);
    CHECK(gSlots[slot::kFlagBarLines]);
    CHECK(gSlots[slot::kFlagBandPlayingOnly]);
}

TEST(base_applies_wherever_it_is) {
    ThemeParse r = parse(R"({"background": "#010203", "base": "monochrome"})");
    CHECK_EQ(r.base, 1);
    CHECK_EQ(gSlots[slot::kFrameClear], 0x010203u);
    CHECK_EQ(gSlots[slot::kGrid + 2], theme(1).slots[slot::kGrid + 2]);
    r = parse(R"({"base": "Anniversary"})");
    CHECK_EQ(r.base, 2);
    CHECK(coloursAs(gSlots, 2));
    r = parse(R"({"base": "Neon"})");   // unknown base: Original
    CHECK_EQ(r.base, 0);
    CHECK(coloursAs(gSlots, 0));
}

TEST(bad_keys_and_values_skipped) {
    ThemeParse r = parse(R"({
      "nope": "#123456",
      "topBar": { "text": "red", "frame": "#12345", "background": "#abc", "colour": "#111111" },
      "background": 7, "accents": { "red": null, "green": true, "list": ["#010101", {"x": 1}] },
      "patternTracker": { "note": "#0A0B0C" },
      "deep": { "a": { "b": { "c": { "d": { "background": "#ffffff" } } } } },
      "escaped\"key": "#222222"
    })");
    CHECK_EQ(r.badKeys, 5);   // nope, text, frame, colour, escaped"key
    CHECK_EQ(gSlots[slot::kTitle + 1], 0xAABBCCu);   // #RGB
    CHECK_EQ(gSlots[slot::kGrid + 2], 0x0A0B0Cu);
    CHECK_EQ(gSlots[slot::kFrameClear], theme(0).slots[slot::kFrameClear]);
    CHECK_EQ(gSlots[slot::kTitle], theme(0).slots[slot::kTitle]);
    // Not JSON at all, or empty: the base, nothing applied.
    r = parse("background = #FFFFFF\n");
    CHECK_EQ(r.colours, 0);
    CHECK(coloursAs(gSlots, 0));
    r = parse("");
    CHECK(coloursAs(gSlots, 0));
}

TEST(file_cap_sets_truncated) {
    MemStorage fs;
    std::string t = "{\"accents\": {\"green\": \"#010101\"},\n";
    while (t.size() < kThemeFileMax - 20) t += "    \"filler\": 0,\n";
    t += "\"accents\": {\"cyan\": \"#030303\"}}";   // crosses the cap: cut
    putFile(fs, "Themes/Big.json", t);
    ThemeParse r;
    CHECK(loadThemeFile(fs, "Big", gSlots, &r));
    CHECK(r.truncated);
    CHECK_EQ(gSlots[slot::kGreen], 0x010101u);
    CHECK_EQ(gSlots[slot::kCyan], theme(0).slots[slot::kCyan]);
}

TEST(load_missing_file_fails_without_touching_table) {
    MemStorage fs;
    for (uint32_t& s : gSlots) s = 0xAAAAAAu;
    ThemeParse r;
    CHECK(!loadThemeFile(fs, "Nope", gSlots, &r));
    CHECK(!loadThemeFile(fs, "", gSlots, &r));
    CHECK(!loadThemeFile(fs, "../x", gSlots, &r));
    for (uint32_t s : gSlots) CHECK_EQ(s, 0xAAAAAAu);
    putFile(fs, "Themes/Plain.txt", "background = #FFFFFF\n");   // the old format: not read
    CHECK(!loadThemeFile(fs, "Plain", gSlots, &r));
    putFile(fs, "Themes/Empty.json", "");
    CHECK(loadThemeFile(fs, "Empty", gSlots, &r));
    CHECK(coloursAs(gSlots, 0));
}

TEST(list_themes_sorted_capped_json_only) {
    MemStorage fs;
    putFile(fs, "Themes/neon.json", "");
    putFile(fs, "Themes/Amber.JSON", "");
    putFile(fs, "Themes/paper.Json", "");
    putFile(fs, "Themes/old.txt", "");
    putFile(fs, "Themes/.json", "");
    putFile(fs, "Themes/json", "");
    putFile(fs, "Themes/abcdefghijklmnopqrstu.json", "");   // 21 characters: too long
    putFile(fs, "Themes/abcdefghijklmnopqrst.json", "");    // 20: fine
    putFile(fs, "Themes/a:b.json", "");
    putFile(fs, "Themes/caf\xC3\xA9.json", "");             // not printable ASCII
    putFile(fs, "Other/x.json", "");
    char names[kThemeListMax][kThemeNameMax + 1];
    int n = listThemeFiles(fs, names, kThemeListMax);
    CHECK_EQ(n, 4);
    CHECK_STR(names[0], "abcdefghijklmnopqrst");
    CHECK_STR(names[1], "Amber");
    CHECK_STR(names[2], "neon");
    CHECK_STR(names[3], "paper");
    CHECK(themeNameValid("My theme 2"));
    CHECK(!themeNameValid(""));
    CHECK(!themeNameValid("a/b"));
    CHECK(!themeNameValid("abcdefghijklmnopqrstu"));
    MemStorage many;
    for (int i = 39; i >= 0; --i) {
        char p[32];
        std::snprintf(p, sizeof p, "Themes/t%02d.json", i);
        putFile(many, p, "");
    }
    n = listThemeFiles(many, names, kThemeListMax);
    CHECK_EQ(n, kThemeListMax);
    CHECK_STR(names[0], "t00");
    CHECK_STR(names[kThemeListMax - 1], "t31");
    MemStorage none;
    CHECK_EQ(listThemeFiles(none, names, kThemeListMax), 0);
}

TEST(export_writes_ce_json_and_round_trips) {
    MemStorage fs;
    CHECK(writeThemeFile(fs, "Mono \"2\"", theme(1).slots, 1) == ThemeWrite::kOk);
    const std::string t = fileText(fs, "Themes/Mono \"2\".json");
    CHECK(t.find("{\n  \"name\": \"Mono \\\"2\\\"\",\n  \"base\": \"Monochrome\",\n  \"background\": \"#4b4b4b\",\n"
                 "  \"topBar\": {\n    \"text\": ") == 0);
    CHECK(t.find("  \"labels\": {\n    \"active\": {\n      \"text\": ") != std::string::npos);
    CHECK(t.find("  \"waveform\": {\n    \"audio\": \"#") != std::string::npos);
    CHECK(t.find("    \"progress\": {\n      \"text\": \"#") != std::string::npos);
    CHECK(t.find("miniMenu") == std::string::npos);   // no OpenTracker colour: not written
    CHECK(t.find("keyboard") == std::string::npos);
    CHECK(t.size() > 3000 && t.size() < kThemeFileMax);
    CHECK(t.substr(t.size() - 4) == "}\n}\n");
    // Reading it back keeps every exported colour; a second export is the same text.
    ThemeParse r;
    CHECK(loadThemeFile(fs, "Mono \"2\"", gSlots, &r));
    CHECK_EQ(r.base, 1);
    CHECK_EQ(r.badKeys, 0);
    CHECK(r.colours > 80);   // the keys with an OpenTracker colour (94 of 140)
    CHECK(writeThemeFile(fs, "Again", gSlots, 1) == ThemeWrite::kOk);
    std::string again = fileText(fs, "Themes/Again.json");
    CHECK(again.substr(again.find("\"base\"")) == t.substr(t.find("\"base\"")));
    // Exists: refused, the file stays.
    CHECK(writeThemeFile(fs, "Again", theme(0).slots, 0) == ThemeWrite::kExists);
    CHECK(fileText(fs, "Themes/Again.json") == again);
    CHECK(writeThemeFile(fs, "a/b", theme(0).slots, 0) == ThemeWrite::kFailed);
}

TEST(unreadable_text_guard) {
    // List text on its own background: both come back from the base.
    ThemeParse r = parse(R"({"popups": {"stepList": {"textNormal": "#000000"}}, "background": "#000000"})");
    CHECK(r.guarded);
    CHECK_EQ(gSlots[slot::kList + 4], theme(0).slots[slot::kList + 4]);
    CHECK_EQ(gSlots[slot::kList + 3], theme(0).slots[slot::kList + 3]);
    CHECK_EQ(gSlots[slot::kFrameClear], 0u);   // the rest applies
    r = parse(R"({"topBar": {"text": "#123456", "background": "#123456"}})");
    CHECK(r.guarded);
    CHECK_EQ(gSlots[slot::kTitle], theme(0).slots[slot::kTitle]);
    r = parse(kSample);
    CHECK(!r.guarded);
}

TEST(theme_view_reads_its_slots) {
    uint32_t s[kThemeSlotCount];
    parse(R"({"background": "#010203", "accents": {"red": "#040506"}, "patternTracker": {"note": "#070809"}})", s);
    Theme t = themeView("Mine", s);
    CHECK_STR(t.name, "Mine");
    CHECK_EQ(t.bg, 0x010203u);
    CHECK_EQ(t.accent, 0x040506u);
    CHECK_EQ(t.track[0], 0x070809u);
    CHECK(t.slots == s);
}

TEST(shipped_example_themes_parse_cleanly) {
    // themes/*.json (the release's Themes folder): every file parses with no
    // bad keys, keeps its text readable and differs from its base.
    const char* names[] = {"Amber", "Barbie", "Bubblegum", "C64",   "GameBoy", "Halloween",
                           "Lava",  "Matrix", "Neon",      "Ocean", "Paper",   "Vaporwave"};
    MemStorage fs;
    for (const char* n : names) {
        const std::string path = std::string("../themes/") + n + ".json";
        FILE* f = std::fopen(path.c_str(), "rb");
        CHECK(f != nullptr);
        if (!f) {
            std::printf("       missing %s\n", path.c_str());
            continue;
        }
        std::string t;
        char b[512];
        size_t k;
        while ((k = std::fread(b, 1, sizeof b, f)) > 0) t.append(b, k);
        std::fclose(f);
        putFile(fs, std::string("Themes/") + n + ".json", t);
        ThemeParse r;
        CHECK(loadThemeFile(fs, n, gSlots, &r));
        if (r.badKeys || r.guarded || r.truncated) std::printf("       %s: %d bad keys%s\n", n, r.badKeys, r.guarded ? ", guarded" : "");
        CHECK_EQ(r.badKeys, 0);
        CHECK(!r.guarded);
        CHECK(!r.truncated);
        CHECK(!coloursAs(gSlots, r.base));
        CHECK(gSlots[slot::kList + 4] != gSlots[slot::kFrameClear]);
    }
    char list[kThemeListMax][kThemeNameMax + 1];
    CHECK_EQ(listThemeFiles(fs, list, kThemeListMax), int(sizeof(names) / sizeof(names[0])));
}
