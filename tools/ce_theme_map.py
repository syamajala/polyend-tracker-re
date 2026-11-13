#!/usr/bin/env python3
"""CE theme map: Tracker CE JSON theme keys -> OpenTracker colour slots.

Writes src/core/ui/ce_theme_map.inc (run after changing the map):
    tools/ce_theme_map.py

Clean room: the map comes from black-box observation only. The CE web theme
editor's rendered previews (a theme with one colour per key, screenshotted)
were matched pixel by pixel against the simulator with one colour per slot,
and against our stock slot notes (src/core/ui/theme.h). No CE code was read.

KEYS lists every key in the order the editor exports them (the order Export
theme writes). MAP is applied in order, later entries win: first the CE
keys that colour whole groups of our slots (ROLE), then the exact slots.
The export slot of a key is the first slot of its first exact entry (of its
role entry when it has none).
"""
import os

# Every CE theme key, in the editor's export order ("name" / "base" aside).
KEYS = """
background
topBar.text topBar.background topBar.frame topBar.text2
songTimer.text songTimer.background songTimer.frame songTimer.text2
labels.active.text labels.active.background labels.active.frame labels.active.text2
labels.inactive.text labels.inactive.background labels.inactive.frame labels.inactive.text2
labels.activeButton.text labels.activeButton.background labels.activeButton.frame labels.activeButton.text2
labels.inactiveButton.text labels.inactiveButton.background labels.inactiveButton.frame labels.inactiveButton.text2
labels.highlightedButton.text labels.highlightedButton.background labels.highlightedButton.frame labels.highlightedButton.text2
labels.redButton.text labels.redButton.background labels.redButton.frame labels.redButton.text2
moreButton.text moreButton.background moreButton.activeDot moreButton.inactiveDot
keyboard.keyBackground keyboard.text keyboard.activeKey
editBox.text editBox.boxBackground editBox.frame
bottomButtons.text bottomButtons.background bottomButtons.frame
patternTracker.trackName patternTracker.background patternTracker.note patternTracker.instrument
patternTracker.fx1 patternTracker.fx2 patternTracker.inactiveNote patternTracker.inactiveInstrument
patternTracker.inactiveFx1 patternTracker.inactiveFx2 patternTracker.selection patternTracker.gridDivider
patternTracker.inactiveTrack patternTracker.playhead patternTracker.selectionText patternTracker.verticalDivider
patternTracker.stepNumber patternTracker.greenMode patternTracker.unfocusedTrack patternTracker.activeTrack
patternTracker.trackEnd
songPlayer.text songPlayer.activeBlock songPlayer.scrollbar songPlayer.background songPlayer.inactiveText
songPlayer.editMode songPlayer.scrollbarBackground songPlayer.playhead songPlayer.emptyTrack
songPlayer.playheadActive
waveform.audio waveform.background waveform.progress.text waveform.progress.cursor
waveform.sliceMarker waveform.activeSlice waveform.slicePlayhead
frames.active frames.inactive frames.green
popups.stepTitle.text popups.stepTitle.background popups.stepTitle.frame
popups.stepList.frameNormal popups.stepList.frameSelect popups.stepList.scrollbar popups.stepList.background
popups.stepList.textNormal popups.stepList.textSelect popups.stepList.scrollbarTrack
popups.note.padBackground popups.note.padText popups.note.selectionFrame
popups.noteKeyboard.text popups.noteKeyboard.background popups.noteKeyboard.activeText
popups.dialog.text popups.dialog.background popups.dialog.frame
bars.active.primary bars.active.container bars.active.background
bars.sampleLoaderNormal.primary bars.sampleLoaderNormal.container bars.sampleLoaderNormal.background
lists.sampleLoader.frameNormal lists.sampleLoader.frameSelect lists.sampleLoader.scrollbar
lists.sampleLoader.background lists.sampleLoader.textNormal lists.sampleLoader.textSelect
lists.sampleLoader.scrollbarTrack
lists.sampleLoaderInactive.frameNormal lists.sampleLoaderInactive.frameSelect
lists.sampleLoaderInactive.scrollbar lists.sampleLoaderInactive.background
lists.sampleLoaderInactive.textNormal lists.sampleLoaderInactive.textSelect
lists.sampleLoaderInactive.scrollbarTrack
accents.white accents.red accents.orange accents.yellow accents.green accents.cyan accents.grey
miniMenu.text miniMenu.boxBackground miniMenu.selectedBox miniMenu.background miniMenu.frame
miniMenu.inactiveDot miniMenu.activeDot
""".split()

