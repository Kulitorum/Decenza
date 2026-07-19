#!/usr/bin/env python3
"""Reject hardcoded font.family literals in QML.

A font family named as a string literal is a bet that the family exists on
every platform. When it does not, Qt falls back to a host font, and the same
screen measures differently on two machines — which is the exact failure the
bundled Decenza Sans exists to prevent (see the font notes in CLAUDE.md).

These were originally found by reading warnings out of a running app's log —
seven literals across four families ("serif", "monospace", "Arial" x4 and a
"Material Icons" reference to a font this project does not bundle). That is a
poor way to discover something a grep finds in milliseconds, which is why this
check exists. See the commit that added it for the full list.

What to do instead:
  - Nothing. Omit font.family and inherit the bundled application font, which
    is what almost every label should do.
  - Need a specific face (a monospaced code block)? Name REAL families in
    priority order via Qt.font({families: [...], pixelSize: ...}). Note
    `font.families:` is not assignable as a grouped property — that spelling
    fails at LOAD, not at compile.
  - Need an icon? Use a themed SVG through ThemedIcon. Icon fonts are not
    bundled here, and a glyph cannot follow Theme.iconColor.

Exit code 1 on any finding, so this is a blocking check.
"""

import pathlib
import re
import sys

QML_DIR = pathlib.Path(__file__).resolve().parent.parent / "qml"

# Matches `font.family: "X"` and `font.family: \'X\'`, plus the grouped form
# `font { family: "X" }`. Qt.font({families: [...]}) is the sanctioned escape
# hatch and is deliberately NOT matched — it takes a prioritised list, which is
# the thing we want people to use when they genuinely need a specific face.
#
# Known limits, stated rather than pretended away: a family built at runtime
# (`font.family: someExpression`) and a QML JS assignment
# (`label.font.family = "Arial"`) are not matched. Both are legal QML. This is a
# grep, not a parser; it catches the idiom people actually write.
PATTERN = re.compile(
    r"""(?:font\s*\.\s*family|(?<![\w.])family)\s*:\s*(['"])([^'"]+)\1"""
)

# Strip // to end-of-line and /* */ spans before matching, so a commented-out
# example is not a blocking false positive. A blocking check has no override,
# so a false positive stops a PR with a message that will not match what the
# author sees on screen.
LINE_COMMENT = re.compile(r"//.*$")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)

# Families the project actually bundles (resources/fonts/). Naming one of these
# explicitly is redundant but not a portability bug, so it is allowed.
BUNDLED = {"Decenza Sans", "Noto Sans Math"}


def main() -> int:
    findings = []
    for path in sorted(QML_DIR.rglob("*.qml")):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            # Report as a finding rather than a traceback: the job still fails,
            # but the message names the file instead of a stack.
            findings.append((path.relative_to(QML_DIR.parent), 0,
                             f"<not valid UTF-8: {exc}>"))
            continue
        text = BLOCK_COMMENT.sub("", text)
        for lineno, line in enumerate(text.splitlines(), 1):
            m = PATTERN.search(LINE_COMMENT.sub("", line))
            if m and m.group(2) not in BUNDLED:
                rel = path.relative_to(QML_DIR.parent)
                findings.append((rel, lineno, m.group(2)))

    if not findings:
        print(f"check_font_family_literals: OK — no hardcoded font.family "
              f"literals in {QML_DIR.name}/")
        return 0

    print("Hardcoded font.family literals found. Each is a bet that the family "
          "exists on every platform;\nwhen it does not, Qt falls back to a host "
          "font and metrics stop matching across machines.\n")
    for rel, lineno, family in findings:
        print(f"  {rel}:{lineno}: font.family: \"{family}\"")
    print("\nFix by omitting font.family (inherits the bundled application "
          "font), or — if a specific\nface is genuinely needed — naming real "
          "families via Qt.font({families: [...]}).\nFor icons use ThemedIcon "
          "with an SVG; icon fonts are not bundled and cannot follow "
          "Theme.iconColor.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
