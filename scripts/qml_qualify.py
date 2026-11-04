#!/usr/bin/env python3
"""Apply qmllint's own suggested scope prefixes to unqualified accesses.

WHY THIS EXISTS, AND WHY IT DOES NOT GUESS
------------------------------------------
Qualifying an identifier means deciding WHICH scope it belongs to. Reading indentation to
decide is how you get this, from a real bulk pass:

    ValueInput.qml has `gear` on a delegate. The pass prefixed it with the component root,
    which has no such member, so every gear tap assigned `undefined`.

qmllint already knows the answer. For an unqualified access whose owner has an id, its JSON
finding carries an auto-applicable edit naming that id (shown trimmed):

    {"id": "unqualified", "line": 142, "column": 35, "suggestions": [{"isAutoApplicable": true,
     "documentEdits": [{"location": {"line": 142, "column": 35, "length": 0},
                        "replacement": "livePresetDelegate."}]}]}

That is the RIGHT id — `livePresetDelegate`, not the page root, and not the other delegate in the
same file. (Against an injected context property qmllint offers no id at all, because there is
none; declare `required property var modelData` on a delegate with an id first.) This script
applies that edit. It never infers a prefix.

THE OTHER RULE: INSERT RIGHT-TO-LEFT
------------------------------------
Two insertions on one line, applied left-to-right, put the second one in the wrong place because
the first shifted every column after it. That is how `results.length` became
`results.shotHistoryPage.length` — undefined, `+=` made it NaN, and Shot History pagination died.
qmllint did not catch it: `results` is a signal-handler parameter typed `var`, so nothing checks
that chain. Sorting by descending column is the whole fix, and --verify re-checks it anyway.

WHAT IT DOES NOT HANDLE
-----------------------
Sites with no suggestion are reported, never guessed at. In practice they are:
  * bare calls to the file's own functions — qmllint says "Unqualified access" with no Info block
  * `event` in a `Keys.onXPressed: { ... }` handler — Info says `"event" is ambiguous. Use a
    function instead: (event) => ...`, and the fix is the `function(event)` form, not a prefix
  * ids from outer components inside a nested component — Info says to set
    `pragma ComponentBehavior: Bound`, which is a semantic change and needs a delegate audit first

Its input is the JSON `scripts/qmllint_report.py --raw-out` writes, never qmllint's text output,
which drops findings (see that script's docstring).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

def read_findings(path: Path, target: Path) -> list[dict]:
    """The `unqualified` findings for `target` from a qmllint_report.py --raw-out JSON file."""
    entries = json.loads(path.read_text())["files"]
    # Match on the full path, not the basename: two files can share a name.
    mine = [e for e in entries if Path(e["filename"]).resolve() == target.resolve()]
    if len(mine) != 1:
        sys.exit(f"{path} has {len(mine)} entries for {target}; it must have exactly one. "
                 f"Re-run qmllint_report.py --raw-out over a tree that includes it.")
    return [w for w in mine[0]["warnings"] if w["id"] == "unqualified"]


def collect(findings: list[dict], lines: list[str]):
    """Return (fixes, unsuggested). fixes maps line -> [(col, prefix)]."""
    fixes: dict[int, list[tuple[int, str]]] = defaultdict(list)
    unsuggested: list[tuple[int, str, str]] = []
    for w in findings:
        ln, col = w["line"], w["column"]
        edits = [(s_, e) for s_ in w["suggestions"] if s_.get("isAutoApplicable")
                 for e in s_["documentEdits"]]
        # Only a pure insertion of `<id>.` at the flagged column is a qualification.
        prefix = [e["replacement"][:-1] for _, e in edits
                  if e["location"]["length"] == 0 and e["location"]["line"] == ln
                  and e["location"]["column"] == col
                  and re.fullmatch(r"[A-Za-z_]\w*\.", e["replacement"])]
        if len(prefix) == 1:
            fixes[ln].append((col, prefix[0]))
            continue
        src = lines[ln - 1] if 0 < ln <= len(lines) else ""
        why = " ".join(w["suggestions"][0]["message"].split()) if w["suggestions"] else "no suggestion"
        unsuggested.append((ln, src.strip()[:70], why[:70]))
    return fixes, unsuggested


def apply(path: Path, fixes: dict[int, list[tuple[int, str]]]) -> int:
    lines = path.read_text().split("\n")
    applied = 0
    for ln, items in fixes.items():
        s = lines[ln - 1]
        # RIGHT-TO-LEFT: every insertion shifts the columns after it.
        for col, prefix in sorted(set(items), key=lambda x: -x[0]):
            s = s[:col - 1] + prefix + "." + s[col - 1:]
            applied += 1
        lines[ln - 1] = s
    path.write_text("\n".join(lines))
    return applied


def verify(path: Path) -> int:
    """Re-check the diff for a prefix inserted INTO an existing dotted chain.

    Paired-line, not textual: if dropping one segment from a new chain yields a chain that was
    in the old line, the insertion landed mid-chain. This is the check that would have caught
    results.shotHistoryPage.length, which no linter reported.
    """
    diff = subprocess.run(["git", "diff", "--", str(path)],
                          capture_output=True, text=True).stdout.split("\n")
    chains = lambda s: set(re.findall(r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)+", s))
    bad, minus, i = 0, [], 0
    while i < len(diff):
        line = diff[i]
        if line.startswith("-") and not line.startswith("---"):
            minus.append(line[1:])
            i += 1
            continue
        if line.startswith("+") and not line.startswith("+++"):
            plus = []
            while i < len(diff) and diff[i].startswith("+") and not diff[i].startswith("+++"):
                plus.append(diff[i][1:])
                i += 1
            if len(minus) != len(plus):
                # Unequal hunk: the paired comparison below cannot align lines, so it would
                # silently check nothing. Say so rather than reporting a clean run.
                print(f"  UNVERIFIED HUNK ({len(minus)} removed / {len(plus)} added) — "
                      f"mid-chain check could not run here; inspect by hand.", file=sys.stderr)
                bad += 1
            if len(minus) == len(plus):
                for before, after in zip(minus, plus):
                    if before == after:
                        continue
                    old = chains(before)
                    for chain in chains(after):
                        if chain in old:
                            continue
                        parts = chain.split(".")
                        # (a) INSERTION: a segment was added into an existing chain.
                        hit = False
                        for k in range(1, len(parts)):
                            if ".".join(parts[:k] + parts[k + 1:]) in old:
                                print(f"  MID-CHAIN INSERT: {after.strip()}", file=sys.stderr)
                                bad += 1
                                hit = True
                                break
                        if hit:
                            continue
                        # (b) SUBSTITUTION: same length, exactly one segment differs. This is what
                        # replacing the seven characters `parent.` does to a `parent.parent.X`
                        # chain — it rewrites the SECOND segment and leaves the first, so the
                        # result reads a member off the wrong object. Five of those shipped past
                        # the insertion check above, which cannot see them: nothing was added.
                        for prev in old:
                            pp = prev.split(".")
                            if len(pp) != len(parts):
                                continue
                            diff = [i for i in range(len(pp)) if pp[i] != parts[i]]
                            if len(diff) == 1 and diff[0] > 0:
                                print(f"  MID-CHAIN SUBST: {prev.strip()} -> {chain}",
                                      file=sys.stderr)
                                bad += 1
                                break
            minus = []
            continue
        minus = []
        i += 1
    return bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="QML file to qualify, repo-relative")
    ap.add_argument("--raw", required=True,
                    help="JSON from scripts/qmllint_report.py --raw-out covering this file")
    ap.add_argument("--apply", action="store_true", help="write changes (default: dry run)")
    args = ap.parse_args()

    path = Path(args.file)
    fixes, unsuggested = collect(read_findings(Path(args.raw), path),
                                 path.read_text().split("\n"))

    total = sum(len(v) for v in fixes.values())
    prefixes = Counter(p for v in fixes.values() for _, p in v)
    print(f"{path}: {total} suggested across {len(fixes)} lines  {dict(prefixes)}")
    if unsuggested:
        print(f"  {len(unsuggested)} with NO suggestion — handle by hand, do not guess:")
        for ln, src, why in unsuggested:
            print(f"    L{ln}: {src}\n         {why}")
    if not args.apply:
        print("  (dry run; pass --apply to write)")
        return 0

    print(f"  applied {apply(path, fixes)}")
    bad = verify(path)
    print(f"  mid-chain insertions: {bad}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
