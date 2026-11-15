// Synth help files (modules/synth_help.h): the Markdown subset reader, and
// every help file of the profile's modules against its engine's descriptor.
// $OT_HELP_DIRS (tests/Makefile): the profile modules' folders, ':' separated;
// each may hold help/<NAME>.md for its engines.
#include "test.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "modules/module.h"
#include "modules/synth_descriptor.h"
#include "modules/synth_help.h"
#include "ui/gfx.h"

using namespace ot;
namespace sh = ot::synthhelp;

namespace {

const char kSample[] =
    "# Demo\n"
    "A demo engine: the guide, for the docs only.\n"
    "\n"
    "## Osc\n"
    "A guide heading, not the reference.\n"
    "```\n"
    "# Settings\n"
    "```\n"
    "# Settings\n"
    "\n"
    "## Extra\n"
    "\n"
    "#### Try this\n"
    "## Not a section, inside the docs-only part? No: a ## ends it.\n"
    "\n"
    "## Osc\n"
    "\n"
    "The oscillator,\n"
    "on two source lines.\n"
    "\n"
    "### Wave\n"
    "The wave shape.\n"
    "\n"
    "- Saw: bright\n"
    "  and buzzy.\n"
    "- Square: hollow: like a clarinet.\n"
    "\n"
    "```\n"
    "## not a heading\n"
    "```\n"
    "### Level\r\n"
    "| a table first |\r\n"
    "\r\n"
    "- low is quiet: high is loud\r\n"
    "# Appendix\n"
    "## Osc\n"
    "After the reference: docs only.\n";

std::string findStr(const char* sec, const char* set, const char* choice) {
    char out[sh::kMaxLine];
    const int n = sh::find(kSample, sizeof(kSample) - 1, sec, set, choice, out, sizeof(out));
    CHECK_EQ(int(std::strlen(out)), n);
    return out;
}

}  // namespace

TEST(reader_lines) {
    CHECK(findStr("Osc", nullptr, nullptr) == "The oscillator, on two source lines.");
    CHECK(findStr("Osc", "Wave", nullptr) == "The wave shape.");
    CHECK(findStr("Osc", "Wave", "Saw") == "bright and buzzy.");
    CHECK(findStr("Osc", "Wave", "Square") == "hollow: like a clarinet.");
    CHECK(findStr("Osc", "Wave", "Sine") == "");
    CHECK(findStr("Osc", "Wave", "") == "Saw: bright and buzzy.");   // the first item, whole
    CHECK(findStr("Osc", "Level", nullptr) == "");                     // a table first: no line
    CHECK(findStr("Osc", "Level", "") == "low is quiet: high is loud");
    CHECK(findStr("Wave", nullptr, nullptr) == "");                    // a setting is no section
    CHECK(findStr("Not a section, inside the docs-only part? No: a ## ends it.", nullptr, nullptr) == "");
    // Without "# Settings" the whole text is the reference (a hand-made card file).
    const char bare[] = "## Osc\nThe oscillator.\n";
    char out[64];
    CHECK_EQ(sh::find(bare, sizeof(bare) - 1, "Osc", nullptr, nullptr, out, sizeof(out)), 15);
}

TEST(reader_walk_order) {
    struct Got {
        std::vector<std::string> v;
    } got;
    char buf[64];
    sh::walk(kSample, sizeof(kSample) - 1, buf, sizeof(buf),
             [](void* ctx, const sh::Entry& e) {
                 static_cast<Got*>(ctx)->v.push_back(std::to_string(e.level) + ":" +
                                                      std::string(e.name, size_t(e.nameLen)) + "|" +
                                                      std::string(e.line, size_t(e.lineLen)));
             },
             &got);
    const std::vector<std::string> want = {
        "2:Extra|",
        "2:Not a section, inside the docs-only part? No: a ## ends it.|",
        "2:Osc|The oscillator, on two source lines.",
        "3:Wave|The wave shape.",
        "4:|Saw: bright and buzzy.",
        "4:|Square: hollow: like a clarinet.",
        "3:Level|",
        "4:|low is quiet: high is loud",
    };
    CHECK_EQ(got.v.size(), want.size());
    for (size_t i = 0; i < want.size() && i < got.v.size(); ++i) {
        if (got.v[i] != want[i]) std::printf("  entry %zu: '%s' want '%s'\n", i, got.v[i].c_str(), want[i].c_str());
        CHECK(got.v[i] == want[i]);
    }
}

