// [I] #3 Multisample: the SFZ reader, zone choice and layout, multisample.bin,
// and the voice's Multi play.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "audio/audio_tables.h"
#include "audio/voice.h"
#include "formats/multisample_side.h"
#include "formats/sfz.h"
#include "mem_storage.h"
#include "model/multisample.h"
#include "test.h"
#include "ui_fakes.h"

#include "formats/wav.h"
#include "ui/screens.h"

using namespace ot;
using namespace ot::audio;

namespace {
std::string pathOf(const char* text, const sfz::File& f, int i, const char* sfzPath) {
    char out[256];
    if (!sfz::samplePath(text, f, f.regions[i], sfzPath, out, sizeof(out))) return "<fail>";
    return out;
}
}  // namespace

// ---- SFZ ----------------------------------------------------------------------

TEST(sfz_note_values) {
    CHECK_EQ(sfz::noteValue("60", 2), 60);
    CHECK_EQ(sfz::noteValue("c4", 2), 60);
    CHECK_EQ(sfz::noteValue("C4", 2), 60);
    CHECK_EQ(sfz::noteValue("c#4", 3), 61);
    CHECK_EQ(sfz::noteValue("db4", 3), 61);
    CHECK_EQ(sfz::noteValue("c-1", 3), 0);
    CHECK_EQ(sfz::noteValue("b-1", 3), 11);
    CHECK_EQ(sfz::noteValue("g9", 2), 127);
    CHECK_EQ(sfz::noteValue("a9", 2), -1);
    CHECK_EQ(sfz::noteValue("128", 3), -1);
    CHECK_EQ(sfz::noteValue("x4", 2), -1);
    CHECK_EQ(sfz::noteValue("", 0), -1);
}

TEST(sfz_from_mars_region) {
    // A Junos From Mars line: a path with spaces and backslashes, loops and
    // other opcodes we skip.
    const char* t =
        "//// Samples From Mars (c) 2024/////\r\n\r\n"
        "<region> sample=..\\..\\WAV\\02. Bass\\Funk Bass\\024 Funk Bass Junos C0.wav lokey=0 hikey=24 "
        "end=118661 loop_mode=loop_continuous loopstart=51736 loopend=81446 pitch_keycenter=24 volume=-6\r\n\r\n"
        "<region> sample=..\\..\\WAV\\02. Bass\\Funk Bass\\025 Funk Bass Junos C#0.wav lokey=25 hikey=25 "
        "end=118645 pitch_keycenter=25\r\n";
    static sfz::File f;
    CHECK(sfz::parse(t, uint32_t(std::strlen(t)), f));
    CHECK_EQ(f.count, 2);
    CHECK_EQ(f.regions[0].lokey, 0);
    CHECK_EQ(f.regions[0].hikey, 24);
    CHECK_EQ(f.regions[0].root, 24);
    CHECK_EQ(f.regions[0].end, 118661u);
    CHECK_EQ(f.regions[1].root, 25);
    CHECK(pathOf(t, f, 0, "Samples/mars/Presets/SFZ/Funk Bass.sfz") ==
          "Samples/mars/WAV/02. Bass/Funk Bass/024 Funk Bass Junos C0.wav");
    CHECK(pathOf(t, f, 1, "/Samples/mars/Presets/SFZ/Funk Bass.sfz") ==
          "/Samples/mars/WAV/02. Bass/Funk Bass/025 Funk Bass Junos C#0.wav");
    // Climbing above the card's root fails.
    CHECK(pathOf(t, f, 0, "a.sfz") == "<fail>");
}

