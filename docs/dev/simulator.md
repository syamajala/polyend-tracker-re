# Simulator

The desktop simulator runs the **same platform-independent core** (`src/core`) as the
firmware on Linux, with host implementations of the hardware interfaces: an SDL2 window
with the screen, pads and keys, audio out, ALSA MIDI, and a folder as the SD card. Most
features can be developed and tested without flashing.

The full reference is [`sim/README.md`](https://github.com/syamajala/polyend-tracker-re/blob/main/sim/README.md).

## Run it

```sh
make -C sim
build/sim/opentracker-sim [--sd DIR] [--scale 2] [--line-in]
```

- `--sd DIR` is the SD card root (default `~/.opentracker-sd`, created on first run). The
  workspace is saved there when the window closes; Config is kept in `DIR.eeprom`.
- **Keyboard:** F1–F8 are the screen keys, arrows, Space = Play, Shift, Delete, Page Up /
  Down = encoder. **Tab** switches between command mode (letters = module keys, e.g. `2`
  Performance, `0` Song) and pad mode (letter rows = pads). The mouse works on every
  control; right-click latches a button (e.g. Shift).
- `--usb-in NAME` stands in for USB audio from the computer ([USB audio](../guide/usb-audio.md)):
  a capture device, e.g. the monitor of a null sink the browser plays into
  (`--usb-in list` prints the names; see `sim/README.md`). `--usb-in-tone HZ` is the
  headless version.

## Scripts

Without a window (`--script FILE` and/or `--screenshot OUT.png`), time is virtual: every
simulated millisecond is one app loop pass, so runs are deterministic and fast.

```text
wait 300               # advance time (ms)
tap PERFORMANCE        # press and release a button (names: src/core/ui/buttons.h)
press SHIFT / release SHIFT
hold F5 400
pad 12 tap             # pads 0..47
enc 3                  # encoder detents
screenshot perf.png
```

Plus checks (`expect-screen`, `expect-sound`, `expect-playing` …) and `--wav OUT.wav` to
record the audio. `make -C sim check` runs the simulator's own checks.

## Display budgets

The Tracker's display chip (FT813) has a small display-list memory (RAM_DL). The `budget`
and `fragcheck` commands in `sim/scripts/display_budget.txt` and `fork_screens.txt` measure
each screen's largest frame and check it looks identical with the fragment cache on and
off. A new or changed screen needs an entry there.

## Standing in for a Tracker

- `--serve` acts like a Tracker with remote control on, on a pseudo-terminal, so
  `tools/tracker_remote.py` scripts run against it (CI does this).
- `--remote DEV` and `--mirror DEV` use a real Tracker as the panel or show its screen:
  see [Remote control](../guide/remote-control.md).
