#!/usr/bin/env python3
"""RAM_DL cost check on a Tracker (issue #71 part 2): visits screens over the
remote and compares the UI's display list count with what the FT81x built.

  tools/dl_check.py PORT [--screens pattern,mixer,...] [--log FILE] [--tol W]

Per screen it waits for fresh telemetry lines ("telemetry: ui ... last frame N
DL words ... hw B B", one per 5 s) and prints N + 36 (Ui::kDriverDlWords: the
driver's font setup) against B / 4 (REG_CMD_DL after the frame). A screen
fails when they differ by more than --tol words, the chip faulted, or the
hardware count went over 2048 words. Needs Config > USB > Remote control On.
Screens that animate (Pattern while playing) differ frame to frame; the static
ones should match within a few words.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracker_remote as tr  # noqa: E402

DRIVER_WORDS = 36   # Ui::kDriverDlWords
RAM_DL_WORDS = 2048

# name -> remote commands that open the screen from anywhere.
SCREENS = {
    "pattern": "tap PATTERN",
    "mixer": "tap MASTER",
    "config": "tap CONFIG",
    "song": "tap SONG",
    "performance": "tap PERFORMANCE",
    "loader": "tap SAMPLELOADER",
    # MIDI 1's Parameters page: CC A-F as six list columns (the page that
    # froze the display). The key toggles Parameters <-> LFO and remembers the
    # last one: back from LFO if it opens there.
    "midi": "tap PATTERN; wait 200; press INSTR; wait 400; enc -70; enc 51; release INSTR; wait 100; "
            "tap INSTRPARAMS; wait 300; if-text LFO tap INSTRPARAMS; wait 300; expect-text CC",
}

UI_LINE = re.compile(r"telemetry: ui .*?last frame (\d+) DL words.*?copro faults (\d+).*?"
                     r"low detail (\d+), hw (\d+) B max (\d+) B")


def next_ui_line(link, start, timeout):
    """(counted, faults, low detail, hw bytes, hw max bytes), next index; None on timeout."""
    end = time.time() + timeout
    i = start
    with link.cond:
        while True:
            while i < len(link.lines):
                m = UI_LINE.search(link.lines[i])
                i += 1
                if m:
                    return tuple(int(v) for v in m.groups()), i
            left = end - time.time()
            if left <= 0:
                return None, i
            link.cond.wait(left)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--screens", default=",".join(SCREENS), help="comma-separated, from: " + ", ".join(SCREENS))
    ap.add_argument("--samples", type=int, default=2, help="telemetry lines per screen (default 2)")
    ap.add_argument("--tol", type=int, default=8, help="allowed |hw - (counted + 36)| in words (default 8)")
    ap.add_argument("--log", help="keep every received line here")
    a = ap.parse_args()
    names = [s.strip() for s in a.screens.split(",") if s.strip()]
    bad = [s for s in names if s not in SCREENS]
    if bad:
        ap.error("unknown screen(s): " + ", ".join(bad))
    try:
        link = tr.Link(a.port, a.log)
    except OSError as e:
        print("cannot open %s: %s" % (a.port, e), file=sys.stderr)
        return 2
    runner = tr.Runner(link, ".", tr.load_buttons())
    runner.stop_on_fail = True
    failed = 0
    seen_faults = None
    try:
        link.connect()
        print("build %s" % runner.status().get("build", "unknown"))
        print("%-12s %-20s %8s %8s %8s %6s %6s %4s" %
              ("screen", "shown", "counted", "+36", "hw", "diff", "faults", "low"))
        for name in names:
            runner.run_text(SCREENS[name])
            time.sleep(1.0)
            shown = runner.status().get("scr", "?")
            # The first line after the move may cover the old screen's frames:
            # skip it, then take the samples.
            _, i = next_ui_line(link, link.mark(), 12)
            for _ in range(a.samples):
                got, i = next_ui_line(link, i, 12)
                if got is None:
                    print("%-12s no telemetry line (is telemetry on?)" % name)
                    failed += 1
                    break
                counted, faults, low, hw, hwmax = got
                est = counted + DRIVER_WORDS
                hww = hw // 4
                diff = hww - est
                new_fault = seen_faults is not None and faults > seen_faults
                seen_faults = faults
                ok = (abs(diff) <= a.tol and hww <= RAM_DL_WORDS and hwmax // 4 <= RAM_DL_WORDS
                      and not new_fault)
                if not ok:
                    failed += 1
                print("%-12s %-20s %8d %8d %8d %+6d %6d %4d%s" %
                      (name, shown[:20], counted, est, hww, diff, faults, low,
                       "" if ok else "  <-- FAIL"))
    except tr.Fail as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    finally:
        link.close()
    print("%d sample(s) failed" % failed if failed else "all screens within %d words" % a.tol)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
