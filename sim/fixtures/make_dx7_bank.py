#!/usr/bin/env python3
"""Writes a synthetic DX7 32-voice bank (VMEM sysex, 4104 bytes) for the
simulator checks: every voice zero apart from transpose (C3) and its name
"VOICE  1".."VOICE 32". The checksum is not checked by the firmware.
    make_dx7_bank.py OUT.syx
"""
import sys

data = bytearray([0xF0, 0x43, 0x00, 0x09, 0x20, 0x00])
for k in range(32):
    voice = bytearray(128)
    voice[117] = 24                                   # transpose: C3
    voice[118:128] = f"VOICE {k + 1:2d}  ".encode()[:10]
    data += voice
data += bytes([0x00, 0xF7])
assert len(data) == 4104
with open(sys.argv[1], "wb") as f:
    f.write(data)