TEST(reader_cut_and_empty) {
    char out[8];
    CHECK_EQ(sh::find(kSample, sizeof(kSample) - 1, "Osc", "Wave", nullptr, out, sizeof(out)), 7);
    CHECK(std::strcmp(out, "The wav") == 0);
    CHECK_EQ(sh::find("", 0, "Osc", nullptr, nullptr, out, sizeof(out)), 0);
    CHECK_EQ(out[0], '\0');
    CHECK_EQ(sh::splitChoice("1.79 MHz: fast", 14), 8);
    CHECK_EQ(sh::splitChoice("no name", 7), -1);
}

// ---------------------------------------------------------------------------
// The profile's help files against the descriptors.

namespace {

bool readFile(const std::string& path, std::string* out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char tmp[4096];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) out->append(tmp, n);
    std::fclose(f);
    return true;
}

const char* const kOnOff[2] = {"Off", "On"};

// A display that binds the custom fonts (the widths of the generated fonts).
struct FontDisplay : IDisplay {
    uint16_t width() const override { return 800; }
    uint16_t height() const override { return 480; }
    void dlStart() override {}
    void clearColorRGB(uint8_t, uint8_t, uint8_t) override {}
    void clear(bool, bool, bool) override {}
    void colorRGB(uint8_t, uint8_t, uint8_t) override {}
    void colorA(uint8_t) override {}
    void lineWidth(uint16_t) override {}
    void pointSize(uint16_t) override {}
    void scissorXY(uint16_t, uint16_t) override {}
    void scissorSize(uint16_t, uint16_t) override {}
    void begin(Prim) override {}
    void end() override {}
    void vertex2f(int32_t, int32_t) override {}
    void vertex2ii(uint16_t, uint16_t, uint8_t, uint8_t) override {}
    void text(int16_t, int16_t, uint8_t, uint16_t, const char*) override {}
    void number(int16_t, int16_t, uint8_t, uint16_t, int32_t) override {}
    void swap() override {}
    bool bindFontSet(int) override { return true; }
};

// The widest the help box draws `s`: both font sets, ROM and custom fonts.
int helpWidth(const char* s) {
    FontDisplay d;
    int w = 0;
    for (uint8_t set = 0; set < kFontSetCount; ++set)
        for (IDisplay* disp : {static_cast<IDisplay*>(nullptr), static_cast<IDisplay*>(&d)}) {
            applyFontSet(disp, set);
            const int x = textWidth(sh::kHelpFont, s);
            w = x > w ? x : w;
        }
    applyFontSet(nullptr, 0);
    return w;
}

// Checks one help file: names, order-free coverage, widths and markup.
struct Lint {
    const mod::SynthDescriptor* d = nullptr;
    std::string path;
    int errors = 0;
    const synthui::Section* sec = nullptr;
    int param = -1;                       // the current setting's parameter, -1 none
    std::vector<std::string> seen;        // "section/setting/choice" keys
    int tips = 0;                         // items of the current number setting
    bool macros = false;                  // under "## Macros"

    void error(int line, const std::string& msg) {
        std::printf("  %s:%d: %s\n", path.c_str(), line, msg.c_str());
        ++errors;
    }
    // The names of a setting's choices (enum or Off / On), nullptr for a number.
    const char* const* choices(int p, int* n) const {
        const synthui::ParamUi& u = d->ui[p];
        if (u.fmt == synthui::kFmtEnum) return *n = u.nameCount, u.names;
        if (u.fmt == synthui::kFmtOnOff) return *n = 2, kOnOff;
        return *n = 0, nullptr;
    }
    void checkLine(const sh::Entry& e, const std::string& name, const std::string& line) {
        if (line.empty()) return error(e.lineNo, "'" + name + "': no device line (the paragraph or item text)");
        if (line.find_first_of("*`[]<>|_") != std::string::npos)
            error(e.lineNo, "'" + name + "': markup in the device line");
        const int w = helpWidth(name.c_str()) + sh::kHelpNameGap + helpWidth(line.c_str());
        if (w > sh::kHelpLineW)
            error(e.lineNo, "'" + name + "': device line " + std::to_string(w) + " px > " +
                                std::to_string(sh::kHelpLineW) + " px of the help box");
    }
    void once(int line, const std::string& key) {
        for (const std::string& s : seen)
            if (s == key) return error(line, "'" + key + "' twice");
        seen.push_back(key);
    }

