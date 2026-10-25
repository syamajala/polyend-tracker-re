// Performance (spec 06 §3, spec 12 §A-§E).
//
// Presets come from one of two sources (spec 12 §A): the global presets in the
// config image (EEPROM) or the per-project presets in project.mt, chosen by
// the Config option at screen start. The engine-side state is the current
// value per slot (cur); it is pushed to the audio engine offsets
// (audio::PerfOffsets) and the sequencer's performance hooks (step repeater,
// play mode, pattern length, MIDI velocity/transpose, spec 03 §2.6).
// Per-track patterns go through Sequencer::setTrackPattern (spec 12 §C).
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app/perf_presets.h"
#include "formats/perf_scenes.h"
#include "ui/screens.h"

namespace ot {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr uint32_t kCcPollMs = 50;      // spec 06 §3.4 update 0x436b4
constexpr uint32_t kLabelPollMs = 100;
// Text anchors of the FX panels (centre y; vertical label bottom) [S snap 021].
constexpr int kTitleValueY = 53;
constexpr int kRowY = 298;
constexpr int kLabelBottom = 266;
constexpr int kLabelDx = 2;
// [I] Scene marks: 3-px bars inside the 63-px panel, 5 px from its edges
// (closer to the edge, a B bar reads as the next panel's A: the panels
// are only 2 px apart).
constexpr int kSceneBarW = 3;
constexpr int kSceneBarLeftDx = 5;
constexpr int kSceneBarRightDx = 63 - 5 - kSceneBarW;
constexpr int kSceneBarH = 21;      // full height: the row's cell y - 10 .. y + 10
constexpr int kSceneBarMinH = 5;    // a scene out of the mix
// Scene A in the theme's red accent, B in blue (the stock themes have no
// blue; a lighter one than 0000ff, which barely shows on the dark panels).
constexpr uint32_t kSceneBBlue = 0x3C8CFF;
// Cell text: perf::valueText, with a zero shown as "---" [S snap 021].
void cellText(uint8_t ty, int v, char* out, int cap) {
    perf::valueText(ty, v, out, cap);
    if (std::strcmp(out, perf::kZeroText) == 0) std::snprintf(out, size_t(cap), "---");
}
// One cell centred on (cx, cy) in font 2; "---" is three 6 x 1 dashes at a
// 10-px pitch on row cy + 1 (the stock 0.5-px glyph strokes) [S snap 021].
void cell(Gfx& g, int cx, int cy, uint32_t c, const char* v) {
    if (std::strcmp(v, "---") != 0) {
        g.text(cx, cy, kFont2, c, v, IDisplay::kOptCenter);
        return;
    }
    g.alpha(222);
    g.rectsBegin(c);
    for (int k = 0; k < 3; ++k) g.rectsAdd(cx - 14 + 10 * k, cy + 1, 6, 1);
    g.rectsEnd();
    g.alpha(255);
}
}  // namespace

// ---------------------------------------------------------------------------
// start / stop (spec 06 §3.1, spec 12 §A.5, §C.4)

void PerformanceScreen::start(int opt) {
    (void)opt;
    App& a = app_;
    // spec 16 §A.15: coming back while the FX were kept alive resumes
    // without resetting the rows.
    if (keepAlive_ != KeepAlive::None) {
        keepAlive_ = KeepAlive::None;
        lastPattern_ = a.patternNumber();
        labelsDirty_ = false;
        refreshBar();
        return;
    }
    engage();
    // Per-track patterns := the current pattern (spec 06 §3.1 step 1).
    const uint8_t curPat = a.patternNumber();
    for (int t = 0; t < 8; ++t) {
        perf::setTrackPattern(a.project, t, curPat);
        keyHeld_[t] = keyUsed_[t] = false;
        trackCcPending_[t] = false;
    }
    lastPattern_ = curPat;
    edit = false;
    copySrc_ = -1;
    sceneCcPending_ = false;
    view = View::Play;
    editScene = -1;
    labelsDirty_ = false;
    refreshBar();
}

void PerformanceScreen::engage() {
    App& a = app_;
    // Rows of the previously selected source are cleared before the source
    // pointers switch (spec 12 §A.5 quirk: a newly selected source keeps its
    // stale rows; they are not applied). The engine starts on source 0.
    if (!src.valid()) src = perf::globalSource(a.cfg);
    for (int s = 0; s < perf::kSlots; ++s) src.setRow(s, 0);
    sourceIndex = a.cfg.perfPresets < 2 ? a.cfg.perfPresets : sourceIndex;   // spec 12 §A.3
    src = perf::select(sourceIndex, a.cfg, a.project);
    src.validate();
    for (int s = 0; s < perf::kSlots; ++s) {
        const int r = src.row(s);
        cur[s] = (r >= 1 && r <= 3) ? src.value(s, r) : 0;   // recomputed, not applied
        live[s] = 0;
        liveValid[s] = false;
        held_[s] = false;
        ccPending_[s] = false;
    }
    // Step 3: FX enabled on every track. 2.x tracks 9-16 take the FX too
    // but have no pattern override (the Tracker Mini stores 1-8 only).
    // [I] #54: the toggles are remembered (Project::perfFxOff, saved with
    // the project) instead of every track starting on.
    for (int t = 0; t < kTrackCount; ++t) {
        fxOn[t] = !((a.project.perfFxOff >> t) & 1u);
        pending_[t] = 0;
    }
    fxKey_ = -1;
    fader = 0;   // [I] scenes: the crossfader starts at A, applied on its first move
    a.seq.setPerformanceMode(true);   // step 4
}

void PerformanceScreen::stop() {
    App& a = app_;
    for (auto& h : held_) h = false;
    if (view == View::EditScene) editSceneEnd();   // [I] leaving saves the scene being edited
    view = View::Play;
    // spec 16 §A.15: switching to Instrument Params or Master keeps the FX
    // live indefinitely; to any other screen while playing they stay live
    // until the next pattern end (or Stop); stopped, they revert at once.
    // The switch happens on the screen key's press, so it is still down.
    if (songSceneNow()) {
        keepAlive_ = KeepAlive::Song;   // [I] the song's scene plays on
    } else if (ui_.isDown(btn::InstrParams) || ui_.isDown(btn::Master)) {
        keepAlive_ = KeepAlive::Always;
    } else if (a.seq.isPlaying()) {
        keepAlive_ = KeepAlive::UntilPatternEnd;
        keepEnds_ = a.patternEndCount();
    } else {
        revert();
    }
    leave();
}

void PerformanceScreen::perfBackground(uint32_t now) {
    (void)now;
    App& a = app_;
    if (keepAlive_ == KeepAlive::Song && !(a.seq.isPlaying() && a.seq.isSongMode())) {
        keepAlive_ = KeepAlive::None;   // [I] Stop (or the song left) ends the song's scenes
        revert();
        return;
    }
    if (keepAlive_ != KeepAlive::UntilPatternEnd) return;
    if (!a.seq.isPlaying() || a.patternEndCount() != keepEnds_) {
        keepAlive_ = KeepAlive::None;
        revert();
    }
}

// spec 16 §A.15: the stock screen-key handler runs on every screen key while
// the FX are live, not only when leaving the Performance screen. From
// Instrument Params / Master: Instrument Params or Master keep them;
// the Performance key comes back and resumes; any other screen key reverts
// at once when stopped, and while playing
// keeps them until the next pattern end (or Stop).
void PerformanceScreen::perfScreenKey(uint8_t id) {
    if (keepAlive_ != KeepAlive::Always || id == btn::InstrParams || id == btn::Master || id == btn::Performance) return;
    App& a = app_;
    if (a.seq.isPlaying()) {
        keepAlive_ = KeepAlive::UntilPatternEnd;
        keepEnds_ = a.patternEndCount();
    } else {
        keepAlive_ = KeepAlive::None;
        revert();
    }
}

// Exports and renders run without Performance FX: stock reverts them on any
// other screen while stopped (spec 16 §A.15), and a render always stops the
// transport first.
void PerformanceScreen::perfRevertKept() {
    if (keepAlive_ == KeepAlive::None) return;
    keepAlive_ = KeepAlive::None;
    revert();
}

