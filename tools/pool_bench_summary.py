#!/usr/bin/env python3
"""[I] #21 benchmark table from tools/pool_bench.sh logs.

  tools/pool_bench_summary.py bench

One row per OT Pool song, one column per image folder (h300, f300, ...): the
median cyc_per_voice of the song's telemetry cpu lines with a sounding voice,
skipping the first line (the note may not have started yet). Overruns are
summed. Pool-on column = h300 (or any folder whose name starts with h)."""
import os
import re
import statistics
import sys

CPU = re.compile(r"telemetry: cpu avg=[\d.]+% peak=[\d.]+% overruns=(\d+) .*synth_voices=(\d+) cyc_per_voice=(\d+)")
SONGS = ["ACD", "PERC", "FM4S", "CS-Type", "8-Type", "DX7", "VAP", "FAT", "WTFM",
         "JU6", "D-Type", "A-Type", "T-Type", "Hand", "B-Type", "ARC"]


def read(path):
    vals, ovr = [], 0
    with open(path, errors="replace") as f:
        for line in f:
            m = CPU.search(line)
            if not m:
                continue
            ovr += int(m.group(1))
            if int(m.group(2)) > 0 and int(m.group(3)) > 0:
                vals.append(int(m.group(3)))
    vals = vals[1:] if len(vals) > 2 else vals
    return (int(statistics.median(vals)) if vals else None), ovr, len(vals)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "bench"
    images = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
    pool = next((i for i in images if i.startswith("h")), None)
    data = {}
    for im in images:
        for n in range(1, len(SONGS) + 1):
            p = os.path.join(root, im, f"pool{n}.log")
            if os.path.exists(p):
                data[(im, n)] = read(p)
    print("song".ljust(14) + "".join(im.rjust(16) for im in images) + ("   pool vs best other" if pool else ""))
    for n, name in enumerate(SONGS, 1):
        row = f"{n:2} {name}".ljust(14)
        cells = {}
        for im in images:
            v = data.get((im, n))
            if v and v[0] is not None:
                cells[im] = v[0]
                row += (f"{v[0]:,}" + (f" ovr{v[1]}" if v[1] else "") + f" n{v[2]}").rjust(16)
            else:
                row += "-".rjust(16)
        if pool in cells:
            others = [c for im, c in cells.items() if im != pool]
            if others:
                base = min(others)
                row += f"   {100.0 * (cells[pool] - base) / base:+.1f} %"
        print(row)


if __name__ == "__main__":
    main()
