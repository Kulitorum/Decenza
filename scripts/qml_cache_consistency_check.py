#!/usr/bin/env python3
"""Fail if any compiled QML cache unit differs from a fresh compile of its source.

Every QML module wires every QML source into every unit; this is the backstop for
a unit compiled against an older view of another file anyway (a module added
without the wiring, or a Qt change the wiring does not see). It regenerates every qmlcachegen output listed in each given
build directory's build.ninja into a scratch directory, using the real tool,
and compares bytes.

usage: qml_cache_consistency_check.py --qmlcachegen <tool> <build-dir> [<build-dir> ...]
"""
from __future__ import annotations

import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

# Each build directory has its own generated launcher (cmake/QmlCacheGeneration.cmake).
LAUNCHER_RE = re.compile(r"\S*qmlcachegen_stable/qmlcachegen_stable(?:\.cmd)?")
OUTPUT_RE = re.compile(r"^build (\S*\.rcc/qmlcache/\S+?(?:_qml|_js|_mjs)\.cpp)[\s:]")
# The unit's object file names the module target that compiles it.
OBJECT_RE = re.compile(r"^build \S*CMakeFiles/([^/\s]+)\.dir/\S*\.rcc/qmlcache/(\S+?\.cpp)\.o(?:bj)?[\s:]")


def units(build_dir: Path, tool: str):
    lines = (build_dir / "build.ninja").read_text(encoding="utf-8").split("\n")
    owner = {m.group(2): m.group(1) for m in map(OBJECT_RE.match, lines) if m}
    for i, line in enumerate(lines):
        match = OUTPUT_RE.match(line)
        if not match:
            continue
        for following in lines[i + 1:i + 8]:
            if following.startswith("  COMMAND = "):
                command = LAUNCHER_RE.sub(tool, following[len("  COMMAND = "):])
                if tool in command:
                    name = Path(match.group(1)).name
                    yield owner.get(name, "?"), match.group(1), command
                break


def regenerate(build_dir: Path, scratch: Path, index: int, module: str, output: str,
               command: str):
    real_output = re.search(r"-o (\S+\.cpp)", command).group(1)
    fresh = scratch / f"{index}_{Path(real_output).name}"
    result = subprocess.run(command.replace(f"-o {real_output}", f"-o {fresh}"),
                            shell=True, capture_output=True, text=True)
    name = Path(output).name
    if result.returncode != 0:
        return module, name, "error", (result.stderr or result.stdout).strip()[:400]
    on_disk = Path(real_output) if os.path.isabs(real_output) else build_dir / output
    try:
        same = on_disk.read_bytes() == fresh.read_bytes()
    except OSError as error:
        return module, name, "error", str(error)
    return module, name, "same" if same else "stale", ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmlcachegen", required=True)
    parser.add_argument("build_dirs", nargs="+", type=Path)
    args = parser.parse_args()

    totals = defaultdict(lambda: defaultdict(int))
    problems = []
    dirty_dirs = []
    with tempfile.TemporaryDirectory(prefix="qml-cache-check-") as scratch_dir:
        scratch = Path(scratch_dir)
        for build_dir in args.build_dirs:
            jobs = list(units(build_dir, args.qmlcachegen))
            if not jobs:
                problems.append(f"{build_dir}: no qmlcachegen outputs found in build.ninja")
                continue
            with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
                results = pool.map(lambda job: regenerate(build_dir, scratch, *job),
                                   ((i, *job) for i, job in enumerate(jobs)))
                for module, name, state, detail in results:
                    totals[module][state] += 1
                    if state != "same":
                        problems.append(f"{state.upper()}: {module}: {name}"
                                        + (f"\n    {detail}" if detail else ""))
                        if build_dir not in dirty_dirs:
                            dirty_dirs.append(build_dir)

    for module in sorted(totals):
        counts = totals[module]
        print(f"{module}: {sum(counts.values())} units compared, "
              f"{counts['stale']} stale, {counts['error']} failed to regenerate")
    for problem in problems:
        print(problem)
    if problems:
        # Touching the .qml is not enough for a Wasm unit: the native build
        # re-enters the Wasm child only when the package id changes.
        print("QML cache consistency check FAILED. A stale unit runs interpreted at run time."
              " Delete the listed units, then run:")
        for build_dir in dirty_dirs:
            print(f"    cmake --build {build_dir}")
        return 1
    print("QML cache consistency check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
