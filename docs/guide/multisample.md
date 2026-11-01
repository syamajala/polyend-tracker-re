# Multisample instruments

A multisample instrument holds several recordings of one sound, one per note (a Juno pad
sampled every few semitones, say). Each key plays the nearest recording, repitched by the
distance, so the sound stays natural across the keyboard instead of stretching one sample
over five octaves.

The Tracker loads them from **SFZ** files, a plain-text format most sample packs and
samplers use.

## Get an SFZ

Many packs ship one: copy the pack folder (WAVs and `.sfz` together) to the card.

For a folder of note samples without an SFZ, `tools/make_sfz.py` writes one on the
computer, from the file names:

```
tools/make_sfz.py ~/Samples/MyPack
```

It looks into every subfolder, reads the note from a leading MIDI number
(`054 Dusty Pluck Junos F#2.wav`) or a note name (`Pad C3.wav`), and skips folders with
fewer than 8 notes (kits, chords). `--name-octave 3` is for packs that call middle C
"C3"; `--dry-run` shows what it would write. Then copy the folders to the card.

## Load it

1. In the **Sample Loader**, pick an empty instrument on the right.
2. Select the `.sfz` file and press **Add**. A panel opens instead of loading at once:

| Key | Sets |
|---|---|
| F1 **Low** / F2 **High** | the note range to load |
| F3 **Every** | the spacing: every note, 2nd, 3rd, 4th, 6th or 12th |
| F4 **Max length** | cuts each note to 8 s ... 0.5 s with a short fade (**Full** keeps them whole) |
| **Size** | what it takes in sample memory (shown only) |
| F6 **Cancel** / F8 **Load** | |

Turn the encoder to change the selected value. The panel says how many notes the SFZ has,
which ones it will load and how much memory is left. It opens with values that fit in
about half the free memory, so another instrument still fits; **Load** is greyed while the
choice doesn't fit or no note is in the range.

At most 48 notes go into one instrument: space them out or narrow the range for a bigger
pack. A spacing of a 3rd or 4th note usually sounds as good as every note.

## Play it

The instrument plays like any other: pads, the pattern, MIDI in. Its **Sample Playback**
page shows the ninth play mode, **Multi**:

- **Zone** (F1) steps through the loaded notes and **Root** shows each one's note; F5
  zooms. The zone edges stay where the loader put them.
- Switching to another play mode keeps the notes: switch back to **Multi** any time.

## Good to know

- Multisamples load **mono**, as one-shots: SFZ loops, velocity layers and round robins
  aren't used yet. Each note plays to its end (or its Max length).
- The notes are saved in the project like any sample. Stock firmware and the Tracker Mini
  open the project too and play the instrument as a **Slice** instrument, one slice per
  note.
- Long samples take a lot of memory: a 10 s mono note is about 0.9 MB. **Max length** is
  the quickest way to fit a pack.