TEST(sfz_levels_and_comments) {
    const char* t =
        "<control> default_path=Samples/Piano/\n"
        "/* block\n comment <region> sample=no.wav */\n"
        "<global> lokey=10\n"
        "<group> pitch_keycenter=c3 offset=100\n"
        "<region> sample=a.wav hikey=50 // a comment\n"
        "<region> sample=b.wav key=d3 end=999\n"
        "<group>\n"
        "<region> sample=c.wav\n"
        "<region> lokey=1\n"            // no sample: dropped
        "<curve> sample=ignored.wav\n"  // not ours
        "#define $X 1\n";
    static sfz::File f;
    CHECK(sfz::parse(t, uint32_t(std::strlen(t)), f));
    CHECK_EQ(f.count, 3);
    CHECK_EQ(f.dropped, 1);
    const sfz::Region& a = f.regions[0];
    CHECK_EQ(a.lokey, 10);   // from <global>
    CHECK_EQ(a.hikey, 50);
    CHECK_EQ(a.root, 48);    // c3, from <group>
    CHECK_EQ(a.offset, 100u);
    CHECK_EQ(a.end, sfz::kToEnd);
    const sfz::Region& b = f.regions[1];
    CHECK_EQ(b.lokey, 10);   // the group's lokey wins over key
    CHECK_EQ(b.hikey, 50);   // from key
    CHECK_EQ(b.root, 48);    // pitch_keycenter (group) wins over key
    CHECK_EQ(b.end, 999u);
    const sfz::Region& c = f.regions[2];
    CHECK_EQ(c.lokey, 10);   // a new <group> starts from <global>
    CHECK_EQ(c.hikey, 127);
    CHECK_EQ(c.root, 60);    // SFZ's default centre
    CHECK_EQ(c.offset, 0u);
    CHECK(pathOf(t, f, 0, "Samples/Piano/piano.sfz") == "Samples/Piano/Samples/Piano/a.wav");
    // A region of one key without a centre plays that key unpitched.
    const char* k = "<region> sample=x.wav key=64";
    CHECK(sfz::parse(k, uint32_t(std::strlen(k)), f));
    CHECK_EQ(f.regions[0].root, 64);
    CHECK_EQ(f.regions[0].lokey, 64);
    CHECK_EQ(f.regions[0].hikey, 64);
    // Nothing to play.
    const char* e = "// just a comment\n<group> lokey=1";
    CHECK(!sfz::parse(e, uint32_t(std::strlen(e)), f));
}

TEST(sfz_generated_by_make_sfz) {
    // tools/make_sfz.py output.
    const char* t =
        "// Written by OpenTracker tools/make_sfz.py: one region per note sample.\n<group>\n\n"
        "<region> sample=000 Dusty Pluck Junos C-2.wav lokey=0 hikey=0 pitch_keycenter=0\n"
        "<region> sample=001 Dusty Pluck Junos C#-2.wav lokey=1 hikey=127 pitch_keycenter=1\n";
    static sfz::File f;
    CHECK(sfz::parse(t, uint32_t(std::strlen(t)), f));
    CHECK_EQ(f.count, 2);
    CHECK(pathOf(t, f, 1, "Samples/mars/Junos/Dusty Pluck/Dusty Pluck.sfz") ==
          "Samples/mars/Junos/Dusty Pluck/001 Dusty Pluck Junos C#-2.wav");
}

// ---- zone choice and layout ------------------------------------------------------

namespace {
std::vector<ms::Candidate> chromatic(int lo, int hi, uint32_t frames) {
    std::vector<ms::Candidate> c;
    for (int k = lo; k <= hi; ++k) c.push_back(ms::Candidate{uint8_t(k), uint8_t(k - lo), frames});
    return c;
}
}  // namespace

TEST(multisample_unique_by_root) {
    ms::Candidate c[5] = {{60, 0, 1}, {48, 1, 2}, {60, 2, 3}, {50, 3, 4}, {48, 4, 5}};
    const int n = ms::uniqueByRoot(c, 5);
    CHECK_EQ(n, 3);
    CHECK_EQ(c[0].root, 48);
    CHECK_EQ(c[0].region, 1);   // the first region per root stays
    CHECK_EQ(c[1].root, 50);
    CHECK_EQ(c[2].root, 60);
    CHECK_EQ(c[2].region, 0);
}

TEST(multisample_plan_every_third) {
    auto c = chromatic(0, 108, 44100);
    ms::Options o;
    o.lo = 36;
    o.hi = 84;
    o.every = 2;   // 3rd
    ms::Plan p;
    ms::plan(c.data(), int(c.size()), o, p);
    CHECK_EQ(p.count, 17);   // 36, 39, ... 84
    CHECK(!p.tooMany);
    CHECK_EQ(p.root[0], 36);
    CHECK_EQ(p.root[16], 84);
    // Max length cuts every zone.
    o.length = 8;   // 0.5 s
    ms::plan(c.data(), int(c.size()), o, p);
    CHECK_EQ(p.frames[3], 22050u);
    // Every note over 4 octaves: 49 targets, more than 48 zones.
    o.every = 0;
    ms::plan(c.data(), int(c.size()), o, p);
    CHECK(p.tooMany);
    CHECK_EQ(p.count, kMultiMaxZones);
}

