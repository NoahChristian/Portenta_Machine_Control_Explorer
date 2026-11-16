#!/usr/bin/env python3
"""
Draw the board the way it LOOKS ON THE BENCH, from tools/terminals.csv.

WHY A DRAWING AND NOT THE PHOTOGRAPH
------------------------------------
Two reasons, and the second is the one that matters.

Licensing: Arduino's product photograph is store imagery, and the official
pinout PDF is CC BY-SA 4.0. ShareAlike is viral - embedding that artwork
would pull this whole panel into BY-SA, which collides with its MIT license.
Terminal ORDER and block composition are facts about the hardware rather
than anyone's artwork, so a drawing made from those facts is our own and
carries no such obligation.

Function: a photograph is a picture of a board. This is a picture of THIS
board, right now - every terminal is an element the panel colors from live
state. That is the whole difference between an explorer and a table of "ok".

WHAT IT IS DRAWN TO LOOK LIKE
-----------------------------
The board as it sits on the bench, deliberately: dark PCB, green screw
terminals, silkscreen labels rotated to read bottom-to-top exactly as they
are printed. NOT the pinout diagram. Someone at the bench is looking at the
hardware, and a key that asks them to mentally re-map a schematic onto the
thing in front of them has handed the work back to the person it was meant
to help.
"""
import csv
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "tools" / "terminals.csv"
OUT = REPO / "web" / "_board.svg"

W, H = 1300, 700
PITCH, TW, TH = 21.0, 17.0, 60.0
GAP = 26.0
TOP_Y, BOT_Y = 92.0, 560.0
MARGIN = 34.0


def load():
    lines = []
    with SRC.open(newline="", encoding="utf-8") as f:
        for ln in f:
            if ln.startswith("#") or not ln.strip():
                continue
            lines.append(ln)
    out = {"top": [], "bot": []}
    for r in csv.DictReader(lines):
        out[r["row"]].append(r)
    return out


def blocks(items):
    """Preserve file order; group consecutive rows by block name."""
    seq, seen = [], {}
    for r in items:
        b = r["block"]
        if b not in seen:
            seen[b] = []
            seq.append(b)
        seen[b].append(r)
    return [(b, seen[b]) for b in seq]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def draw_row(blks, y_green, labels_below, x0):
    p, x = [], x0
    for name, terms in blks:
        bw = len(terms) * PITCH + 8
        p.append('<rect class="conn" x="%.1f" y="%.1f" width="%.1f" '
                 'height="%.1f" rx="3"/>' % (x, y_green, bw, TH))
        ty = y_green + 10
        for i, r in enumerate(terms):
            tx = x + 4 + i * PITCH + (PITCH - TW) / 2
            chan, kind, conf = r["chan"], r["kind"], r["conf"]
            cls = "term k-" + kind + ("" if chan else " nochan")
            if chan and conf != "measured":
                cls += " unverified"
            attrs = ' data-kind="%s" data-conf="%s"' % (kind, conf)
            if chan:
                attrs += (' data-ch="%s" tabindex="0" role="button"'
                          ' aria-label="%s, %s"'
                          % (esc(chan), esc(chan), esc(name)))
            p.append('<g class="%s"%s>' % (cls, attrs))
            p.append('<rect class="tbody" x="%.1f" y="%.1f" width="%.1f" '
                     'height="%.1f" rx="2"/>' % (tx, ty, TW, TH - 20))
            cxx, cyy = tx + TW / 2, ty + (TH - 20) / 2
            p.append('<circle class="screw" cx="%.1f" cy="%.1f" r="5.2"/>'
                     % (cxx, cyy))
            p.append('<line class="slot" x1="%.1f" y1="%.1f" x2="%.1f" '
                     'y2="%.1f"/>' % (cxx - 3.4, cyy, cxx + 3.4, cyy))
            p.append('</g>')
            # SILKSCREEN LABEL, rotated to read bottom-to-top as printed.
            lx = tx + TW / 2
            ly = (y_green + TH + 8) if labels_below else (y_green - 8)
            anchor = "start" if labels_below else "end"
            p.append('<text class="silk" x="%.1f" y="%.1f" text-anchor="%s" '
                     'transform="rotate(-90 %.1f %.1f)">%s</text>'
                     % (lx, ly, anchor, lx, ly, esc(r["label"])))
        tyy = (y_green + TH + 96) if labels_below else (y_green - 96)
        p.append('<text class="blocktitle" x="%.1f" y="%.1f">%s</text>'
                 % (x + 2, tyy, esc(name)))
        x += bw + GAP
    return p


