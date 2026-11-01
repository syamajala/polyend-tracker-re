#!/usr/bin/env python3
"""[I] #3: write an SFZ for each folder of note samples (one WAV per note), so
the Tracker's Sample Loader can load the folder as a multisample instrument.

  tools/make_sfz.py FOLDER... [--force] [--dry-run] [--name-octave {3,4}]

Every folder under FOLDER (FOLDER included) that holds two or more WAVs with
different notes and no .sfz gets one, named after the folder. It lists every
note: the Tracker picks the range and the spacing when it loads (Sample
Loader, Add on the .sfz).

The note comes from the file name:
- a MIDI note number, as the From Mars packs start their names
  ("054 Dusty Pluck Junos F#2.wav", "100StrongTriangle_SH5_E6.wav");
- else a note name (C4, F#2, Bb3, B-1); --name-octave says which octave
  number middle C (MIDI 60) has: 4 (SFZ, default) or 3 (Yamaha / Roland).
When both are there and the number matches the name in one of the two
conventions, the number wins; when they disagree, the file is skipped.

Each region gets pitch_keycenter = its note and a key range up to halfway to
its neighbours, so the SFZ also plays in other samplers. Existing SFZ files
are kept unless --force.
"""
import argparse
import os
import re
import sys

MIN_NOTES = 8   # fewer notes: a kit or a few chords, not an instrument

NOTE_INDEX = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}
# A note name: letter, optional # / b / s, octave (may be negative: B-1, C-2),
# not inside a word and not followed by more digits.
NAME_RE = re.compile(r"(?<![A-Za-z])([A-Ga-g])(#|b|s)?(-?\d)(?!\d)")
# A MIDI note number at the very start of the name (From Mars: "054 ...", "100...").
LEAD_NUM_RE = re.compile(r"^(\d{1,3})(?!\d)")


def name_to_midi(letter, acc, octave, middle_c_octave):
    n = NOTE_INDEX[letter.lower()]
    if acc in ("#", "s"):
        n += 1
    elif acc == "b":
        n -= 1
    return (octave - middle_c_octave) * 12 + 60 + n   # C<middle_c_octave> = 60


def parse_name(filename):
    """(leading number or None, last note name as (letter, acc, octave) or None)."""
    stem = os.path.splitext(os.path.basename(filename))[0]
    names = NAME_RE.findall(stem)
    m = LEAD_NUM_RE.match(stem)
    num = int(m.group(1)) if m else None
    if num is not None and num > 127:
        num = None
    name = (names[-1][0], names[-1][1], int(names[-1][2])) if names else None
    return num, name


def folder_convention(parsed):
    """Octave number of middle C that the folder's numbered files agree on, or None."""
    votes = {3: 0, 4: 0}
    for num, name in parsed:
        if num is None or name is None:
            continue
        for mco in votes:
            if name_to_midi(*name, mco) == num:
                votes[mco] += 1
    best = max(votes, key=votes.get)
    return best if votes[best] else None


def note_of(filename, middle_c_octave=4):
    """MIDI note of one sample file name, or None (a note name is required)."""
    num, name = parse_name(filename)
    if name is None:
        return None
    v = name_to_midi(*name, middle_c_octave)
    return v if 0 <= v <= 127 else None


def wavs_in(folder):
    try:
        return sorted(f for f in os.listdir(folder) if f.lower().endswith(".wav") and not f.startswith("._"))
    except OSError:
        return []


def folder_notes(folder, middle_c_octave=4):
    """{note: file} of a folder; the numbered files' convention wins over the default."""
    files = wavs_in(folder)
    parsed = [parse_name(f) for f in files]
    mco = folder_convention(parsed) or middle_c_octave
    notes = {}
    for f, (num, name) in zip(files, parsed):
        if name is None:
            continue
        v = name_to_midi(*name, mco)
        if num is not None and num != v:
            continue   # number and name disagree: not a note sample
        if 0 <= v <= 127:
            notes.setdefault(v, f)   # first file per note (takes "_0001" sort after)
    return notes


def build_sfz(folder, middle_c_octave=4, min_notes=MIN_NOTES):
    """The SFZ text for a folder, or None when it isn't a set of note samples."""
    notes = folder_notes(folder, middle_c_octave)
    if len(notes) < min_notes:
        return None
    keys = sorted(notes)
    lines = [
        "// Written by OpenTracker tools/make_sfz.py: one region per note sample.",
        "<group>",
        "",
    ]
    for i, k in enumerate(keys):
        lo = 0 if i == 0 else (keys[i - 1] + k) // 2 + 1
        hi = 127 if i == len(keys) - 1 else (k + keys[i + 1]) // 2
        lines.append(f"<region> sample={notes[k]} lokey={lo} hikey={hi} pitch_keycenter={k}")
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("folders", nargs="+")
    ap.add_argument("--force", action="store_true", help="replace existing .sfz files")
    ap.add_argument("--dry-run", action="store_true", help="print what would be written")
    ap.add_argument("--name-octave", type=int, choices=(3, 4), default=4,
                    help="octave number of middle C in note names (default 4)")
    ap.add_argument("--min-notes", type=int, default=MIN_NOTES,
                    help=f"fewest different notes for a folder to count (default {MIN_NOTES})")
    a = ap.parse_args(argv)
    written = 0
    for top in a.folders:
        for root, dirs, _files in os.walk(top):
            dirs[:] = sorted(d for d in dirs if not d.startswith((".", "__MACOSX")))
            have = [f for f in os.listdir(root) if f.lower().endswith(".sfz")]
            if have and not a.force:
                continue
            text = build_sfz(root, a.name_octave, a.min_notes)
            if text is None:
                continue
            out = os.path.join(root, os.path.basename(os.path.normpath(root)) + ".sfz")
            regions = text.count("<region>")
            print(f"{out}: {regions} notes")
            if not a.dry_run:
                with open(out, "w", newline="\n") as fh:
                    fh.write(text)
            written += 1
    print(f"{written} SFZ file(s) {'to write' if a.dry_run else 'written'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