# Groups of our slots that share one meaning across screens (the stock
# tables' colour roles, spec 20 §1.4).
ROLE = {
    "text": [87, 3, 4, 8, 12, 44, 49, 53, 67, 69, 70, 125, 135, 145, 148, 152, 157, 166, 171, 172, 173, 199, 209,
             216, 242, 246, 73, 83, 84, 91, 101, 104, 105, 108, 115, 118, 119, 122, 141, 220, 230, 234, 238, 243,
             266, 60, 161, 164, 200],
    "accent": [130, 7, 11, 15, 19, 23, 27, 30, 31, 35, 36, 37, 38, 39, 40, 42, 43, 47, 59, 88, 95, 98, 112, 116,
               123, 127, 136, 139, 146, 154, 158, 184, 215, 218, 221, 225, 226, 227, 240, 248, 251, 257, 264, 267,
               46, 64, 72, 97, 111, 204, 168],
    "alert": [51, 74, 79, 150, 197, 211, 232, 55, 261],
    "good": [270, 102, 109, 132, 191, 208, 222, 236, 249, 258, 75, 80, 260],
    "yellow": [269, 194, 256],
    "orange": [268],
    "cyan": [271, 252],
    "background": [0, 142, 196],
    "title_bg": [2, 6],
    "title_text": [1, 5],
    "label_bg": [10, 14, 18, 22, 34, 62, 68, 71, 160],
    "strip": [58, 202],
    "label_text": [9, 17, 20, 25, 28, 29, 32, 33, 41, 159],
    "label_text_off": [13, 21, 24, 203],
    "dim": [66, 245, 190, 193, 16, 57],
    "panel": [86, 50, 77, 82, 93, 100, 107, 114, 121, 144, 149, 153, 156, 224, 229, 235, 239, 76, 81, 126, 223,
              228, 54, 247, 129, 134, 210, 213],
    "scroll_thumb": [85, 92, 99, 106, 113, 120, 143, 201],
    "scroll_track": [89, 96, 103, 110, 117, 124, 147, 205],
    "inverse_text": [45, 48, 52, 137, 212, 262, 265],
    "inverse_bg": [138, 195, 198, 214, 219, 233, 250, 254, 255, 263],
}

# Fork slots after the stock table (src/core/ui/theme.h).
LIST_TEXT, LIST_TEXT_SEL, LOADER_INS_SEL, TEMPO_FILL = 277, 278, 279, 280
FULL_POPUP_TEXT, FULL_POPUP_BAND, LOADER_HEADER = 281, 282, 283
SLOT_COUNT = 284   # colour slots end here; 284 is a flag


def seq(prefix, names, first):
    return [(f"{prefix}.{n}", [first + i]) for i, n in enumerate(names) if n]


