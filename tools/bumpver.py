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

    python tools/bumpver.py 0.32.0     set it
    python tools/bumpver.py --next     increment the minor
    python tools/bumpver.py --show     print the current value
"""
import io
import os
import re
import sys

KEY = "CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION"
CONF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "prj.conf")
PAT = re.compile(r'^%s="(\d+)\.(\d+)\.(\d+)"$' % re.escape(KEY), re.M)


def read():
    with io.open(CONF, encoding="utf-8", newline="") as f:
        text = f.read()
    m = PAT.search(text)
    if m is None:
        sys.exit("%s: no %s line found" % (CONF, KEY))
    return text, m


def main():
    args = sys.argv[1:]
    text, m = read()
    cur = "%s.%s.%s" % m.groups()

    if not args or args[0] == "--show":
        print(cur)
        return 0

    if args[0] == "--next":
        want = "%s.%d.0" % (m.group(1), int(m.group(2)) + 1)
    else:
        want = args[0]
        if not re.fullmatch(r"\d+\.\d+\.\d+", want):
            sys.exit("version must be X.Y.Z, got %r" % want)

    if want == cur:
        sys.exit("already at %s - pick a different version" % cur)

    new = text[:m.start()] + '%s="%s"' % (KEY, want) + text[m.end():]
    with io.open(CONF, "w", encoding="utf-8", newline="") as f:
        f.write(new)

    # Read it back. The whole point of the file.
    _, m2 = read()
    got = "%s.%s.%s" % m2.groups()
    if got != want:
        sys.exit("FAILED: asked for %s, file now says %s" % (want, got))

    print("%s -> %s" % (cur, want))
    return 0


if __name__ == "__main__":
    sys.exit(main())