TEST(multisample_plan_sparse_pack) {
    // A pack sampled every 4th note: every 3rd target picks the nearest one,
    // repeats dropped.
    std::vector<ms::Candidate> c;
    for (int k = 36; k <= 84; k += 4) c.push_back(ms::Candidate{uint8_t(k), uint8_t(c.size()), 1000});
    ms::Options o;
    o.lo = 36;
    o.hi = 84;
    o.every = 0;   // every note: each sample once
    ms::Plan p;
    ms::plan(c.data(), int(c.size()), o, p);
    CHECK_EQ(p.count, int(c.size()));
    for (int i = 0; i < p.count; ++i) CHECK_EQ(p.root[i], 36 + 4 * i);
    // Targets outside the pack's range clamp to its ends.
    o.lo = 0;
    o.hi = 127;
    ms::plan(c.data(), int(c.size()), o, p);
    CHECK_EQ(p.root[0], 36);
    CHECK_EQ(p.root[p.count - 1], 84);
}

TEST(multisample_layout_exact_starts) {
    // Zone starts must equal the voice's slice start for its point,
    // len * point / 65535, for awkward lengths.
    const uint32_t lens[] = {1, 7, 12345, 44100, 99991, 1000003};
    for (uint32_t base : lens) {
        ms::Plan p;
        p.count = 40;
        for (int i = 0; i < p.count; ++i) p.frames[i] = base + uint32_t(i * 977);
        ms::layout(p);
        uint64_t sum = 0;
        for (int i = 0; i < p.count; ++i) {
            CHECK_EQ(p.start[i], uint32_t(uint64_t(p.totalFrames) * p.point[i] / 65535u));
            if (i > 0) CHECK(p.start[i] >= p.start[i - 1] + p.frames[i - 1]);   // no overlap
            sum += p.frames[i];
        }
        CHECK(p.start[p.count - 1] + p.frames[p.count - 1] <= p.totalFrames);
        // Padding stays small: under count * (len / 65535 + 3) frames.
        CHECK(p.totalFrames - sum <= uint64_t(p.count) * (p.totalFrames / 65535 + 3));
    }
}

TEST(multisample_defaults_fit_budget) {
    // 109 notes of 5 s: every 3rd C2..C6 = 17 zones.
    auto c = chromatic(0, 108, 5 * 44100);
    ms::Options o;
    CHECK(ms::defaults(c.data(), int(c.size()), 100u * 44100u, o));
    CHECK_EQ(o.lo, 36);
    CHECK_EQ(o.hi, 84);
    CHECK_EQ(int(o.every), 2);
    CHECK_EQ(int(o.length), 0);   // 85 s fits 100 s
    CHECK(ms::defaults(c.data(), int(c.size()), 40u * 44100u, o));
    CHECK_EQ(int(o.every), 2);
    CHECK_EQ(int(o.length), 5);   // 2 s x 17 = 34 s
    CHECK(ms::defaults(c.data(), int(c.size()), 5u * 44100u, o));
    CHECK_EQ(int(o.every), 4);    // 6th note, 9 zones x 0.5 s
    CHECK(!ms::defaults(c.data(), int(c.size()), 44100u, o));
    // A pack below C2 keeps its own range.
    auto sub = chromatic(12, 30, 1000);
    CHECK(ms::defaults(sub.data(), int(sub.size()), 1u << 30, o));
    CHECK_EQ(o.lo, 12);
    CHECK_EQ(o.hi, 30);
}

