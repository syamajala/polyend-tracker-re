// [I] Config's help texts for the host tests: help/Config.md of the repository
// (the card's Help/Config.md, make help), read through modules/synth_help.h.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "modules/synth_help.h"
#include "ui/screens.h"

inline const std::string& configHelpFile() {
    static const std::string text = [] {
        const char* fx = std::getenv("OT_FIXTURES");   // tests/fixtures
        std::string path = std::string(fx ? fx : "fixtures") + "/../../help/Config.md", out;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
            std::fclose(f);
        }
        return out;
    }();
    return text;
}

// The help text of option i of Config category cat ("" when it has none).
inline std::string configHelpOf(int cat, int i) {
    ot::ConfigScreen::OptionInfo o;
    if (!ot::ConfigScreen::optionInfo(cat, i, &o)) return "";
    char out[ot::synthhelp::kMaxLine];
    const std::string& t = configHelpFile();
    ot::synthhelp::find(t.data(), uint32_t(t.size()), ot::ConfigScreen::categoryName(cat), o.name, nullptr, out,
                        sizeof out);
    return out;
}
