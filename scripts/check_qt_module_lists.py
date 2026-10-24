#!/usr/bin/env python3
"""Fail unless every install-qt-action step takes `modules:` from .github/qt-modules.txt.

Each install's `modules:` must be ${{ steps.<id>.outputs.list }} of an earlier, unconditional step
in the same job whose run block is the two-line selector below, and that selector's profiles must
resolve through scripts/qt_modules.sh. Parsed by indentation, so the raw count of install-qt-action
lines in each file must equal the parsed count; a step the parser cannot see is a failure, not a
smaller OK. Composite actions may not install Qt.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSTALL = re.compile(r"^\s*(-\s+)?uses:\s*['\"]?jurplel/install-qt-action@")
KEY = re.compile(r"^(\s*)(['\"]?)([A-Za-z0-9_-]+)\2:\s*(#.*)?$")
MODULES_REF = re.compile(r"^\s*modules:\s*\$\{\{\s*steps\.([A-Za-z0-9_-]+)\.outputs\.list\s*\}\}\s*(#.*)?$")
SELECT = re.compile(r'^\s*list="\$\(bash scripts/qt_modules\.sh ([a-z -]+)\)"\s*$')
EMIT = re.compile(r'^\s*echo "list=\$list" >> "\$GITHUB_OUTPUT"\s*$')


def indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def code(lines: list[str]) -> list[str]:
    return [l for l in lines if l.strip() and not l.lstrip().startswith("#")]


def jobs(lines: list[str]):
    """Yields (job name, [(first line number, step lines)]) for each job under top-level `jobs:`."""
    try:
        start = next(i for i, l in enumerate(lines) if re.match(r"^jobs:\s*(#.*)?$", l))
    except StopIteration:
        return
    body = [(n, l) for n, l in enumerate(lines[start + 1:], start + 2) if code([l])]
    if not body:
        return
    job_indent = indent(body[0][1])
    headers = [(i, KEY.match(l).group(3)) for i, (n, l) in enumerate(body)
               if indent(l) == job_indent and KEY.match(l)]
    for h, (i, name) in enumerate(headers):
        end = headers[h + 1][0] if h + 1 < len(headers) else len(body)
        job = body[i + 1:end]
        steps_at = next((k for k, (_, l) in enumerate(job) if re.match(r"^\s*steps:\s*(#.*)?$", l)), None)
        if steps_at is None:
            yield name, []
            continue
        key_indent = indent(job[steps_at][1])
        items = []
        item_indent = None
        for n, l in job[steps_at + 1:]:
            if indent(l) <= key_indent and not l.lstrip().startswith("- "):
                break
            if l.lstrip().startswith("- ") and (item_indent is None or indent(l) == item_indent):
                item_indent = indent(l)
                items.append((n, [l]))
            elif items:
                items[-1][1].append(l)
        yield name, items


def check_workflow(wf: Path, problems: list[str]) -> int:
    rel = wf.relative_to(ROOT)
    lines = wf.read_text(encoding="utf-8").split("\n")
    raw = sum(1 for l in code(lines) if INSTALL.match(l))
    parsed = 0
    for job, steps in jobs(lines):
        ids = {}
        for idx, (n, body) in enumerate(steps):
            m = next((re.match(r"^\s*(-\s+)?id:\s*([A-Za-z0-9_-]+)\s*$", l) for l in body
                      if re.match(r"^\s*(-\s+)?id:", l)), None)
            if m:
                ids[m.group(2)] = (idx, n, body)
        for idx, (n, body) in enumerate(steps):
            if not any(INSTALL.match(l) for l in code(body)):
                continue
            parsed += 1
            where = f"{rel}:{n} ({job})"
            mods = [l for l in code(body) if re.match(r"^\s*modules:", l)]
            ref = MODULES_REF.match(mods[0]) if len(mods) == 1 else None
            if not ref:
                problems.append(f"{where}: modules must be ${{{{ steps.<id>.outputs.list }}}}, "
                                f"found {mods[0].strip() if mods else 'none'}")
                continue
            sel = ids.get(ref.group(1))
            if not sel:
                problems.append(f"{where}: steps.{ref.group(1)} is not a step in this job")
                continue
            sel_idx, sel_n, sel_body = sel
            sel_code = code(sel_body)
            select = [SELECT.match(l) for l in sel_code if SELECT.match(l)]
            if sel_idx > idx:
                problems.append(f"{where}: selector {ref.group(1)} (line {sel_n}) runs after the install")
            elif any(re.match(r"^\s*(-\s+)?if:", l) for l in sel_code):
                problems.append(f"{where}: selector {ref.group(1)} (line {sel_n}) is conditional")
            elif len(select) != 1 or not any(EMIT.match(l) for l in sel_code):
                problems.append(f"{where}: selector {ref.group(1)} (line {sel_n}) must run "
                                '`list="$(bash scripts/qt_modules.sh <profiles>)"` and '
                                '`echo "list=$list" >> "$GITHUB_OUTPUT"`')
            else:
                r = subprocess.run(["bash", str(ROOT / "scripts" / "qt_modules.sh"),
                                    *select[0].group(1).split()],
                                   capture_output=True, text=True)
                if r.returncode != 0 or not r.stdout.strip():
                    problems.append(f"{where}: qt_modules.sh {select[0].group(1)} failed: "
                                    f"{r.stderr.strip() or 'empty list'}")
    if parsed != raw:
        problems.append(f"{rel}: {raw} install-qt-action line(s) but {parsed} parsed as steps; "
                        "the indentation parser missed one")
    return parsed


def main() -> int:
    problems: list[str] = []
    gh = ROOT / ".github"
    installs = sum(check_workflow(wf, problems)
                   for wf in sorted([*gh.glob("workflows/*.yml"), *gh.glob("workflows/*.yaml")]))
    for action in sorted([*gh.glob("actions/**/action.yml"), *gh.glob("actions/**/action.yaml")]):
        if any(INSTALL.match(l) for l in code(action.read_text(encoding="utf-8").split("\n"))):
            problems.append(f"{action.relative_to(ROOT)}: install Qt in a workflow job, not a composite action")
    if installs == 0:
        problems.append("no install-qt-action steps found; the parser is blind")
    if problems:
        print("check_qt_module_lists: FAIL — CI Qt modules come from .github/qt-modules.txt only")
        for p in problems:
            print("  " + p)
        return 1
    print(f"check_qt_module_lists: OK — {installs} Qt installs, all reading .github/qt-modules.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
