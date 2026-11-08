# Building

## Firmware

Needs the **Arm GNU toolchain** (`arm-none-eabi-gcc`; CI uses 14.2.Rel1), Python 3 and the
two module repositories from upstream 3.x: the synth engines
([opentracker-synths](https://github.com/opentracker-fw/opentracker-synths)) and the effects
([opentracker-fx](https://github.com/opentracker-fw/opentracker-fx)), side by side.

```sh
git clone https://github.com/opentracker-fw/opentracker-synths
git clone https://github.com/opentracker-fw/opentracker-fx
make clean
make -j MODULES_DIR=$PWD/../opentracker-synths
```

`MODULES_DIR` defaults to `../../opentracker-synths` (two levels up, where CI puts it);
set it, or export it once, if the repositories sit next to this one. `opentracker-fx` is
found next to `opentracker-synths`.

A **build profile** (`profiles/<name>.toml`, `PROFILE=`, default `full`) picks the modules
compiled into the image: `full` has ACD, FAT, VAP, PERC, WTFM and the master chorus, BBD
chorus and tape inserts; `core` has none. Each profile builds into `build/<profile>/`.

**Card modules** (fork #21): a profile entry with `card = true` stays out of the image.
`make card` builds it as `build/<profile>/otm/<name>.otm` for the card's `Modules/` folder,
and the host builds (`make host-test`, the simulator, the tools) compile it in, as a device
with the card's modules would have it. `in_tree = true` takes the module's path from this
repository instead of `MODULES_DIR`. `full` builds in the five Mini synths (ACD, FAT,
VAP, PERC, WTFM); its other synths are card modules: DX7 (`modules/dx7`, in this
repository), FM4S, JU6S, D-Type, CS-Type, A-Type, T-Type, 8-Type, Hand, B-Type and ARC. No
image has those built in.

At boot the firmware looks at every `Modules/*.otm` (`src/core/modules/module_files.h`):
a module without synth engines (an effect) loads into the card pool for good; a synth
module is only listed, so its engines show in the Model list. The card synths in the pool
are the ones the project's synth slots use: Open loads them before the slots' patches,
and a Model change loads the new one once the choice has rested for 1/4 s, while stopped
(while playing the slot says "Loads at Stop"). Every load starts the synth voices over
(`SynthBank::holdEngines`), and the slots' synths share the fast block again. A synth that
doesn't fit the pool (32 KB) stays unloaded, with "Card memory full" on its slot;
the first slot gets the room first. Two slots on one engine load it once.

A synth module's `.otm` carries its **hot row**: the functions that do most of its work,
hottest first, from `tools/hot_rows/<module>.hot`. When the image has a fast block in
SRAM_L, the loader shares it between the loaded synths and copies as much of each row
there as its share holds; the rest runs from the slower pool. The rows come from every mode
of every synth under the QEMU bench (`profiles/allsynths.toml`): run `make hot-rows`
after changing a synth's DSP code, and commit the new rows. A row function the module
build no longer has is reported by `make card` and simply stays in the slower pool.

**Shared fast pool** (the default when the profile compiles in a synth with a hot
row): the built-in synths join in. Each built-in synth with a hot row
keeps one section per function (`OT_FASTRUN_ROW=1`), and its row runs from flash,
laid out back to back (`tools/hot_pool.py layout`). The SRAM_L block that the
synths' `.fastrun` code used to take (13,696 B) is the fast block. All the
project's synths take turns for it, built in or from the card, in slot order. A
built-in row moves into its share when the slots change (Open, or a Model change
while stopped). Its calls back into flash go through stubs, and its tables and
vtable pointer follow it (`src/core/modules/hot_rows.h`). A row with no room
keeps running from flash: slower, never wrong. The image links twice with
`--emit-relocs`. `tools/hot_pool.py table` writes the move tables from the first
link, and the second link must give the same ones. The table step lists what
still reaches a row from flash only. That code runs the flash copy (for example
PERC's snare path). On the device, the pool cut card synths' cost per voice by
22-29 % and the built-in synths' by 1-7 %, with 160 B more stack headroom (#21).
Changing a synth while stopped re-shares the block, which cuts synth notes still
ringing.

`VERSION` defaults to the Makefile's value, which follows the last upstream merge, so leave
it unset unless you need a different one. This writes `build/full/opentracker.elf`,
`build/full/opentracker.hex` and the flashable image `build/full/PolyendTrackerOTv<digits>.ptf`
(e.g. `PolyendTrackerOTv300.ptf` for 3.0.0; the letter after `OT` is the profile's variant,
`v` for `full`).

!!! warning "Change VERSION? Run `make clean` first"
    Objects aren't rebuilt when only `VERSION` changes, so the image would report the old
    version. Check with `strings build/full/opentracker.elf | grep -x <version>`.

The image name must be exactly 24 characters (the update bootloader rejects other
lengths), so it carries the version digits only.

### Build options

| Option | Builds |
|---|---|
| `OT_STACK_GUARD_TEST=1` | a stack guard test image: with `/Firmware/ot_guardtest.txt` on the card it overflows the stack once on purpose, and the next boot logs PASSED / FAILED; build dir `build/full/guardtest` |
| `OT_HOT_POOL=0` | the old layout without the shared fast pool: each built-in synth's `.fastrun` code in SRAM_L, card synths in the slower pool (see above); build dir `build/<profile>/nopool`. The fallback image when testing a boot-layout change |
| `SELFTEST=1` | a hardware bring-up image that always runs the selftest |
| `OT_SDRAM_CACHE=0`, `OT_FASTRUN=0`, `OT_USB_MSC=0` | diagnostic variants (no SDRAM cache, all audio code in flash, no USB mass storage) |

### Size checks

```sh
python3 tools/telemetry_check.py size --size-tool arm-none-eabi-size build/full/opentracker.elf
python3 tools/telemetry_check.py frames build/full
make budget   # the profile's module flash budgets
```

- **Flash** (`.text + .data`): fails above 800,000 B, warns above 720,000 B (upstream fails at 720,000 B; the fork's remote control and Performance mode need more). The image itself may not pass 877,664 B, the stock 1.9.2 extent (`tools/check_image.py`).
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

It builds and runs the tests on every core (`nproc`); `HOST_JOBS=N` sets the number of
jobs, `HOST_JOBS=1` runs them one by one.

On some kernels AddressSanitizer needs `sudo sysctl -w vm.mmap_rnd_bits=28`. A newer GCC
can miss warnings that GCC 14 reports (CI builds with `-Werror`); to reproduce CI exactly:
`make -C tests CXX="g++ -D_FORTIFY_SOURCE=2"`.

## Simulator

```sh
make -C sim
build/full/sim/opentracker-sim
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
