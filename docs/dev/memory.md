# OpenTracker memory audit: fork vs upstream 2.1.2

*2026-10-23. Measured from the ELF images (Games off) and on a Tracker over
the remote-control link. Fork: syamajala/polyend-tracker-re `main` (a8ac7b4,
on 2.1.1); upstream: sandlbn/polyend-tracker-re `v2` at 2.1.2 (177150b).
Fork figures corrected the same day from clean builds: the first version
listed the fork's room above `_ebss` as upstream's.*

## Fork `main` on 2026-11-01

*Re-measured from `main` bc714a8 (Games off), after USB audio, multisample,
scene bank files, fader ranges and the SDRAM sharing of #109. The tables
below are the 2026-10-23 audit and are kept for the comparison with upstream.*

| | 2026-10-23 | 2026-11-01 |
|---|---:|---:|
| Flash (`.text` + `.data`) | — | 774,144 B (fail > 800,000 B: ~26 KB left) |
| SRAM `.bss` | 98,284 B | 109,492 B |
| Room above `_ebss` (heap + stack) | 92,448 B | 79,104 B |
| **Margin over the 75 KiB stack rule** | **15,648 B** | **2,304 B** |
| `.sdram_bss` used (148 KB region) | 90,960 B | 129,344 B |
| **`.sdram_bss` free** | **60,592 B** | **22,208 B** |

- **SRAM is tight again.** USB audio is about half of the drop (~7 KB: its rings
  and buffers stay in SRAM because the USB interrupt and the audio block both
  touch them); the rest is spread over other features. Nothing moves from
  SDRAM back to SRAM unless the 75 KiB rule is lowered; the real stack peak is
  still ~18 KB.
- **`.sdram_bss` options left, each with a cost:** the Sample Loader's name pool
  (25.8 KB; shrinking it to 16 KB saves ~10 KB but long-name folders lose
  entries), folding Recents, the text scratch and the multisample staging buffer
  into `g_io` (~10 KB, needs a lifetime audit), `FF_FS_TINY` for the 7.2 KB of
  file handles (a streaming-speed risk). The remote bridge (7.3 KB) stays.

## Summary

- **Upstream is at its limits on both RAMs.** It has 1,104 B free in
  `.sdram_bss` and 1,280 B over its 90 KiB stack rule.
- **Most of that pressure is fixed cost, not features.** A per-screen buffer
  for each browser list, the screens on the SRAM heap, and a stack rule set
  without measurement account for tens of KB.
- **The fork's fixed SRAM is about 1 KB more than upstream's** (169,412 B vs
  168,420 B), even with its extra features. Our margin comes from measuring
  the real stack use, putting the screens in SDRAM and sharing the browser
  buffers.
- **Ready to merge:** branch `feature/sdram-shared-lists` (off `upstream/v2`,
  two commits) frees 16 KB of `.sdram_bss` and 8.4 KB of heap on upstream.
- **These fixes don't need a module system.** They are independent of the
  modular-architecture discussion and can land first. A module system mainly
  helps flash (features in or out of a build); on its own it doesn't reduce
  these fixed RAM costs.

## SRAM (256 KB)

| | Upstream 2.1.2 | Fork `main` |
|---|---:|---:|
| Audio code run from SRAM (`.fastrun`) | 40,248 B | 41,424 B |
| Other initialised data | 2,904 B | 7,016 B |
| `.bss` | 102,580 B | 98,284 B |
| DMA + USB buffers | 22,688 B | 22,688 B |
| **Fixed SRAM total** | **168,420 B** | **169,412 B** |
| Room above `_ebss` (heap + stack) | 93,440 B | 92,448 B |
| Link-time stack rule (`STACK_MIN`) | 92,160 B (90 KiB) | 76,800 B (75 KiB) |
| **Margin over the rule** | **1,280 B** | **15,648 B** |
| Screens (`ScreenSet`) on the heap | 15,884 B | 0 (in SDRAM) |
| Stack guard + heap reserve | — | 12,288 B |
| Stack really left at run time | ~77.5 KB | ~80 KB |

**Measured on a Tracker** (fork, stack painting + telemetry):

