#!/usr/bin/env python3
"""Solve the board's geometry from tools/terminals.csv into a C table.

WHAT CHANGED, AND WHY IT IS BETTER
----------------------------------
This script used to emit 62 KB of finished SVG - every one of eighty terminals
written out in full, with its coordinates, its label, its hover tip and its
LED - which was then spliced into the page and embedded in a C byte array.
Two things were wrong with that.

It was markup assembled by a program, which is the rule this repository
carries from a sibling project on this bench: THE PAGE IS AUTHORED AS HTML. A generated
62 KB SVG cannot be opened in a browser and nudged; it can only be
regenerated, which means the drawing's appearance lived in Python string
concatenation rather than in a file a person can read.

And it could not carry live state at all. A terminal's hover tip has to say
what that terminal is reading, and with eighty of them written out flat there
is no way for a template engine to fill them in: that would be eighty registry
variables, and the registry's whole design is a flat table of names.

So the markup moved into web/index.html, where ONE terminal is written once
inside <!--#each term-->, and this script emits only what the markup cannot
know: where each terminal sits. The page went from 62 KB to a few, the drawing
is editable by hand, and every terminal's tip, class and LED now come from the
row provider in app/src/page_vars.cpp at render time.

PROPORTIONS ARE MEASURED, NOT ESTIMATED
---------------------------------------
Taken off the reference photograph by color extent rather than by eye:

    green terminal blocks   x 355 -> 1585 of a board spanning x 310 -> 1610
                            i.e. 1230 of 1300, ~95% - THE CONNECTORS RUN
                            ESSENTIALLY THE FULL WIDTH OF THE BOARD, which
                            an early version of this drawing got wrong by
                            inset-ing them into a polite margin.
    connector height        ~92 px against a ~29 px terminal pitch, so the
                            body is a little over 3 pitches tall.

Both rows therefore span the SAME width while holding different terminal
counts (38 top, 41 bottom), which is why the pitch is fixed and the
inter-block gap is solved per row. That is also how the real board does it.

CHANNEL LEDS ARE REAL AND THEY ARE ON THE PCB, not inside the plastic:
a small LED sits between each channel's silkscreen label and the connector,
on DIGITAL INPUTS, DIGITAL OUTPUTS and PROGRAMMABLE DIGITAL I/O only.
Encoders have none, and neither do GND or the +24V supply terminals -
checked in the photograph rather than assumed, because drawing an indicator
that does not exist invites someone to wait for it to change.

WHY A DRAWING AND NOT THE PHOTOGRAPH
------------------------------------
Licensing: Arduino's product photograph is store imagery, and the official
AKX00032 pinout is CC BY-SA 4.0. ShareAlike is viral - embedding either would
pull this whole panel into BY-SA, which collides with its MIT license.
Terminal order, block composition and physical proportions are facts about
the hardware rather than anyone's artwork, so a drawing made from those facts
is our own and carries no such obligation.

    python tools/gen_board.py            regenerate
    python tools/gen_board.py --check    fail if the table is stale
"""
import argparse
import csv
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "tools" / "terminals.csv"
OUT = REPO / "app" / "src" / "board_table.inc"

MARGIN_X = 45.0
SPAN = 1230.0           # measured: the connectors run nearly edge to edge
PITCH = 27.0
PAD = 6.0               # connector body overhang past its end terminals
CONN_H = 88.0
TOP_Y = 81.0            # measured, mapped from the reference
BOT_Y = 512.0

# Only these carry a per-channel LED on the PCB.
LED_KINDS = {"din", "do", "pdio"}


def load():
    lines = [ln for ln in SRC.open(encoding="utf-8")
             if not ln.startswith("#") and ln.strip()]
    out = {"top": [], "bot": []}
    for r in csv.DictReader(lines):
        out[r["row"]].append(r)
    return out


def blocks(items):
    seq, seen = [], {}
    for r in items:
        b = r["block"]
        if b not in seen:
            seen[b] = []
            seq.append(b)
        seen[b].append(r)
    return [(b, seen[b]) for b in seq]


