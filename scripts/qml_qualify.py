#!/usr/bin/env python3
"""Apply qmllint's own suggested scope prefixes to unqualified accesses.

WHY THIS EXISTS, AND WHY IT DOES NOT GUESS
------------------------------------------
Qualifying an identifier means deciding WHICH scope it belongs to. Reading indentation to
decide is how you get this, from a real bulk pass:

    ValueInput.qml has `gear` on a delegate. The pass prefixed it with the component root,
    which has no such member, so every gear tap assigned `undefined`.

qmllint already knows the answer and prints it. For an unqualified access it emits (this is real
output from the PATCHED qmllint, taken AFTER the delegate gained `required property var modelData`
— that matters: against an injected context property qmllint offers no id at all, because there is
no id to offer. The suggestion appears once the role is a declared member of a delegate with an id):

    Warning: .../FlushPage.qml:142:35: Unqualified access [unqualified]
                                text: modelData.name
                                      ^^^^^^^^^
    Info: modelData is a member of a parent element.
          You can qualify the access with its id to avoid this warning.

                                text: livePresetDelegate.modelData.name
                                      ^^^^^^^^^^^^^^^^^^^

That last block is the corrected line, with the RIGHT id — `livePresetDelegate`, not the page
root, and not the other delegate in the same file. This script reads that suggestion and applies
it. It never infers a prefix.

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

It DOES now handle qmllint's glued-diagnostic bug (see GLUED_RE) — that one used to lose sites
silently rather than report them, which is the worst possible behaviour for a tool whose entire
claim is "never guesses". An accounting check in main() refuses to edit if the parsed count and
qmllint's own count for the file disagree, so a future format change fails loudly instead.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

WARN = re.compile(r"^Warning: (\S+?):(\d+):(\d+): Unqualified access \[unqualified\]$")
QUALIFY_HINT = "qualify the access with its id"

# qmllint 6.11.1 omits the trailing newline after some hint lines ("You first have to give the
# element an id"), so the NEXT diagnostic is glued onto the end of that line and matches nothing.
# Measured on this tree: RecipeWizardPage.qml has 144 unqualified sites and 20 of them vanish
# without this split — not reported as unsuggested, silently absent. scripts/qmllint_report.py
# carries the same regex for the same reason; it splits in memory and never writes the repaired
# text back, so anything reading its --raw-out (i.e. this script) must split again itself.
GLUED_RE = re.compile(r"(?<!^)(?=(?:Warning|Error|Info): )", re.MULTILINE)


def read_raw(path: Path) -> list[str]:
    """Split qmllint output into lines, repairing glued diagnostics first (see GLUED_RE)."""
    return GLUED_RE.sub("\n", path.read_text(errors="replace")).split("\n")


def collect(raw: list[str], target: str):
    """Return (fixes, unsuggested) for one file. fixes maps line -> [(col, prefix)]."""
    fixes: dict[int, list[tuple[int, str]]] = defaultdict(list)
    unsuggested: list[tuple[int, str, str]] = []
    for i, line in enumerate(raw):
        m = WARN.match(line)
        if not m or not m.group(1).endswith(target):
            continue
        ln, col = int(m.group(2)), int(m.group(3))
        src = raw[i + 1] if i + 1 < len(raw) else ""
        if i + 4 < len(raw) and QUALIFY_HINT in raw[i + 4]:
            # raw[i+6] is the corrected line; the prefix is what now sits at the same column.
            prefix = re.match(r"([A-Za-z_]\w*)\.", raw[i + 6][col - 1:])
            if prefix:
                fixes[ln].append((col, prefix.group(1)))
                continue
        why = raw[i + 3].strip() if i + 3 < len(raw) else ""
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
                        for k in range(1, len(parts)):
                            if ".".join(parts[:k] + parts[k + 1:]) in old:
                                print(f"  MID-CHAIN: {after.strip()}", file=sys.stderr)
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
    ap.add_argument("--raw", required=True, help="saved qmllint output covering this file")
    ap.add_argument("--apply", action="store_true", help="write changes (default: dry run)")
    args = ap.parse_args()

    path = Path(args.file)
    raw = read_raw(Path(args.raw))
    # Match on the full relative path, not the basename: two files can share a name.
    fixes, unsuggested = collect(raw, "/" + str(path))

    # Accounting, because the failure mode above is SILENT. Count the file's unqualified
    # diagnostics independently of the parse and refuse to run if the two disagree.
    reported = sum(1 for line in raw
                   if (m := WARN.match(line)) and m.group(1).endswith("/" + path.name))
    seen = sum(len(v) for v in fixes.values()) + len(unsuggested)
    if seen != reported:
        print(f"ACCOUNTING MISMATCH: parsed {seen} but qmllint reported {reported} for {path}. "
              f"Refusing to edit — the parse is losing diagnostics.", file=sys.stderr)
        return 1

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
