#!/usr/bin/env python3
"""Reject a production source compiled into more than one test target.

Each test is its own executable, so a production `.cpp` listed in two targets'
source lists is compiled twice — and nothing anywhere notices. Measured on a
clean rebuild in July 2026: 17 sources, 46 redundant object files, 215 s of cpu,
with two of the five slowest edges in the WHOLE build being the same
profilemanager.cpp object built for different targets.

The fix is a narrow intermediate library linked by exactly the targets that need
the source — NOT decenza_testlib, which every test target links, and which
would trade a bounded compile fan-out for a relink fan-out across all 106. See
"Shared sources go in a NARROW library" in docs/CLAUDE_MD/TESTING.md for the
measured table.

Two violations, both with a correct answer:

  1. A production source in two or more add_decenza_test() source lists.
  2. A production source in a test target's list that a library the target
     already links (decenza_testlib and friends) also compiles. This is pure
     waste — delete the line. tst_visualizershotparse carried exactly this for
     shotanalysis.cpp, undetected, until the audit that produced this script.

Why a text check and not a build-graph query: this runs in text-invariants.yml,
which is build-free by design (no Qt, no configure, ~1 min). A .ninja_log
analysis would need a configured build directory, and — learned the hard way —
is booby-trapped besides: `Rebuild` leaves orphaned object files on disk AND
stale entries in the log, so both "latest entry per output" and "file exists"
report duplicates that no longer build. Reading CMakeLists.txt has no such
failure mode: it describes what the build IS, not what it once did.

Exit code 1 on any finding, so this is a blocking check.
"""

from __future__ import annotations

import pathlib
import re
import sys
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent
CMAKE = ROOT / "tests" / "CMakeLists.txt"

# A source line inside a block: ${CMAKE_SOURCE_DIR}/src/foo/bar.cpp
SOURCE_RE = re.compile(r"\$\{CMAKE_SOURCE_DIR\}/(src/[\w/.-]+\.cpp)")
# Openers we care about, capturing the target/library name.
TEST_RE = re.compile(r"^\s*add_decenza_test\(\s*(\w+)")
# STATIC|OBJECT|SHARED, not just STATIC: decenza_testresources is an OBJECT
# library (a generated qrc must be, see tests/CMakeLists.txt), and a future
# OBJECT library carrying a src/*.cpp would otherwise be invisible here — which
# would silently disable the "already compiled by a library you link" check for
# it.
LIB_RE = re.compile(r"^\s*add_library\(\s*(\w+)\s+(?:STATIC|OBJECT|SHARED)\b")
# set(FOO_SOURCES ...) bundles, expanded into whichever block uses ${FOO_SOURCES}.
SETVAR_RE = re.compile(r"^\s*set\(\s*(\w+)\s*$")
VARUSE_RE = re.compile(r"\$\{(\w+)\}")
LINK_RE = re.compile(r"^\s*target_link_libraries\(\s*(\w+)\s+\w+\s+(.*)", re.S)


def parse(text: str):
    """Return (target_sources, lib_sources, target_links).

    Blocks are delimited by a line that is exactly ')' — the convention this
    file uses throughout. A block whose sources are a ${VAR} reference gets the
    variable's contents substituted, which is how PROFILEMANAGER_SOURCES used
    to hide nine duplicate compiles behind one token.
    """
    lines = text.split("\n")
    variables: dict[str, list[str]] = {}
    raw_targets: dict[str, list[str]] = {}
    raw_libs: dict[str, list[str]] = {}
    links: dict[str, set[str]] = defaultdict(set)

    i = 0
    while i < len(lines):
        line = lines[i]
        kind = name = None
        if m := TEST_RE.match(line):
            kind, name = "test", m.group(1)
        elif m := LIB_RE.match(line):
            kind, name = "lib", m.group(1)
        elif m := SETVAR_RE.match(line):
            kind, name = "var", m.group(1)
        elif m := LINK_RE.match(line):
            # target_link_libraries may wrap; consume to the closing paren.
            body, j = m.group(2), i
            while ")" not in body and j + 1 < len(lines):
                j += 1
                body += " " + lines[j]
            links[m.group(1)].update(re.findall(r"\bdecenza_\w+", body))
            i = j + 1
            continue

        if kind is None:
            i += 1
            continue

        bucket = {"test": raw_targets, "lib": raw_libs, "var": variables}[kind]

        # A single-line call — add_library(foo STATIC ${...}/src/x.cpp) — closes
        # on its own line. Scanning ahead for a standalone ')' would swallow
        # everything up to the NEXT block's close, silently dropping a real
        # target. That is not hypothetical: it dropped one add_decenza_test the
        # first time OBJECT libraries were recognised here, and showed up only
        # as the reported target count falling by one.
        if line.rstrip().endswith(")"):
            bucket[name] = [line]
            i += 1
            continue

        body, j = [], i + 1
        while j < len(lines) and lines[j].strip() != ")":
            body.append(lines[j])
            j += 1
        bucket[name] = body
        i = j + 1

    def expand(body: list[str]) -> set[str]:
        found = set(SOURCE_RE.findall("\n".join(body)))
        for var in VARUSE_RE.findall("\n".join(body)):
            if var in variables:
                found |= set(SOURCE_RE.findall("\n".join(variables[var])))
        return found

    return (
        {t: expand(b) for t, b in raw_targets.items()},
        {l: expand(b) for l, b in raw_libs.items()},
        links,
    )


