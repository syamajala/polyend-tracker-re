# USB audio

The Tracker shows up on a computer as a USB sound card, **OpenTracker**, next to its USB
MIDI, serial and storage. Whatever the computer plays into it (a browser, a DAW, a
software synth) can be heard through the Tracker and sampled with it, and the computer can
record the Tracker. No driver needed.

Works with **Linux and Windows**. macOS sees the device but isn't supported: it can't
follow the Tracker's clock, so expect a click every half-minute or so.

## Hear the computer

1. Connect the Tracker over USB and pick **OpenTracker** as the computer's audio output.
2. On the Tracker, open the **Global Mixer** (Master, three presses) and unmute **USB**
   (F8), the last column.

Like Line in, USB starts muted in a new project. Its column works like the others: hold
F8 and turn the encoder for the level, hold F8 + Delete for 0 dB, Shift + F8 to solo it.
The level and mute are saved with the project.

USB audio joins the mix after the limiter, under master volume: the delay and reverb sends
don't get it, and it isn't sent back to the computer (see below).

## Sample the computer

- **Sample Recorder:** Source **USB** (the fifth, after Mic HG). Always stereo, no Gain.
  With Monitor On you hear it at full level whatever the mixer says, as with Line in.
- **Resample:** From **USB** records it on the bar, like Line in. You hear it through the
  Global Mixer USB column.

## Record the Tracker on the computer

Pick **OpenTracker** as the input in the DAW or recorder and record. The computer gets the
Tracker's master bus, the same signal Resample From Master takes:

- After the limiter and **before master volume**: turn the Tracker down for listening
  without changing the recording level.
- **Line in** is in it when unmuted in the Global Mixer, so the Tracker also works as the
  sound card for whatever is plugged into it.
- Not in it: the metronome, file preview, and the computer's own USB audio. A DAW that
  records the Tracker while playing through it doesn't record its own playback again.

Nothing to turn on: it's sent while the computer records.

## Good to know

- Latency is about 5 ms each way on the Tracker plus the computer's own buffer: fine for
  sampling, recording and listening along, not for playing a software synth live from the
  pads.
- The Tracker runs at 44,117 Hz, a little over the 44,100 Hz it names. Linux and Windows
  adjust for that on their side when recording, as with any USB sound card.
- The computer's volume control works as usual (the Tracker has no volume of its own on
  the USB side).
- While USB Storage mode copies files, USB audio drops out.
