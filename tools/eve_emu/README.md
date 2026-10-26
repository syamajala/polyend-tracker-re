# FT81x emulator replay (#71)

Checks what the UI's frames really cost on the display chip: frames recorded
from the simulator are replayed on Bridgetek's EVE emulator (FT813, like the
Tracker), which reports how many RAM_DL words each frame built (REG_CMD_DL)
and whether the coprocessor faulted. RAM_DL holds 2048 words; a frame that
needs more faults and, before #71 part 3, froze a static screen.

```
make -C sim eve-check
```

1. The simulator runs `sim/scripts/display_budget.txt` and
   `sim/scripts/fork_screens.txt` with `--frames DIR`: at every `budget NAME`
   checkpoint it draws the screen once more with the fragment cache off (as
   the remote frame export does) and saves `DIR/NAME.otf` plus the UI's own
   RAM_DL count in `DIR/index.txt`.
   With custom fonts it also saves `DIR/NAME.text.otf`: the same screen with
   its text as `CMD_TEXT` (`kCustomText`), the pixel reference for the glyph
   runs `Gfx::text` draws instead (#71 part 4).
2. `otf2cmd` turns each frame into the word stream the FT81x driver sends
   (`src/hal/ft81x.cpp`: the per-frame font setup, pictures as DL words).
3. `replay.exe` sends each stream to the emulator and reads REG_CMD_DL; for
   a frame with a reference it shows both (the Tracker's panel timings) and
   compares the scanned-out pixels.
4. `check.py` fails on a fault, more than 2048 words, a frame where the
   emulator and the UI's count (+36 words of driver font setup with custom
   fonts) differ by more than 8 words, or a glyph-run frame whose pixels
   differ from its `CMD_TEXT` form. CI runs this (job `display-emulator`).

The pixel comparison is how the coprocessor's text alignment was pinned
down: it treats a string as spanning width - 1 pixels from its first column,
so `OPT_RIGHTX` puts it at x - (w - 1) and `OPT_CENTERX` at x - (w - 1) / 2
(`Gfx::textAsGlyphs`; the simulator's `CMD_TEXT` now does the same for
custom fonts). `REPLAY_DUMP=1` makes `replay.exe` write both framebuffers of
a differing frame (`NAME.a.rgb`, `NAME.b.rgb`, 800 x 480 x 4 bytes).

The emulator and the UI agree to within 1 word on every checkpoint (the UI
counts one word less: the frame's closing DISPLAY). On the device the same
comparison (`tools/dl_check.py`) is within -5..+2 words (2026-10-25).

## Requirements

- `wine` (64-bit) and `x86_64-w64-mingw32-g++` (Arch: `wine mingw-w64-gcc`;
  Ubuntu: `wine wine64 mingw-w64`).
- Network on the first run: `check.py` downloads `bt8xxemu.dll`, `zlib.dll`
  and the two headers from github.com/Bridgetek/EVE-Emulator at a pinned
  commit (v5.3.35), checks their SHA-256 and keeps them in `build/eve_emu`.
  Bridgetek's licence allows using the emulator to develop EVE software but
  not redistributing it on its own, so it is never committed.

## Your own frames

Frames from a Tracker (`tools/tracker_remote.py PORT -c "frame NAME.otf"`)
replay the same way: put them in a directory with an `index.txt` of
`NAME WORDS` lines (the UI count, e.g. from the telemetry line's
"last frame N DL words") and run `tools/eve_emu/check.py DIR`.

When `ft81x.cpp` changes what it sends per frame (font setup, drawImage,
drawRgb332), change `otf2cmd.cpp` with it.
