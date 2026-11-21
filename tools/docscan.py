#!/usr/bin/env python3
"""
Check the documents against the tree they describe.

WHY THIS EXISTS. On 2026-11-20 a sweep found the README claiming temperature,
the encoders, RS485 and CAN were "not built yet" while all four were built and
on the panel; a boot indicator described two releases after it was removed; a
console command named `d1 walk` that had been `dowalk` since the file it lived
in was renamed; and a terminal map whose header said nothing in it was
measured while one row was. Every one of those was written true and went stale
underneath.

None was caught by anything, because this repository gates everything EXCEPT
PROSE. Generated files have --check, spelling has a word list, secrets have two
hooks, commit messages have a hook. A sentence claiming a subsystem does not
exist had nothing in front of it.

WHAT THIS CAN AND CANNOT DO, said plainly so nobody reads a pass as more than
it is. It checks two mechanical things:

  1. every relative link in a document resolves, heading anchors included;
  2. every repository path a document names in backticks exists.

TWO OTHER CHECKS WERE TRIED AND WITHDRAWN. Both absences are decisions, and
the measurements behind them are here so nobody spends the afternoon again.

**A command check was written and removed.** It read every verb this firmware
registers out of `SHELL_CMD_REGISTER` and refused any command-shaped backtick
phrase that named something else, which does catch `d1 walk` - the renamed
command that started all this. Run against these documents it produced **45
findings, of which 2 were real and 1 was the command**: the rest were prose
(`no supply`, `per rev`), C fragments (`struct net_buf`, `const char *`),
devicetree (`startup-delay-us = <50000>`) and captured output (`rc -19`,
`image state-read`) - all legitimately in backticks, none of them commands.
Nothing short of understanding the sentence separates those, and an allowlist
would have started at forty entries and grown with every document.

A gate that fires on routine work is one people learn to `--no-verify` past,
and this repository has already watched that happen once - see the narrowing
of the MAC rule after it blocked a git-describe SHA. One true positive is not
worth forty false ones, and the true positive it found is a command name
inside a DATED CAPTURE, which this project's conventions say to leave alone
anyway.

**A count check was never attempted.** "NOTHING HERE IS measured YET" was a
sentence about a CSV column that had stopped being true. Only a human knows
that is a claim about data rather than a description, so there is nothing to
check mechanically.

What NONE of this can do is notice that "not built yet" became false - that is
a claim about the world. The only defense is the habit named in docs/todo.md:
when a subsystem lands, grep the documents for its name before committing.

Usage:
    python tools/docscan.py              # check the tree, exit 1 on any finding
    python tools/docscan.py --selftest   # prove each check fires, and passes
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# The prefixes a backticked path is allowed to name. A path starting anywhere
# else belongs to Zephyr, MCUboot or another tree entirely -
# `subsys/net/lib/http/...` and `boot/bootutil/...` are quoted all over this
# repository and are NOT ours to resolve.
#
# `boards/shields/` AND NOT `boards/`, which the first run of this check got
# wrong: Zephyr has a boards/ directory too, and `boards/common/usb/
# Kconfig.cdc_acm_serial.defconfig` in docs/OTA.md is ITS file, correctly
# cited. One character of over-reach produced a false positive in a document
# that was right.
OWN_PREFIXES = ("app/", "docs/", "tools/", "web/", "mcuboot/", "boards/shields/")

LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
TICK_RE = re.compile(r"`([^`\n]{1,120})`")


def own_path(token: str) -> str | None:
    """The repository-relative path a backticked token names, or None.

    Strips a trailing `:123` or `:12-30` line reference, which is how this
    project cites a line.

    Refuses anything holding `*` or `{`, because both name SEVERAL files
    rather than one: `tools/*.ps1` is a glob, and `app/src/whoami_core.{c,h}`
    is the shell's brace form, used in the README to name a header and its
    implementation together. Expanding either here would mean guessing which
    of the expansions the sentence meant.
    """
    t = re.sub(r":\d+(-\d+)?$", "", token).replace("\\", "/")
    if "*" in t or "{" in t or not t.startswith(OWN_PREFIXES):
        return None
    return t


def slugs(md: Path) -> set[str]:
    """GitHub's heading anchors for one markdown file.

    Lowercase, drop everything that is not alphanumeric / space / hyphen /
    underscore, then spaces to hyphens. A repeated heading gets -1, -2, which
    is why the count is tracked rather than the set alone.
    """
    out: set[str] = set()
    seen: dict[str, int] = {}
    for line in md.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("#"):
            continue
        title = line.lstrip("#").strip()
        if not title:
            continue
        s = title.lower()
        s = re.sub(r"[^\w\s-]", "", s)
        s = re.sub(r"\s", "-", s)
        n = seen.get(s, 0)
        seen[s] = n + 1
        out.add(s if n == 0 else f"{s}-{n}")
    return out


def scan(root: Path, docs: list[Path]) -> list[str]:
    bad: list[str] = []
    anchors: dict[Path, set[str]] = {}

    for doc in docs:
        rel = doc.relative_to(root).as_posix()
        in_fence = False
        for n, line in enumerate(doc.read_text(encoding="utf-8",
                                               errors="replace").splitlines(), 1):
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
                continue

            # A FENCED BLOCK IS SAMPLE TEXT, not a reference: captured output,
            # another machine's commands, a snippet of someone else's tree.
            # Nothing inside one is checked - links included, which the
            # selftest caught this file getting wrong.
            if in_fence:
                continue

            # --- 1. links resolve, anchors included --------------------------
            for target in LINK_RE.findall(line):
                if target.startswith(("http://", "https://", "mailto:", "#")):
                    continue
                path, _, frag = target.partition("#")
                if not path:
                    continue
                dest = (doc.parent / path).resolve()
                if not dest.exists():
                    bad.append(f"{rel}:{n}: link target does not exist: {target}")
                    continue
                if frag and dest.suffix == ".md":
                    if dest not in anchors:
                        anchors[dest] = slugs(dest)
                    if frag.lower() not in anchors[dest]:
                        bad.append(f"{rel}:{n}: no such heading in "
                                   f"{path}: #{frag}")

            # --- 2. repository paths exist -----------------------------------
            for tick in TICK_RE.findall(line):
                words = tick.strip().rstrip(".,;:").split()
                if not words:
                    continue
                # The FIRST word only: `pwsh tools/build.ps1` names a path in
                # its second, and that form is checked too.
                for w in words:
                    p = own_path(w.rstrip(".,;:"))
                    if p is None:
                        continue
                    if not (root / p).exists():
                        bad.append(f"{rel}:{n}: path does not exist: {p}")
    return bad


def docs_of(root: Path) -> list[Path]:
    out = [root / "README.md"]
    out += sorted((root / "docs").glob("*.md"))
    return [p for p in out if p.is_file()]


def selftest() -> int:
    """Prove every check fires, and that clean input passes.

    Half of these cases exist only to show the check still refuses something:
    a gate nobody has watched refuse is a gate nobody has tested.
    """
    import tempfile

    # THE TWO KINDS OF PATH HERE ARE RESOLVED DIFFERENTLY, and conflating them
    # made two of these cases vacuous on the first run: a LINK resolves against
    # the document's own directory, a backticked PATH against the repository
    # root. The test file is written into docs/, so a link reads `MEASURED.md`
    # while a path reads `docs/MEASURED.md`. The selftest found that itself,
    # which is the whole argument for having one.
    cases = [
        # (markdown, expect_a_finding, what it is for)
        ("see [the map](MEASURED.md)", False, "a link that resolves"),
        ("see [gone](NOPE.md)", True, "a link that does not"),
        ("see [tbd](MEASURED.md#tbd---measurements-this-board-is-wired-"
         "for-and-has-not-taken)", False, "an anchor that exists"),
        ("see [tbd](MEASURED.md#no-such-heading)", True, "an anchor that does not"),
        ("`tools/docscan.py` is this file", False, "a path that exists"),
        ("`tools/nothing_here.py` is not", True, "a path that does not"),
        ("`app/src/canopen.c` holds it", False, "a source path that exists"),
        ("`tools/*.ps1` are the scripts", False, "a wildcard is not a path"),
        ("`app/src/whoami_core.{c,h}` are vendored", False,
         "a brace form names two files, not one"),
        ("`subsys/net/lib/http/server.h` is Zephyr's", False,
         "another tree's path is not ours to resolve"),
        ("`app/src/canopen.c:120` names a line", False, "a path with a line number"),
        ("`app/src/gone.c:120` does not", True, "a missing path with a line number"),
        ("```\n[gone](NOPE.md)\n`app/src/gone.c`\n```", False,
         "a fenced block is sample text, links and paths both"),
        ("run `pwsh tools/build.ps1` to build", False, "a path in a second word"),
        ("run `pwsh tools/gone.ps1` to build", True, "a missing path in a second word"),
        ("`boards/common/usb/Kconfig.cdc_acm_serial.defconfig` is Zephyr's",
         False, "boards/ is Zephyr's too; only boards/shields/ is ours"),
        # The command check that was withdrawn would have fired on both of
        # these. They are kept as cases so that reinstating it means seeing
        # them turn red first.
        ("run `d1 walk` to sweep", False, "WITHDRAWN: a stale command is not caught"),
        ("the string `no supply` is prose", False, "WITHDRAWN: and prose is not flagged"),
    ]

    root = REPO
    fails = 0
    with tempfile.TemporaryDirectory() as td:
        # The temporary file must sit in docs/ so relative links resolve the
        # way a real document's would.
        tmp = root / "docs" / f"_docscan_selftest_{os.getpid()}.md"
        try:
            for body, want, what in cases:
                tmp.write_text(body + "\n", encoding="utf-8", newline="\n")
                found = scan(root, [tmp])
                got = bool(found)
                ok = got == want
                fails += 0 if ok else 1
                print(f"  {'ok  ' if ok else 'FAIL'}  {what}"
                      f"{'' if ok else f'  -> {found}'}")
        finally:
            tmp.unlink(missing_ok=True)
        _ = td

    print(f"\n{len(cases) - fails} of {len(cases)} selftest cases as expected")
    return 1 if fails else 0


def main(argv: list[str]) -> int:
    if "--selftest" in argv:
        return selftest()

    if not (REPO / "app" / "src").is_dir():
        print(f"docscan: {REPO}/app/src not found - NOT a pass", file=sys.stderr)
        return 1

    docs = docs_of(REPO)
    if not docs:
        print("docscan: no documents found - NOT a pass", file=sys.stderr)
        return 1

    bad = scan(REPO, docs)
    for b in bad:
        print(f"docscan: {b}", file=sys.stderr)

    # The zeros are the output. A check that prints nothing when it passes is
    # indistinguishable from one that did not run.
    print(f"docscan: {len(docs)} document(s), {len(bad)} finding(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