TEST(multisample_zone_for_note) {
    MultiMap m{};
    m.on = 1;
    m.count = 3;
    m.root[0] = 48;
    m.root[1] = 51;
    m.root[2] = 54;
    CHECK_EQ(multiZone(m, 0), 0);
    CHECK_EQ(multiZone(m, 49), 0);
    CHECK_EQ(multiZone(m, 50), 1);
    CHECK_EQ(multiZone(m, 52), 1);
    CHECK_EQ(multiZone(m, 53), 2);
    CHECK_EQ(multiZone(m, 127), 2);
    m.root[1] = 52;   // a tie (50: 48 and 52) takes the lower
    CHECK_EQ(multiZone(m, 50), 0);
    m.count = 1;
    CHECK_EQ(multiZone(m, 100), 0);
}

// ---- multisample.bin ---------------------------------------------------------------

TEST(multisample_side_round_trip) {
    for (int s = 0; s < kSampleInstrumentCount; ++s) multiClear(s);
    CHECK(!multiMapsUsed());
    MemStorage fs;
    CHECK(saveMultiMapsFile(fs, "P/project.mt") == FmtStatus::Ok);
    CHECK(!fs.exists("P/multisample.bin"));   // nothing to write
    MultiMap& a = multiMap(3);
    a.on = 1;
    a.count = 3;
    a.root[0] = 40;
    a.root[1] = 43;
    a.root[2] = 46;
    MultiMap& b = multiMap(47);
    b.on = 0;
    b.count = 48;
    for (int i = 0; i < 48; ++i) b.root[i] = uint8_t(20 + i);
    CHECK(multiMapsUsed());
    CHECK(saveMultiMapsFile(fs, "P/project.mt") == FmtStatus::Ok);
    CHECK(fs.exists("P/multisample.bin"));
    CHECK(copyMultiMapsFile(fs, "P/project.mt", "Q/project.mt") == FmtStatus::Ok);
    for (int s = 0; s < kSampleInstrumentCount; ++s) multiClear(s);
    CHECK(loadMultiMapsFile(fs, "Q/project.mt") == FmtStatus::Ok);
    CHECK_EQ(multiMap(3).on, 1);
    CHECK_EQ(multiMap(3).count, 3);
    CHECK_EQ(multiMap(3).root[2], 46);
    CHECK_EQ(multiMap(47).on, 0);
    CHECK_EQ(multiMap(47).count, 48);
    CHECK_EQ(multiMap(47).root[47], 67);
    CHECK_EQ(multiMap(0).count, 0);
    // A damaged file clears the maps.
    uint8_t buf[msside::kMaxFileSize];
    const uint32_t n = encodeMultiMaps(buf);
    buf[msside::kHeaderSize + 3] ^= 1;
    CHECK(decodeMultiMaps(buf, n) == FmtStatus::BadMagic);
    CHECK(!multiMapsUsed());
    // No slot left: the file is removed on save.
    CHECK(saveMultiMapsFile(fs, "P/project.mt") == FmtStatus::Ok);
    CHECK(!fs.exists("P/multisample.bin"));
}

// ---- voice -------------------------------------------------------------------------

namespace {
std::vector<int16_t> rampSample(uint32_t len = 65535) {
    std::vector<int16_t> d(len);
    for (uint32_t i = 0; i < len; ++i) d[i] = int16_t(int32_t(i) - 32768);
    return d;
}
int idxOf(float v) { return int(std::lround(double(v) * 32768.0)) + 32768; }
}  // namespace