// [I] Song slot scenes (#10). A slot with a scene puts it on A (and its
// second scene on B), the fader at A, on any screen; elsewhere the FX stay
// live until Stop or a slot without a scene. A slot without one keeps what
// plays: on this screen, everything; elsewhere the song's scene goes.
bool PerformanceScreen::songSceneNow() const {
    const App& a = app_;
    return a.seq.isPlaying() && a.seq.isSongMode() && a.project.slotScene(a.seq.songPosition());
}

void PerformanceScreen::perfSongSlot(int slot) {
    App& a = app_;
    const int sc = a.project.slotScene(slot), to = a.project.slotSceneTo(slot);
    const bool shown = ui_.active() == kModPerformance;
    songRamp_ = sc && to;
    if (!sc) {
        if (keepAlive_ == KeepAlive::Song) {
            keepAlive_ = KeepAlive::None;
            revert();
        }
        return;
    }
    if (!shown) {
        if (keepAlive_ == KeepAlive::None) engage();
        keepAlive_ = KeepAlive::Song;
    }
    // A scene open for editing keeps playing alone; leaving it applies A / B.
    const bool editing = shown && view == View::EditScene;
    if (!editing) releaseDriven();
    a.project.perfSceneOn[0] = uint8_t(sc - 1);
    if (to) a.project.perfSceneOn[1] = uint8_t(to - 1);
    fader = 0;
    fxKey_ = -1;   // [I] #54: every slot start applies the scene's toggles again
    if (!editing) applyScenes();
    ui_.dirty();
}

int PerformanceScreen::rampFader(int ramp, int count, int pass, int len, int pos, int micro) {
    // In micro steps (48 per step) from the slot start: the ramp runs from
    // `start` for `r`; before it the fader is at A, after it at B.
    constexpr int kM = 48;
    const int total = count * len * kM;
    int at = ((pass - 1) * len + pos) * kM + micro - 1;
    int start = 0, r = total;
    if (ramp == Project::kRampEach) {
        at = pos * kM + micro - 1;
        r = len * kM;
    } else if (ramp == Project::kRampFirst) {
        r = len * kM;
    } else if (const int n = Project::rampLastPasses(ramp); n && n < count) {
        r = n * len * kM;
        start = total - r;
    } else if (const int s = Project::rampSteps(ramp); s && s * kM < total) {
        r = s * kM;
        start = total - r;
    }
    if (r <= 0) return kFaderMax;
    return clampi((at - start) * kFaderMax / r, 0, kFaderMax);
}

void PerformanceScreen::perfSongTick() {
    App& a = app_;
    if (!songRamp_ || view == View::EditScene) return;
    const bool shown = ui_.active() == kModPerformance;
    if (!shown && keepAlive_ != KeepAlive::Song) return;
    const Sequencer& q = a.seq;
    const int slot = q.songPosition();
    const int x = rampFader(a.project.slotRamp(slot), a.project.slotCount(slot), q.songPass(), q.patternLength(),
                            q.patternPosition(), q.masterMicroStep());
    if (x == fader) return;
    fader = x;
    applyScenes();
    if (shown) ui_.dirty();
}

void PerformanceScreen::revert() {
    // spec 12 §C.4: full revert, perf mode off, overrides dropped (the
    // sequencer plays the current pattern again).
    applyAll(true);
    app_.seq.setPerformanceMode(false);
}

void PerformanceScreen::leave() {
    App& a = app_;
    // "Leave" callback (spec 12 §A.5): global -> forced config write,
    // per project -> forced Workspace autosave.
    if (sourceIndex == 0) a.requestConfigSave(true);
    else {
        a.markProjectDirty();
        a.autosave();
    }
}

void PerformanceScreen::edited() {
    // "Edited" callback (spec 12 §A.5).
    if (sourceIndex == 0) app_.markConfigDirty();
    else app_.markProjectDirty();
}

// ---------------------------------------------------------------------------
// FX apply / revert (spec 12 §E)

// Rebuild every track's performance state from the slots' current values and
// push it to the engine and the sequencer. 2.x: all 16 tracks; the voice FX
// (audio::PerfOffsets) exist on the sample tracks 1-8 only, tracks 9-16 take
// the sequencer side (Volume as velocity offset, Tune as transpose, step
// repeater, play mode; 2x-01 §5). A slot with cur = 0 is reverted;
// disabled tracks are reverted; a global FX (Pattern length) is applied once
// (spec 12 §E.1). FX types are exclusive, so at most one slot per type is
// normally active; if two are, the higher slot wins.
void PerformanceScreen::applyAll(bool revertEverything) {
    App& a = app_;
    Sequencer& q = a.seq;
    int globalLen = -1;
    int djFilter = 0, tapeStop = 0;   // [I] master inserts (audio::MasterFx), once for the mix
    for (int s = 0; s < perf::kSlots && !revertEverything; ++s) {
        if (!src.valid() || cur[s] == 0) continue;
        if (src.type(s) == perf::kDjFilter) djFilter = cur[s];
        if (src.type(s) == perf::kTapeStop) tapeStop = cur[s];
    }
    if (a.exportingStems()) djFilter = tapeStop = 0;   // [I] #51: master FX only on a full-mix export
    a.engine.setMasterDjFilter(djFilter);
    a.setTapeStop(tapeStop);
    for (int t = 0; t < kTrackCount; ++t) {
        audio::PerfOffsets p;
        int8_t repeat = -1, velOff = 0, transpose = 0;
        bool transposeOn = false;
        uint8_t playMode = 0;
        const bool on = fxOn[t] && !revertEverything;
        for (int s = 0; s < perf::kSlots && !revertEverything; ++s) {
            const uint8_t ty = src.valid() ? src.type(s) : 0;
            const int v = cur[s];
            if (ty == 0 || v == 0) continue;
            if (ty == perf::kPatternLength) {
                globalLen = perf::lengthOverride(v);
                continue;
            }
            if (ty == perf::kDjFilter || ty == perf::kTapeStop) continue;   // [I] applied above, on the master bus
            if (!on) continue;
            switch (ty) {
                case perf::kVolume: p.volume = int16_t(v); velOff = int8_t(v); break;
                case perf::kPanning: p.pan = int16_t(v); break;   // spec 12 §E.2: pan + v, 0..100
                case perf::kTune:
                    p.tune = int16_t(v);
                    if (v >= -48 && v <= 48) {   // MIDI transpose (spec 12 §E.2 id 3)
                        transposeOn = true;
                        transpose = int8_t(v);
                    }
                    break;
                case perf::kLowPass: case perf::kHighPass: case perf::kBandPass:
                    p.filterType = int8_t(ty - perf::kLowPass);
                    p.cutoff = float(v) / 100.0f;
                    break;
                case perf::kDelaySend: p.dsend = int16_t(v); break;
                case perf::kReverbSend: p.rsend = int16_t(v); break;
                case perf::kStart: p.start = perf::startOffset(v); break;
                case perf::kEnd: p.end = perf::endOffset(v); break;
                case perf::kDirection: p.reverse = v != 0; break;
                case perf::kVolLfo: p.lfoSpeed[kAutoVolume] = int8_t(v); break;
                case perf::kPanLfo: p.lfoSpeed[kAutoPanning] = int8_t(v); break;
                case perf::kFineLfo: p.lfoSpeed[kAutoFinetune] = int8_t(v); break;
                case perf::kFilterLfo: p.lfoSpeed[kAutoCutoff] = int8_t(v); break;
                case perf::kPosLfo: p.lfoSpeed[kAutoWavetablePos] = p.lfoSpeed[kAutoGranularPos] = int8_t(v); break;
                case perf::kRepeater: repeat = int8_t(v - 1); break;
                case perf::kPlayMode: playMode = uint8_t(v); break;
                case perf::kBitDepth: p.bits = int16_t(v); break;
                case perf::kOverdrive: p.od = int16_t(v); break;
                default: break;
            }
        }
        if (t < kMidiTrackStart) a.engine.setPerf(t, p);
        q.setPerfRepeat(t, repeat);
        q.setPerfPlayMode(t, playMode);
        q.setPerfVelocityOffset(t, velOff);
        q.setPerfTranspose(t, transposeOn && transpose != 0, transpose);
    }
    q.setPerfGlobalLength(globalLen);
}

