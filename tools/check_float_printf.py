#!/usr/bin/env python3
"""Refuse printf float conversions (%f %e %g %a) in the firmware sources.

The firmware links newlib's printf without float support (Makefile: no
-u _printf_float, ~11 KB of flash, #122), so "%.2f" would print nothing on the
device while the simulator and host tests (glibc) still show the number. Format
with fm::fixed (src/core/fmath.h) and "%s" instead. Comments are not checked.
Usage: check_float_printf.py [DIR...] (default: src third_party/teensy3)
"""
import pathlib
import re
import sys

SPEC = re.compile(r'%[-+ 0#]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|L|z|j|t)?([diouxXcspnfFeEgGaA%])')
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)


def float_specs(text):
    """(line, spec) for every float conversion in a string literal."""
    for m in TOKEN.finditer(text):
        tok = m.group(0)
        if tok[0] != '"':
            continue
        for s in SPEC.finditer(tok):
            if s.group(1) in 'fFeEgGaA':
                yield text.count('\n', 0, m.start()) + 1, s.group(0)


def main(argv):
    roots = argv or ['src', 'third_party/teensy3']
    bad = 0
    for root in roots:
        for p in sorted(pathlib.Path(root).rglob('*')):
            if p.suffix not in ('.c', '.cpp', '.h'):
                continue
            for line, spec in float_specs(p.read_text(errors='replace')):
                print(f'{p}:{line}: "{spec}" prints nothing on the device; use fm::fixed (src/core/fmath.h)')
                bad += 1
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