ROLE_MAP = (
    # 1. Roles: a CE colour over every slot of one of our colour roles.
    [("accents.white", ROLE["text"]), ("accents.red", ROLE["accent"]), ("accents.red", ROLE["alert"]),
     ("accents.green", ROLE["good"]), ("accents.yellow", ROLE["yellow"]), ("accents.orange", ROLE["orange"]),
     ("accents.cyan", ROLE["cyan"]), ("background", ROLE["background"]), ("topBar.background", ROLE["title_bg"]),
     ("topBar.text", ROLE["title_text"]), ("bottomButtons.background", ROLE["label_bg"]),
     ("bottomButtons.background", ROLE["strip"]), ("bottomButtons.text", ROLE["label_text"]),
     ("labels.inactiveButton.text", ROLE["label_text_off"]), ("labels.inactive.text", ROLE["dim"]),
     ("popups.stepList.background", ROLE["panel"]), ("popups.stepList.scrollbar", ROLE["scroll_thumb"]),
     ("popups.stepList.scrollbarTrack", ROLE["scroll_track"]), ("popups.stepTitle.text", ROLE["inverse_text"]),
     ("popups.stepTitle.background", ROLE["inverse_bg"])]
)
MAP = ROLE_MAP + (
    # 2. Exact slots. Screen background, also behind Config's lists.
    [("background", [0, 86, 142, 196])]
    # Title bar and its song timer group (stock sets at slots 1 and 5).
    + seq("topBar", ["text", "background", "frame", "text2"], 1)
    + [("topBar.frame", [266])]
    # The Notes popup's title (CE: the normal title bar).
    + [("topBar.text", [FULL_POPUP_TEXT]), ("topBar.background", [FULL_POPUP_BAND])]
    + seq("songTimer", ["text", "background", "frame", "text2"], 5)
    # Bottom bar: strip, label text, separators, value line, underlines.
    + [("bottomButtons.background", [58]), ("bottomButtons.text", [9, 17]), ("bottomButtons.frame", [57]),
       ("labels.activeButton.text2", [12, 20]), ("labels.activeButton.frame", [164]),
       ("labels.inactiveButton.text", [13, 21]), ("moreButton.inactiveDot", [26])]
    # Lists (Config, popups): selection, thumb, text, track. Config's own
    # text has the fork slots (CE: grey items, white selection).
    + [("labels.active.frame", [83, 84]), ("popups.stepList.scrollbar", [85]),
       ("popups.stepList.textNormal", [87]), ("popups.stepList.scrollbarTrack", [89]),
       ("labels.inactive.text", [LIST_TEXT]), ("labels.active.text", [LIST_TEXT_SEL])]
    # Sample Loader list (stock set at slot 97).
    + [("lists.sampleLoader.frameSelect", [97, 98]), ("lists.sampleLoader.scrollbar", [99]),
       ("lists.sampleLoader.background", [100, LOADER_HEADER]), ("lists.sampleLoader.textNormal", [101]),
       ("lists.sampleLoader.scrollbarTrack", [103]),
       ("lists.sampleLoaderInactive.frameSelect", [LOADER_INS_SEL])]
    # Value bars.
    + [("bars.active.primary", [73, TEMPO_FILL]), ("bars.active.background", [76]), ("bars.active.container", [77]),
       ("bars.sampleLoaderNormal.primary", [78]), ("bars.sampleLoaderNormal.background", [81]),
       ("bars.sampleLoaderNormal.container", [82])]
    # Focus frames.
    + [("frames.active", [130]), ("frames.inactive", [131, 133]), ("frames.green", [132])]
    # Popups: title, list panel, text boxes, note pads.
    + seq("popups.stepTitle", ["text", "background", "frame"], 137)
    + [("popups.stepList.background", [50, 54])]
    + seq("popups.dialog", ["text", "background", "frame"], 148)
    + seq("popups.dialog", ["text", "background", "frame"], 209)
    + seq("editBox", ["text", "boxBackground", "frame"], 152)
    + seq("popups.note", ["padBackground", "padText", "selectionFrame"], 156)
    # Pattern grid block (stock slots 174..194). 187 is the playing-row band
    # (CE: only while playing, see slot::kFlagBandPlayingOnly); 192 (the
    # multi-step selection) has no CE key and keeps the base colour.
    + seq("patternTracker", ["trackName", "background", "note", "instrument", "fx1", "fx2",
                             "inactiveNote", "inactiveInstrument", "inactiveFx1", "inactiveFx2",
                             "selection", "gridDivider", "inactiveTrack", "playhead", "selectionText",
                             "verticalDivider", "stepNumber", "greenMode", None, "activeTrack", "trackEnd"], 174)
    # Song list (stock slots 199..208).
    + [("songPlayer.activeBlock", [199]), ("songPlayer.text", [200]), ("songPlayer.scrollbar", [201]),
       ("songPlayer.background", [202]), ("songPlayer.inactiveText", [203]), ("songPlayer.editMode", [204]),
       ("songPlayer.scrollbarBackground", [205]), ("songPlayer.playhead", [206]),
       ("songPlayer.emptyTrack", [207]), ("songPlayer.playheadActive", [208])]
    # Waveform and its overlay (stock slots 244..251).
    + [("waveform.audio", [244]), ("waveform.progress.text", [246]), ("waveform.background", [247]),
       ("waveform.progress.cursor", [248]), ("waveform.slicePlayhead", [249]),
       ("waveform.activeSlice", [250]), ("waveform.sliceMarker", [251])]
    # The stock accent slots.
    + [("accents.red", [267]), ("accents.orange", [268]), ("accents.yellow", [269]),
       ("accents.green", [270]), ("accents.cyan", [271]), ("accents.grey", [272])]
)


