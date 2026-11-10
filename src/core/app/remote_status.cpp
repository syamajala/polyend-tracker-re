// [I] Remote drive mode, App side (core/remote/remote.h): the status line a
// host script reads back, with the performance numbers of the running device.
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app/project_io.h"
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
    {   // [I] the open project, <folder>/<name> (spaces as '_')
        char p[mt::kFolderMax + mt::kNameMax + 2];
        projectPath(p, sizeof p);
        field(out, cap, n, "proj", p[0] ? p : "-");
    }
    // The git commit of this image (#60), so logs trace back to the code.
    field(out, cap, n, "build", kBuildHash);
    field(out, cap, n, "note", ui.notice()[0] ? ui.notice() : "-");
    return n;
}

namespace {
// "@RC DO OK <word>", or "@RC DO FAIL <word> <why>" when there is a reason.
void callAnswer(App& a, const char* word, const char* why = nullptr) {
    remote::DeviceBridge* r = a.remoteBridge();
    if (!r) return;
    char line[40];
    std::snprintf(line, sizeof line, why ? "@RC DO FAIL %s %s" : "@RC DO OK %s", word, why);
    r->sendLine(line);
}
}  // namespace

// [I] The calls a test script would otherwise make with key taps (File >
// Open in a browser, the PLAY key), so a script needs no screen layout.
void App::remoteCall(const char* cmd) {
    const bool play = !std::strcmp(cmd, "PLAY");
    if (play || !std::strcmp(cmd, "STOP")) {
        // The PLAY key's own path (song mode, the count-in ...), only when it changes something.
        if (seq.isPlaying() != play) togglePlay(false);
        return callAnswer(*this, play ? "PLAY" : "STOP");
    }
    if (std::strncmp(cmd, "OPEN ", 5)) return callAnswer(*this, "-", "unknown");
    const char* path = cmd + 5;
    const bool force = !std::strncmp(path, "-f ", 3);
    if (force) path += 3;
    char folder[mt::kFolderMax + 1], name[mt::kNameMax + 1];
    if (!splitProjectPath(path, folder, sizeof folder, name, sizeof name)) return callAnswer(*this, "OPEN", "path");
    // What File > Open would ask about first: never answered for the user. A
    // running job (an autosave) finishes first, as for File > Open.
    if (rendering_ || usbActive_) return callAnswer(*this, "OPEN", "busy");
    if (ui.askOpen()) return callAnswer(*this, "OPEN", "dialog");
    if (unsavedChanges() && !force) return callAnswer(*this, "OPEN", "unsaved");
    // The open job stops playback itself.
    const auto done = [](void* ctx, bool ok) {
        App& a = *static_cast<App*>(ctx);
        a.showOpened(ok);   // as File > Open
        callAnswer(a, "OPEN", ok ? nullptr : "load");
    };
    if (!startOpenProject(folder, name, done, this)) callAnswer(*this, "OPEN", "missing");
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
    {
        char call[remote::kLineMax + 1];
        if (r->takeCall(call, sizeof call)) remoteCall(call);
    }
    if (r->takeQuery()) {
        // status fields (~340 B with build= and usb*), a notice of up to 63
        // chars and proj= (a longer path is cut short)
        char s[remote::kLineMax + 480];
        remoteStatus(s, sizeof s);
        r->sendLine(s);
    }
}

}  // namespace ot
