# Install and go back

## Get an image

The firmware image is a single file named like `PolyendTrackerOTv212.ptf` (for version
2.2.1). Three ways to get one:

- **Releases:** tagged versions are published on the fork's
  [Releases](https://github.com/syamajala/polyend-tracker-re/releases) page. *(No release
  has been made yet.)*
- **CI builds:** every push to `main` builds an image. Open the latest
  [CI run](https://github.com/syamajala/polyend-tracker-re/actions/workflows/ci.yml) on
  `main` and download the `firmware-…` artifact (you need to be logged in to GitHub).
- **Build it yourself:** see [Building](dev/building.md).

!!! warning "Keep the file name"
    The Tracker's update bootloader only accepts names of exactly 24 characters. Don't
    rename the `.ptf` file.

## Install

1. Copy the `.ptf` file to the `/Firmware` folder on the Tracker's SD card (or use
   [USB Storage mode](guide/more.md#usb-storage-mode) to copy it without taking the card
   out).
2. On the Tracker, go to **Config > Firmware update**, select the file and confirm.
3. If the screen stays dark after the update, press the power button once.

## Go back to stock firmware

Keep Polyend's stock image, `PolyendTracker_1.9.2.ptf`, in `/Firmware` too. Then either:

- install it the same way, from **Config > Firmware update**, or
- if OpenTracker doesn't start: put the stock file's name in a text file
  `/Firmware/ot_restore.txt` and power-cycle the Tracker. This check runs before anything
  else in OpenTracker, so it works even when the rest of the firmware doesn't.

!!! tip "Keep `/Firmware` tidy"
    The rear reset button installs a *random* `.ptf` from `/Firmware`. Keep only the
    image you want plus the stock one there.

## Projects

- Projects are saved in the **Tracker Mini 2.2.1 format** (16-track patterns, three synth
  slots). Older original-Tracker projects open as they are, with tracks 13–16 empty.
- The fork's additions (Performance scenes, song counts and scenes, MIDI LFOs) are stored
  in extra files next to `project.mt`. Stock firmware, upstream OpenTracker and the Tracker
  Mini ignore them and open the project normally.
- Stock firmware for the original Tracker reads only tracks 1–8 of 2.x projects (expected
  from the stock loader's rules; not yet confirmed on a stock unit).
