#!/usr/bin/env python3
"""Fail if the current Qt version is written anywhere but qt-version.txt.

Scans workflows, CMake files and build scripts for the version qt-version.txt holds. Only that
version is a pin; an older one in a dated reference or a frozen tool is history, so it passes.
Whole-line comments are skipped. Prose docs are out of scope.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

import qt_version

ROOT = Path(__file__).resolve().parent.parent
ROOTS = (".github", "cmake", "scripts", "tools", "src")
SUFFIXES = {".yml", ".yaml", ".cmake", ".py", ".sh", ".ps1"}


def scanned_files() -> list[Path]:
    files = [ROOT / "CMakeLists.txt"]
    for root in ROOTS:
        for path in sorted((ROOT / root).rglob("*")):
            if path.is_file() and (path.suffix in SUFFIXES or path.name == "CMakeLists.txt"):
                files.append(path)
    return [f for f in files if "__pycache__" not in f.parts]


def findings(text: str, version: str) -> list[int]:
    pattern = re.compile(r"(?<![\d.])" + re.escape(version) + r"(?![\d])")
    return [n for n, line in enumerate(text.splitlines(), 1)
            if not line.lstrip().startswith("#") and pattern.search(line)]


def self_test() -> None:
    assert findings("version: '9.8.7'\n", "9.8.7") == [1]
    assert findings("  # checked on 9.8.7\n", "9.8.7") == []
    assert findings("find_package(Qt6 9.8.6 EXACT)\n", "9.8.7") == []
    assert findings("version 9.8.70 and 19.8.7\n", "9.8.7") == []
    assert findings("Qt/9.8.7/macos\nx\n", "9.8.7") == [1]
    print("check_qt_version_pins: self-test OK")


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        self_test()
        return 0
    version = qt_version.pinned()
    bad = []
    for path in scanned_files():
        for line in findings(path.read_text(encoding="utf-8", errors="replace"), version):
            bad.append(f"{path.relative_to(ROOT)}:{line}")
    if bad:
        print(f"check_qt_version_pins: Qt {version} is written outside qt-version.txt. Read it "
              "through cmake/DecenzaQtVersion.cmake, scripts/qt_version.py, or the release "
              "workflow's needs.plan.outputs.qt_version instead:", file=sys.stderr)
        for entry in bad:
            print(f"  {entry}", file=sys.stderr)
        return 1
    print(f"check_qt_version_pins: OK — Qt {version} is written only in qt-version.txt "
          f"({len(scanned_files())} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
