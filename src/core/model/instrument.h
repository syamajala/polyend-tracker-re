// In-memory sample instrument (spec 02 §6.2) and MIDI instrument (spec 02 §3.3).
#pragma once

#include <cstdint>

#include "model/constants.h"

namespace ot {

enum class SampleType : uint8_t { WaveFile = 0, Wavetable = 1 };

enum class PlayMode : uint8_t {
    OneShot = 0,
    ForwardLoop = 1,
    BackwardLoop = 2,
    PingPongLoop = 3,
    Slice = 4,
    BeatSlice = 5,
    Wavetable = 6,
    Granular = 7,
};

enum class FilterType : uint8_t { LowPass = 0, HighPass = 1, BandPass = 2 };
enum class LfoShape : uint8_t { RevSaw = 0, Saw = 1, Triangle = 2, Square = 3, Random = 4 };
enum class GrainShape : uint8_t { Square = 0, Triangle = 1, Gauss = 2 };
enum class GranularLoop : uint8_t { Forward = 0, Backward = 1, PingPong = 2 };

// Automation targets; index into Instrument::env / Instrument::lfo.
enum AutomationTarget : uint8_t {
    kAutoVolume = 0,
    kAutoPanning = 1,
    kAutoCutoff = 2,
    kAutoWavetablePos = 3,
    kAutoGranularPos = 4,
    kAutoFinetune = 5,
    kAutomationCount = 6,
};

constexpr int kSliceCount = 48;
constexpr int kInstrumentNameLen = 31;  // max chars, spec 02 §6.2 body 0x05

struct Envelope {
    float amount = 1.0f;
    uint16_t delayMs = 0;
    uint16_t attackMs = 0;
    uint16_t holdMs = 0;
    uint16_t decayMs = 0;
    float sustain = 1.0f;
    uint16_t releaseMs = 1000;
    uint8_t loop = 0;      // "use LFO instead of envelope"
    uint8_t enabled = 0;
};

struct Lfo {
    uint8_t shape = uint8_t(LfoShape::Triangle);
    uint8_t speed = 0;     // 0..28 (128 bars .. 1/64)
    float amount = 0.5f;
};

// Sample instrument parameters (the .pti body). Field defaults follow the
// firmware new-instrument initialiser (spec 02 §6.2 "fw default").
struct Instrument {
    uint8_t active = 1;
    uint8_t sampleType = uint8_t(SampleType::WaveFile);
    char name[kInstrumentNameLen + 1] = {};  // NUL-terminated
    uint32_t lengthFrames = 0;
    uint16_t wavetableWindowSize = 2048;
    uint32_t wavetableWindowCount = 0;
    uint32_t reserved38 = 0;
    uint8_t playMode = uint8_t(PlayMode::OneShot);
    uint16_t startPoint = 0;
    uint16_t loopPoint1 = 1;
    uint16_t loopPoint2 = 65534;
    uint16_t endPoint = 65535;
    uint32_t wavetableCurrentWindow = 0;
    Envelope env[kAutomationCount];
    Lfo lfo[kAutomationCount];
    float cutoff = 1.0f;
    float resonance = 0.0f;
    uint8_t filterType = uint8_t(FilterType::LowPass);
    uint8_t filterEnabled = 0;
    int8_t tune = 0;
    int8_t finetune = 0;
    uint8_t volume = 50;
    int16_t panning = 50;
    uint8_t delaySend = 0;
    uint16_t slices[kSliceCount] = {};
    uint8_t sliceCount = 0;
    uint8_t selectedSlice = 0;
    uint16_t grainLength = 441;
    uint16_t granularPosition = 0;
    uint8_t grainShape = uint8_t(GrainShape::Square);
    uint8_t granularLoop = uint8_t(GranularLoop::Forward);
    uint8_t reverbSend = 0;
    uint8_t overdrive = 0;
    uint8_t bitDepth = 16;
    uint8_t reserved173 = 0;

    // Runtime sample state (not stored in the body).
    bool stereo = false;

    Instrument() { setDefault(); }