// Other slots with the same FX type, or an excluded filter type, go to row 0
// with value 0 (spec 12 §B.2 step 3, spec 06 §3.4 pad step 2).
void PerformanceScreen::resetOthers(int c) {
    const uint8_t ty = src.type(c);
    if (ty == 0) return;
    const perf::FxDesc& d = perf::fx(ty);
    for (int s = 0; s < perf::kSlots; ++s) {
        if (s == c) continue;
        const uint8_t o = src.type(s);
        if (o != ty && (o == 0 || (o != d.exclA && o != d.exclB))) continue;
        src.setRow(s, 0);
        cur[s] = 0;
        liveValid[s] = false;
    }
}

// ---------------------------------------------------------------------------
// Inputs (spec 06 §3.4)

void PerformanceScreen::onPad(uint8_t pad, uint8_t state) {
    if (view == View::Bank) {
        // [I] A pad selects its scene; the selected scene's pad opens it.
        const int sc = padScene(pad);
        if (state != kPress || sc < 0) return;
        if (sc == bankCursor) editSceneStart(sc);
        else bankCursor = sc;
        refreshBar();
        ui_.dirty();
        return;
    }
    const int c = pad % 12, r = pad / 12;
    if (c >= perf::kSlots || r >= perf::kRows || !src.valid()) return;
    if (state == kPress) {
        if (edit) column = c;   // [I] the edit cursor follows the pad: Up / Down and the encoder act on it
        liveValid[c] = false;
        const uint8_t row = uint8_t(src.type(c) ? r : 0);
        src.setRow(c, row);
        held_[c] = true;
        resetOthers(c);
        cur[c] = row ? src.value(c, row) : 0;
        applyAll(false);
    } else if (state == kRelease) {
        // Re-apply the stored value: an encoder tweak outside edit mode is
        // dropped (spec 12 §E.1). The row stays latched; [I] except Tape
        // stop, which is momentary (release = spin up).
        if (src.type(c) == perf::kTapeStop && !edit) src.setRow(c, 0);
        cur[c] = padValue(c);
        held_[c] = false;
        applyAll(false);
    }
    ui_.dirty();
}

void PerformanceScreen::padLeds(uint8_t* lv) {
    // One lit pad per column at the selected row (spec 06 §3.4), DIM level.
    // [I] A column a scene drives lights its scene A and scene B rows (a
    // scene without it: the off row), each from DIM at no share to FULL at
    // all of it, so the light slides between them with the fader (stepped
    // FX switch at the middle, sceneShare); the same row in both: FULL.
    if (!src.valid()) return;
    App& a = app_;
    const int dim = a.cfg.padDim(), full = a.cfg.padFull();
    if (view == View::Bank) {
        // [I] The scenes: stored ones lit, the scenes on A / B full.
        for (int p = 0; p < 48; ++p) {
            const int sc = padScene(p);
            if (sc < 0) continue;
            bool stored = false;
            for (uint8_t r : a.project.perfScene[sc]) stored = stored || r;
            if (sc == a.project.perfSceneOn[0] || sc == a.project.perfSceneOn[1]) lv[p] = uint8_t(full);
            else if (stored) lv[p] = uint8_t(dim);
        }
        return;
    }
    for (int c = 0; c < perf::kSlots; ++c) {
        const uint8_t rA = a.project.sceneOn(0)[c], rB = a.project.sceneOn(1)[c];
        if (src.type(c) && (rA || rB) && !edit && view == View::Play) {
            auto level = [&](int share) { return uint8_t(dim + (full - dim) * share / kFaderMax); };
            const int pa = (rA <= 3 ? rA : 0) * 12 + c, pb = (rB <= 3 ? rB : 0) * 12 + c;
            if (pa == pb) {
                lv[pa] = uint8_t(full);
            } else {
                lv[pa] = level(sceneShare(0, c));
                lv[pb] = level(sceneShare(1, c));
            }
            continue;
        }
        const int r = src.row(c);
        if (r < perf::kRows) lv[r * 12 + c] = uint8_t(dim);
    }
}

uint8_t PerformanceScreen::shownPattern(int t) const {
    if (t < 0 || t >= kTrackCount) return 1;
    return pending_[t] ? pending_[t] : trackPatternOf(t);
}

// Pattern the track plays: stock 0x142 for tracks 1-8; tracks 9-16 always
// play the current pattern (no override on the Tracker Mini).
uint8_t PerformanceScreen::trackPatternOf(int t) const {
    App& a = app_;
    if (t < kMidiTrackStart) return perf::trackPattern(a.project, t);
    return a.patternNumber();
}

void PerformanceScreen::storeTrackPattern(int t, uint8_t num) {
    App& a = app_;
    if (t >= kMidiTrackStart) return;
    perf::setTrackPattern(a.project, t, num);
    a.markProjectDirty();
}

void PerformanceScreen::trackPatternNow(int t, int num) {
    // Track key + encoder / Up / Down: immediate switch (spec 12 §C.2).
    App& a = app_;
    if (t >= kMidiTrackStart) return;
    num = clampi(num, 1, 255);
    storeTrackPattern(t, uint8_t(num));
    a.seq.setTrackPattern(t, uint8_t(num), true);
    pending_[t] = 0;
}

void PerformanceScreen::trackPatternQueued(int t, int num) {
    // Track key release after Left/Right, CC 41-48: switch at that track's
    // loop end (spec 12 §C.2).
    App& a = app_;
    if (t >= kMidiTrackStart) return;
    num = clampi(num, 1, 255);
    storeTrackPattern(t, uint8_t(num));
    a.seq.setTrackPattern(t, uint8_t(num), false);
}

bool PerformanceScreen::anyTrackKey() const {
    for (bool k : keyHeld_)
        if (k) return true;
    return false;
}

void PerformanceScreen::onEncoder(int d) {
    if (view == View::Bank) {   // [I] the selected scene
        bankCursor = clampi(bankCursor + d, 0, Project::kPerfScenes - 1);
        refreshBar();
        ui_.dirty();
        return;
    }
    if (anyTrackKey()) {
        for (int i = 0; i < 8; ++i)
            if (keyHeld_[i]) {
                const int t = bank * 8 + i;
                keyUsed_[i] = true;
                trackPatternNow(t, trackPatternOf(t) + d);
            }
        refreshBar();
        return;
    }
    if (!src.valid()) return;
    // [I] Nothing held: in edit mode the encoder edits the selected row of
    // the edit column (a tapped pad selects both), otherwise it is the
    // scene crossfader.
    bool padHeld = false;
    for (bool h : held_) padHeld = padHeld || h;
    if (!padHeld) {
        if (edit) editSelected(d);
        else if (view == View::Play) setFader(fader + d * kFaderStep);   // [I] no fader while editing a scene
        return;
    }
    bool any = false;
    for (int c = 0; c < perf::kSlots; ++c) {
        const uint8_t ty = src.type(c);
        if (!held_[c] || ty == 0) continue;
        const perf::FxDesc& f = perf::fx(ty);
        cur[c] = int16_t(clampi(cur[c] + d, f.min, f.max));
        const int row = src.row(c);
        // Stored only in edit mode on rows 1..3; never into the live row
        // (spec 12 §B.2: stock writes out of the slot there).
        if (edit && row >= 1 && row <= 3) {
            src.setValue(c, row, cur[c]);
            edited();
        } else if (row == perf::kLiveRow) {
            live[c] = cur[c];
        }
        any = true;
    }
    if (any) {
        applyAll(false);
        ui_.dirty();
    }
}

void PerformanceScreen::editSelected(int d) {
    const int c = column;
    const uint8_t ty = src.type(c);
    const int row = src.row(c);
    if (ty == 0 || row < 1 || row > 3) return;
    const perf::FxDesc& f = perf::fx(ty);
    cur[c] = int16_t(clampi(src.value(c, row) + d, f.min, f.max));
    src.setValue(c, row, cur[c]);
    edited();
    applyAll(false);
    ui_.dirty();
}

void PerformanceScreen::changeType(int d) {
    const int c = column;
    // The old type is reverted first (spec 12 §A.5 [FW:0x445fc]).
    src.setType(c, uint8_t((src.type(c) + perf::kFxCount + d) % perf::kFxCount));
    for (int r = 0; r < perf::kRows; ++r) src.setValue(c, r, 0);
    src.setRow(c, 0);
    cur[c] = 0;
    liveValid[c] = false;
    edited();
    applyAll(false);
}