def main():
    assert len(KEYS) == len(set(KEYS)) == 140
    idx = {k: i for i, k in enumerate(KEYS)}
    groups, leaves = [""], []
    for k in KEYS:
        g, _, l = k.rpartition(".")
        if g not in groups:
            groups.append(g)
        if l not in leaves:
            leaves.append(l)
    assert len(groups) < 256 and len(leaves) < 256
    pool, entries, export = [], [], [0xFFFF] * len(KEYS)
    exact = set()   # keys whose export slot comes from an exact entry
    for n, (key, slots) in enumerate(MAP):
        assert key in idx, key
        assert all(0 <= s < SLOT_COUNT and not 273 <= s <= 276 for s in slots), key
        entries.append((idx[key], len(pool), len(slots)))
        if n < len(ROLE_MAP):
            if export[idx[key]] == 0xFFFF:
                export[idx[key]] = slots[0]
        elif key not in exact:
            exact.add(key)
            export[idx[key]] = slots[0]
        pool += slots
    out = os.path.join(os.path.dirname(__file__), "..", "src", "core", "ui", "ce_theme_map.inc")
    with open(out, "w") as f:
        w = f.write
        w("// Generated by tools/ce_theme_map.py: do not edit. CE JSON theme keys and\n"
          "// the OpenTracker slots each one colours (ui/theme_file.cpp).\n")
        w(f"constexpr int kCeKeyCount = {len(KEYS)};\n")
        w("// Key group (dotted object path, \"\" = top level) and leaf names.\n")
        w("const char* const kCeGroups[] = {\n")
        for g in groups:
            w(f'    "{g}",\n')
        w("};\nconst char* const kCeLeaves[] = {\n")
        for i in range(0, len(leaves), 6):
            w("    " + " ".join(f'"{l}",' for l in leaves[i:i + 6]) + "\n")
        w("};\n// Keys in export order: {group, leaf}.\nconst uint8_t kCeKeys[kCeKeyCount][2] = {\n")
        for k in KEYS:
            g, _, l = k.rpartition(".")
            w(f"    {{{groups.index(g)}, {leaves.index(l)}}},   // {k}\n")
        w("};\n// Slot lists, applied in order (later wins): {key, first, count} into kCeSlots.\n")
        w("struct CeMapEntry {\n    uint8_t key;\n    uint16_t first;\n    uint8_t count;\n};\n")
        w("const CeMapEntry kCeMap[] = {\n")
        for k, first, n in entries:
            w(f"    {{{k}, {first}, {n}}},   // {KEYS[k]}\n")
        w("};\nconst uint16_t kCeSlots[] = {\n")
        for i in range(0, len(pool), 16):
            w("    " + " ".join(f"{s}," for s in pool[i:i + 16]) + "\n")
        w("};\n// The slot each key exports (0xFFFF: no OpenTracker colour, not written).\n")
        w("const uint16_t kCeExport[kCeKeyCount] = {\n")
        for i in range(0, len(export), 12):
            w("    " + " ".join(f"{s}," for s in export[i:i + 12]) + "\n")
        w("};\n")
    print("wrote", os.path.normpath(out), f"({len(KEYS)} keys, {len(entries)} entries, {len(pool)} slots)")


if __name__ == "__main__":
    main()