def transitive_libs(target: str, links: dict[str, set[str]]) -> set[str]:
    """Libraries reachable from a target, including through library-to-library
    links. add_decenza_test() links decenza_testlib for EVERY target, which is
    not written per-target and so has to be seeded here."""
    seen, stack = set(), ["decenza_testlib", *links.get(target, ())]
    while stack:
        lib = stack.pop()
        if lib in seen:
            continue
        seen.add(lib)
        stack.extend(links.get(lib, ()))
    return seen


def main() -> int:
    text = CMAKE.read_text()
    target_sources, lib_sources, links = parse(text)

    # Refuse on any sign the parse was incomplete, rather than reporting a
    # clean-looking result over a subset. A block parser that loses a target
    # reports FEWER duplicates, so under-parsing fails OPEN — it looks like
    # success. This guard exists because that happened: recognising OBJECT
    # libraries made single-line add_library() calls swallow the block after
    # them, and the only visible symptom was the target count dropping by one.
    naive = len(re.findall(r"^add_decenza_test\(", text, re.M))
    if not target_sources:
        print(f"check_test_source_duplication: FAILED to parse any target from "
              f"{CMAKE}. Refusing to pass — a check that silently analyses "
              f"nothing is worse than no check.")
        return 1
    if len(target_sources) != naive:
        print(f"check_test_source_duplication: parsed {len(target_sources)} "
              f"targets but {CMAKE.name} declares {naive}. The parser lost "
              f"{naive - len(target_sources)} — refusing to report a result "
              f"over a subset. Fix the parser, not this message.")
        return 1

    # Violation 1: one source, several test targets.
    owners: dict[str, list[str]] = defaultdict(list)
    for target, sources in target_sources.items():
        for src in sources:
            owners[src].append(target)
    shared = {s: sorted(t) for s, t in owners.items() if len(t) > 1}

    # Violation 2: a source a linked library already compiles.
    redundant: list[tuple[str, str, str]] = []
    for target, sources in target_sources.items():
        reachable = transitive_libs(target, links)
        for src in sorted(sources):
            for lib in sorted(reachable):
                if src in lib_sources.get(lib, ()):
                    redundant.append((target, src, lib))
                    break

    if not shared and not redundant:
        n = len(target_sources)
        print(f"check_test_source_duplication: OK — no production source is "
              f"compiled into more than one of the {n} test targets.")
        return 0

    if shared:
        total = sum(len(t) - 1 for t in shared.values())
        print(f"{len(shared)} production source(s) are compiled into more than "
              f"one test target — {total} redundant compile(s), paid on every "
              f"build:\n")
        for src, targets in sorted(shared.items()):
            print(f"  {src}")
            print(f"      {len(targets)}x: {', '.join(targets)}")
        print("\nFix: compile it ONCE into a narrow static library linked by "
              "exactly those targets.\nSee 'Shared sources go in a NARROW "
              "library' in docs/CLAUDE_MD/TESTING.md — and note that\nadding it "
              "to decenza_testlib is NOT the fix: that library is linked by "
              "every test\ntarget, so it trades a bounded compile fan-out for a "
              "relink fan-out across all of them.")

    if redundant:
        if shared:
            print()
        print(f"{len(redundant)} source(s) are listed by a test target that "
              f"ALREADY links a library compiling them.\nThis is pure waste — "
              f"delete the line, nothing else is needed:\n")
        for target, src, lib in sorted(redundant):
            print(f"  {target}: remove {src}  (already in {lib})")

    return 1


if __name__ == "__main__":
    sys.exit(main())
