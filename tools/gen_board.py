#!/usr/bin/env python3
"""
Draw the board the way it LOOKS ON THE BENCH, from tools/terminals.csv.

WHY A DRAWING AND NOT THE PHOTOGRAPH
------------------------------------
Licensing: Arduino's product photograph is store imagery, and the official
AKX00032 pinout is CC BY-SA 4.0. ShareAlike is viral - embedding either would
pull this whole panel into BY-SA, which collides with its MIT license.
Terminal order, block composition and physical proportions are facts about
the hardware rather than anyone's artwork, so a drawing made from those facts
is our own and carries no such obligation.

Function: a photograph is a picture of a board. This is a picture of THIS
board - every terminal is an element the page colors from live state, and
every channel LED is one the firmware can light. That is the whole difference
between an explorer and a list of "ok".

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

THE CONNECTOR IS THREE BANDS, not a rectangle with a dot:
    top     green, with a dark crescent wire guide per terminal
    middle  a dark strip carrying a brass screw block per terminal
    bottom  green, with a dark square wire-entry hole per terminal

CHANNEL LEDS ARE REAL AND THEY ARE ON THE PCB, not inside the plastic:
a small LED sits between each channel's silkscreen label and the connector,
on DIGITAL INPUTS, DIGITAL OUTPUTS and PROGRAMMABLE DIGITAL I/O only.
Encoders have none, and neither do GND or the +24V supply terminals -
checked in the photograph rather than assumed, because drawing an indicator
that does not exist invites someone to wait for it to change.
"""
import csv
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "tools" / "terminals.csv"
OUT = REPO / "web" / "_board.svg"

W, H = 1300, 700
MARGIN_X = 45.0
SPAN = 1230.0           # measured: the connectors run nearly edge to edge
PITCH = 27.0
PAD = 6.0               # connector body overhang past its end terminals
CONN_H = 88.0
TOP_Y = 81.0            # measured, mapped from the reference
BOT_Y = 512.0
LED_R = 3.4

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


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def solve_gap(blks):
    """Both rows span SPAN exactly, so the gap absorbs the count difference."""
    nterm = sum(len(t) for _, t in blks)
    body = nterm * PITCH + len(blks) * 2 * PAD
    nglue = max(len(blks) - 1, 1)
    return (SPAN - body) / nglue


def draw_row(blks, y, labels_below):
    p, x, gap = [], MARGIN_X, solve_gap(blks)
    for name, terms in blks:
        n = len(terms)
        bw = n * PITCH + 2 * PAD
        # --- connector body, three bands -----------------------------------
        p.append('<rect class="conn" x="%.1f" y="%.1f" width="%.1f" '
                 'height="%.1f" rx="3"/>' % (x, y, bw, CONN_H))
        p.append('<rect class="connband" x="%.1f" y="%.1f" width="%.1f" '
                 'height="18"/>' % (x, y + 30, bw))
        for i, r in enumerate(terms):
            cx = x + PAD + i * PITCH + PITCH / 2
            chan, kind, conf = r["chan"], r["kind"], r["conf"]
            # wire guide (top band)
            p.append('<path class="guide" d="M %.1f %.1f a 7 6 0 0 0 13 0 z"/>'
                     % (cx - 6.5, y + 11))
            # wire entry hole (bottom band)
            p.append('<rect class="hole" x="%.1f" y="%.1f" width="13" '
                     'height="15" rx="2"/>' % (cx - 6.5, y + 60))
            cls = "term k-" + kind + ("" if chan else " nochan")
            if chan and conf != "measured":
                cls += " unverified"
            attrs = ' data-kind="%s" data-conf="%s"' % (kind, conf)
            if chan:
                attrs += (' data-ch="%s" tabindex="0" role="button"'
                          % esc(chan))
            p.append('<g class="%s"%s>' % (cls, attrs))
            # A <title> on the group IS the hover tip, and the page rewrites
            # it on every poll, so hovering a terminal shows that terminal's
            # live state rather than the name of the board.
            p.append('<title>%s</title>'
                     % esc(chan + " - " + name if chan
                           else r["label"] + " - " + name))
            p.append('<rect class="hit" x="%.1f" y="%.1f" width="%.1f" '
                     'height="%.1f"/>' % (cx - PITCH / 2, y, PITCH, CONN_H))
            p.append('<rect class="screwblk" x="%.1f" y="%.1f" width="15" '
                     'height="15" rx="2"/>' % (cx - 7.5, y + 31))
            p.append('<circle class="screw" cx="%.1f" cy="%.1f" r="4.6"/>'
                     % (cx, y + 38.5))
            p.append('</g>')
            # --- silkscreen label, rotated to read bottom-to-top as printed
            ly = (y + CONN_H + 7) if labels_below else (y - 20)
            anchor = "start" if labels_below else "end"
            p.append('<text class="silk" x="%.1f" y="%.1f" text-anchor="%s" '
                     'transform="rotate(-90 %.1f %.1f)">%s</text>'
                     % (cx, ly, anchor, cx, ly, esc(r["label"])))
            # --- the channel LED, on the PCB between label and connector
            if kind in LED_KINDS and chan:
                lyy = y - 9 if not labels_below else y + CONN_H + 9
                p.append('<circle class="chled" data-led="%s" cx="%.1f" '
                         'cy="%.1f" r="%.1f"/>' % (esc(chan), cx, lyy, LED_R))
        tyy = (y + CONN_H + 96) if labels_below else (y - 100)
        p.append('<text class="blocktitle" x="%.1f" y="%.1f">%s</text>'
                 % (x + 1, tyy, esc(name)))
        x += bw + gap
    return p


