#!/usr/bin/env python3
"""[I] The docs site's synth pages (docs/dev/docs.md): one page per synth help file
(modules/<synth>/help/<NAME>.md in the synth module repository, format in
src/core/modules/synth_help.h) and an overview, written to docs/synths/ (not in git).

A page is the whole help file: the guide, then the "# Settings" reference one heading
level down (## Settings, ### a section, #### a setting), so the page has one title.
The overview lists the synths with the first paragraph of each guide.

    docs_synth_pages.py [MODULES_DIR]    (default ../opentracker-synths)
"""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "synths")


def page(lines):
    out, fence, settings = [], False, False
    for line in lines:
        if line.startswith("```"):
            fence = not fence
        elif not fence and line.startswith("#"):
            if line.startswith("# "):
                settings = line.rstrip() == "# Settings"
                if settings:
                    line = "#" + line
            elif settings:
                line = "#" + line
        out.append(line)
    return out


def title_and_intro(lines):
    title, intro = "", []
    for line in lines:
        s = line.strip()
        if not title:
            if s.startswith("# "):
                title = s[2:]
            continue
        if not s:
            if intro:
                break
            continue
        if s.startswith("#"):
            break
        intro.append(s)
    return title, " ".join(intro)


def main():
    modules = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "opentracker-synths")
    files = sorted(glob.glob(os.path.join(modules, "modules", "*", "help", "*.md")))
    if not files:
        sys.exit(f"no help files under {modules}/modules/*/help/")
    os.makedirs(OUT, exist_ok=True)
    rows = []
    for f in files:
        with open(f, encoding="utf-8") as h:
            lines = h.read().splitlines(keepends=True)
        name = os.path.splitext(os.path.basename(f))[0].lower()
        with open(os.path.join(OUT, name + ".md"), "w", encoding="utf-8") as h:
            h.writelines(page(lines))
        title, intro = title_and_intro(lines)
        rows.append((title.lower(), title, name, intro))
    with open(os.path.join(OUT, "index.md"), "w", encoding="utf-8") as h:
        h.write("# Synths\n\n")
        h.write("Every synth model, how it works and what each setting does. The Settings part of\n"
                "each page is also what the help box in [Edit Patch](../guide/edit-patch.md) shows.\n\n")
        h.write("| Synth | |\n|---|---|\n")
        for _, title, name, intro in sorted(rows):
            h.write(f"| [{title}]({name}.md) | {intro} |\n")
    print(f"{len(rows)} synth pages in docs/synths/")


if __name__ == "__main__":
    main()
