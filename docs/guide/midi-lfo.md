# MIDI instrument LFO

Each MIDI instrument (MIDI 1–16) gets one **tempo-locked LFO**, sent alongside its notes.

![The MIDI Instrument LFO page](../images/midi-lfo.png)

## Set it up

1. Select a MIDI instrument: hold **Instrument** and turn the encoder.
2. Press **Instrument Params**, then **Instrument Params** again: the key steps
   through the MIDI parameters page, the **MIDI Instrument LFO** page and the
   [MIDI Instrument Preset](midi-presets.md) page.

| Setting (key) | Choices |
|---|---|
| **Target** (F1) | Off, a CC (0–127, named where common, e.g. `1 ModWh`), pitch bend, or channel pressure. |
| **Shape** (F2) | Rev saw, Saw, Triangle, Square, Random. |
| **Rate** (F3) | From 128 steps per cycle down to 1/8 step, locked to the tempo. |
| **Depth** (F4) | How far it moves around Center. |
| **Center** (F5) | The value it moves around. |
| **Retrigger** (F6) | **Free** (follows the song position) or **Note** (restarts with each note). |

## Good to know

- LFO messages use at most **half of a DIN MIDI port's bandwidth**, so notes are never
  delayed by them.
- The LFO runs while the sequencer plays. At Stop, its target is left at **Center**.
- **Depth** is a percentage of half the target's range around Center.
- A step FX on the same target (a CC FX on the LFO's CC, pitch bend, or pressure) moves
  the LFO's **Center**, so the pattern can shift the wobble around.
- Stored in `midi_lfo.bin` next to `project.mt`; other firmware ignores it.