def center():
    """Connectors placed and sized from the reference, not sketched."""
    p = ['<g class="feat">']
    # USB-A host, J15 - white shell, black tongue
    p.append('<rect class="shell" x="222" y="272" width="52" height="132" '
             'rx="3"/>')
    p.append('<rect class="cavity" x="230" y="286" width="36" height="104" '
             'rx="2"/>')
    p.append('<rect class="tongue" x="236" y="300" width="24" height="76"/>')
    p.append('<text class="featlbl" x="248" y="424">USB A &middot; J15</text>')
    # RJ45, J13 - metal shield, trapezoid cavity, link LEDs
    p.append('<rect class="shield" x="352" y="270" width="136" height="136" '
             'rx="4"/>')
    p.append('<path class="cavity" d="M 368 290 h 86 v 58 h -20 v 30 '
             'h -46 v -30 h -20 z"/>')
    for i in range(8):
        p.append('<rect class="pin" x="%.1f" y="300" width="3" height="40"/>'
                 % (375 + i * 9))
    p.append('<rect class="ledg" x="462" y="282" width="16" height="20" '
             'rx="2"/>')
    p.append('<rect class="leda" x="462" y="374" width="16" height="20" '
             'rx="2"/>')
    p.append('<text class="featlbl" x="420" y="424">RJ45 &middot; J13</text>')
    # micro-USB B, J16 - the trapezoid is what makes it read as micro
    p.append('<path class="shell" d="M 604 306 h 30 l 6 8 v 44 l -6 8 h -30 '
             'l -6 -8 v -44 z"/>')
    p.append('<path class="cavity" d="M 612 316 h 20 l 3 5 v 28 l -3 5 h -20 '
             'l -3 -5 v -28 z"/>')
    p.append('<text class="featlbl" x="619" y="390">micro B &middot; J16'
             '</text>')
    # RST
    p.append('<rect class="btnbody" x="664" y="326" width="38" height="34" '
             'rx="3"/>')
    p.append('<circle class="btn" cx="683" cy="343" r="11"/>')
    p.append('<text class="featlbl" x="683" y="378">RST</text>')
    # I2C header
    p.append('<rect class="hdr" x="880" y="312" width="22" height="62" '
             'rx="2"/>')
    p.append('<text class="featlbl" x="891" y="390">I2C</text>')
    # WiFi / BLE SMA
    p.append('<circle class="sma" cx="1152" cy="343" r="21"/>')
    p.append('<circle class="smai" cx="1152" cy="343" r="8"/>')
    p.append('<text class="featlbl" x="1152" y="388">WiFi / BLE</text>')
    p.append('<text class="brand" x="800" y="330">PORTENTA MACHINE CONTROL'
             '</text>')
    p.append('<text class="brand2" x="800" y="352">ARDUINO PRO</text>')
    p.append('</g>')
    return p


def main():
    data = load()
    top, bot = blocks(data["top"]), blocks(data["bot"])
    P = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
         'class="board">' % (W, H),
         '<rect class="rail" x="0" y="4" width="%d" height="28" rx="8"/>' % W,
         '<rect class="rail" x="0" y="%d" width="%d" height="28" rx="8"/>'
         % (H - 32, W),
         '<rect class="pcb" x="10" y="30" width="%d" height="%d" rx="4"/>'
         % (W - 20, H - 62)]
    P += draw_row(top, TOP_Y, True)
    P += draw_row(bot, BOT_Y, False)
    P += center()

    # Left-edge indicator LEDs. Real, and the panel CANNOT drive them; shown
    # so the page matches the board, marked read-only so nobody waits on one.
    for i, nm in enumerate(["RS485 TX", "RS485 RX", "CAN TX", "CAN RX",
                            "3V3", "12V", "24V"]):
        ly = 232 + i * 20
        P.append('<circle class="led ro" cx="46" cy="%d" r="4.4"/>' % ly)
        P.append('<text class="ledlbl" x="58" y="%.1f">%s</text>'
                 % (ly + 3.4, esc(nm)))

    P.append('</svg>')
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(P) + "\n", encoding="utf-8", newline="\n")

    nled = sum(1 for r in data["top"] + data["bot"]
               if r["kind"] in LED_KINDS and r["chan"])
    nc = sum(1 for r in data["top"] + data["bot"] if r["chan"])
    print("wrote %s  %d bytes" % (OUT.relative_to(REPO), OUT.stat().st_size))
    print("  %d terminals, %d channels, %d channel LEDs"
          % (len(data["top"]) + len(data["bot"]), nc, nled))
    print("  top gap %.1f, bottom gap %.1f - both rows span %.0f px"
          % (solve_gap(top), solve_gap(bot), SPAN))
    return 0


if __name__ == "__main__":
    sys.exit(main())
