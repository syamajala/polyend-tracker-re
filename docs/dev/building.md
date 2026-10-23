# Building

## Firmware

Needs the **Arm GNU toolchain** (`arm-none-eabi-gcc`; CI uses 14.2.Rel1) and Python 3.

```sh
make clean
make -j VERSION=2.1.2
```

This writes `build/opentracker.elf`, `build/opentracker.hex` and the flashable image
`build/PolyendTrackerOTv212.ptf`.

!!! warning "Change VERSION? Run `make clean` first"
    Objects aren't rebuilt when only `VERSION` changes, so the image would report the old
    version. Check with `strings build/opentracker.elf | grep -x 2.1.2`.

The image name must be exactly 24 characters (the update bootloader rejects other
lengths), so it carries the version digits only.

### Build options

| Option | Builds |
|---|---|
| `OT_GAMES=1` | an image with Games (off by default in this fork: it frees 5.8 KB of SRAM and ~49 KB of flash); build dir `build/games` |
| `OT_STACK_GUARD_TEST=1` | a stack guard test image: with `/Firmware/ot_guardtest.txt` on the card it overflows the stack once on purpose, and the next boot logs PASSED / FAILED; build dir `build/guardtest` |
| `SELFTEST=1` | a hardware bring-up image that always runs the selftest |
| `OT_SDRAM_CACHE=0`, `OT_FASTRUN=0`, `OT_USB_MSC=0` | diagnostic variants (no SDRAM cache, all audio code in flash, no USB mass storage) |

### Size checks

```sh
python3 tools/telemetry_check.py size --size-tool arm-none-eabi-size build/opentracker.elf
python3 tools/telemetry_check.py frames build
```

- **Flash** (`.text + .data`): fails above 720,000 B, warns above 600,000 B.
- **Stack headroom** (`_estack − _ebss`): fails below 75 KiB.
- **libm**: fails if double-precision math functions get linked.
- **Frames**: fails if any single function's stack frame is as large as the stack guard
  (8 KB) or unbounded, warns above 75 % of it.

## Host tests

The core (`src/core`) is platform-independent and tested on the host with a C++17
compiler (CI uses GCC 14, with AddressSanitizer and UBSan):

```sh
make host-test
```

On some kernels AddressSanitizer needs `sudo sysctl -w vm.mmap_rnd_bits=28`. A newer GCC
can miss warnings that GCC 14 reports (CI builds with `-Werror`); to reproduce CI exactly:
`make -C tests CXX="g++ -D_FORTIFY_SOURCE=2"`.

## Simulator

```sh
make -C sim
build/sim/opentracker-sim
```

Needs SDL2 (and ALSA for MIDI). See [Simulator](simulator.md).

## Test songs

```sh
make testsongs
```

Builds nine **OT Test** songs and seven **BLAST GRID** synth remixes into
`build/testsongs/`. Copy its `Projects/` and `Samples/` folders to a card.

## Performance gate

```sh
make perf-gate [LOG=tracker.log]
```

Instruction counts of the audio engine on an emulated Cortex-M4 (QEMU) against
`tests/perf_baseline.txt` (+5 % max) and per-engine limits, the flash and stack checks,
a host benchmark, and, with `LOG=`, the telemetry thresholds of a device log. Needs
`qemu-system-arm` for the M4 part (skipped otherwise). Run it for anything on the audio
path.
