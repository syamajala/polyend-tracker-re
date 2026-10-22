#!/usr/bin/env python3
"""Memory use of two firmware images side by side (issue #25), as a Markdown
table for a CI job summary:

  tools/size_report.py BASE.elf HEAD.elf [--size-tool arm-none-eabi-size] [--nm-tool ...]

Rows: flash (.text + .data, as telemetry_check.py size), the SRAM sections
(.data with the SRAM code .fastrun, .bss, .dma, USB buffers), .sdram_bss, and
the stack headroom _estack - _ebss with its limit (telemetry_check.STACK_MIN).
Prints only; the limits are checked by telemetry_check.py size.
"""
import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telemetry_check import FLASH_FAIL, FLASH_WARN, STACK_MIN  # noqa: E402

SRAM_SECTIONS = [".data", ".bss", ".dma", ".usbdescriptortable", ".dmabuffers", ".usbbuffers"]


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise OSError(f"{cmd[0]} failed: {r.stderr.strip()}")
    return r.stdout


def measure(elf, size_tool, nm_tool):
    """ELF -> {row name: bytes}."""
    sections = {}
    for line in run([size_tool, "-A", elf]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith(".") and parts[1].isdigit():
            sections[parts[0]] = int(parts[1])
    berkeley = run([size_tool, elf]).splitlines()[1].split()
    syms = {}
    for line in run([nm_tool, elf]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    m = {
        "Flash (.text + .data)": int(berkeley[0]) + int(berkeley[1]),
        "SRAM total": sum(sections.get(s, 0) for s in SRAM_SECTIONS),
        "  .data (incl. .fastrun code)": sections.get(".data", 0),
        "  .fastrun code in SRAM": syms.get("__fastrun_end", 0) - syms.get("__fastrun_start", 0),
        "  .bss": sections.get(".bss", 0),
        "  .dma + USB buffers": sum(sections.get(s, 0) for s in SRAM_SECTIONS[2:]),
        "SDRAM .sdram_bss": sections.get(".sdram_bss", 0),
    }
    if "_estack" in syms and "_ebss" in syms:
        m["Stack headroom (_estack - _ebss)"] = syms["_estack"] - syms["_ebss"]
    return m


def table(base, head):
    out = ["| | base | this PR | change |", "|---|---:|---:|---:|"]
    for key in head:
        b, h = base.get(key), head[key]
        if b is None:
            out.append(f"| {key.strip()} | – | {h:,} B | |")
            continue
        d = h - b
        change = "0" if d == 0 else f"**{d:+,} B**"
        name = key.strip() if not key.startswith("  ") else "&nbsp;&nbsp;" + key.strip()
        out.append(f"| {name} | {b:,} B | {h:,} B | {change} |")
    return out


def notes(head):
    out = []
    flash = head["Flash (.text + .data)"]
    if flash > FLASH_FAIL:
        out.append(f"- :x: flash {flash:,} B is over the {FLASH_FAIL:,} B limit")
    elif flash > FLASH_WARN:
        out.append(f"- flash {flash:,} B: {FLASH_FAIL - flash:,} B left before the {FLASH_FAIL:,} B limit")
    room = head.get("Stack headroom (_estack - _ebss)")
    if room is not None:
        mark = ":x: " if room < STACK_MIN else ""
        out.append(f"- {mark}stack headroom {room:,} B: {room - STACK_MIN:+,} B against the {STACK_MIN:,} B limit")
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("head")
    ap.add_argument("--size-tool", default="arm-none-eabi-size")
    ap.add_argument("--nm-tool", default=None)
    ap.add_argument("--title", default="Firmware memory (Games off)")
    a = ap.parse_args(argv)
    nm = a.nm_tool or (a.size_tool[:-4] + "nm" if a.size_tool.endswith("size") else "nm")
    try:
        base = measure(a.base, a.size_tool, nm)
        head = measure(a.head, a.size_tool, nm)
    except (OSError, IndexError, ValueError) as e:
        print(f"size_report: {e}", file=sys.stderr)
        return 2
    print(f"### {a.title}\n")
    print("\n".join(table(base, head)))
    n = notes(head)
    if n:
        print()
        print("\n".join(n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
