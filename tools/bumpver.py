#!/usr/bin/env python3
"""Set the image version in app/prj.conf, and FAIL if it did not change.

Why this exists rather than a one-line sed: the version was bumped by hand with

    t.replace('SIGN_VERSION="0.25.0"', 'SIGN_VERSION="0.26.0"'); print("0.26.0")

inside a PowerShell one-liner, where the quote escaping mangled the pattern.
str.replace does not fail when it matches nothing, and the print ran anyway, so
it reported a bump it had not made. Every later bump then looked for a version
that had never been written and did nothing in turn - six images in a row
flashed carrying the same version while each one claimed the next.

So this reads the value back out of the file afterwards and exits non-zero if
it is not what was asked for. A bump that cannot prove it happened is a bump
that did not happen.

TWO NUMBERS, BECAUSE THEY DO TWO DIFFERENT JOBS (changed 2026-11-19).

Until 0.123.0 the minor field WAS the build counter: one flash, one bump, 123
of them. That made the version a usable citation key, and docs/MEASURED.md
spends it freely - every capture in that file is stamped with the image it was
taken on. Which is exactly why the release could not simply be renumbered to
0.8.0: the counter would climb back through 0.101.0 and the rest, and two
different images would answer to a stamp that already means one of them, with
nothing anywhere to notice.

imgtool parses `maj.min.rev+build` (mcuboot/scripts/imgtool/version.py) and
MCUboot carries the build number as its own uint32 in the image header, so both
jobs fit in one string:

    maj.min.rev   the RELEASE. Changes when a human decides it does.
    +build        the FLASH COUNTER. Changes every single time, never repeats.

app/src/shell_mcuboot.c already prints all four fields out of the header, so
the build number is visible on the board and not only in this file.

    python tools/bumpver.py --next      next flash: build + 1, release kept
    python tools/bumpver.py 0.9.0       new release, build still + 1
    python tools/bumpver.py 0.9.0+200   set both outright
    python tools/bumpver.py --show      print the current value

FIELD WIDTHS ARE THE HEADER'S, NOT THIS SCRIPT'S. image.h:149 is uint8 major,
uint8 minor, uint16 revision, uint32 build. Checked here so an over-wide field
is refused by name, rather than by a struct.pack traceback inside imgtool
halfway through a build. Leading zeros are refused for the same reason:
imgtool's own grammar is `([1-9]\\d*|0)`, so `0.08.0` is not a version at all.
"""
import io
import os
import re
import sys

KEY = "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION"
CONF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "prj.conf")

# The build part is OPTIONAL so this still reads a file written under the old
# scheme. That matters once: the 0.123.0 -> 0.8.0+124 changeover is a `set`,
# and a parser that insisted on a build field could not have read the line it
# was being asked to replace.
PAT = re.compile(
    r'^%s="([1-9]\d*|0)\.([1-9]\d*|0)\.([1-9]\d*|0)(?:\+([1-9]\d*|0))?"$'
    % re.escape(KEY), re.M)

# Same grammar as imgtool's, applied to what the caller types.
ARG = re.compile(r"^([1-9]\d*|0)\.([1-9]\d*|0)\.([1-9]\d*|0)(?:\+([1-9]\d*|0))?$")

# image.h:149 - STRUCT_PACKED image_version.
LIMITS = (("major", 0xFF), ("minor", 0xFF), ("revision", 0xFFFF),
          ("build", 0xFFFFFFFF))


def read():
    with io.open(CONF, encoding="utf-8", newline="") as f:
        text = f.read()
    m = PAT.search(text)
    if m is None:
        sys.exit("%s: no well-formed %s line found" % (CONF, KEY))
    return text, m


def parts(m):
    """(major, minor, revision, build) as ints. An absent build reads as 0,
    which is also what MCUboot stores for a version written without one."""
    return tuple(int(g) if g is not None else 0 for g in m.groups())


def fmt(p):
    """Always emit the build field. Once the counter exists it is the half of
    the string that distinguishes two flashes of the same release, so dropping
    it when it happens to be 0 would make exactly one image unidentifiable."""
    return "%d.%d.%d+%d" % p


def check(p):
    for value, (name, cap) in zip(p, LIMITS):
        if value > cap:
            sys.exit("%s is %d, and the image header holds at most %d (image.h:149)"
                     % (name, value, cap))


def main():
    args = sys.argv[1:]
    text, m = read()
    cur = parts(m)

    if not args or args[0] == "--show":
        print(fmt(cur))
        return 0

    if args[0] == "--next":
        # The release is deliberately untouched: a flash is not a release.
        want = (cur[0], cur[1], cur[2], cur[3] + 1)
    else:
        a = ARG.match(args[0])
        if a is None:
            sys.exit("version must be maj.min.rev or maj.min.rev+build with no "
                     "leading zeros, got %r" % args[0])
        g = tuple(int(x) if x is not None else None for x in a.groups())
        # A release set without a build still advances the counter. The point of
        # the counter is that no two flashes share a number, and cutting a
        # release is a flash like any other.
        want = (g[0], g[1], g[2], g[3] if g[3] is not None else cur[3] + 1)

    check(want)

    if want == cur:
        sys.exit("already at %s - pick a different version" % fmt(cur))

    new = text[:m.start()] + '%s="%s"' % (KEY, fmt(want)) + text[m.end():]
    with io.open(CONF, "w", encoding="utf-8", newline="") as f:
        f.write(new)

    # Read it back. The whole point of the file.
    _, m2 = read()
    got = parts(m2)
    if got != want:
        sys.exit("FAILED: asked for %s, file now says %s" % (fmt(want), fmt(got)))

    print("%s -> %s" % (fmt(cur), fmt(want)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
