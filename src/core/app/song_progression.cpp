// [I] #121 Song mode progressions: named chord patterns in the song (ui/progression.h).
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app/project_fields.h"
#include "ui/progression.h"

namespace ot {

namespace {
// A pattern for this chord and voicing: the name's first word is the chord
// ("F" of "F VI") and its last word the voicing's ("Wide"), or no voicing
// word for Smooth ("F", "F VI").
bool nameIsChord(const char* name, const char* chord, uint8_t voicing) {
    const size_t n = std::strlen(chord);
    if (std::strncmp(name, chord, n) != 0 || (name[n] != 0 && name[n] != ' ')) return false;
    // [I] #123 the next word is a numeral ("F VI") or the voicing's, never
    // another chord: "Am F C G" (Insert Pattern) is no Am pattern.
    if (name[n] == ' ') {
        const char* w = name + n + 1;
        const char* e = std::strchr(w, ' ');
        const size_t wl = e ? size_t(e - w) : std::strlen(w);
        bool voicingWord = false;
        for (uint8_t v = 1; v < prog::kVoicingCount; ++v) {
            const char* vw = prog::voicingWord(v);
            voicingWord = voicingWord || (std::strlen(vw) == wl && std::strncmp(w, vw, wl) == 0);
        }
        const char c = w[0] == 'b' || w[0] == '#' ? w[1] : w[0];
        if (!voicingWord && c != 'i' && c != 'v' && c != 'I' && c != 'V') return false;
    }
    const char* last = std::strrchr(name, ' ');
    last = last && last >= name + n ? last + 1 : "";
    for (uint8_t v = 1; v < prog::kVoicingCount; ++v)
        if (std::strcmp(last, prog::voicingWord(v)) == 0) return v == voicing;
    return voicing == prog::kSmooth;
}

// Slots first.. play num[0..n) (the slot at `first` itself when `replace`);
// the song after them moves down.
void placeSlots(Project& pr, int len, int first, bool replace, const uint8_t* num, int n) {
    const int shift = n - (replace ? 1 : 0);
    for (int s = len - 1; s >= first + (replace ? 1 : 0); --s) {
        const int d = s + shift;
        pr.playlist[d] = pr.playlist[s];
        pr.playlistCount[d] = pr.playlistCount[s];
        pr.playlistScene[d] = pr.playlistScene[s];
        pr.playlistSceneTo[d] = pr.playlistSceneTo[s];
        pr.playlistRamp[d] = pr.playlistRamp[s];
    }
    for (int i = 0; i < n; ++i) {
        pr.playlist[first + i] = num[i];
        pr.playlistCount[first + i] = 0;
        pr.playlistScene[first + i] = 0;
        pr.playlistSceneTo[first + i] = 0;
        pr.playlistRamp[first + i] = 0;
    }
}
}  // namespace

int App::insertProgression(const prog::Progression& p, int after, const char** why, uint8_t voicing) {
    static const char* sink;
    if (!why) why = &sink;
    *why = nullptr;
    if (p.count == 0) return -1;
    commitCurrentPattern();
    Project& pr = project;
    int len = 0;
    while (len < kSongSlots && pr.playlist[len]) ++len;
    if (len == 0) pr.playlist[len++] = 1;
    after = after < 0 ? 0 : after >= len ? len - 1 : after;
    const int src = pr.playlist[after];
    // An empty, unnamed slot pattern is filled instead of kept in front.
    const bool replace = !patternHasData(src) && !names.get(src)[0];
    const int first = replace ? after : after + 1;
    if (len - (replace ? 1 : 0) + p.count > kSongSlots) {
        *why = "Song is full";
        return -1;
    }
    uint8_t scale = pr.u8(pf::kScale);
    if (scale >= kScaleCount) scale = 0;
    const prog::Tonic tonic = prog::tonic(p, pr.u8(pf::kRootNote), scale);
    // Pattern per chord: named already, a chord earlier in this progression,
    // or a new one (0 here).
    uint8_t num[prog::kMaxChords] = {};
    char name[prog::kMaxChords][16];
    int fresh = 0;
    for (int i = 0; i < p.count; ++i) {
        prog::chordName(p, i, tonic, name[i], sizeof(name[i]));
        for (int n = 1; n <= kPatternCount && !num[i]; ++n)
            if (nameIsChord(names.get(n), name[i], voicing)) num[i] = uint8_t(n);
        bool dup = false;
        for (int j = 0; j < i && !num[i]; ++j) dup = dup || std::strcmp(name[j], name[i]) == 0;
        if (!num[i] && !dup) ++fresh;
    }
    // The new patterns: empty, unnamed and not in the song (the replaced
    // slot's own pattern may be used).
    uint8_t pool[prog::kMaxChords] = {};
    int got = 0;
    for (int n = 1; n <= kPatternCount && got < fresh; ++n) {
        if (patternHasData(n) || names.get(n)[0]) continue;
        bool used = false;
        for (int s = 0; s < len && !used; ++s) used = pr.playlist[s] == n && !(replace && s == after);
        if (!used) pool[got++] = uint8_t(n);
    }
    if (got < fresh) {
        *why = "No empty patterns";
        return -1;
    }
    uint8_t last[kStockTrackCount];
    for (int t = 0; t < kStockTrackCount; ++t) last[t] = storedStock(src).tracks[t].lastStep;
    const uint8_t ins = currentInstrument();
    int prev[prog::kMaxVoiced], prevCount = 0;   // the previous chord's notes (Smooth)
    int next = 0;
    for (int i = 0; i < p.count; ++i) {
        for (int j = 0; j < i && !num[i]; ++j)
            if (std::strcmp(name[j], name[i]) == 0) num[i] = num[j];
        if (num[i]) {
            // A pattern already made: its notes are what plays before the next chord.
            const PatternStock& st = storedStock(num[i]);
            int k = 0;
            while (k < prog::kMaxVoiced && st.tracks[k].steps[0].note >= 0) {
                prev[k] = st.tracks[k].steps[0].note;
                ++k;
            }
            if (k) prevCount = k;
            continue;
        }
        const int n = pool[next++];
        num[i] = uint8_t(n);
        defaultStored(n);
        PatternStock& st = storedStock(n);
        for (int t = 0; t < kStockTrackCount; ++t) st.tracks[t].lastStep = last[t];
        int notes[prog::kMaxVoiced];
        const int k = prog::voice(p, i, tonic, voicing, prev, prevCount, notes);
        for (int j = 0; j < k; ++j) {
            Step& s = st.tracks[j].steps[0];
            s.note = int8_t(notes[j]);
            s.instrument = pops::mapInstrument(j, ins);
            prev[j] = notes[j];
        }
        prevCount = k;
        markPatternDirty(n);
        char label[PatternNames::kMaxLen + 1];
        const char* word = prog::voicingWord(voicing);
        std::snprintf(label, sizeof(label), "%s %s%s%s", name[i], p.chords[i].numeral, word[0] ? " " : "", word);
        names.set(n, label);
        if (n == patternNumber()) reloadCurrentFromStore();
    }
    markNamesDirty();
    placeSlots(pr, len, first, replace, num, p.count);
    markProjectDirty();
    return first;
}


// [I] #123 Into: Pattern - the whole progression in one pattern, a chord per
// bar, to play as a loop or resample and slice (Slices: Step hits).
int App::insertProgressionPattern(const prog::Progression& p, int after, const char** why, uint8_t voicing) {
    static const char* sink;
    if (!why) why = &sink;
    *why = nullptr;
    if (p.count == 0) return -1;
    commitCurrentPattern();
    Project& pr = project;
    int len = 0;
    while (len < kSongSlots && pr.playlist[len]) ++len;
    if (len == 0) pr.playlist[len++] = 1;
    after = after < 0 ? 0 : after >= len ? len - 1 : after;
    const int src = pr.playlist[after];
    const bool replace = !patternHasData(src) && !names.get(src)[0];
    const int first = replace ? after : after + 1;
    if (!replace && len + 1 > kSongSlots) {
        *why = "Song is full";
        return -1;
    }
    uint8_t scale = pr.u8(pf::kScale);
    if (scale >= kScaleCount) scale = 0;
    const prog::Tonic tonic = prog::tonic(p, pr.u8(pf::kRootNote), scale);
    // The name: the chords, then the voicing's word ("Am F C G Wide").
    char label[PatternNames::kMaxLen + 1];
    int o = 0;
    for (int i = 0; i < p.count; ++i) {
        char c[16];
        prog::chordName(p, i, tonic, c, sizeof(c));
        o += std::snprintf(label + o, sizeof(label) - size_t(o), "%s%s", i ? " " : "", c);
        if (o >= int(sizeof(label))) o = int(sizeof(label)) - 1;
    }
    const char* word = prog::voicingWord(voicing);
    if (word[0]) std::snprintf(label + o, sizeof(label) - size_t(o), " %s", word);
    // The same progression and voicing made before: its pattern again.
    uint8_t num = 0;
    for (int n = 1; n <= kPatternCount && !num; ++n)
        if (std::strcmp(names.get(n), label) == 0) num = uint8_t(n);
    if (!num) {
        for (int n = 1; n <= kPatternCount && !num; ++n) {
            if (patternHasData(n) || names.get(n)[0]) continue;
            bool used = false;
            for (int s = 0; s < len && !used; ++s) used = pr.playlist[s] == n && !(replace && s == after);
            if (!used) num = uint8_t(n);
        }
        if (!num) {
            *why = "No empty patterns";
            return -1;
        }
        // A bar per chord (the metronome's), halved until the pattern fits.
        int bar = resampleBarSteps();
        while (bar > 1 && bar * p.count > kStepCount) bar /= 2;
        defaultStored(num);
        PatternStock& st = storedStock(num);
        for (int t = 0; t < kStockTrackCount; ++t) st.tracks[t].lastStep = uint8_t(bar * p.count - 1);
        const uint8_t ins = currentInstrument();
        int prev[prog::kMaxVoiced], prevCount = 0;
        for (int i = 0; i < p.count; ++i) {
            int notes[prog::kMaxVoiced];
            const int k = prog::voice(p, i, tonic, voicing, prev, prevCount, notes);
            for (int j = 0; j < k; ++j) {
                Step& s = st.tracks[j].steps[i * bar];
                s.note = int8_t(notes[j]);
                s.instrument = pops::mapInstrument(j, ins);
                prev[j] = notes[j];
            }
            // A track the chord before used and this one doesn't: stop it.
            for (int j = k; j < prevCount && j < kStockTrackCount; ++j) st.tracks[j].steps[i * bar].note = kNoteOff;
            prevCount = k;
        }
        markPatternDirty(num);
        names.set(num, label);
        markNamesDirty();
        if (num == patternNumber()) reloadCurrentFromStore();
    }
    placeSlots(pr, len, first, replace, &num, 1);
    markProjectDirty();
    return first;
}

}  // namespace ot
