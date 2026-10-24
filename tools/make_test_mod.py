#!/usr/bin/env python3
"""Write a small 4-channel ProTracker MOD for the import scripts
(tools/remote_scripts/module_import.txt): four short 8-bit samples (sine and
saw loops, a noise and a sweep one-shot) and two patterns with notes on every
channel and a few common effects. Deterministic, so the output is the same
every run.

  tools/make_test_mod.py OUT.mod
"""
import math
import random
import struct
import sys

PERIODS = [428, 381, 339, 320, 285, 254, 226, 214, 190, 170, 160, 143]
EFFECTS = [(0x0, 0x00), (0xA, 0x02), (0x4, 0x46), (0x1, 0x03), (0xC, 0x30), (0x0, 0x37), (0x3, 0x08), (0xE, 0x93)]


def sample(kind, n, rnd):
    out = bytearray()
    for i in range(n):
        t = i / n
        if kind == 0:
            v = math.sin(2 * math.pi * i / 32)
        elif kind == 1:
            v = (i % 64) / 32.0 - 1
        elif kind == 2:
            v = rnd.uniform(-1, 1) * (1 - t)
        else:
            v = math.sin(2 * math.pi * i * (40 * (1 - t) + 4) / 2000) * (1 - t)
        out.append(int(max(-127, min(127, v * 100))) & 0xFF)
    return bytes(out)


def build(npat=2):
    rnd = random.Random(1)
    samples = [sample(0, 2048, rnd), sample(1, 2048, rnd), sample(2, 1500, rnd), sample(3, 3000, rnd)]
    head = b"AAImport".ljust(20, b"\0")
    for i in range(31):
        if i < 4:
            words = len(samples[i]) // 2
            loop = (0, words) if i < 2 else (0, 1)
            head += ("smp%d" % i).encode().ljust(22, b"\0") + struct.pack(">HBBHH", words, 0, 64, *loop)
        else:
            head += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    order = list(range(npat)) + [0] * (128 - npat)
    head += bytes([npat, 127]) + bytes(order) + b"M.K."
    pats = bytearray()
    for p in range(npat):
        for row in range(64):
            for ch in range(4):
                if (row + ch) % 2 == 0 or ch == 2:
                    s = ch + 1
                    per = PERIODS[(row * 3 + ch * 5 + p) % 12]
                    e, v = EFFECTS[(row + p + ch) % len(EFFECTS)]
                    pats += bytes([(s & 0xF0) | (per >> 8), per & 0xFF, ((s & 0xF) << 4) | e, v])
                else:
                    pats += b"\0\0\0\0"
    return head + bytes(pats) + b"".join(samples)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], "wb") as f:
        f.write(build())
