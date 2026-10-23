# Troubleshooting

## The Tracker doesn't start, or keeps restarting

- **Go back to stock:** put the stock image's file name (for example
  `PolyendTracker_1.9.2.ptf`) in `/Firmware/ot_restore.txt` on the card and power-cycle.
  This runs before the rest of OpenTracker. See [Install and go back](../install.md).
- The update bootloader and the rear reset button are never touched by OpenTracker; the
  worst case is reflashing.

## "Restarted after a crash (stack overflow)"

OpenTracker has a **stack guard**: a protected memory area below the stack. If the stack
ever overflows into it, the Tracker resets cleanly instead of silently corrupting memory,
and after the restart it shows **"Restarted after a crash (stack overflow)"** for a few
seconds. The diag log records "reset reason: core lockup".

- Please report it as an [issue](https://github.com/syamajala/polyend-tracker-re/issues),
  with what you were doing and, if possible, the diag log.
- To run without the guard (for example to compare), create an empty file
  `/Firmware/ot_noguard.txt`.

## Sound problems

- **Crackles or dropped notes when busy:** check the **CPU meter** (Config > General >
  CPU meter). The voice limiter cuts synth voices first when the audio gets close to its
  limit; lowering **Synth voices** or synth polyphony helps.
- **A synth slot is silent and shows "not available":** its model has no engine in this
  build (WTFM).
- **DX7 voice sounds different from a hardware DX7:** report the voice (`.syx` file) in an
  issue; the engine is checked against Dexed, not hardware.

## The diag log and telemetry

Over USB, the Tracker also appears as a serial port with a **diag log**: boot messages,
warnings, and every 5 seconds a **telemetry** line:

```text
telemetry: render cyc/block min … avg … max … | loop max … ms | stack peak … B heap … B of … B (free … B) | rx restarts … stale …
```

- **render**: audio CPU per 128-sample block (the budget is 522,240 cycles).
- **loop max**: the slowest main-loop pass (UI, SD card).
- **stack peak / free**: the deepest stack use since boot and what's left.
- **rx restarts / stale**: Line in health (I2S input restarts and stale blocks).

Read it with any serial terminal, or capture it with `tools/tracker_remote.py --log`.
`tools/telemetry_check.py log FILE` checks a captured log against the thresholds.

## Known limitations

See [Home](../index.md#known-limitations).
