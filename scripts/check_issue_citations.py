#!/usr/bin/env python3
"""Catch #NNNN citations in source that point at a pull request, not an issue.

Comments here carry a lot of "why", and a wrong citation is worse than none: a
reader follows it, finds something unrelated, and either distrusts the comment or
believes the wrong story. In one PR a cache bug was cited as #1724 (a merged PR
about turn scoring), corrected to #1713 (a real issue -- but the equipment-fork
bug, a different mechanism with the same symptom), and only correct on the third
pass, when it turned out the bug had no issue number at all.

What this checks is the mechanical half, which is the half that recurs: a #NNNN in
a code comment that resolves to a PULL REQUEST. Citing "the PR that changed this"
reads to every later reader as "the bug this fixes", and the numbers are adjacent
enough to transpose. It cannot check that an issue's CONTENT matches the claim --
that stays a human job -- so it prints every issue title it resolves, making a
mismatch visible on the CI log without failing the build.

Usage:
    python3 scripts/check_issue_citations.py            # changed files vs origin/main
    python3 scripts/check_issue_citations.py --all      # whole tree

Requires `gh` authenticated. Skips (exit 0) when unavailable, so it never blocks a
build for an environment reason.
"""

import json
import re
import subprocess
import sys

REPO = "Kulitorum/Decenza"
SOURCE_SUFFIXES = (".cpp", ".h", ".qml", ".py", ".md")
# A citation, not a colour literal (#1a2b3c) and not a markdown heading.
CITATION = re.compile(r"(?<![\w#/])#(\d{3,5})\b")
# Paths that legitimately discuss PR numbers as PR numbers.
EXEMPT_PREFIXES = ("openspec/changes/archive/", "docs/plans/", "CHANGELOG")


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def gh_available() -> bool:
    return sh(["gh", "auth", "status"]).returncode == 0


def changed_files() -> list[str]:
    base = sh(["git", "merge-base", "HEAD", "origin/main"]).stdout.strip()
    if not base:
        return []
    out = sh(["git", "diff", "--name-only", base, "HEAD"]).stdout
    return [f for f in out.splitlines() if f.endswith(SOURCE_SUFFIXES)]


def all_files() -> list[str]:
    out = sh(["git", "ls-files"]).stdout
    return [f for f in out.splitlines() if f.endswith(SOURCE_SUFFIXES)]


def classify(num: str) -> tuple[str, str]:
    """Return (kind, title). kind is 'pr', 'issue', or 'unknown'."""
    r = sh(["gh", "api", f"repos/{REPO}/issues/{num}",
            "--jq", '{t: .title, pr: (.pull_request != null)}'])
    if r.returncode != 0:
        return "unknown", ""
    try:
        d = json.loads(r.stdout)
    except json.JSONDecodeError:
        return "unknown", ""
    return ("pr" if d.get("pr") else "issue"), d.get("t", "")


def main() -> int:
    if not gh_available():
        print("check_issue_citations: gh unavailable, skipping")
        return 0

    files = all_files() if "--all" in sys.argv else changed_files()
    files = [f for f in files if not f.startswith(EXEMPT_PREFIXES)]
    if not files:
        print("check_issue_citations: no source files to check")
        return 0

    # number -> list of "path:line"
    sites: dict[str, list[str]] = {}
    for path in files:
        try:
            with open(path, encoding="utf-8", errors="replace") as fh:
                for n, line in enumerate(fh, 1):
                    for num in CITATION.findall(line):
                        sites.setdefault(num, []).append(f"{path}:{n}")
        except OSError:
            continue

    if not sites:
        print("check_issue_citations: no citations found")
        return 0

    failures = []
    for num in sorted(sites, key=int):
        kind, title = classify(num)
        where = sites[num]
        if kind == "pr":
            failures.append((num, title, where))
        elif kind == "issue":
            print(f"  #{num}  {title}")
            for w in where:
                print(f"          {w}")
        else:
            print(f"  #{num}  (could not resolve — check by hand)")

    if failures:
        print("\nFAIL: these cite a PULL REQUEST as if it were an issue.\n"
              "A reader follows the number expecting the bug and finds the change.\n"
              "Use the issue number, or describe the bug without one if it was never filed.\n")
        for num, title, where in failures:
            print(f"  #{num} is a PR: {title}")
            for w in where:
                print(f"      {w}")
        return 1

    print("\ncheck_issue_citations: OK — titles above; confirm each matches its claim.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