| | Value |
|---|---|
| Deepest stack use | 18,084 B (long session: project load and play, Sample Editor effects, Save / Save As, resample, USB storage) |
| Typical | 11–12.5 KB |
| Heap | 232 B once the screens are in SDRAM |
| Deepest single frame | 6,032 B (`-fstack-usage`, every function) |

So about 60 KB of the stack is never touched. The 90 KiB rule is what's
tight, not the stack itself.

## SDRAM (16 MB)

The fixed layout is the same in both: about 11.25 MB of sample pool, 1.56 MB
for delay and reverb, and patterns, undo, clipboard, Performance, stems and
the remote frame buffer. It ends a little below `.sdram_bss`.

| `.sdram_bss` | Upstream 2.1.2 | Upstream + `sdram-shared-lists` | Fork `main` today |
|---|---:|---:|---:|
| Region | 92 KB @ 0x70FD9000 | 92 KB | 148 KB @ 0x70FDB000 |
| Used | 93,104 B | 77,056 B | 90,960 B |
| **Free** | **1,104 B** | **17,152 B** | **60,592 B** |
| Unused SDRAM above the region | 64 KB | 64 KB | 0 |

The SDRAM above the region is used only by the self-test's SD benchmark,
which never runs alongside the app.

## Recommendations for upstream, by payoff

1. **Merge `feature/sdram-shared-lists`** (a034a95, 6b0ccaa).
   - **Shared browser lists:** the Sample Loader, Synth patch browser, File
     Open/Save, Import Mod and firmware-update lists share one buffer
     (`browserListMem()`, 11,400 B). Only one is ever on screen, and each
     rebuilds its list when it opens.
   - **One screenshot row buffer:** the row is converted to BMP bytes in
     place.
   - **Result:** `.sdram_bss` −16,048 B, heap −8.4 KB, flash −344 B. All 91
     upstream test suites and `make -C sim check` pass. In the fork it's
     device-tested by a remote script that visits every list (24 checks).
2. **Screens in SDRAM** (fork 928db9d, `feature/screens-sdram`). `ScreenSet`
   moves from the heap into `.sdram_bss`, which frees the remaining ~7.5 KB
   of heap after step 1. Device-tested: render and loop times were identical
   while browsing a 578-file folder with previews.
3. **Base the stack rule on measurement.**
   - **Measure:** paint the stack and report the peak in telemetry.
   - **Guard:** add a SYSMPU guard below the stack (fork #34: 8 KB; an
     overflow locks up the core, resets, and is reported on the next boot).
   - **Check frames:** a CI check that fails if a single frame could jump
     over the guard (`telemetry_check.py frames`).
   - **Then lower the rule.** The fork went from 90 to 75 KiB, which frees
     about 15 KB of SRAM at link time without moving anything.
4. **Keep the Open's `Project` off the stack, with one spare copy** (fork
   ac02790, with 51d9f57). Upstream keeps a `Project` on the stack in
   `startOpenProject` and in the Workspace load step, plus
   `ProjectJob::proj`. One SDRAM `jobProject_` outside the job state replaces
   all three. That's no extra SDRAM, two big stack frames fewer, and one copy
   fewer.
5. **Grow `.sdram_bss` into the unused 64 KB above it.** It's a linker-script
   and `board.h` size change, plus a `static_assert` that the region stays
   below the end of SDRAM.

**Only if SRAM is still needed:**

| Candidate | Saves | Condition |
|---|---:|---|
| Instruments to SDRAM | ~17 KB | Read at note-on; needs a CPU check |
| SD bounce buffer | ~16 KB | Only once SDHC → SDRAM DMA is verified |
| Diagnostic log ring | ~8 KB | Shrink it, or move it after boot |
| `.fastrun` code back to flash | varies | Each piece costs audio CPU |

## Tools used

- **`tools/size_report.py`:** flash, SRAM sections, `.sdram_bss`, stack
  headroom and free space, base vs head. Runs as a CI job summary on every PR.
- **`tools/telemetry_check.py`:**
  - `size`: the link-time limits;
  - `frames`: the deepest frame vs the stack guard;
  - `log`: stack peak, heap, CPU and overruns from a device log.
- **`-fstack-usage`** on every build; `pahole -C <type> build/opentracker.elf`
  for struct layouts.
- **Remote-control scripts** (`tools/remote_scripts/*.txt`), run on the
  simulator in CI and on a Tracker with the telemetry log captured.
