#!/usr/bin/env python3
"""Reject hardcoded font.family literals in QML.

A font family named as a string literal is a bet that the family exists on
every platform. When it does not, Qt falls back to a host font, and the same
screen measures differently on two machines — which is the exact failure the
bundled Decenza Sans exists to prevent (see the font notes in CLAUDE.md).

Four live instances were found by reading warnings out of a running app's log,
which is a poor way to discover a class of defect that a grep can find at commit
time:

    font.family: "serif"          decorative italic "i" on the profile info
                                  badge. No platform provides a family by that
                                  name, so Qt ran a full font-alias sweep
                                  (~45 ms, with a warning) and fell back anyway.
    font.family: "monospace"      a code snippet, same generic-alias problem
                                  (~66 ms).
    font.family: "Arial"  x4      flip-clock digits. Present on macOS and
                                  Windows, frequently absent on Linux and
                                  Android, where the two halves of a flipping
                                  card would stop lining up.
    font.family: "Material Icons" a private-use codepoint in a font this
                                  project does not bundle, and the only such
                                  reference left in the tree. It rendered as
                                  whatever the host had, or as a tofu box.

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

# font.family: "Anything" — the grouped-property form. Qt.font({...}) is the
# sanctioned escape hatch and is deliberately NOT matched.
PATTERN = re.compile(r'font\.family\s*:\s*"([^"]+)"')

# Families the project actually bundles (resources/fonts/). Naming one of these
# explicitly is redundant but not a portability bug, so it is allowed.
BUNDLED = {"Decenza Sans", "Noto Sans Math"}


def main() -> int:
    findings = []
    for path in sorted(QML_DIR.rglob("*.qml")):
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("//"):
                continue
            m = PATTERN.search(line)
            if m and m.group(1) not in BUNDLED:
                rel = path.relative_to(QML_DIR.parent)
                findings.append((rel, lineno, m.group(1)))

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
