#!/usr/bin/env python3
"""Reject a QML/JS statement that opens with ( [ or ` after an unterminated line.

JavaScript inserts no semicolon before a line starting with one of those, so
the line is parsed as a continuation of the one above:

    shot.enjoyment0to100 = page.editEnjoyment
    (loader.ensure() as ConversationOverlay)?.openWithShot(shot, ...)

is `page.editEnjoyment(loader.ensure() as ...)?.openWithShot(...)` — a call on
a number, which threw on every tap and left the post-shot AI Advice button dead
in 2.0.8. It is valid JavaScript, so the compiler and qmllint both pass it; the
only symptom is a TypeError in the running app's log.

Fix by binding the value to a local first (`const x = ...` then `x?.f()`),
never by adding a leading `;`.

A line-based grep, not a parser: the previous line counts as unterminated when
it ends in an identifier, a literal, `)` or `]`. A `}` is not flagged — it
almost always closes a block. Exit code 1 on any finding.

Run --self-test to exercise fixtures through this same checker.
"""

import pathlib
import re
import sys

QML_DIR = pathlib.Path(__file__).resolve().parent.parent / "qml"

OPENERS = ("(", "[", "`")
UNTERMINATED = re.compile(r"""[\w)\]"'`]$""")
# `//` only after whitespace or at line start, so "http://..." stays intact.
LINE_COMMENT = re.compile(r"(^|\s)//.*$")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)


def scan(text):
    """Yield (lineno, previous_line, line) for each hazard in one file."""
    # Blank block comments but keep their newlines, so line numbers stay true.
    text = BLOCK_COMMENT.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    prev = ""
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = LINE_COMMENT.sub("", raw).strip()
        if not line:
            continue
        if line.startswith(OPENERS) and UNTERMINATED.search(prev):
            yield lineno, prev, line
        prev = line


def main() -> int:
    findings = []
    paths = sorted(list(QML_DIR.rglob("*.qml")) + list(QML_DIR.rglob("*.js")))
    for path in paths:
        rel = path.relative_to(QML_DIR.parent)
        for lineno, prev, line in scan(path.read_text(encoding="utf-8")):
            findings.append(f"{rel}:{lineno}: statement opens with '{line[0]}' "
                            f"after unterminated line: {prev[-60:]!r}")

    if not findings:
        print(f"check_qml_asi_hazards: OK — {len(paths)} files in {QML_DIR.name}/")
        return 0
    print("\n".join(findings))
    print(f"\ncheck_qml_asi_hazards: {len(findings)} finding(s). The line is parsed "
          "as a continuation of the one above it. Bind the value to a local first "
          "(`const x = ...`, then `x?.f()`).")
    return 1


def self_test() -> int:
    must_flag = {
        "shipped 2.0.8 shape":
            "a.b = c.d\n(loader.ensure() as T)?.open()\n",
        "array after call":
            "foo()\n[1, 2].forEach(f)\n",
        "template after literal":
            "var s = 'x'\n`y`\n",
        "comment between":
            "a = b\n// opens the overlay\n(x)?.open()\n",
        "url in previous line":
            "var u = \"http://x\"\n(y)?.open()\n",
    }
    must_pass = {
        "after brace": "onClicked: {\n(loader.ensure() as T)?.open()\n",
        "after semicolon": "a = b;\n(x)?.open()\n",
        "after comma": "f(a,\n(b))\n",
        "after closing block": "if (x) {\n}\n(y)?.open()\n",
        "inside block comment": "a = b\n/*\n(x)\n*/\n",
    }
    failed = []
    for name, src in must_flag.items():
        if not list(scan(src)):
            failed.append(f"not flagged: {name}")
    for name, src in must_pass.items():
        if list(scan(src)):
            failed.append(f"false positive: {name}")
    if failed:
        print("check_qml_asi_hazards self-test FAILED:\n  " + "\n  ".join(failed))
        return 1
    print(f"check_qml_asi_hazards self-test: OK — {len(must_flag)} flagged, "
          f"{len(must_pass)} passed")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv else main())
