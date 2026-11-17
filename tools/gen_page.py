#!/usr/bin/env python3
"""Generate app/src/web_gen/page_index.h from web/index.html.

    python tools/gen_page.py            regenerate
    python tools/gen_page.py --check    fail if the header is stale

WHAT THIS IS NOW. A thin wrapper around tools/mkhtml.py in the
Arduino_HTTP_HTML repository, which is the generator for the HtmlTpl SSI
engine this panel renders with. The real work is there: it strips the
design-time preview bodies, VALIDATES every <!--#v name--> against the registry
in app/src/page_vars.cpp, hashes the result, and writes the page out as a raw
string literal the engine parses in place.

WHAT THIS USED TO BE, AND WHY IT WENT. A 130-line script that turned
web/index.html into a C array of bytes, which the firmware served verbatim
while JavaScript on the page polled /state.json and built every table in the
browser. It embedded markup in C and checked nothing: a misspelled field name
produced an empty cell, in one branch, with no error at build time and no error
at run time. The whole point of the generator in the library is the opposite -
a name that does not exist in the registry fails the build and names the line.

THE REGISTRY VALIDATION IS THE REASON THIS RUNS AT ALL. Without --registry the
tool happily emits a page full of tags that will fault on the device, and the
symptom is a page with holes in it rather than a build that stopped. So the
registry is not optional here and the path is not a parameter.

WHERE THE LIBRARY IS. Beside this repository by default, which is how the
sketchbook is laid out; override with HTMLTPL_DIR. app/CMakeLists.txt resolves
it the same way and fails the build with the same message, because a checkout
that can compile the engine but not regenerate its pages would be a build that
serves whatever page was committed last.
"""
import argparse
import os
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_LIB = REPO.parent / "Arduino_HTTP_HTML"

# The page carries the board drawing, so it is bigger than a page of tables and
# smaller than the 62 KB of flat SVG the previous design embedded. 32768 is the
# tool's default and this page is already past it.
#
# RAISED FROM 65536 ON 2026-11-16, DELIBERATELY. That figure was chosen to
# leave room for "the write controls M4 adds"; since then the page has gained
# the temperature panel, a wire-level serial terminal and a Modbus decoder,
# and it was within 7 bytes of the limit. The failure mode that mattered was
# not the size - it was that the response to hitting the limit became shaving
# words off comments, which trades documentation for a number nobody chose
# deliberately.
#
# 98304 is 50% more, still small enough that a careless addition is noticed,
# and roughly 6% of slot0 on a board at 65%.
BUDGET = 98304


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the generated header is stale")
    args = ap.parse_args()

    lib = pathlib.Path(os.environ.get("HTMLTPL_DIR", DEFAULT_LIB))
    mkhtml = lib / "tools" / "mkhtml.py"
    if not mkhtml.exists():
        print("gen_page: Arduino_HTTP_HTML not found at %s" % lib,
              file=sys.stderr)
        print("The panel is a TEMPLATE rendered on the board, not markup "
              "assembled in C, so the", file=sys.stderr)
        print("generator is not optional. Clone it beside this repository or "
              "set HTMLTPL_DIR.", file=sys.stderr)
        return 1

    cmd = [
        sys.executable, str(mkhtml),
        "--src", str(REPO / "web"),
        "--out", str(REPO / "app" / "src" / "web_gen"),
        "--registry", str(REPO / "app" / "src" / "page_vars.cpp"),
        "--budget", str(BUDGET),
        # app/src is C firmware, not a sketch directory: no api.d.ts, no
        # template-vars.json, no editor snippets.
        "--no-editor-files",
    ]
    if args.check:
        cmd.append("--check")

    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