void PerformanceScreen::swapColumn(int nc) {
    // Shift + Left/Right in edit mode: type + 4 values (spec 06 §3.4).
    const int c = column;
    const uint8_t t = src.type(nc);
    src.setType(nc, src.type(c));
    src.setType(c, t);
    for (int r = 0; r < perf::kRows; ++r) {
        const int16_t v = src.value(nc, r);
        src.setValue(nc, r, src.value(c, r));
        src.setValue(c, r, v);
    }
    const int pair[2] = {c, nc};
    for (int s : pair) {
        const int r = src.row(s);
        cur[s] = (r >= 1 && r <= 3) ? src.value(s, r) : 0;
        liveValid[s] = false;
    }
    edited();
    applyAll(false);
}

void PerformanceScreen::onButton(uint8_t id, uint8_t state) {
    App& a = app_;
    if (id == btn::Performance) {
        // [I] The scene bank page: open / back to playing; from a scene
        // being edited, back to the bank (saving it). Stock 2.x toggled the
        // track bank here (2x-01 §5): Left / Right do that now.
        if (state != kPress) return;
        if (view == View::Play) openBank();
        else if (view == View::Bank) view = View::Play;
        else editSceneEnd();
        refreshBar();
        ui_.dirty();
        return;
    }
    if (view == View::Bank) {
        bankButton(id, state);
        return;
    }
    if (btn::isFKey(id)) {
        const int k = id, t = bank * 8 + k;
        if (state == kPress) {
            if (ui_.shift()) trk::setMuted(a, t, !trk::muted(a, t));
            else {
                keyHeld_[k] = true;
                keyUsed_[k] = false;
            }
        } else if (state == kRelease && keyHeld_[k]) {
            keyHeld_[k] = false;
            if (pending_[t]) {
                trackPatternQueued(t, pending_[t]);
                pending_[t] = 0;
            } else if (!keyUsed_[k]) {
                setFxOff(uint16_t(a.project.perfFxOff ^ (1u << t)));
                if (view == View::EditScene) editFxTouched_ = true;   // [I] #54: the edited scene's toggles
                a.markProjectDirty();
                applyAll(false);   // enable = apply the active slots, disable = revert
            }
        }
        refreshBar();
        return;
    }
    if (state != kPress) {
        if (id == btn::Shift) refreshBar();
        return;
    }
    if (!src.valid()) return;
    switch (id) {
        case btn::Rec: edit = !edit; break;
        case btn::Left:
        case btn::Right: {
            const int d = id == btn::Right ? 1 : -1;
            if (edit) {
                const int nc = clampi(column + d, 0, perf::kSlots - 1);
                if (ui_.shift() && nc != column) swapColumn(nc);
                column = nc;
            } else if (ui_.isDown(btn::Delete) && !anyTrackKey()) {
                // [I] Delete+Left = clear the scene on A, Delete+Right = the one on B
                if (view == View::Play) clearScene(d > 0 ? 1 : 0);
            } else if (ui_.shift() && !anyTrackKey()) {
                // [I] Shift+Left = store into the scene on A, Shift+Right = on B
                if (view == View::Play) storeScene(d > 0 ? 1 : 0);
            } else if (!anyTrackKey()) {
                // [I] The F keys' tracks: Left = 1-8, Right = 9-16 (stock 2.x:
                // the Performance key); held track keys are dropped.
                if (bank != (d > 0 ? 1 : 0)) {
                    bank = d > 0 ? 1 : 0;
                    for (int i = 0; i < 8; ++i) keyHeld_[i] = keyUsed_[i] = false;
                    for (auto& p : pending_) p = 0;
                }
            } else {
                for (int i = 0; i < 8; ++i)
                    if (keyHeld_[i]) {
                        const int t = bank * 8 + i;
                        keyUsed_[i] = true;
                        if (t < kMidiTrackStart) pending_[t] = uint8_t(clampi(shownPattern(t) + d, 1, 255));
                    }
            }
            break;
        }
        case btn::Up:
        case btn::Down: {
            const int d = id == btn::Up ? 1 : -1;
            if (anyTrackKey()) {
                for (int i = 0; i < 8; ++i)
                    if (keyHeld_[i]) {
                        const int t = bank * 8 + i;
                        keyUsed_[i] = true;
                        trackPatternNow(t, trackPatternOf(t) + d);
                    }
            } else if (edit) {
                changeType(d);
            }
            break;
        }
        case btn::Copy:
            if (!edit) break;
            if (!ui_.shift()) copySrc_ = column;
            else if (copySrc_ >= 0 && copySrc_ != column) {
                src.setType(column, src.type(copySrc_));
                for (int r = 0; r < perf::kRows; ++r) src.setValue(column, r, src.value(copySrc_, r));
                src.setRow(column, 0);
                cur[column] = 0;
                liveValid[column] = false;
                edited();
                applyAll(false);
            }
            break;
        case btn::Play: play(); break;
        default: break;
    }
    refreshBar();
}

void PerformanceScreen::play() {
    // spec 06 §3.4 Play, spec 12 §C.3.
    App& a = app_;
    const uint8_t curPat = a.patternNumber();
    if (a.seq.isPlaying()) {
        if (!ui_.shift()) {
            a.togglePlay(false);   // stop everything; perf mode and FX stay (§C.4)
        } else {
            // SPEC GAP: "continue in song/pattern mode at the next sync
            // point" -- every track is queued back to the current pattern, so
            // each switches at its own loop end.
            for (int t = 0; t < kTrackCount; ++t) trackPatternQueued(t, curPat);
        }
        return;
    }
    if (ui_.shift()) {
        for (int t = 0; t < kTrackCount; ++t) {
            if (t < kMidiTrackStart) perf::setTrackPattern(a.project, t, curPat);
            pending_[t] = 0;
        }
        a.seq.clearTrackPatterns();
        a.seq.playSong(a.project.playlistPos);
    } else {
        a.seq.userPlay(0);   // performance mode stays set: all tracks from step 0
    }
}

// ---------------------------------------------------------------------------
// MIDI CC (spec 12 §B.2, §B.3): latched here, handled in update().

void PerformanceScreen::perfTrackPatternCc(uint8_t track, uint8_t value) {
    if (track >= 8) return;
    trackCcVal_[track] = value;
    trackCcPending_[track] = true;
}

void PerformanceScreen::perfSlotCc(uint8_t slot, uint8_t value) {
    if (slot >= perf::kSlots) return;
    ccVal_[slot] = value;
    ccPending_[slot] = true;
}

void PerformanceScreen::perfSceneCc(uint8_t value) {
    sceneCcVal_ = value > 127 ? 127 : value;
    sceneCcPending_ = true;
}

void PerformanceScreen::update(uint32_t now) {
    App& a = app_;
    if (now - lastPoll_ >= kCcPollMs) {
        lastPoll_ = now;
        bool apply = false;
        for (int t = 0; t < 8; ++t) {
            if (!trackCcPending_[t]) continue;
            trackCcPending_[t] = false;
            keyHeld_[t] = keyUsed_[t] = false;
            pending_[t] = 0;
            trackPatternQueued(t, trackCcVal_[t] + 1);   // 1..128
            labelsDirty_ = true;
        }
        for (int s = 0; s < perf::kSlots && src.valid(); ++s) {
            if (!ccPending_[s]) continue;
            ccPending_[s] = false;
            const uint8_t ty = src.type(s);
            src.setRow(s, uint8_t(ty ? perf::kLiveRow : 0));
            live[s] = int16_t(perf::ccToValue(ty, ccVal_[s]));
            liveValid[s] = true;
            resetOthers(s);
            cur[s] = ty ? live[s] : 0;
            apply = true;
        }
        // [I] CC 63: scene crossfader.
        if (sceneCcPending_ && view != View::EditScene) {   // [I] kept while editing a scene
            sceneCcPending_ = false;
            setFader((sceneCcVal_ * kFaderMax + 63) / 127);
        }
        // OpenTracker applies a Pattern length slot too (stock only shows it,
        // spec 12 §B.2 quirk; README).
        if (apply) {
            applyAll(false);
            ui_.dirty();
        }
        // Song advance / queued whole-pattern change: the overrides are gone,
        // the labels show the new current pattern (spec 12 §C.3).
        const uint8_t curPat = a.patternNumber();
        if (curPat != lastPattern_) {
            lastPattern_ = curPat;
            for (int t = 0; t < 8; ++t) perf::setTrackPattern(a.project, t, curPat);
            labelsDirty_ = true;
        }
    }
    // A queued track pattern blinks on its label (manual p.278, MANUAL_AUDIT #39).
    bool queued = false;
    for (int t = 0; t < 8; ++t) queued = queued || pending_[t] || a.seq.queuedTrackPattern(t) != 0;
    const bool on = !queued || (now / 200u) % 2u == 0;
    if (on != blinkOn_) {
        blinkOn_ = on;
        ui_.dirty();
    }
    if (labelsDirty_ && now - lastLabels_ >= kLabelPollMs) {
        lastLabels_ = now;
        labelsDirty_ = false;
        refreshBar();
        ui_.dirty();
    }
}

