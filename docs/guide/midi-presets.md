# MIDI presets

A **MIDI preset** stores how a synth is set up on a MIDI instrument: velocity, offset, the
six CC slots A–F **with names**, and the [MIDI LFO](midi-lfo.md). Save it once, then load it
into any MIDI instrument in any project.

A preset describes the synth, not a sound or a channel:

- **No channel.** It loads into whichever MIDI instrument is open, and that instrument's
  channel stays. A synth that listens on several channels (an Elektron Model:Cycles, one
  channel per track) uses one preset on each of them.
- **No program change.** To switch the synth's sound in a song, set a CC slot to
  **Prg Chg** and put that FX on a step.

![The MIDI Instrument Preset page](../images/midi-presets.png)

## The Preset page

1. Select a MIDI instrument: hold **Instrument** and turn the encoder.
2. Press **Instrument Params** until the title reads **MIDI Instrument Preset** (it steps
   through Parameters, LFO and Preset).

The left list shows the CC slots A–F with their CC numbers and names; the right side shows
the preset this instrument came from or was saved as.

| Key | Action |
|---|---|
| Encoder, **Up / Down** | Select a CC slot. |
| **F6 Label** | Name the selected slot (up to 9 characters). The name replaces "CC A" … "CC F" on the MIDI parameters page. |
| **Delete** | Clear the selected slot's name. |
| **F1 Load** | List the presets on the card; the encoder or **Up / Down** move, **F8 Load** loads into this instrument, **F7 Cancel** closes the list. |
| **F2 Save** | Save under the preset's name (no name yet: as Save As). |
| **F3 Save As** | Save under a new name (asks before replacing a preset). |

The CC numbers themselves are set on the MIDI parameters page as before. The names and the
preset name are saved with the project (`midi_names.bin`, next to `project.mt`), so
`project.mt` stays readable by the original firmware.

## The preset file

Presets are text files in `MidiPresets/` on the card, named `<name>.txt`, so they can be
written or edited on a computer:

```
# OpenTracker MIDI preset
velocity = 127
offset = 0
cc A = 43 Cutoff
cc B = 44 Resonance
cc C = 16 EG Attack
cc D = 7
cc E = 64
cc F = PC
lfo target = CC 43
lfo shape = 2
lfo rate = 8
lfo depth = 50
lfo center = 64
lfo retrigger = 0
```

- `cc A` … `cc F`: a CC number (0–127), `PC` (program change), `AT` (channel pressure) or
  `PAT` (poly aftertouch), then an optional name.
- `lfo target`: `off`, `CC n`, `PB` (pitch bend) or `AT`.
- `lfo shape`: 0 Rev saw, 1 Saw, 2 Triangle, 3 Square, 4 Random.
- `lfo rate`: 0 (128 steps per cycle) to 22 (1/8 step), as listed on the LFO page.
- `lfo depth` 0–100, `lfo center` 0–127, `lfo retrigger` 0 Free or 1 Note.

Lines starting with `#` are comments, keys ignore case, and a missing or unknown line keeps
the default, so a preset can be as short as one `cc` line.
