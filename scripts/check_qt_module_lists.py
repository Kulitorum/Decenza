#!/usr/bin/env python3
"""Every CI Qt install takes its add-on modules from .github/qt-modules.txt.

Each install-qt-action step's `modules:` must be the output of a step in the same job that runs
scripts/qt_modules.sh. The lists used to be typed into each step by hand, nine copies across two
workflows, and adding qttasktree meant finding all nine. A literal list anywhere is a copy that
can drift.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STEP = re.compile(r"^(\s*)- ")
JOB = re.compile(r"^  [A-Za-z0-9_-]+:\s*$")
MODULES_REF = re.compile(r"^\s*modules:\s*\$\{\{\s*steps\.([A-Za-z0-9_-]+)\.outputs\.list\s*\}\}\s*$")


def steps_by_job(lines: list[str]):
    """Yields (job_steps) where each step is (first_line_number, [lines])."""
    job: list[tuple[int, list[str]]] = []
    for n, line in enumerate(lines, 1):
        if JOB.match(line):
            if job:
                yield job
            job = []
        elif STEP.match(line) and line.startswith("      - "):
            job.append((n, [line]))
        elif job:
            job[-1][1].append(line)
    if job:
        yield job


def main() -> int:
    problems: list[str] = []
    installs = 0
    for wf in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
        rel = wf.relative_to(ROOT)
        for job in steps_by_job(wf.read_text(encoding="utf-8").split("\n")):
            selectors = {}
            for _, body in job:
                text = "\n".join(body)
                m = re.search(r"^\s*id:\s*([A-Za-z0-9_-]+)\s*$", text, re.M)
                if m and "scripts/qt_modules.sh" in text:
                    selectors[m.group(1)] = True
            for n, body in job:
                if not any("jurplel/install-qt-action" in l for l in body):
                    continue
                installs += 1
                mods = [l for l in body if re.match(r"^\s*modules:", l)]
                ref = MODULES_REF.match(mods[0]) if len(mods) == 1 else None
                if not ref:
                    problems.append(f"{rel}:{n}: modules must be ${{{{ steps.<id>.outputs.list }}}} "
                                    f"from scripts/qt_modules.sh, found {mods[0].strip() if mods else 'none'}")
                elif ref.group(1) not in selectors:
                    problems.append(f"{rel}:{n}: steps.{ref.group(1)} is not a step in this job "
                                    "that runs scripts/qt_modules.sh")
    if problems:
        print("check_qt_module_lists: FAIL — CI Qt modules come from .github/qt-modules.txt only")
        for p in problems:
            print("  " + p)
        return 1
    print(f"check_qt_module_lists: OK — {installs} Qt installs, all reading .github/qt-modules.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
