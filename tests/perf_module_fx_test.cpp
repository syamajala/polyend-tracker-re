// [I] #159 Performance Master FX column: a column that plays a parameter of
// a master effect module (IModulePage::perfPlay), set up in the FX type
// list like MIDI CC, never saved into the effect's settings.
#include <cstdio>
#include <cstring>
#include <memory>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"
#include "echo.h"
#include "formats/perf_bank.h"
#include "modules/module.h"
#include "modules/module_ui.h"
#include "ui/screens.h"

using namespace ot;

namespace {

mod::IModule& echoModule() {
    mod::ModuleHost& h = mod::ModuleHost::instance();
    h.init();   // idempotent
    return *const_cast<mod::IModule*>(h.find("ECHO"));
}
// What the Echo plays (the page's settings with the Performance rows over them).
audio::echo::Params echoPlays() { return static_cast<audio::echo::Echo*>(echoModule().masterInsert())->params(); }

struct Rig {
    FakeNvStore nv;
    std::unique_ptr<PerfPatternMemory> mem{new PerfPatternMemory};
    Harness h{false};
    Rig() {
        h.app->setNvStore(&nv);
        h.app->seq.setPerfPatternMemory(mem.get());
        h.app->boot();
        h.app->cfg.perfPresets = 1;
    }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
    void setRow(int pad, int value) {
        h.push(InputEvent::kPadDown, uint8_t(pad));
        h.enc(int8_t(value));
        h.push(InputEvent::kPadUp, uint8_t(pad));
    }
    // Edit mode: the edit column becomes Master FX on effect `id`, page row `row`.
    void pickFx(const char* id, int row) {
        h.tap(btn::Down);
        h.enc(int8_t(perf::kModuleFx - p().typeCursor));
        h.tap(btn::Right);
        App::MasterFx fx;
        int k = 0;
        while (h.app->masterFx(k, fx) && std::strcmp(fx.id, id) != 0) ++k;
        h.enc(int8_t(k - p().fxCursor_));
        h.tap(btn::Right);
        for (int k = 0; k < perf::kFxRows && p().fxRowCursor_ < row; ++k) h.enc(1);   // bounded: no rows = no hang
        h.tap(btn::F8);
    }
};

}  // namespace

TEST(module_fx_perf_play_is_not_saved) {
    mod::IModulePage* pg = echoModule().page();
    mod::IProjectSection* sec = echoModule().section();
    sec->reset();
    int lo = -1, hi = -1;
    CHECK(!pg->perfRange(0, lo, hi));   // the on row isn't playable
    CHECK(pg->perfRange(2, lo, hi));
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 100);
    CHECK(!sec->dirty());
    pg->perfPlay(2, 90);   // Feedback 90: on while it plays
    CHECK_EQ(echoPlays().feedback, 90);
    CHECK_EQ(echoPlays().on, 1);
    CHECK(!sec->dirty());
    mod::ModuleRow r{};
    pg->row(2, r);
    CHECK_STR(r.value, "45");   // the page keeps the setting
    CHECK(!mod::pageOn(pg));
    uint8_t buf[16];
    CHECK_EQ(sec->encode(buf, sizeof(buf)), 0u);   // Off at the defaults: nothing saved
    // A page edit while it plays changes the setting under the played row.
    pg->adjust(7, 5);   // Mix 35 -> 40
    CHECK_EQ(echoPlays().mix, 40);
    CHECK_EQ(echoPlays().feedback, 90);
    pg->perfPlay(2, 200);   // clamped to the range
    CHECK_EQ(echoPlays().feedback, 100);
    pg->perfPlay(2, -1);
    CHECK_EQ(echoPlays().feedback, 45);
    CHECK_EQ(echoPlays().on, 0);
    char t[24];
    pg->perfText(1, 7, t, sizeof(t));   // Time: as the page names it
    CHECK_STR(t, audio::echo::kTimeName[7]);
    sec->reset();
}

TEST(module_fx_column_plays_the_parameter) {
    Rig r;
    App& a = *r.h.app;
    mod::IModulePage* pg = echoModule().page();
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.pickFx("ECHO", 2);
    CHECK(!p.typeList);
    CHECK_EQ(p.src.type(0), perf::kModuleFx);
    CHECK_EQ(p.src.target(0), perf::fxTarget(mod::ModuleHost::chainRank("ECHO"), 2));
    CHECK_EQ(a.project.perfExtType[0], perf::kModuleFx);
    char nm[32];
    p.columnName(0, nm, sizeof(nm));
    CHECK_STR(nm, "Echo Feedback");
    r.setRow(12, 1);    // row 1: Feedback 0 (the first step from off)
    r.setRow(24, 61);   // row 2: Feedback 60
    r.h.tap(btn::Rec);
    CHECK_EQ(p.src.value(0, 1), 1);
    CHECK_EQ(p.src.value(0, 2), 61);
    r.h.pad(24);
    CHECK_EQ(echoPlays().feedback, 60);
    CHECK_EQ(echoPlays().on, 1);
    CHECK(!mod::pageOn(pg));   // the FX bank still says Off
    r.h.pad(12);
    CHECK_EQ(echoPlays().feedback, 0);
    r.h.pad(0);   // the off row: released, the repeats play out
    CHECK_EQ(echoPlays().feedback, 45);
    CHECK_EQ(echoPlays().on, 0);
    // Leaving the screen releases it too.
    r.h.pad(24);
    CHECK_EQ(echoPlays().on, 1);
    r.h.tap(btn::Pattern);
    CHECK_EQ(echoPlays().on, 0);
}

TEST(module_fx_column_scenes_switch_on_at_the_middle) {
    Rig r;
    App& a = *r.h.app;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.tap(btn::Rec);
    r.pickFx("ECHO", 7);   // Mix
    r.setRow(12, 81);      // row 1: Mix 80
    r.h.tap(btn::Rec);
    // Scene A without the column, scene B with row 1.
    r.h.pad(0);
    r.h.down(btn::Shift);
    r.h.tap(btn::Left);
    r.h.pad(12);
    r.h.tap(btn::Right);
    r.h.up(btn::Shift);
    r.h.pad(0);
    CHECK_EQ(echoPlays().on, 0);
    p.setFader(40);
    CHECK_EQ(echoPlays().on, 0);
    p.setFader(60);   // past the middle: B's Mix 80
    CHECK_EQ(echoPlays().on, 1);
    CHECK_EQ(echoPlays().mix, 80);
    p.setFader(0);
    CHECK_EQ(echoPlays().on, 0);
    CHECK_EQ(echoPlays().mix, 35);
    (void)a;
}

TEST(module_fx_column_in_bank_files) {
    PerfBank b{};
    b.type[4] = perf::kModuleFx;
    b.target[4] = perf::fxTarget(mod::ModuleHost::chainRank("ECHO"), 2);
    b.value[4][0] = 61;   // Feedback 60
    b.value[4][1] = 0;    // off
    b.value[4][2] = 1;    // Feedback 0
    static char text[4096];
    const size_t n = encodePerfBank(b, text, sizeof(text));
    CHECK(n > 0);
    CHECK(std::strstr(text, "column 5 = Master FX: 60 - 0 @ fx ECHO 2\n") != nullptr);
    PerfBank d{};
    CHECK(decodePerfBank(text, n, d));
    CHECK_EQ(d.type[4], perf::kModuleFx);
    CHECK_EQ(d.target[4], b.target[4]);
    CHECK_EQ(d.value[4][0], 61);
    CHECK_EQ(d.value[4][1], 0);
    CHECK_EQ(d.value[4][2], 1);
}
