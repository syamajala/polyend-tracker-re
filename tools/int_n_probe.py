#!/usr/bin/env python3
"""Is the FT81x INT_N pin wired to the MCU? (issue #71: a fault interrupt
instead of polling.) Needs an image built with OT_REMOTE_REGS=1 and
Config > USB > Remote control On.

  tools/int_n_probe.py /dev/ttyACM0 [--toggles 8]

The FT81x pulls INT_N low while an enabled interrupt flag is set. The flags
stay set until REG_INT_FLAGS is read, which the driver never does, so with
REG_INT_MASK = INT_CMDEMPTY, REG_INT_EN = 1 / 0 drives the pin low / high.
INT_N is made push-pull (REG_GPIOX bit 9) so it needs no pull-up. While it
toggles, every K66 port's GPIOx_PDIR is read; a bit that follows every
toggle is the pin. Unused pins (PCR MUX 0, disabled: they read 0) are made
GPIO inputs for the probe and put back; pins in use are only read.
Everything is restored at the end, also on an error.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tracker_remote import Fail, Link, Runner, load_buttons  # noqa: E402

REG_ID, REG_GPIOX = 0x302000, 0x30209C
REG_INT_FLAGS, REG_INT_EN, REG_INT_MASK = 0x3020A8, 0x3020AC, 0x3020B0
INT_CMDEMPTY = 0x20
PORTS = "ABCDE"


def pcr_addr(port, pin):
    return 0x40049000 + 0x1000 * port + 4 * pin


def pdir_addr(port):
    return 0x400FF010 + 0x40 * port


def pddr_addr(port):
    return 0x400FF014 + 0x40 * port


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--toggles", type=int, default=8)
    ap.add_argument("--log")
    a = ap.parse_args()
    link = Link(a.port, a.log)
    r = Runner(link, ".", load_buttons())
    link.connect()
    restore_pcr = []
    saved = {}
    try:
        chip = r.ftrd(REG_ID, 1)[0]
        print("REG_ID 0x%02X%s" % (chip, "" if chip == 0x7C else "  (expected 0x7C)"))
        if chip != 0x7C:
            raise Fail("display not answering")
        saved["gpiox"] = int.from_bytes(r.ftrd(REG_GPIOX, 2), "little")
        saved["mask"] = r.ftrd(REG_INT_MASK, 1)[0]
        saved["en"] = r.ftrd(REG_INT_EN, 1)[0]
        print("REG_GPIOX 0x%04X (INT_N %s), REG_INT_MASK 0x%02X, REG_INT_EN %d" % (
            saved["gpiox"], "push-pull" if saved["gpiox"] & 0x200 else "open-drain", saved["mask"], saved["en"]))

        # Pin map: MUX of every PCR, GPIO direction; unused pins -> GPIO input.
        used = {}
        for p in range(len(PORTS)):
            pcrs = r.peek(pcr_addr(p, 0), 8) + r.peek(pcr_addr(p, 8), 8) + r.peek(pcr_addr(p, 16), 8) + \
                r.peek(pcr_addr(p, 24), 8)
            pddr = r.peek(pddr_addr(p), 1)[0]
            for pin, v in enumerate(pcrs):
                mux = (v >> 8) & 7
                used[(p, pin)] = mux
                if mux == 0 and not (pddr >> pin) & 1:
                    r.poke(pcr_addr(p, pin), v | 0x100)
                    restore_pcr.append((pcr_addr(p, pin), v))
        print("pins: %d made GPIO inputs for the probe, %d GPIO, %d on a peripheral" % (
            len(restore_pcr), sum(1 for m in used.values() if m == 1), sum(1 for m in used.values() if m > 1)))

        r.ftwr(REG_GPIOX, saved["gpiox"] | 0x200, 2)   # INT_N push-pull
        r.ftwr(REG_INT_MASK, INT_CMDEMPTY, 1)

        def ports():
            return [r.peek(pdir_addr(p), 1)[0] for p in range(len(PORTS))]

        follows = {}   # (port, pin) -> [count low when asserted & high when released, inverted count]
        for _ in range(a.toggles):
            r.ftwr(REG_INT_EN, 1, 1)
            on = ports()
            r.ftwr(REG_INT_EN, 0, 1)
            off = ports()
            for p in range(len(PORTS)):
                for pin in range(32):
                    lo, hi = (on[p] >> pin) & 1, (off[p] >> pin) & 1
                    f = follows.setdefault((p, pin), [0, 0])
                    if lo == 0 and hi == 1:
                        f[0] += 1
                    elif lo == 1 and hi == 0:
                        f[1] += 1
        hits = [(k, v) for k, v in follows.items() if max(v) == a.toggles]
        if not hits:
            print("RESULT: no MCU pin follows INT_N (%d toggles): not routed to the K66" % a.toggles)
        for (p, pin), v in hits:
            print("RESULT: PT%s%d follows INT_N %s (%d/%d toggles), PCR MUX was %d" % (
                PORTS[p], pin, "(active low)" if v[0] == a.toggles else "INVERTED", max(v), a.toggles,
                used[(p, pin)]))
        partial = [(k, v) for k, v in follows.items() if 0 < max(v) < a.toggles]
        for (p, pin), v in partial:
            print("       PT%s%d followed %d/%d (noise or another signal)" % (PORTS[p], pin, max(v), a.toggles))
    finally:
        try:
            if saved:
                r.ftwr(REG_INT_EN, saved.get("en", 0), 1)
                r.ftwr(REG_INT_MASK, saved.get("mask", 0xFF), 1)
                r.ftwr(REG_GPIOX, saved["gpiox"], 2)
            for addr, v in restore_pcr:
                r.poke(addr, v)
            print("restored: display interrupt registers, REG_GPIOX, %d pin(s)" % len(restore_pcr))
        finally:
            link.close()


if __name__ == "__main__":
    try:
        main()
    except Fail as e:
        print("error: %s" % e)
        sys.exit(1)