TEST(multisample_voice_plays_nearest_zone) {
    initTables();
    auto data = rampSample();
    VoiceControl c;
    c.antiAliasing = false;
    Instrument ins;
    ins.lengthFrames = 65535;
    ins.playMode = uint8_t(PlayMode::Slice);
    ins.env[0].enabled = 0;
    ins.sliceCount = 3;
    ins.slices[0] = 0;
    ins.slices[1] = 20000;
    ins.slices[2] = 40000;
    ins.selectedSlice = 2;   // the selection doesn't matter in Multi
    MultiMap m{};
    m.on = 1;
    m.count = 3;
    m.root[0] = 48;
    m.root[1] = 60;
    m.root[2] = 72;
    auto play = [&](uint8_t note, const MultiMap* mm, uint8_t fxT = 0, uint8_t fxV = 0) {
        Voice v;
        v.noteOn(&ins, data.data(), 65535, note, -1, fxT, fxV, 0, 0, nullptr, nullptr, nullptr, mm);
        VoiceOut o;
        float ds[kBlock] = {}, rs[kBlock] = {};
        v.control(c);
        v.render(c, o, ds, rs);
        return std::pair<int, float>(idxOf(o.amp[0]), v.rate());
    };
    auto r = play(60, &m);
    CHECK_EQ(r.first, 20000);
    CHECK_NEAR(r.second, pitchRate(60), 1e-6);   // the root: unpitched
    r = play(50, &m);
    CHECK_EQ(r.first, 0);
    CHECK_NEAR(r.second, pitchRate(62), 1e-6);   // 2 semitones over root 48
    r = play(70, &m);
    CHECK_EQ(r.first, 40000);
    CHECK_NEAR(r.second, pitchRate(58), 1e-6);   // 2 under root 72
    // Slice FX doesn't pick a zone in Multi.
    r = play(60, &m, kFxSlice, 0);
    CHECK_EQ(r.first, 20000);
    // Without the map: plain Slice (the selected slice, pitched by the note).
    r = play(60, nullptr);
    CHECK_EQ(r.first, 40000);
    CHECK_NEAR(r.second, pitchRate(60), 1e-6);
}

// ---- the Sample Loader and the App --------------------------------------------

namespace {
// A mono WAV of `frames` frames, every sample `v` (a zone tells itself apart).
std::vector<uint8_t> flatWav(uint32_t frames, int16_t v) {
    std::vector<int16_t> d(frames, v);
    std::vector<uint8_t> out(frames * 2 + 64);
    out.resize(wavEncode(d.data(), frames, 1, out.data(), out.size()));
    return out;
}
LoaderScreen& loader(Harness& h) { return *static_cast<LoaderScreen*>(h.app->ui.screen(kModLoader)); }
bool openEntry(Harness& h, const char* label) {
    LoaderScreen& l = loader(h);
    for (int i = 0; i < l.count; ++i) {
        char out[128];
        l.entryLabel(i, out, sizeof(out));
        if (std::strcmp(out, label) == 0) {
            l.sdCursor = i;
            h.tap(btn::F1);
            return true;
        }
    }
    return false;
}
// Samples/Keys: notes 54..66, 2000 frames each, value = note * 100, and the
// SFZ make_sfz.py writes for them.
void keysFolder(Harness& h) {
    h.fs.mkdir("Samples");
    h.fs.mkdir("Samples/Keys");
    std::string sfzText = "<group>\n";
    for (int n = 54; n <= 66; ++n) {
        char name[48];
        std::snprintf(name, sizeof(name), "%03d Keys.wav", n);
        h.fs.addFile(std::string("Samples/Keys/") + name, flatWav(2000, int16_t(n * 100)));
        sfzText += std::string("<region> sample=") + name + " pitch_keycenter=" + std::to_string(n) + "\n";
    }
    h.fs.addFile("Samples/Keys/Keys.sfz", std::vector<uint8_t>(sfzText.begin(), sfzText.end()));
}
}  // namespace