def main():
    data = load()
    top, bot = blocks(data["top"]), blocks(data["bot"])
    P = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
         'class="board" role="img" '
         'aria-label="Portenta Machine Control, top view, live">' % (W, H),
         '<title>Portenta Machine Control - top view, live</title>',
         '<rect class="rail" x="0" y="6" width="%d" height="26" rx="8"/>' % W,
         '<rect class="rail" x="0" y="%d" width="%d" height="26" rx="8"/>'
         % (H - 32, W),
         '<rect class="pcb" x="8" y="30" width="%d" height="%d" rx="4"/>'
         % (W - 16, H - 62)]

    P += draw_row(top, TOP_Y, True, MARGIN)
    P += draw_row(bot, BOT_Y, False, MARGIN)

    cy = 310
    P.append('<g class="feat">')
    P.append('<rect class="usba" x="150" y="%d" width="74" height="52" '
             'rx="3"/>' % (cy - 26))
    P.append('<text class="featlbl" x="187" y="%d">USB A  J15</text>'
             % (cy + 44))
    P.append('<rect class="rj45" x="300" y="%d" width="92" height="74" '
             'rx="4"/>' % (cy - 34))
    P.append('<text class="featlbl" x="346" y="%d">RJ45  J13</text>'
             % (cy + 58))
    P.append('<rect class="usbm" x="470" y="%d" width="34" height="24" '
             'rx="3"/>' % (cy - 12))
    P.append('<text class="featlbl" x="487" y="%d">micro  J16</text>'
             % (cy + 32))
    P.append('<circle class="btn" cx="560" cy="%d" r="13"/>' % cy)
    P.append('<text class="featlbl" x="560" y="%d">RST</text>' % (cy + 32))
    P.append('<rect class="hdr" x="880" y="%d" width="24" height="56" '
             'rx="2"/>' % (cy - 22))
    P.append('<text class="featlbl" x="892" y="%d">I2C</text>' % (cy + 50))
    P.append('<circle class="sma" cx="1120" cy="%d" r="19"/>' % cy)
    P.append('<circle class="smai" cx="1120" cy="%d" r="7"/>' % cy)
    P.append('<text class="featlbl" x="1120" y="%d">WiFi / BLE</text>'
             % (cy + 42))
    P.append('<text class="brand" x="700" y="%d">PORTENTA MACHINE CONTROL'
             '</text>' % (cy - 6))
    P.append('<text class="brand2" x="700" y="%d">ARDUINO PRO</text>'
             % (cy + 18))
    P.append('</g>')

    # Left-edge indicator LEDs. They are real, and the panel CANNOT drive
    # them - they are shown so the page matches the board, and marked
    # read-only so nobody waits for one to respond to a click.
    for i, nm in enumerate(["RS485 TX", "RS485 RX", "CAN TX", "CAN RX",
                            "3V3", "12V", "24V"]):
        ly = 196 + i * 21
        P.append('<circle class="led ro" cx="40" cy="%d" r="4.6"/>' % ly)
        P.append('<text class="ledlbl" x="52" y="%.1f">%s</text>'
                 % (ly + 3.6, esc(nm)))

    P.append('</svg>')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(P) + "\n", encoding="utf-8", newline="\n")

    nt = len(data["top"]) + len(data["bot"])
    nc = sum(1 for r in data["top"] + data["bot"] if r["chan"])
    print("wrote %s  %d bytes" % (OUT.relative_to(REPO), OUT.stat().st_size))
    print("  %d terminals, %d named channels, %d top blocks, %d bottom"
          % (nt, nc, len(top), len(bot)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