// ---------------------------------------------------------------------------
// [I] Scenes A / B and the crossfader (Octatrack-style, not in stock)

namespace {
bool isFilter(uint8_t ty) { return ty == perf::kLowPass || ty == perf::kHighPass || ty == perf::kBandPass; }
// FX that cannot blend: switch from A to B at the middle of the fade.
bool isStepped(uint8_t ty) {
    switch (ty) {
        case perf::kDirection: case perf::kVolLfo: case perf::kPanLfo: case perf::kFineLfo:
        case perf::kFilterLfo: case perf::kPosLfo: case perf::kRepeater: case perf::kPlayMode:
        case perf::kPatternLength:
        case perf::kTapeStop:   // [I] on / off at the middle (its rows are lengths, not amounts)
            return true;
        default:
            return false;
    }
}
// a .. b at x / 100, rounded to the nearest.
int lerp100(int a, int b, int x) {
    const int n = a * (100 - x) + b * x;
    return (n >= 0 ? n + 50 : n - 50) / 100;
}
}  // namespace

void PerformanceScreen::storeScene(int scene) {
    App& a = app_;
    // Pads held: only their columns (their rows, row 0 = off in the scene),
    // the rest of the scene stays; none held: every column (a snapshot).
    // Stored rows 1..3 only: a live MIDI-CC value (row 4) is not kept.
    bool any = false;
    for (bool h : held_) any = any || h;
    int count = 0, last = -1;
    for (int s = 0; s < perf::kSlots; ++s) {
        if (any && !held_[s]) continue;
        const uint8_t r = src.row(s);
        a.project.sceneOn(scene)[s] = uint8_t(src.type(s) && r >= 1 && r <= 3 ? r : 0);
        ++count;
        last = s;
    }
    if (!any) {   // [I] #54: a whole-scene store also keeps the FX toggles
        const int i = a.project.perfSceneOn[scene ? 1 : 0] % Project::kPerfScenes;
        a.project.perfSceneFxOff[i] = a.project.perfFxOff;
        a.project.perfSceneFxSet = uint16_t(a.project.perfSceneFxSet | (1u << i));
        fxKey_ = -1;
    }
    a.markProjectDirty();
    char msg[64];
    const int num = a.project.perfSceneOn[scene ? 1 : 0] + 1;   // [I] the bank scene on A / B
    const char ab = scene ? 'B' : 'A';
    if (!any) {
        std::snprintf(msg, sizeof(msg), "Scene %d stored (%c)", num, ab);
    } else if (count == 1) {
        const uint8_t ty = src.type(last), r = a.project.sceneOn(scene)[last];
        char v[24] = "off";
        if (r) perf::valueText(ty, src.value(last, r), v, sizeof(v));
        std::snprintf(msg, sizeof(msg), "Scene %d (%c): %s %s", num, ab, ty ? perf::fx(ty).name : "---", v);
    } else {
        std::snprintf(msg, sizeof(msg), "Scene %d (%c): %d columns stored", num, ab, count);
    }
    ui_.notify(msg, 1500);
    ui_.dirty();
}

int PerformanceScreen::sceneShare(int scene, int c) const {
    const int x = fader, toB = x >= kFaderMax / 2;
    const uint8_t ty = src.type(c);
    if (isStepped(ty)) return (scene == 1) == bool(toB) ? 100 : 0;   // switches at the middle
    if (isFilter(ty)) {
        // As applyScenes: the scenes' filters (the highest slot of each)
        // of different types cross over, A out by the middle, B in after it.
        uint8_t ft[2] = {0, 0};
        for (int s = 0; s < perf::kSlots; ++s)
            for (int sc = 0; sc < 2; ++sc) {
                const uint8_t r = app_.project.sceneOn(sc)[s];
                if (r >= 1 && r <= 3 && isFilter(src.type(s))) ft[sc] = src.type(s);
            }
        if (ft[0] && ft[1] && ft[0] != ft[1]) {
            if (scene == 0) return x * 2 >= kFaderMax ? 0 : kFaderMax - x * 2;
            return x * 2 <= kFaderMax ? 0 : x * 2 - kFaderMax;
        }
    }
    return scene ? x : kFaderMax - x;
}

int16_t PerformanceScreen::padValue(int c) const {
    const int row = src.row(c);
    if (row >= 1 && row <= 3) return src.value(c, row);
    return row == perf::kLiveRow && liveValid[c] ? live[c] : 0;
}

void PerformanceScreen::releaseDriven() {
    App& a = app_;
    // The FX types the scenes on A / B drive (applyScenes: a filter in
    // either scene drives all three filter types): their slots go back to
    // the pad rows.
    bool driven[perf::kFxCount] = {};
    bool filter = false;
    for (int s = 0; s < perf::kSlots; ++s) {
        const uint8_t ty = src.type(s);
        if (ty == 0 || ty >= perf::kFxCount) continue;
        for (int sc = 0; sc < 2; ++sc) {
            const uint8_t r = a.project.sceneOn(sc)[s];
            if (r < 1 || r > 3) continue;
            driven[ty] = true;
            filter = filter || isFilter(ty);
        }
    }
    if (filter) driven[perf::kLowPass] = driven[perf::kHighPass] = driven[perf::kBandPass] = true;
    for (int s = 0; s < perf::kSlots; ++s) {
        const uint8_t ty = src.type(s);
        if (ty == 0 || ty >= perf::kFxCount || !driven[ty]) continue;
        cur[s] = padValue(s);
        liveValid[s] = false;
    }
}

void PerformanceScreen::clearScene(int scene) {
    App& a = app_;
    // The slots the scenes drove go back to their pad rows, then the scene
    // left over (if any) blends again at the fader.
    releaseDriven();
    // Pads held: only their columns leave the scene; none held: all.
    bool any = false;
    for (bool h : held_) any = any || h;
    for (int s = 0; s < perf::kSlots; ++s)
        if (!any || held_[s]) a.project.sceneOn(scene)[s] = 0;
    if (!any) clearSceneFx(a.project.perfSceneOn[scene ? 1 : 0]);   // [I] #54: back to "-"
    a.markProjectDirty();
    if (perfFaderUsed(a.project)) {
        applyScenes();
    } else {
        fader = 0;   // no scene: a new one starts from A again
        applyAll(false);
    }
    char msg[40];
    const int num = a.project.perfSceneOn[scene ? 1 : 0] + 1;   // [I] the bank scene on A / B
    std::snprintf(msg, sizeof(msg), any ? "Scene %d (%c): column cleared" : "Scene %d cleared (%c)", num,
                  scene ? 'B' : 'A');
    ui_.notify(msg, 1500);
    ui_.dirty();
}

// ---------------------------------------------------------------------------
// [I] Scene bank (#10)

void PerformanceScreen::openBank() {
    view = View::Bank;
    bankCursor = app_.project.perfSceneOn[0];
    for (auto& h : held_) h = false;
}

void PerformanceScreen::setSceneOn(int ab, int scene) {
    App& a = app_;
    // The slots the old pair drove go back to their pad rows; the fader
    // stays where it is and blends the new pair.
    releaseDriven();
    a.project.perfSceneOn[ab ? 1 : 0] = uint8_t(clampi(scene, 0, Project::kPerfScenes - 1));
    a.markProjectDirty();
    fxKey_ = -1;   // [I] #54: a scene put on A / B applies its toggles
    if (perfFaderUsed(a.project)) applyScenes();
    else applyAll(false);
    char msg[32];
    std::snprintf(msg, sizeof(msg), "Scene %d on %c", scene + 1, ab ? 'B' : 'A');
    ui_.notify(msg, 1200);
}

