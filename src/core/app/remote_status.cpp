// [I] Remote drive mode, App side (core/remote/remote.h): the status line a
// host script reads back, with the performance numbers of the running device.
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "build_id.h"
#include "remote/display_tap.h"
#include "remote/remote.h"

namespace ot {

namespace {
// Appends " key=value", spaces in the value as '_'.
void field(char* out, int cap, int& n, const char* key, const char* value) {
    if (n >= cap - 1) return;
    int k = std::snprintf(out + n, size_t(cap - n), " %s=", key);
    if (k < 0 || n + k >= cap) return;
    n += k;
    for (const char* v = value; *v && n < cap - 1; ++v) out[n++] = *v == ' ' ? '_' : *v;
    out[n] = 0;
}
void fieldInt(char* out, int cap, int& n, const char* key, long value) {
    char v[16];
    std::snprintf(v, sizeof v, "%ld", value);
    field(out, cap, n, key, v);
}
}  // namespace

int App::remoteStatus(char* out, int cap) {
    int n = std::snprintf(out, size_t(cap), "@RC S");
    if (n < 0 || n >= cap) return 0;
    const Screen* s = ui.current();
    field(out, cap, n, "scr", s ? s->name() : "-");
    fieldInt(out, cap, n, "play", seq.isPlaying() ? 1 : 0);
    fieldInt(out, cap, n, "step", seq.step(0));
    fieldInt(out, cap, n, "pat", patternNumber());
    fieldInt(out, cap, n, "ins", currentInstrument() + 1);
    // [I] Song mode: on / off, the playing slot (1-based) and its pass (#10).
    fieldInt(out, cap, n, "song", seq.isSongMode() ? 1 : 0);
    fieldInt(out, cap, n, "slot", seq.songPosition() + 1);
    fieldInt(out, cap, n, "pass", seq.songPass());
    // [I] Performance FX on (perf mode, any screen) and the scenes on A / B (1-based).
    fieldInt(out, cap, n, "perf", seq.performanceMode() ? 1 : 0);
    fieldInt(out, cap, n, "scA", project.perfSceneOn[0] + 1);
    fieldInt(out, cap, n, "scB", project.perfSceneOn[1] + 1);
    fieldInt(out, cap, n, "fader", perfCc ? perfCc->perfFader() : 0);
    fieldInt(out, cap, n, "peak", engine.takeOutputPeak());
    // Performance: render load per block in permille of the budget (average,
    // peak of the last ~1-2 s), overrun blocks since boot, the synth voice
    // governor (cap / budget, reductions), free sample memory in KiB.
    const audio::CpuMeter& c = engine.cpu();
    fieldInt(out, cap, n, "cpuA", c.hasData() ? long(c.avg()) : -1);
    fieldInt(out, cap, n, "cpuP", c.hasData() ? long(c.peak()) : -1);
    fieldInt(out, cap, n, "ovr", long(c.overruns()));
    fieldInt(out, cap, n, "gov", engine.governor().cap());
    fieldInt(out, cap, n, "govB", engine.governor().budget());
    fieldInt(out, cap, n, "cuts", long(engine.governor().cuts()));
    fieldInt(out, cap, n, "freeKB", long(bank.freeSamples() * 2 / 1024));
    // [I] USB audio in (#97): packets, under-runs, drops, ring fill (frames),
    // feedback (10.14), playing.
    if (hal_.usbInStats) {
        HalRefs::UsbInStats u{};
        hal_.usbInStats(&u);
        fieldInt(out, cap, n, "usbPk", long(u.packets));
        fieldInt(out, cap, n, "usbUr", long(u.underruns));
        fieldInt(out, cap, n, "usbDr", long(u.dropped));
        fieldInt(out, cap, n, "usbFill", long(u.fill));
        fieldInt(out, cap, n, "usbFb", long(u.feedback));
        fieldInt(out, cap, n, "usbOn", u.playing ? 1 : 0);
    }
    // [I] USB audio out (#97 step 2): packets, under-runs, blocks dropped,
    // ring fill (frames), packet step (10.14), sending audio.
    if (hal_.usbOutStats) {
        HalRefs::UsbOutStats u{};
        hal_.usbOutStats(&u);
        fieldInt(out, cap, n, "usbOPk", long(u.packets));
        fieldInt(out, cap, n, "usbOUr", long(u.underruns));
        fieldInt(out, cap, n, "usbOOv", long(u.overflows));
        fieldInt(out, cap, n, "usbOFill", long(u.fill));
        fieldInt(out, cap, n, "usbOStep", long(u.step));
        fieldInt(out, cap, n, "usbOOn", u.playing ? 1 : 0);
    }
    // A confirm dialog is open (Ui::ask: "Unsaved changes", "This will stop
    // the pattern", ...), so scripts answer it without matching its wording.
    fieldInt(out, cap, n, "ask", ui.askOpen() ? 1 : 0);
    // The git commit of this image (#60), so logs trace back to the code.
    field(out, cap, n, "build", kBuildHash);
    field(out, cap, n, "note", ui.notice()[0] ? ui.notice() : "-");
    return n;
}

void App::serviceRemote() {
    remote::DeviceBridge* r = hal_.remote;
    if (!r) return;
    r->setDisplay(hal_.display);
    engine.setOutputMeter(r->driving());
    if (r->driving() != remoteDrivingShown_) {
        remoteDrivingShown_ = r->driving();
        ui.dirty();
    }
    // Display command export: a recorded frame is drawn with the fragment
    // cache off (its CMD_APPENDs would leave RAM_G copies out of the stream).
    if (remote::DisplayTap* t = r->tap()) {
        if (t->requested()) {
            if (!remoteCacheOff_) {
                remoteCacheOff_ = true;
                remoteCacheWas_ = ui.fragments.enabled;
                ui.fragments.enabled = false;
            }
            t->arm();
        } else if (remoteCacheOff_ && !t->busy()) {
            remoteCacheOff_ = false;
            ui.fragments.enabled = remoteCacheWas_;
        }
    }
    if (r->takeRedraw()) ui.dirty();
    if (r->takeQuery()) {
        char s[remote::kLineMax + 352];   // status fields (~340 B with build= and usb*) + a notice of up to 63 chars
        remoteStatus(s, sizeof s);
        r->sendLine(s);
    }
}

}  // namespace ot