def solve_gap(blks):
    """Both rows span SPAN exactly, so the gap absorbs the count difference."""
    nterm = sum(len(t) for _, t in blks)
    body = nterm * PITCH + len(blks) * 2 * PAD
    nglue = max(len(blks) - 1, 1)
    return (SPAN - body) / nglue


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def solve(data):
    """Returns (terminals, blocks) as plain dicts of solved geometry."""
    terms, blks = [], []

    for rowname, y, top in (("top", TOP_Y, True), ("bot", BOT_Y, False)):
        bs = blocks(data[rowname])
        gap = solve_gap(bs)
        x = MARGIN_X
        for name, items in bs:
            bw = len(items) * PITCH + 2 * PAD
            blks.append({"x": x, "y": y, "w": bw, "name": name, "top": top})
            for i, r in enumerate(items):
                terms.append({
                    "x": x + PAD + i * PITCH + PITCH / 2,
                    "y": y,
                    "top": top,
                    "label": r["label"],
                    "chan": r["chan"],
                    "kind": r["kind"],
                    "conf": r["conf"],
                    "block": name,
                    "led": r["kind"] in LED_KINDS and bool(r["chan"]),
                })
            x += bw + gap

    return terms, blks


def emit(terms, blks, gaps):
    L = []
    a = L.append
    a("/*")
    a(" * GENERATED by tools/gen_board.py from tools/terminals.csv - DO NOT EDIT.")
    a(" *")
    a(" * Regenerate with:  python tools/gen_board.py")
    a(" * tools/gen_board.py --check fails if this file is stale, because a")
    a(" * stale geometry table draws a board that is subtly not this board and")
    a(" * compiles perfectly while doing it.")
    a(" *")
    a(" * WHAT IS HERE AND WHAT IS NOT. These are COORDINATES AND NAMES only.")
    a(" * No markup: the SVG lives in web/index.html, where one terminal is")
    a(" * written once inside <!--#each term--> and the row provider in")
    a(" * page_vars.cpp walks this table. Nothing in this repository builds")
    a(" * markup in C.")
    a(" *")
    a(" * Both rows span %.0f px exactly while holding different terminal"
      % SPAN)
    a(" * counts, so the pitch is fixed at %.0f and the inter-block gap is"
      % PITCH)
    a(" * solved per row: top %.1f, bottom %.1f. See the script's header for"
      % (gaps[0], gaps[1]))
    a(" * where the proportions were measured.")
    a(" *")
    a(" * SPDX-License-Identifier: MIT")
    a(" */")
    a("")
    a("/* clang-format off */")
    a("")
    a("static const struct pmc_term pmc_terms[] = {")
    a("\t/*     x      y   top  label        chan        kind      conf      block                       led */")
    for t in terms:
        a("\t{ %7.1f, %5.1f, %-5s %-12s %-11s %-9s %-9s %-27s %s }," % (
            t["x"], t["y"],
            "true," if t["top"] else "false,",
            cstr(t["label"]) + ",",
            cstr(t["chan"]) + ",",
            cstr(t["kind"]) + ",",
            cstr(t["conf"]) + ",",
            cstr(t["block"]) + ",",
            "true" if t["led"] else "false"))
    a("};")
    a("")
    a("static const struct pmc_block pmc_blocks[] = {")
    a("\t/*     x      y      w  top    name */")
    for b in blks:
        a("\t{ %7.1f, %5.1f, %5.1f, %-6s %s }," % (
            b["x"], b["y"], b["w"],
            "true," if b["top"] else "false,",
            cstr(b["name"])))
    a("};")
    a("")
    a("/* clang-format on */")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the generated table is stale")
    args = ap.parse_args()

    data = load()
    terms, blks = solve(data)
    gaps = (solve_gap(blocks(data["top"])), solve_gap(blocks(data["bot"])))
    text = emit(terms, blks, gaps)

    current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
    if current == text:
        print("  ok      %s" % OUT.relative_to(REPO))
        return 0
    if args.check:
        print("  STALE   %s" % OUT.relative_to(REPO), file=sys.stderr)
        print("", file=sys.stderr)
        print("Run:  python tools/gen_board.py", file=sys.stderr)
        return 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(text, encoding="utf-8", newline="\n")
    nled = sum(1 for t in terms if t["led"])
    nch = sum(1 for t in terms if t["chan"])
    print("  wrote   %s" % OUT.relative_to(REPO))
    print("  %d terminals in %d blocks, %d channels, %d channel LEDs"
          % (len(terms), len(blks), nch, nled))
    print("  top gap %.1f, bottom gap %.1f - both rows span %.0f px"
          % (gaps[0], gaps[1], SPAN))
    return 0


if __name__ == "__main__":
    sys.exit(main())
