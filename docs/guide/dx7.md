# DX7 synth

A **6-operator FM engine that plays Yamaha DX7 voices**, available as a sixth synth model
next to ACD, FAT, VAP, WTFM and PERC. Thousands of free DX7 voices exist as SysEx
(`.syx`) files; the Tracker plays them directly.

![Synth Parameters with the DX7 model](../images/dx7-params.png)

## Use a DX7 voice

1. Put `.syx` files in **`Patches/DX7/`** on the SD card. Both kinds work:
    - **32-voice banks** (4,104 bytes, the usual "bank" files);
    - **single-voice dumps** (163 bytes).
2. Select a synth instrument (S1–S3): hold **Instrument** and turn the encoder.
3. Press **Instrument Params**. On **Synth Parameters**, set **Model** to **DX7**
   (it's the last entry in the list).
4. Press **Load Patch** (F7).

![Load Patch: banks are listed first](../images/dx7-load.png)

5. Banks come first in the list and open like folders: highlight one and press **Open**
   (F8). Pick a voice, **Preview** (F5, hold) to hear it, **Confirm** (F8) to load it.
   **Left** goes back to the bank list. Single-voice files sit with the patches and
   preview and load directly.

![A bank's voices](../images/dx7-bank.png)

The loaded voice is named after the DX7 voice. **Save** (in Edit Patch) writes it as an
ordinary `.patch` file in `Patches/DX7/`, with the voice stored inside it; the project
keeps it too.

## Playing

- DX7 slots play from any track like the other synths: notes, the step FX for pitch
  (Glide, Slide, Tune, Micro-tune), the Volume FX and the macro FX.
- **Polyphony:** set it on Synth Parameters. On a **polyphony 1** slot, notes play
  legato and the envelopes carry on from note to note, as on a real DX7 in mono mode; use
  polyphony 2 or more for separate notes.
- **Velocity:** the Volume step FX sets the note's velocity.
- **CPU:** about 6 % per voice (estimated). The voice limiter cuts synth voices first if
  the audio gets close to its limit.

## Good to know

- The sound core is a port of Google's Music Synthesizer for Android (msfa, Apache 2.0),
  checked against [Dexed](https://asb2m10.github.io/dexed/) note by note. Our additions:
  amplitude modulation, the feedback loops of algorithms 4 and 6, and key sync.
- **Edit Patch** shows Volume and the macro pages. There's no operator editor yet: edit
  voices in a DX7 editor on a computer (Dexed works) and copy the `.syx` file over.
- A Tracker Mini has no DX7 model: it resets a DX7 slot to its default when it opens the
  project.
