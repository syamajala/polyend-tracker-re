# AGENTS.md

OpenTracker: firmware for the original Polyend Tracker (NXP K66, Cortex-M4, 256 KB SRAM,
16 MB SDRAM), written from behavioural specifications. This file lists the rules that are
easy to break; `README.md` and `sim/README.md` have the rest.

## Build and test

- Firmware: `make clean && make -j VERSION=2.2.1`. The Makefile's default VERSION is
  stale, and objects aren't rebuilt when it changes, so run `make clean` whenever it does.
- Host tests: `make host-test`. Simulator: `make -C sim check` (headless; it needs SDL2).
- Audio changes: `make perf-gate`. Flash / stack headroom / libm:
  `tools/telemetry_check.py size build/opentracker.elf`.
- Test in the simulator and the host tests first. The device is for sound, real CPU and
  stack use, pads/LEDs, codec and SD timing.
- `OT_GAMES=0`, `OT_FASTRUN=0` and `OT_USB_MSC=0` each build into their own directory
  under `build/`. Say which build you measured when you quote memory numbers.
- `pahole -C App build/opentracker.elf` shows struct layouts.

## Code rules

- **No static constructors.** No namespace-scope object or pointer with a dynamic
  initializer (e.g. `T* const p = f();` with `f()` in another file). Use `constexpr`/
  `constinit`, an `extern` array in a header with `inline` helpers, or init in `start()`.
  Check: `arm-none-eabi-nm -C build/opentracker.elf | grep _GLOBAL__sub_I`, compared with
  the base branch.
- **Memory:** SRAM is the scarce one. Large buffers, data and UI state go in SDRAM
  (`OT_SDRAM_BSS`, `src/core/sdram_bss.h`). New code runs from flash, not `OT_FASTRUN_FN`,
  unless a benchmark shows it's needed.
- **Audio path:** no allocation, no I/O, no locks.
- `src/core` is platform-independent (firmware, simulator, host tests); `src/hal` is the
  K66 layer.
- Reverse-engineered stock behaviour carries its spec reference in a comment
  (e.g. `spec 06 §7.5`).

## Compatibility

- Stock files (`project.mt`, patterns, `.pti`, `.patch`) keep their exact stock layout.

## Pull requests

- Branch from `v2` and open PRs against it. PRs are squash-merged, so rebase any stacked
  branch after the one below it merges.
- Before opening one: `make host-test`, `make -C sim check`, `make perf-gate` if anything on
  the audio path changed, and compare the flash / SRAM / stack numbers and the
  `_GLOBAL__sub_I` count with the base.