void PerformanceScreen::editSceneStart(int scene) {
    if (!src.valid()) return;
    // The pads take the scene's rows (and the scenes on A / B let go), so
    // what is heard is the scene alone; the rows before are put back after.
    releaseDriven();
    const uint8_t* rows = app_.project.perfScene[scene];
    for (int c = 0; c < perf::kSlots; ++c) {
        savedRows_[c] = src.row(c);
        src.setRow(c, uint8_t(src.type(c) && rows[c] <= 3 ? rows[c] : 0));
        cur[c] = padValue(c);
        liveValid[c] = false;
        held_[c] = false;
    }
    // [I] #54: the F keys show and edit the scene's FX toggles (the live
    // ones when it has none); the live toggles come back after.
    savedFxOff_ = app_.project.perfFxOff;
    editFxTouched_ = false;
    if (app_.project.sceneHasFx(scene)) setFxOff(app_.project.perfSceneFxOff[scene]);
    applyAll(false);
    editScene = scene;
    view = View::EditScene;
}

void PerformanceScreen::editSceneEnd() {
    App& a = app_;
    if (editScene >= 0 && src.valid()) {
        uint8_t* rows = a.project.perfScene[editScene];
        for (int c = 0; c < perf::kSlots; ++c) {
            const uint8_t r = src.row(c);
            rows[c] = uint8_t(src.type(c) && r >= 1 && r <= 3 ? r : 0);
            src.setRow(c, savedRows_[c]);
            cur[c] = padValue(c);
            liveValid[c] = false;
        }
        // [I] #54: F keys changed while editing are the scene's toggles; a
        // scene without toggles whose keys weren't touched stays "-".
        if (editFxTouched_ || a.project.sceneHasFx(editScene)) {
            a.project.perfSceneFxOff[editScene] = a.project.perfFxOff;
            a.project.perfSceneFxSet = uint16_t(a.project.perfSceneFxSet | (1u << editScene));
        }
        setFxOff(savedFxOff_);
        fxKey_ = -1;   // the scenes on A / B apply their (maybe edited) toggles again
        a.markProjectDirty();
        char msg[32];
        std::snprintf(msg, sizeof(msg), "Scene %d saved", editScene + 1);
        ui_.notify(msg, 1200);
        bankCursor = editScene;
    }
    editScene = -1;
    view = View::Bank;
    if (perfFaderUsed(a.project)) applyScenes();
    else applyAll(false);
}

void PerformanceScreen::clearBankScene(int scene) {
    App& a = app_;
    const bool playing = scene == a.project.perfSceneOn[0] || scene == a.project.perfSceneOn[1];
    if (playing) releaseDriven();
    std::memset(a.project.perfScene[scene], 0, sizeof(a.project.perfScene[scene]));
    clearSceneFx(scene);   // [I] #54
    a.markProjectDirty();
    if (playing) {
        if (perfFaderUsed(a.project)) applyScenes();
        else applyAll(false);
    }
    char msg[32];
    std::snprintf(msg, sizeof(msg), "Scene %d cleared", scene + 1);
    ui_.notify(msg, 1200);
}

void PerformanceScreen::bankButton(uint8_t id, uint8_t state) {
    if (state != kPress) return;
    switch (id) {
        case btn::Left: bankCursor = clampi(bankCursor - 1, 0, Project::kPerfScenes - 1); break;
        case btn::Right: bankCursor = clampi(bankCursor + 1, 0, Project::kPerfScenes - 1); break;
        case btn::Up: bankCursor = bankCursor >= 8 ? bankCursor - 8 : bankCursor; break;
        case btn::Down: bankCursor = bankCursor < 8 ? bankCursor + 8 : bankCursor; break;
        case btn::F1: setSceneOn(0, bankCursor); break;
        case btn::F2: setSceneOn(1, bankCursor); break;
        case btn::F3: editSceneStart(bankCursor); break;
        case btn::F4: clearBankScene(bankCursor); break;
        case btn::F8: view = View::Play; break;
        default: return;
    }
    refreshBar();
    ui_.dirty();
}

// [I] #54: the per-track FX toggles (bit t = track t+1 off), live and saved.
void PerformanceScreen::setFxOff(uint16_t off) {
    app_.project.perfFxOff = off;
    for (int t = 0; t < kTrackCount; ++t) fxOn[t] = !((off >> t) & 1u);
    refreshBar();
}

void PerformanceScreen::clearSceneFx(int scene) {
    Project& p = app_.project;
    p.perfSceneFxSet = uint16_t(p.perfSceneFxSet & ~(1u << (scene % Project::kPerfScenes)));
    p.perfSceneFxOff[scene % Project::kPerfScenes] = 0;
    fxKey_ = -1;
}

void PerformanceScreen::setFader(int x) {
    songRamp_ = false;   // [I] moving the fader takes over from the song's ramp until the next slot
    fader = clampi(x, 0, kFaderMax);
    applyScenes();
    ui_.dirty();
}

void PerformanceScreen::applyScenes() {
    if (!src.valid()) return;
    const Project& p = app_.project;
    // Per FX type: the slot and value each scene sets (the higher slot wins,
    // as in applyAll).
    struct Pick {
        int slot = -1;
        int v = 0;
    };
    Pick pa[perf::kFxCount], pb[perf::kFxCount];
    Pick fa, fb;   // the filter (LP / HP / BP share one engine filter)
    uint8_t fta = 0, ftb = 0;
    for (int s = 0; s < perf::kSlots; ++s) {
        const uint8_t ty = src.type(s);
        if (ty == 0 || ty >= perf::kFxCount) continue;
        for (int sc = 0; sc < 2; ++sc) {
            const uint8_t r = p.sceneOn(sc)[s];
            if (r < 1 || r > 3) continue;
            const Pick k{s, src.value(s, r)};
            if (isFilter(ty)) {
                (sc ? fb : fa) = k;
                (sc ? ftb : fta) = ty;
            } else {
                (sc ? pb : pa)[ty] = k;
            }
        }
    }
    const int x = fader;
    const bool toB = x >= kFaderMax / 2;
    int16_t next[perf::kSlots];
    for (int s = 0; s < perf::kSlots; ++s) next[s] = cur[s];
    bool involved[perf::kFxCount] = {};
    bool picked[perf::kSlots] = {};
    auto place = [&](const Pick& k, int v) {
        if (k.slot < 0) return;
        next[k.slot] = int16_t(v);
        picked[k.slot] = true;
    };
    for (int ty = 1; ty < perf::kFxCount; ++ty) {
        if (isFilter(uint8_t(ty))) continue;
        const Pick &A = pa[ty], &B = pb[ty];
        if (A.slot < 0 && B.slot < 0) continue;
        involved[ty] = true;
        if (isStepped(uint8_t(ty))) {
            place(toB ? B : A, toB ? B.v : A.v);   // a scene without it: off
            continue;
        }
        // Continuous: blend (a missing side is 0, no effect) in the slot of
        // the scene the fader is closer to.
        const Pick& at = toB ? (B.slot >= 0 ? B : A) : (A.slot >= 0 ? A : B);
        place(at, lerp100(A.v, B.v, x));
    }
    if (fa.slot >= 0 || fb.slot >= 0) {
        involved[perf::kLowPass] = involved[perf::kHighPass] = involved[perf::kBandPass] = true;
        if (fa.slot >= 0 && fb.slot >= 0 && fta != ftb) {
            // Different filter types: A fades out to the middle, B fades in after it.
            if (!toB) place(fa, lerp100(fa.v, 0, x * 2));
            else place(fb, lerp100(0, fb.v, (x - kFaderMax / 2) * 2));
        } else {
            const Pick& at = toB ? (fb.slot >= 0 ? fb : fa) : (fa.slot >= 0 ? fa : fb);
            place(at, lerp100(fa.slot >= 0 ? fa.v : 0, fb.slot >= 0 ? fb.v : 0, x));
        }
    }
    // [I] #54: the FX toggles of the scene the fader is on (A below the
    // middle, B from it), when that scene carries toggles; a scene with "-"
    // leaves them. Applied once per change of that scene, so a track key
    // tapped meanwhile keeps its effect until then.
    {
        const int i = p.perfSceneOn[toB ? 1 : 0] % Project::kPerfScenes;
        const int key = p.sceneHasFx(i) ? i * 2 + (toB ? 1 : 0) : -1;
        if (key != fxKey_) {
            fxKey_ = key;
            if (key >= 0) setFxOff(p.perfSceneFxOff[i]);
        }
    }
    // Slots of an involved type that the blend did not pick are off; FX in
    // neither scene keep what the pads set.
    for (int s = 0; s < perf::kSlots; ++s) {
        const uint8_t ty = src.type(s);
        if (ty == 0 || ty >= perf::kFxCount || !involved[ty]) continue;
        if (!picked[s]) next[s] = 0;
        cur[s] = next[s];
        liveValid[s] = false;
    }
    applyAll(false);
}

