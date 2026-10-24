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

  1. A production source in two or more test executable/library source lists.
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
VARUSE_RE = re.compile(r"\$\{(\w+)\}")


def commands(text: str):
    """Read CMake calls, independent of where their closing ')' is placed."""
    opener = re.compile(r"(?m)^\s*([A-Za-z_]\w*)\s*\(")
    # Remove comments while keeping quoted arguments (which can contain '#').
    cleaned = re.sub(
        r'"(?:\\.|[^"\\])*"|#\[(=*)\[.*?\]\1\]|#[^\n]*',
        lambda match: match.group() if match.group().startswith('"') else "\n" * match.group().count("\n"),
        text, flags=re.S)
    position = 0
    while match := opener.search(cleaned, position):
        start = position = match.end()
        depth = 1
        quoted = False
        while position < len(cleaned) and depth:
            char = cleaned[position]
            if char == "\\":
                position += 2
                continue
            if char == '"':
                quoted = not quoted
            elif not quoted:
                if char == "[" and (bracket := re.match(r"\[(=*)\[", cleaned[position:])):
                    close = "]" + bracket.group(1) + "]"
                    end = cleaned.find(close, position + bracket.end())
                    if end < 0:
                        raise ValueError("Unterminated CMake bracket argument")
                    position = end + len(close)
                    continue
                if char == "(":
                    depth += 1
                elif char == ")":
                    depth -= 1
            position += 1
        if depth:
            raise ValueError(f"Unterminated CMake command: {match.group(1)}")
        yield match.group(1).lower(), cleaned[start:position - 1]


def parse(text: str):
    """Return production sources and links for every literal test/library target.

    Source bundles expand recursively. target_sources() contributes to the same
    ownership list as the declaration. Functions are definitions, not targets;
    test declarations remain explicit so the completeness check can count them.
    """
    variables: dict[str, list[str]] = defaultdict(list)
    raw_targets: dict[str, list[str]] = {}
    raw_libs: dict[str, list[str]] = {}
    extra_sources: dict[str, list[str]] = defaultdict(list)
    links: dict[str, set[str]] = defaultdict(set)
    function_depth = 0

    for command, body in commands(text):
        if command in {"function", "macro"}:
            function_depth += 1
        elif command in {"endfunction", "endmacro"}:
            function_depth -= 1
        elif function_depth:
            continue
        elif command == "set":
            parts = body.split(maxsplit=1)
            if parts:
                variables[parts[0]].append(parts[1] if len(parts) > 1 else "")
        elif command in {"add_decenza_test", "add_library"}:
            parts = body.split(maxsplit=2)
            if not parts:
                raise ValueError(f"Missing target in {command}()")
            name = parts[0]
            if not re.fullmatch(r"\w+", name):
                raise ValueError(f"Use a literal target name in {command}(): {name}")
            if command == "add_decenza_test":
                raw_targets[name] = [body]
            elif len(parts) > 1 and parts[1] in {"STATIC", "OBJECT", "SHARED"}:
                raw_libs[name] = [body]
        elif command == "target_sources":
            parts = body.split(maxsplit=1)
            if len(parts) == 2:
                extra_sources[parts[0]].append(parts[1])
        elif command == "target_link_libraries":
            parts = body.split(maxsplit=1)
            if len(parts) == 2:
                links[parts[0]].update(re.findall(r"\bdecenza_\w+", parts[1]))

    def expand(body: list[str], seen: frozenset[str] = frozenset()) -> set[str]:
        joined = "\n".join(body)
        found = set(SOURCE_RE.findall(joined))
        for var in VARUSE_RE.findall(joined):
            if var in variables and var not in seen:
                found |= expand(variables[var], seen | {var})
        return found

    return (
        {t: expand(b + extra_sources[t]) for t, b in raw_targets.items()},
        {lib: expand(b + extra_sources[lib]) for lib, b in raw_libs.items()},
        links,
    )


def self_test() -> None:
    fixture = r"""
function(add_decenza_test name)
    add_library(${name} STATIC ignored.cpp)
endfunction()
set(INNER ${CMAKE_SOURCE_DIR}/src/core/a.cpp)
set(OUTER
    ${INNER})
add_library(decenza_base STATIC ${OUTER})
add_library(decenza_object OBJECT
    ${CMAKE_SOURCE_DIR}/src/core/b.cpp) # trailing comment with )
add_decenza_test(tst_first
    tst_first.cpp
    ${OUTER})
# add_decenza_test(tst_comment ignored.cpp)
#[[ add_decenza_test(tst_bracket_comment ignored.cpp) ]]
add_decenza_test(tst_second tst_second.cpp)
target_sources(tst_second PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/b.cpp)
target_link_libraries(tst_second PRIVATE
    decenza_object)
target_link_libraries(decenza_object PUBLIC decenza_base)
set(QUOTED "text with # and )")
"""
    targets, libraries, links = parse(fixture)
    assert targets == {"tst_first": {"src/core/a.cpp"}, "tst_second": {"src/core/b.cpp"}}, targets
    assert libraries == {"decenza_base": {"src/core/a.cpp"}, "decenza_object": {"src/core/b.cpp"}}, libraries
    assert links["tst_second"] == {"decenza_object"}, links
    assert "decenza_base" in transitive_libs("tst_second", links)
    try:
        parse("add_decenza_test(tst_${item} tst_${item}.cpp)")
    except ValueError:
        pass
    else:
        raise AssertionError("A computed target name must not silently hide tests")


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
    try:
        self_test()
        target_sources, lib_sources, links = parse(text)
    except (ValueError, AssertionError) as error:
        print(f"check_test_source_duplication: parser failed: {error}")
        return 1

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

    # Violation 1: one source compiled by several test executables/libraries.
    owners: dict[str, list[str]] = defaultdict(list)
    for target, sources in {**target_sources, **lib_sources}.items():
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
              f"compiled into more than one target across {n} tests and their libraries.")
        return 0

    if shared:
        total = sum(len(t) - 1 for t in shared.values())
        print(f"{len(shared)} production source(s) are compiled into more than "
              f"one test executable/library — {total} redundant compile(s), paid on every "
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
    if "--self-test" in sys.argv:
        self_test()
        print("check_test_source_duplication self-test: OK")
    else:
        sys.exit(main())
