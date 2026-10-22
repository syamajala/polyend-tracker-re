// Render DX7 .syx voices through the DX7 engine (audio/engines/dx7.*) to
// WAV files, for listening on the host [OT].
//
//   dx7_render [-o outdir] [--notes 48,60,72] [--len 1.5] [--vel 0.8] [--gain 0.5] file.syx...
//
// Every voice of every file renders one WAV: the notes one after the other
// (note k starts at 3 k s, held --len s), then all of them as a chord held
// --len s plus 3 s of release. compare_midi.py writes the same schedule as
// a MIDI file, to render the voices in Dexed for an A/B (compare.py). Mono, 16 bit, at the engine's sample rate (44117.647 Hz,
// written as 44118).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/engines/dx7.h"

using namespace ot;
using namespace ot::audio;

namespace {

struct Wav {
    std::vector<int16_t> s;
    int clipped = 0;
    float peak = 0.0f;
    void add(const float* x, int n) {
        for (int i = 0; i < n; ++i) {
            float v = x[i];
            peak = std::fmax(peak, std::fabs(v));
            if (v > 1.0f) { v = 1.0f; ++clipped; }
            if (v < -1.0f) { v = -1.0f; ++clipped; }
            s.push_back(int16_t(std::lrint(v * 32767.0f)));
        }
    }
    bool write(const std::string& path) const {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;
        auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        const uint32_t bytes = uint32_t(s.size() * 2);
        std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16); u16(1); u16(1); u32(44118); u32(44118 * 2); u16(2); u16(16);
        std::fwrite("data", 1, 4, f); u32(bytes);
        std::fwrite(s.data(), 2, s.size(), f);
        std::fclose(f);
        return true;
    }
};

struct Rig {
    ISynthEngine* e = dx7::engine();
    SynthSlotScratch scratch{};
    SynthParams p;
    SynthVoice v[kSynthMaxVoices];
    bool alive[kSynthMaxVoices] = {};
    float gain = 1.0f;
    explicit Rig(const uint8_t* voice) {
        e->slotReset(scratch);
        dx7::setVoice(scratch, voice);
        for (auto& x : v) { x = SynthVoice{}; x.slotScratch = &scratch; }
    }
    void on(int k, int note, float vel) {
        v[k] = SynthVoice{};
        v[k].slotScratch = &scratch;
        v[k].note = uint8_t(note);
        v[k].gate = true;
        SynthNote n;
        n.note = uint8_t(note);
        n.pitch = float(note);
        n.velocity = vel;
        e->noteOn(v[k], p, n);
        alive[k] = true;
    }
    void off(int k) { v[k].gate = false; e->noteOff(v[k], p); }
    void block(Wav& w) {
        float L[kBlock] = {}, R[kBlock] = {};
        e->prepare(scratch, p);
        for (int k = 0; k < kSynthMaxVoices; ++k)
            if (alive[k]) alive[k] = e->render(v[k], p, gain, 0.0f, L, R);
        w.add(L, kBlock);
    }
    bool any() const { for (bool a : alive) if (a) return true; return false; }
};

int blocks(float sec) { return int(sec * kSampleRate / float(kBlock) + 0.5f); }

constexpr float kSlot = 3.0f;   // seconds per single note
constexpr float kTail = 3.0f;   // release after the chord

}  // namespace

int main(int argc, char** argv) {
    std::string outDir = ".";
    std::vector<int> notes = {48, 60, 72};
    float len = 1.5f, vel = 0.8f, gain = 0.5f;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--len" && i + 1 < argc) len = std::strtof(argv[++i], nullptr);
        else if (a == "--vel" && i + 1 < argc) vel = std::strtof(argv[++i], nullptr);
        else if (a == "--gain" && i + 1 < argc) gain = std::strtof(argv[++i], nullptr);
        else if (a == "--notes" && i + 1 < argc) {
            notes.clear();
            for (char* t = std::strtok(argv[++i], ","); t; t = std::strtok(nullptr, ",")) notes.push_back(std::atoi(t));
        } else files.push_back(a);
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: dx7_render [-o dir] [--notes 48,60] [--len s] [--vel 0..1] [--gain g] file.syx...\n");
        return 2;
    }
    for (const std::string& path : files) {
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        uint8_t voices[dx7::kBankVoices][dx7::kVoiceBytes];
        bool csOk = true;
        const int n = dx7::parseSysex(data.data(), data.size(), voices, dx7::kBankVoices, &csOk);
        if (n == 0) { std::fprintf(stderr, "%s: not a DX7 voice or bank dump\n", path.c_str()); continue; }
        for (int vi = 0; vi < n; ++vi) {
            char name[dx7::kNameLen + 1] = {};
            std::memcpy(name, voices[vi] + 145, dx7::kNameLen);
            for (int c = dx7::kNameLen - 1; c >= 0 && name[c] == ' '; --c) name[c] = 0;
            std::string safe;
            for (char* c = name; *c; ++c) safe += (std::isalnum(uint8_t(*c)) ? *c : '_');
            Rig rig(voices[vi]);
            rig.gain = gain;
            Wav w;
            // Fixed schedule (tools/dx7_render/compare_midi.py writes the same
            // one as a MIDI file): note k on at k x kSlot s, off after len s;
            // then the chord, held len s, and kTail s of release.
            float notePeak = 0.0f;
            int at = 0;   // blocks rendered
            auto until = [&](float sec) { for (; at < blocks(sec); ++at) rig.block(w); };
            for (size_t k = 0; k < notes.size(); ++k) {
                until(kSlot * float(k));
                rig.on(0, notes[k], vel);
                until(kSlot * float(k) + len);
                rig.off(0);
            }
            const float chord = kSlot * float(notes.size());
            until(chord);
            notePeak = w.peak;
            for (size_t k = 0; k < notes.size() && k < size_t(kSynthMaxVoices); ++k) rig.on(int(k), notes[k], vel);
            until(chord + len);
            for (size_t k = 0; k < notes.size() && k < size_t(kSynthMaxVoices); ++k) rig.off(int(k));
            until(chord + len + kTail);
            char fn[64];
            std::snprintf(fn, sizeof fn, "%02d_%s.wav", vi + 1, safe.c_str());
            const std::string outPath = outDir + "/" + (n == 1 ? std::string(path.substr(path.find_last_of('/') + 1)) + ".wav" : fn);
            w.write(outPath);
            std::printf("%-12s peak: notes %.2f, chord %.2f%s%s -> %s\n", name, notePeak, w.peak, w.clipped ? " CLIPPED" : "",
                        csOk ? "" : " (bad checksum)", outPath.c_str());
        }
    }
    return 0;
}