// ---------------------------------------------------------------------------
// Layout (spec 06 §3.3)

void PerformanceScreen::refreshBar() {
    BottomBar& b = ui_.bar;
    App& a = app_;
    b.clear();
    if (view == View::Bank) {
        // [I] Drawn by draw() like the track labels.
        // One line each: the red box already shows the selected scene.
        static const char* const kTop[8] = {"Set A", "Set B", "Edit", "Clear", "", "", "", "Back"};
        for (int i = 0; i < 8; ++i) {
            b.set(i, kTop[i], "");
            b.highlight(i, true);
            b.label[i].hidden = true;
        }
        return;
    }
    for (int i = 0; i < 8; ++i) {
        const int t = bank * 8 + i;
        const bool m = trk::muted(a, t);
        char nm[24], v[24];
        trk::name(a, t, nm, sizeof(nm));
        if (ui_.shift()) std::snprintf(v, sizeof(v), "%s", m ? "Unmute" : "Mute");
        else std::snprintf(v, sizeof(v), a.cfg.numbering == 2 ? "Patt: %02X" : "Patt: %d", shownPattern(t));
        // 1.9.2 manual: a highlighted label = FX applied to the track; the
        // text colour shows the mute state (spec 06 §3.3).
        b.set(i, nm, v, !m);
        b.highlight(i, fxOn[t]);
        // Drawn by draw(): the stock bar shows no focus frames here
        // [S snap 021], so "FX on" is the plain stock look and "FX off"
        // greys the title only [I].
        b.label[i].hidden = true;
    }
}

void PerformanceScreen::draw(Gfx& g) {
    const Theme& th = ui_.th();
    if (view == View::Bank) {
        drawBank(g);
        return;
    }
    // 2.x bank B: right-hand text "Tracks 9-16" (M1 plan spec gap 5).
    const char* right = bank ? (edit ? "EDIT  Tracks 9-16" : "Tracks 9-16") : (edit ? "EDIT" : nullptr);
    // [I] With a scene stored: the crossfader with the scenes on its ends,
    // "A 3 ----|----- B 7"; editing a scene: its number.
    char fade[64], title[24];
    const Project& pj = app_.project;
    if (view == View::EditScene) {
        std::snprintf(fade, sizeof(fade), "%s%sEditing", right ? right : "", right ? "  " : "");
        right = fade;
    } else if (perfFaderUsed(pj) && !edit) {   // edit mode: the encoder edits rows, no fader shown
        // The bar shrinks until the whole text fits the title's right side
        // (with "EDIT" / "Tracks 9-16" in front of it).
        for (int cells = 10; cells >= 2; --cells) {
            char bar[12];
            const int pos = fader * cells / kFaderMax;
            for (int i = 0; i <= cells; ++i) bar[i] = i == pos ? '|' : '-';
            bar[cells + 1] = 0;
            std::snprintf(fade, sizeof(fade), "%s%sA %d %s B %d", right ? right : "", right ? "  " : "",
                          pj.perfSceneOn[0] + 1, bar, pj.perfSceneOn[1] + 1);
            if (textWidth(kFont4, fade) <= wg::kRightTextX - wg::kRightTextMinX) break;
        }
        right = fade;
    }
    if (view == View::EditScene) std::snprintf(title, sizeof(title), "Scene %d", editScene + 1);
    else std::snprintf(title, sizeof(title), "Performance FX");
    ui_.drawTitle(g, title, right);
    if (!src.valid()) return;
    // spec 20 §4.10 / VISUAL_DIFF §8.10: 12 FX panels (class 0x0b) 63 px
    // wide at x 11 + 65 i: a title band y 34..69 with the current (or live
    // MIDI-CC, spec 16 §A.14) value, the FX name as vertical font-2 text
    // reading upwards (bottom y 265), and a body y 279..413 + fade with the
    // four stored rows at a 30-px pitch, the active row green [S snap 021].
    const int set = slot::kPerfPanels;
    // Edit mode: the active rows are red (manual p.274, spec 06 §3.3 "the cell
    // colour differs in edit mode"; MANUAL_AUDIT_2026-10-23 #41).
    const uint32_t bg = th.slot(set + 1), textC = th.slot(set), hiC = edit ? th.alert : th.slot(set + 2);
    // Drawn in layers over all 12 columns instead of column by column: the
    // RECTS batches keep the display list (RAM_DL, 8 KB) small enough for
    // the FT81x -- column by column this screen expanded past it on
    // hardware and every frame faulted (the screen froze). The columns do
    // not overlap and each column keeps its own drawing order, so the
    // pixels are the same.
    auto colX = [](int c) { return 11 + 65 * c; };
    // 1. Panel bodies (opaque): title band y 34..70, body y 279..424.
    g.rectsBegin(bg);
    for (int c = 0; c < perf::kSlots; ++c) {
        g.rectsAdd(colX(c), 34, 63, 37);
        g.rectsAdd(colX(c), 279, 63, 146);
    }
    g.rectsEnd();
    // 2. Soft 1-px side edges and the blended rows 33 / 70 / 278 (row 70
    //    lies on the title band: after 1). The column after the one being
    //    edited leaves its translucent left pixels to step 5 (drawn once,
    //    over the focus frame, as column by column).
    const int after = edit && column >= 0 && column + 1 < perf::kSlots ? column + 1 : -1;
    g.alpha(wg::kEdgeA);
    g.rectsBegin(bg);
    for (int c = 0; c < perf::kSlots; ++c) {
        const int x = colX(c), skip = c == after ? 1 : 0;
        if (!skip) {
            g.rectsAdd(x - 1, 34, 1, 37);
            g.rectsAdd(x - 1, 279, 1, 146);
        }
        g.rectsAdd(x + 63, 34, 1, 37);
        g.rectsAdd(x + 63, 279, 1, 146);
        g.rectsAdd(x + skip, 33, 63 - skip, 1);
        g.rectsAdd(x, 70, 63, 1);
        g.rectsAdd(x + skip, 278, 63 - skip, 1);
    }
    g.rectsEnd();
    g.alpha(255);
    // 3. Body fades (one clipped gradient each).
    for (int c = 0; c < perf::kSlots; ++c) wg::bottomFade(g, th, colX(c), 279, 62, 145, bg);
    // 4. Values, FX names and stored rows; "---" cells are collected and
    //    drawn as one batch per colour (three 6 x 1 dashes each).
    struct Dash {
        int16_t cx, cy;
    };
    Dash dashes[2][perf::kSlots * (perf::kRows + 1)];
    int nDash[2] = {0, 0};
    auto put = [&](int cx, int cy, bool hi, const char* v) {
        if (std::strcmp(v, "---") == 0) {
            dashes[hi][nDash[hi]++] = Dash{int16_t(cx), int16_t(cy)};
            return;
        }
        cell(g, cx, cy, hi ? hiC : textC, v);
    };
    for (int c = 0; c < perf::kSlots; ++c) {
        const int x = colX(c), cx = x + 32;
        const uint8_t ty = src.type(c);
        char v[12] = "";
        if (ty != 0) cellText(ty, liveValid[c] ? live[c] : cur[c], v, sizeof(v));
        put(cx, kTitleValueY, false, v[0] ? v : "");
        if (ty != 0)
            g.textRotated(cx + kLabelDx, kLabelBottom - textWidth(kFont2, perf::fx(ty).name), kFont2,
                          th.slot(slot::kPerfVertical), perf::fx(ty).name, 1, IDisplay::kOptCenterX);
        for (int r = 0; r < perf::kRows; ++r) {
            v[0] = 0;
            if (ty != 0) cellText(ty, src.value(c, r), v, sizeof(v));
            put(cx, kRowY + 30 * r, src.row(c) == r, v);
        }
    }
    g.alpha(222);
    for (int hi = 0; hi < 2; ++hi) {
        if (!nDash[hi]) continue;
        g.rectsBegin(hi ? hiC : textC);
        for (int i = 0; i < nDash[hi]; ++i)
            for (int k = 0; k < 3; ++k) g.rectsAdd(dashes[hi][i].cx - 14 + 10 * k, dashes[hi][i].cy + 1, 6, 1);
        g.rectsEnd();
    }
    g.alpha(255);
    // [I] Scenes: a bar on the left edge of the row scene A stores, on the
    // right edge of the row B stores (the fader's sides in the title bar;
    // A red, B blue),
    // as tall as that scene's share of the mix at the fader (sceneShare),
    // from the cell's bottom; never below kSceneBarMinH, so the rows stay marked.
    for (int sc = 0; sc < 2 && view == View::Play; ++sc) {   // one batch per scene colour
        const Project& p = app_.project;
        bool any = false;
        for (int c = 0; c < perf::kSlots; ++c) {
            const int r = p.sceneOn(sc)[c];
            if (src.type(c) == 0 || r < 1 || r > 3) continue;
            if (!any) g.rectsBegin(sc ? kSceneBBlue : th.accent);
            any = true;
            const int h = kSceneBarMinH + ((kSceneBarH - kSceneBarMinH) * sceneShare(sc, c) + kFaderMax / 2) / kFaderMax;
            const int bottom = kRowY + 30 * r + 10;
            g.rectsAdd(sc ? colX(c) + kSceneBarRightDx : colX(c) + kSceneBarLeftDx, bottom - h + 1, kSceneBarW, h);
        }
        if (any) g.rectsEnd();
    }
    // 5. Edit mode: the column being edited carries the red focus frame [I].
    //    Column by column, the next column was drawn over the frame's right
    //    side (its edge at x + 64 and first pixel column x + 65): redraw that
    //    strip in the next column's order (edge, body, blended rows, fade).
    if (edit && column >= 0 && column < perf::kSlots) {
        const int x = colX(column);
        wg::focusFrame(g, th, x - 1, 31, x + 63, 422);
        if (column + 1 < perf::kSlots) {
            const int nx = colX(column + 1);   // x + 65
            g.alpha(wg::kEdgeA);
            g.rectsBegin(bg);
            g.rectsAdd(nx - 1, 34, 1, 37);
            g.rectsAdd(nx - 1, 279, 1, 146);
            g.rectsEnd();
            g.alpha(255);
            g.rectsBegin(bg);
            g.rectsAdd(nx, 34, 1, 37);
            g.rectsAdd(nx, 279, 1, 146);
            g.rectsEnd();
            g.alpha(wg::kEdgeA);
            g.rectsBegin(bg);
            g.rectsAdd(nx, 33, 1, 1);
            g.rectsAdd(nx, 70, 1, 1);
            g.rectsAdd(nx, 278, 1, 1);
            g.rectsEnd();
            g.alpha(255);
            wg::bottomFade(g, th, nx, 279, 62, 145, bg);   // opaque, no text there
        }
    }
    // Labels of tracks with performance FX turned off are lit red (stock 1.9.2,
    // owner photo 2026-10-23: all tracks start enabled and unlit, spec 06 §3.1
    // step 3; a track key release turns a track off and its label red). One
    // batched RECTS list (per-label fills cost ~10 display-list words each).
    {
        bool any = false;
        for (int t = 0; t < 8; ++t) {
            if (ui_.bar.label[t].highlight) continue;   // highlight = FX on
            if (!any) g.rectsBegin(th.slot(slot::kFocusFrame));
            any = true;
            g.rectsAdd(100 * t + 3, 424, 95, 56);
        }
        if (any) g.rectsEnd();
    }
    for (int t = 0; t < 8; ++t) {
        const BarLabel& l = ui_.bar.label[t];
        const int cx = wg::labelCentre(t);
        uint32_t tc = th.slot(l.enabled && l.highlight ? slot::kBottomLabel : slot::kBottomLabelOff);
        uint32_t vc = th.slot((l.enabled ? slot::kBottomLabel : slot::kBottomLabelOff) + 3);
        if (!l.highlight) {
            // On the red fill: white text while audible, black when muted
            // (manual p.272 text colours).
            tc = vc = l.enabled ? 0xFFFFFFu : 0x000000u;
        }
        g.text(cx, wg::kLabelY1, kFont3, tc, l.top, IDisplay::kOptCenter);
        const bool queued = pending_[t] || app_.seq.queuedTrackPattern(t) != 0;
        if (!queued || blinkOn_ || ui_.shift()) g.text(cx, wg::kLabelY2, kFont2, vc, l.bottom, IDisplay::kOptCenter);
    }
}

