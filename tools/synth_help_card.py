#!/usr/bin/env python3
"""[I] The card copy of a synth help file (modules/synth_help.h): its "# Settings"
reference only, from that heading to the next # heading (fenced code skipped),
so the guide above it can grow without the card file growing.

    synth_help_card.py SRC.md OUT.md
"""
import sys


def reference(lines):
    out, fence, inside = [], False, False
    for line in lines:
        bare = line.rstrip("\r\n")
        if bare.startswith("```"):
            fence = not fence
        if not fence and bare.startswith("# "):
            if inside:
                break
            inside = bare.rstrip() == "# Settings"
        if inside:
            out.append(line)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8", newline="") as f:
        lines = f.readlines()
    out = reference(lines)
    if not out:
        sys.exit(f"{sys.argv[1]}: no '# Settings' reference")
    with open(sys.argv[2], "w", encoding="utf-8", newline="") as f:
        f.writelines(out)


if __name__ == "__main__":
    main()
