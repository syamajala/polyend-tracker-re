# Config

The texts the Config screen shows in its help box while a setting's choices are open.
The Tracker reads them from `Help/Config.md` on the SD card (in `Modules.zip` on the
release page); without the file the box stays empty and everything else works.

# Settings

## Project Settings

### Performance presets
Global keeps one set of Performance presets for all projects. Per Project stores the presets inside each project.

### Pattern divider
Draws a highlighted line every N steps in the pattern grid, to make the bar structure easier to read.

### Notes scale
Limits the notes played from the pads to the chosen scale. Chromatic uses every semitone.

### Root note
The note the pad grid starts from. Together with the scale it sets the key of the pads.

### Notes layout
Sets how the rows of pads continue each other. 12 places the rows end to end; smaller values make each row repeat notes from the row below.

### Anti-aliasing
Filters the samples while they are pitched up, which reduces harsh aliasing tones at a small cost of brightness.

### Limiter mode
How hard the master limiter works. Subtle keeps the dynamics natural; Extreme squeezes loud parts much more.

### Sends mode
Pre-fader sends feed delay and reverb before the track volume; Post-fader sends follow the track volume.

### Extra headroom
Lowers the internal mix level to leave more room before clipping when many loud tracks play together.

### Snap to zero
Makes the sample editor markers move to the nearest zero crossing, which avoids clicks at loop and slice points.

## General

### Radio region
Selects the FM band and channel steps used by the built-in radio in your part of the world.

### Display brightness
Sets the backlight level of the screen.

### Display theme
Selects the colour scheme of the user interface. Tracker CE theme files (.json) from the Themes folder of the SD card are listed after the three built-in themes.

### Display font
Selects the set of fonts used on screen.

### Export theme
Saves the current colour scheme as a Tracker CE theme file (.json) in the Themes folder of the SD card, to edit in the CE theme editor and choose under Display theme. An existing file is never overwritten.

### Pads brightness
Sets how bright the pad lights are.

### Pads preview
Lights the pads of the notes on the current pattern line: on the cursor track only, on all tracks, or not at all.

### Recording options
What live recording from the pads stores: notes only, or also the exact timing between steps, the velocity, or both.

### Numbering mode
How step numbers are shown: decimal counting from 0 or from 1, or hexadecimal.

### Pattern arrangement
Layout of the pattern editor and the direction the cursor moves.

### Pattern top info
What the line above the pattern shows.

### Line in channel
Which side of the line input is used: both channels, or one side copied to both. Applies to the line input in the mixer and to the sample recorder.

### CPU meter
Shows the audio processing load next to the play time. Auto: only when it gets high. Always: all the time. Off: never.

### Synth voices
How many synth voices can sound at once over all three synths (1 to 8, default 4). More voices cost more processing time; when the load gets too high, voices are taken away for a while.

### Radio stereo
Records the built-in radio in stereo. Off keeps the radio mono, as the original firmware does; mono is cleaner on weak stations and doubles the recording time.

### Export sample rate
Sample rate of exported songs, patterns, stems and renders. 44.1 kHz converts them, so they play in time in any DAW. 44117 Hz keeps the Tracker's own rate as the original firmware does; a DAW converts them on import.

### Stream from card
Plays long samples straight from the SD card, so they use only a little memory. Auto streams samples longer than the threshold (and long ones when the project would not fit); All streams every long sample. The project reloads when you change it.

### Stream threshold
How long a sample must be before Auto streams it from the card.

### Pad layout
Stock: the pads follow the project's scale and Notes layout. Piano: two keyboards, an octave apart, from the root note's octave: the white keys along a row, the black keys in the row above. The pads light up as a keyboard; notes outside the scale are dimmer.

### Note names
How black keys are named: C# or Db. Auto follows the project's root note and scale, so F major and D minor show flats and G major shows sharps.

## MIDI

### CC in
Which port accepts MIDI control changes for remote control of mixer and performance parameters.

### CC in channel
The MIDI channel listened to for control changes. ALL accepts every channel.

### Clock sync correction
Shifts the incoming MIDI clock by whole clock pulses (one pulse is a sixth of a step) to line the sequencer up with other gear.

## USB

### USB Storage
On shows the SD card on a connected computer as a USB drive, for copying projects and samples. Playback stops and the project is saved first. Eject the drive on the computer before Exit or Off; the project then reloads from the card. Always Off after power-on.

### Remote control
On lets the desktop simulator use this Tracker as its front panel over the USB cable (start it with --remote). While it does, the controls drive the simulator; a short press of the power switch hands them back. Always Off after power-on.

## Firmware

### Firmware update
Installs a firmware file from the Firmware folder of the SD card. The project is saved first.