    void visit(const sh::Entry& e) {
        const std::string name(e.name, size_t(e.nameLen)), line(e.line, size_t(e.lineLen));
        switch (e.level) {
            case sh::kSection:
                sec = nullptr, param = -1;
                if (name == "Macros") {   // [I] the Macros section's line (#176), optional, no settings
                    once(e.lineNo, name), checkLine(e, name, line);
                    macros = true;
                    break;
                }
                macros = false;
                for (int i = 0; i < d->sectionCount; ++i)
                    if (d->sections[i].macro < 0 && name == d->sections[i].longName) sec = &d->sections[i];
                if (!sec) error(e.lineNo, "## '" + name + "': no such section");
                else once(e.lineNo, name), checkLine(e, name, line);
                break;
            case sh::kSetting:
                param = -1, tips = 0;
                if (macros) {
                    error(e.lineNo, "### '" + name + "': the Macros section has no settings");
                    break;
                }
                if (!sec) {
                    error(e.lineNo, "### '" + name + "' outside a section");
                    break;
                }
                for (int i = 0; i < sec->count; ++i)
                    if (d->ui[sec->params[i]].label && name == d->ui[sec->params[i]].label) param = sec->params[i];
                if (param < 0) error(e.lineNo, "### '" + name + "': no such setting in " + sec->longName);
                else once(e.lineNo, std::string(sec->longName) + "/" + name), checkLine(e, name, line);
                break;
            case sh::kChoice: {
                if (param < 0) {
                    error(e.lineNo, "a list item outside a setting (docs-only lists go under ####)");
                    break;
                }
                int n;
                const char* const* names = choices(param, &n);
                const std::string key = std::string(sec->longName) + "/" + d->ui[param].label + "/";
                if (!names) {   // a number setting: one tip
                    if (++tips > 1) error(e.lineNo, "a number setting takes one item (its tip)");
                    once(e.lineNo, key);
                    checkLine(e, "", line);
                    break;
                }
                const int k = sh::splitChoice(e.line, e.lineLen);
                const std::string choice = k < 0 ? "" : line.substr(0, size_t(k));
                bool ok = false;
                for (int i = 0; i < n; ++i) ok |= choice == names[i];
                if (!ok) {
                    error(e.lineNo, "'" + line.substr(0, 24) + "': no such choice of " + d->ui[param].label +
                                        " (write '- Name: text')");
                    break;
                }
                once(e.lineNo, key + choice);
                checkLine(e, choice, k < 0 ? "" : line.substr(size_t(k) + 2));
                break;
            }
        }
    }
    // Every section, setting and choice of the engine has a line.
    void coverage() {
        auto has = [&](const std::string& key) {
            for (const std::string& s : seen)
                if (s == key) return true;
            return false;
        };
        for (int i = 0; i < d->sectionCount; ++i) {
            const synthui::Section& s = d->sections[i];
            if (s.macro >= 0) continue;   // macros: their own help later (#176)
            if (!has(s.longName)) error(0, std::string("missing ## ") + s.longName);
            for (int j = 0; j < s.count; ++j) {
                const int p = s.params[j];
                if (!d->ui[p].label) continue;
                const std::string key = std::string(s.longName) + "/" + d->ui[p].label;
                if (!has(key)) error(0, "missing ### " + key);
                int n;
                const char* const* names = choices(p, &n);
                if (!names && !has(key + "/")) error(0, "missing the tip item of " + key);
                for (int c = 0; names && c < n; ++c)
                    if (!has(key + "/" + names[c])) error(0, "missing '- " + std::string(names[c]) + ":' of " + key);
            }
        }
    }
};

std::vector<std::string> helpDirs() {
    std::vector<std::string> v;
    const char* env = std::getenv("OT_HELP_DIRS");
    const std::string s = env ? env : "";
    size_t a = 0;
    while (a < s.size()) {
        size_t b = s.find(':', a);
        if (b == std::string::npos) b = s.size();
        if (b > a) v.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return v;
}

}  // namespace

TEST(help_files_match_descriptors) {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();
    const std::vector<std::string> dirs = helpDirs();
    int files = 0;
    for (int k = 0; k < h.engineCount(); ++k) {
        const mod::SynthDescriptor* d = h.engineAt(k);
        for (const std::string& dir : dirs) {
            Lint lint;
            lint.d = d;
            lint.path = dir + "/help/" + d->name + ".md";
            std::string text;
            if (!readFile(lint.path, &text)) continue;
            ++files;
            // The guide's title is the engine; the reference follows "# Settings".
            const char* want = d->displayName ? d->displayName : d->name;
            if (text.compare(0, text.find('\n'), std::string("# ") + want) != 0)
                lint.error(1, std::string("the first line is not '# ") + want + "'");
            if (text.find("\n# Settings\n") == std::string::npos) lint.error(0, "no '# Settings' reference");
            char buf[sh::kMaxLine];
            sh::walk(text.data(), uint32_t(text.size()), buf, sizeof(buf),
                     [](void* ctx, const sh::Entry& e) { static_cast<Lint*>(ctx)->visit(e); }, &lint);
            lint.coverage();
            CHECK_EQ(lint.errors, 0);
        }
    }
    std::printf("  %d help file(s) checked\n", files);
}
