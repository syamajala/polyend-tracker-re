# USB audio

The Tracker shows up on a computer as a USB sound card, **OpenTracker**, next to its USB
MIDI, serial and storage. Whatever the computer plays into it (a browser, a DAW, a
software synth) can be heard through the Tracker and sampled with it. No driver needed.

Works with **Linux and Windows**. macOS sees the device but isn't supported: it can't
follow the Tracker's clock, so expect a click every half-minute or so.

## Hear the computer

1. Connect the Tracker over USB and pick **OpenTracker** as the computer's audio output.
2. On the Tracker, open the **Global Mixer** (Master, three presses) and unmute **USB**
   (F8), the last column.

Like Line in, USB starts muted in a new project. Its column works like the others: hold
F8 and turn the encoder for the level, hold F8 + Delete for 0 dB, Shift + F8 to solo it.
The level and mute are saved with the project.

USB audio goes straight to the master mix: the delay and reverb sends don't get it.

## Sample the computer

- **Sample Recorder:** Source **USB** (the fifth, after Mic HG). Always stereo, no Gain.
  With Monitor On you hear it at full level whatever the mixer says, as with Line in.
- **Resample:** From **USB** records it on the bar, like Line in. You hear it through the
  Global Mixer USB column.

## Good to know

- Latency is about 5 ms on the Tracker plus the computer's own buffer: fine for sampling
  and listening along, not for playing a software synth live from the pads.
- The computer's volume control works as usual (the Tracker has no volume of its own on
  the USB side).
- While USB Storage mode copies files, USB audio drops out.
- The Tracker doesn't send audio to the computer yet: the computer also sees an
  **OpenTracker** input, which is silent for now.
