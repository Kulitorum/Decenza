#!/usr/bin/env python3
"""Fail on QML that instantiates a type Decenza replaces with its own wrapper.

One row per banned type. A bare QtQuick.Dialogs FileDialog/FolderDialog destroyed while open
leaves Qt's Android helper registered for activity results, and the picker's result then crashes
the app (#2176); the wrappers close the dialog first. Nothing else catches a regression: it
compiles, lints and tests clean, and crashes only on Android, only when the page goes away
under an open picker.
"""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from source_lexer import code_only  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parent.parent

# banned type -> (replacement, the wrapper file that alone may use it, why)
BANNED_TYPES: dict[str, tuple[str, str, str]] = {
    "FileDialog": ("DecenzaFileDialog", "qml/components/DecenzaFileDialog.qml",
                   "it closes the dialog before its page is destroyed (#2176)"),
    "FolderDialog": ("DecenzaFolderDialog", "qml/components/DecenzaFolderDialog.qml",
                     "it closes the dialog before its page is destroyed (#2176)"),
}

# An object declaration, optionally module-qualified (`Dialogs.FileDialog {`). The lookbehind
# keeps `DecenzaFileDialog {` and `foo.FileDialog` member reads out.
_DECL = re.compile(r"(?<![\w.])(?:[A-Za-z_]\w*\.)?(" + "|".join(BANNED_TYPES) + r")\s*\{")


def violations(path: str, text: str) -> list[str]:
    code = code_only(text, js=True)
    out = []
    for m in _DECL.finditer(code):
        name = m.group(1)
        replacement, wrapper, why = BANNED_TYPES[name]
        if path == wrapper:
            continue
        line = code.count("\n", 0, m.start()) + 1
        out.append(f"{path}:{line}: bare `{name}`: use `{replacement}`; {why}")
    return out


def tracked_qml() -> list[str]:
    out = subprocess.run(["git", "ls-files", "*.qml"], cwd=REPO, capture_output=True, text=True,
                         check=True).stdout.split()
    if not out:
        sys.exit("git ls-files found no .qml files; the scan is broken, not the tree.")
    return out


def self_test() -> int:
    wrapper = BANNED_TYPES["FileDialog"][1]
    cases = [
        ("bare", "x.qml", "Item {\n    FileDialog { id: d }\n}", ["x.qml:2: bare `FileDialog`"]),
        ("qualified", "x.qml", "Item { Dialogs.FolderDialog {} }", ["bare `FolderDialog`"]),
        ("inline component", "x.qml", "Item { component P: FileDialog {} }", ["bare `FileDialog`"]),
        ("wrapper used", "x.qml", "Item { DecenzaFileDialog {} }", []),
        ("enum read", "x.qml", "Item { property int m: FileDialog.OpenFile }", []),
        ("comment", "x.qml", "Item {\n    // FileDialog { }\n}", []),
        ("string", "x.qml", 'Item { property string s: "FileDialog {" }', []),
        ("the wrapper itself", wrapper, "FileDialog {\n}", []),
    ]
    failed = 0
    for label, path, text, expect in cases:
        got = violations(path, text)
        ok = len(got) == len(expect) and all(e in g for e, g in zip(expect, got))
        if not ok:
            failed += 1
            print(f"FAIL {label}: expected {expect}, got {got}")
    print(f"check_qml_banned_types self-test: {len(cases) - failed}/{len(cases)} passed")
    return 1 if failed else 0


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        return self_test()
    missing = [w for _, w, _ in BANNED_TYPES.values() if not (REPO / w).is_file()]
    if missing:
        sys.exit(f"wrapper file(s) missing: {', '.join(missing)}; update BANNED_TYPES.")
    found = []
    files = tracked_qml()
    for path in files:
        found += violations(path, (REPO / path).read_text(encoding="utf-8", errors="replace"))
    for v in found:
        print(v)
    if found:
        print(f"\n{len(found)} banned QML type use(s).")
        return 1
    print(f"No banned QML types in {len(files)} file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