    void setDefault() {
        *this = Instrument(NoInit{});
        // env0 enabled, attack 0; env1..5 disabled, attack 3000 (spec 02 §6.2).
        for (int i = 0; i < kAutomationCount; ++i) {
            env[i] = Envelope{};
            env[i].attackMs = i == 0 ? 0 : 3000;
            env[i].enabled = i == 0 ? 1 : 0;
            lfo[i] = Lfo{};
        }
    }

private:
    struct NoInit {};
    explicit Instrument(NoInit) {}
};

// MIDI instrument record (16 bytes each in project.mt at 0x4FD, spec 02 §3.3).
struct MidiInstrument {
    uint8_t channel = 0;
    uint8_t velocity = 127;
    uint8_t cc[6] = {1, 5, 7, 11, 64, 128};  // CC numbers for MIDI CC FX a..f
    uint8_t extra[8] = {};
};

// [I] MIDI LFO, one per MIDI instrument (not in stock or the Tracker Mini):
// a tempo-locked LFO sent as CC, pitch bend or channel pressure on the
// instrument's channel while the sequencer plays. It targets a CC slot A..F
// and follows the slot's CC (#13: the CCs are picked, with their names, on
// the slots). Stored in the project folder's midi_lfo.bin
// (formats/midi_lfo.h), not in project.mt.
struct MidiLfo {
    static constexpr uint8_t kTargetOff = 0;          // 1..6 = CC slot A..F
    static constexpr uint8_t kTargetPitchBend = 7;
    static constexpr uint8_t kTargetPressure = 8;     // channel pressure (aftertouch)
    static constexpr int kTargetCount = 9;
    // What the LFO sends (wire()): 1..128 = CC 0..127.
    static constexpr uint8_t kWireNone = 0;
    static constexpr uint8_t kWirePitchBend = 129;
    static constexpr uint8_t kWirePressure = 130;
    static constexpr int kShapeCount = 5;             // Rev saw, Saw, Triangle, Square, Random (as sample LFOs)
    static constexpr int kRateCount = 23;             // audio RATE table 128 steps .. 1/8 step
    static constexpr int kRetrigCount = 2;            // 0 Free (song position), 1 Note
    static constexpr int kDepthMax = 64;              // half the CC range: center 64 sweeps 0..127

    uint8_t target = kTargetOff;
    uint8_t shape = 2;     // Triangle
    uint8_t rate = 8;      // 8 steps per cycle
    uint8_t depth = 32;    // 0..kDepthMax: the swing either side of the center (pitch bend: x128)
    uint8_t center = 64;   // 0..127 (pitch bend: x128, 64 = no bend)
    uint8_t retrig = 0;

    bool active() const { return target != kTargetOff && target < kTargetCount; }
    // The message the target sends on instrument `m`: a slot's CC, or Chan AT
    // for a slot set to it; none for Off and a slot on PC / Poly AT.
    uint8_t wire(const MidiInstrument& m) const {
        if (target == kTargetPitchBend) return kWirePitchBend;
        if (target == kTargetPressure) return kWirePressure;
        if (target < 1 || target > 6) return kWireNone;
        const uint8_t cc = m.cc[target - 1];
        return cc <= 127 ? uint8_t(cc + 1) : cc == 129 ? kWirePressure : kWireNone;
    }
    bool isDefault() const {
        const MidiLfo d;
        return target == d.target && shape == d.shape && rate == d.rate && depth == d.depth &&
               center == d.center && retrig == d.retrig;
    }
    void sanitize() {
        const MidiLfo d;
        if (target >= kTargetCount) target = d.target;
        if (shape >= kShapeCount) shape = d.shape;
        if (rate >= kRateCount) rate = d.rate;
        if (depth > kDepthMax) depth = d.depth;
        if (center > 127) center = d.center;
        if (retrig >= kRetrigCount) retrig = d.retrig;
    }
};

// A default Instrument (the fw new-instrument initialiser), built on first
// use; one shared copy (SDRAM on the device, main loop only).
const Instrument& defaultInstrument();

}  // namespace ot
