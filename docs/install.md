# Install and go back

## Get an image

The firmware image is a single file named like `PolyendTrackerOTv300.ptf` (for version
3.0.0). Three ways to get one:

- **Releases:** tagged versions are published on the fork's
  [Releases](https://github.com/syamajala/polyend-tracker-re/releases) page, with
  `Modules.zip` (the card modules, below) and `MidiPresets.zip`.
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

## Install the modules

DX7, the other extra synths and the [master effects](guide/master-fx.md) aren't in the
firmware image: they are **card modules**, `.otm` files the Tracker loads from the SD card.
The five Mini synths (ACD, FAT, VAP, WTFM, PERC) are built in and need nothing.

1. Download **`Modules.zip`** from the same release as the firmware.
2. Unzip it. It holds one folder, `Modules`, with the `.otm` files.
3. Copy the **`Modules`** folder to the top of the SD card, so the files sit in
   `/Modules/` (for example `/Modules/dx7.otm`). If a `Modules` folder is already there,
   **replace all of its files**.
4. Restart the Tracker.

What's in it:

| Kind | Modules |
|---|---|
| Synths | `dx7` (DX7), `fm4` (FM4), `ju6` (JU6), `dtype` (D-Type), `cstype` (CS-Type), `atype` (A-Type), `ttype` (T-Type drums), `eight` (8-Type), `hand` (Hand-Type), `btype` (B-Type), `arc` (Arc-Type) |
| Effects | `dirt`, `lofi`, `chorus`, `bbd`, `echo`, `tape` |

- The card synths show in a synth slot's **Model** list, and the effects in the
  [FX bank](guide/master-fx.md). A module only takes memory while a project uses it;
  **Config > [Memory](guide/more.md#memory)** shows what is loaded.
- You can leave out modules you don't want: delete their files from `/Modules/`.
- **Update the modules with every firmware update.** Modules from another firmware version
  don't load: the boot screen says the module "needs updating".
- In [USB Storage mode](guide/more.md#usb-storage-mode) you can copy synth modules without
  a restart: leaving USB Storage mode reads `/Modules/` again. A changed effect module
  needs a restart.
- Without the folder the Tracker works as before, without those synths and effects. A
  project that uses a missing synth opens with "Synth N (name) not available" on that slot.

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