// [I] Scene bank page: 2 rows of 8 scenes. A cell: the number (dim when
// empty), an A / B tag for the scenes on the fader (A red, B blue, as the
// scene marks), and 12 marks, one per Performance column, as tall as the
// row the scene stores there (a dim dot: not driven).
void PerformanceScreen::drawBank(Gfx& g) {
    const Theme& th = ui_.th();
    const Project& p = app_.project;
    char right[24];
    std::snprintf(right, sizeof(right), "A %d   B %d", p.perfSceneOn[0] + 1, p.perfSceneOn[1] + 1);
    ui_.drawTitle(g, "Scenes", right);
    constexpr int kW = 92, kH = 176, kX0 = 8, kPitchX = 98, kY0 = 40, kPitchY = 192;
    auto cellX = [](int i) { return kX0 + kPitchX * (i % 8); };
    auto cellY = [](int i) { return kY0 + kPitchY * (i / 8); };
    const uint32_t bg = th.slot(slot::kPerfPanels + 1);
    g.rectsBegin(bg);
    for (int i = 0; i < Project::kPerfScenes; ++i) g.rectsAdd(cellX(i), cellY(i), kW, kH);
    g.rectsEnd();
    bool stored[Project::kPerfScenes] = {};
    for (int i = 0; i < Project::kPerfScenes; ++i)
        for (uint8_t r : p.perfScene[i]) stored[i] = stored[i] || r;
    // Marks: dim dots for columns not driven, bars for stored rows.
    g.rectsBegin(th.dim);
    for (int i = 0; i < Project::kPerfScenes; ++i)
        for (int c = 0; c < perf::kSlots; ++c)
            if (!p.perfScene[i][c]) g.rectsAdd(cellX(i) + 4 + 7 * c, cellY(i) + kH - 14, 5, 1);
    g.rectsEnd();
    g.rectsBegin(th.text);
    for (int i = 0; i < Project::kPerfScenes; ++i)
        for (int c = 0; c < perf::kSlots; ++c)
            if (const int r = p.perfScene[i][c]) g.rectsAdd(cellX(i) + 4 + 7 * c, cellY(i) + kH - 13 - 14 * r, 5, 14 * r);
    g.rectsEnd();
    for (int i = 0; i < Project::kPerfScenes; ++i) {
        const int x = cellX(i), y = cellY(i);
        g.textf(x + kW / 2, y + 40, kFontLarge, stored[i] ? th.text : th.dim, IDisplay::kOptCenter, "%d", i + 1);
        const bool onA = i == p.perfSceneOn[0], onB = i == p.perfSceneOn[1];
        if (onA) g.text(onB ? x + kW / 2 - 10 : x + kW / 2, y + 80, kFontText, th.accent, "A", IDisplay::kOptCenter);
        if (onB) g.text(onA ? x + kW / 2 + 10 : x + kW / 2, y + 80, kFontText, kSceneBBlue, "B", IDisplay::kOptCenter);
    }
    wg::focusFrame(g, th, cellX(bankCursor) - 1, cellY(bankCursor) - 1, cellX(bankCursor) + kW, cellY(bankCursor) + kH);
    for (int t = 0; t < 8; ++t) {
        const BarLabel& l = ui_.bar.label[t];
        if (!l.top[0]) continue;
        g.text(wg::labelCentre(t), wg::kLabelY, kFont3, th.slot(slot::kBottomLabel), l.top, IDisplay::kOptCenter);
    }
}

}  // namespace ot