TEST(multisample_loader_panel_loads_zones) {
    Harness h;
    App& a = *h.app;
    keysFolder(h);
    h.tap(btn::SampleLoader);
    h.run(40);
    CHECK(openEntry(h, "/Samples"));
    CHECK(openEntry(h, "/Keys"));
    CHECK(openEntry(h, "Keys.sfz"));   // Add on the .sfz: the panel
    LoaderScreen& l = loader(h);
    CHECK(l.multi);
    // Defaults: the pack's own range inside C2..C6, every 3rd note.
    CHECK_EQ(l.multiOpt.lo, 54);
    CHECK_EQ(l.multiOpt.hi, 66);
    CHECK_EQ(int(l.multiOpt.every), 2);
    h.run(40);
    CHECK(h.disp.lastHasText("Multisample: Keys.sfz"));
    CHECK(h.disp.lastHasText("The SFZ has 13 notes"));
    CHECK(h.disp.lastHasText("Loads 5 of them"));
    std::printf("  DL words Loader Multi panel %u\n", unsigned(a.ui.lastDlWords()));
    CHECK(a.ui.lastDlWords() < 2048);   // FT81x RAM_DL
    h.tap(btn::F8);   // Load
    h.run(40);
    CHECK(!l.multi);
    CHECK(a.instrPresent[0]);
    if (!a.instrPresent[0]) return;
    const Instrument& in = a.instruments[0];
    CHECK_EQ(in.playMode, uint8_t(PlayMode::Slice));
    CHECK_EQ(in.sliceCount, 5);
    CHECK(std::strcmp(in.name, "Keys") == 0);
    const MultiMap& m = multiMap(0);
    CHECK_EQ(m.on, 1);
    CHECK_EQ(m.count, 5);
    const uint8_t want[5] = {54, 57, 60, 63, 66};
    const int16_t* d = a.bank.left(0);
    const uint32_t len = a.bank.frames(0);
    for (int z = 0; z < 5; ++z) {
        CHECK_EQ(m.root[z], want[z]);
        // The slice point lands on the zone's first frame, exactly.
        const uint32_t st = uint32_t(uint64_t(len) * in.slices[z] / 65535u);
        CHECK_EQ(d[st], int16_t(want[z] * 100));
        CHECK_EQ(d[st + 1999], int16_t(want[z] * 100));
    }
    CHECK(multiActive(m, in));

    // Saved with the project, gone in a new one, back when it opens.
    CHECK(a.saveProjectAs("ms"));
    CHECK(h.fs.exists("Projects/ms/multisample.bin"));
    CHECK(h.fs.exists("Workspace/multisample.bin"));
    CHECK(a.newProject());
    CHECK_EQ(multiMap(0).count, 0);
    CHECK(!h.fs.exists("Workspace/multisample.bin"));
    CHECK(a.openProject(kProjectsDir, "ms"));
    CHECK_EQ(multiMap(0).count, 5);
    CHECK_EQ(multiMap(0).root[4], 66);
    CHECK(multiActive(multiMap(0), a.instruments[0]));

    // Copy takes the zones; a new sample in the slot drops them.
    CHECK(a.copyInstrument(0, 3));
    CHECK_EQ(multiMap(3).count, 5);
    CHECK(a.importSample("Samples/Keys/060 Keys.wav", 3));
    CHECK_EQ(multiMap(3).count, 0);
    CHECK(a.deleteInstrument(0));
    CHECK_EQ(multiMap(0).count, 0);
}

TEST(multisample_playback_mode_list) {
    Harness h;
    App& a = *h.app;
    keysFolder(h);
    ms::Candidate c[ms::kMaxCandidates];
    const int n = a.multisampleProbe("Samples/Keys/Keys.sfz", c, ms::kMaxCandidates);
    CHECK_EQ(n, 13);
    CHECK_EQ(c[0].frames, 2000u);
    ms::Options o;
    o.lo = 54;
    o.hi = 66;
    o.every = 3;   // 4th: 54 58 62 66
    CHECK(a.loadMultisample("Samples/Keys/Keys.sfz", 2, c, n, o));
    CHECK_EQ(multiMap(2).count, 4);
    a.setCurrentInstrument(2);
    h.tap(btn::SamplePlayback);
    h.run(40);
    CHECK(h.disp.lastHasText("Zone"));
    CHECK(h.disp.lastHasText("Root"));
    // Play Mode down from Multi: Granular, and the zones stay for later.
    h.tap(btn::F8);   // selects Play Mode (or stays on Multi, the last item)
    h.tap(btn::F7);
    h.run(40);
    CHECK_EQ(a.instruments[2].playMode, uint8_t(PlayMode::Granular));
    CHECK_EQ(multiMap(2).on, 0);
    CHECK_EQ(multiMap(2).count, 4);
    h.tap(btn::F8);   // back up to Multi
    h.run(40);
    CHECK_EQ(a.instruments[2].playMode, uint8_t(PlayMode::Slice));
    CHECK_EQ(multiMap(2).on, 1);
    // An instrument without zones never gets past Granular.
    CHECK(a.importSample("Samples/Keys/060 Keys.wav", 5));
    a.setCurrentInstrument(5);
    a.instruments[5].playMode = uint8_t(PlayMode::Granular);
    h.tap(btn::SamplePlayback);
    h.run(40);
    h.tap(btn::F8);
    h.tap(btn::F8);
    h.run(40);
    CHECK_EQ(a.instruments[5].playMode, uint8_t(PlayMode::Granular));
}
