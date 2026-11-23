#!/usr/bin/env python3
"""Fail if a Qt 6 version is written anywhere but qt-version.txt.

Scans workflows, CMake files and build scripts, skipping whole-line comments, for any Qt 6.1x+
version in dotted, regex-escaped (6\\.12\\.1) or underscore (6_12_1) form. That covers a new copy
of the current version and a stale copy a bump left behind. A dated reference that must keep an
old version goes in DATED. Prose docs are out of scope.
"""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys

import qt_version

ROOT = Path(__file__).resolve().parent.parent
ROOTS = (".github", "cmake", "scripts", "tools", "src", "tests", "android", "ios")
SUFFIXES = {".yml", ".yaml", ".cmake", ".py", ".sh", ".ps1", ".bat", ".gradle", ".rb"}
# Build scripts at the repository root, which the directory roots above do not reach.
ROOT_SCRIPTS = tuple(f":(top,glob)*{suffix}" for suffix in (".sh", ".ps1", ".bat"))
QT_VERSION = re.compile(r"(?<![\d.])6(?:\\*\.|_)(?:[1-9]\d)(?:\\*\.|_)(?:\d+(?![\d])|\*)")

# Versions that are history, not pins: file (relative to the repo root) -> versions it may name.
DATED = {
    # Notes on when qmllint's text output and its OOM were fixed.
    "scripts/qmllint_report.py": {"6.12.0", "6.11.2"},
}


def scanned_files() -> list[Path]:
    # Tracked files only: a local build directory under tools/ holds generated copies.
    tracked = subprocess.run(["git", "ls-files", "-z", "CMakeLists.txt", *ROOT_SCRIPTS, *ROOTS], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.split("\0")
    paths = (ROOT / rel for rel in tracked if rel)
    return [p for p in paths if p.is_file() and p != Path(__file__).resolve()
            and (p.suffix in SUFFIXES or p.name == "CMakeLists.txt")]


def normalized(found: str) -> str:
    return re.sub(r"\\*\.|_", ".", found)


def findings(text: str, allowed: set[str] = frozenset()) -> list[tuple[int, str]]:
    hits = []
    for n, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith(("#", "//")):
            continue
        for match in QT_VERSION.finditer(line):
            version = normalized(match.group(0))
            if version not in allowed:
                hits.append((n, version))
    return hits


def self_test() -> None:
    assert findings("version: '6.18.7'\n") == [(1, "6.18.7")]
    assert findings("  # checked on 6.18.7\n") == []
    assert findings("  // checked on 6.18.7\n") == []
    assert findings("x: 6.18.7  # trailing comment still a pin\n") == [(1, "6.18.7")]
    assert findings(r"grep '6\.18\.7' f" + "\n") == [(1, "6.18.7")]
    assert findings(r'MATCHES "6\\.18\\.7"' + "\n") == [(1, "6.18.7")]
    assert findings(r"C:\Qt\6.18.7\msvc" + "\n") == [(1, "6.18.7")]
    assert findings("version: '6.18.*'\n") == [(1, "6.18.*")]
    assert findings("build/Qt_6_18_7_for_macOS\n") == [(1, "6.18.7")]
    assert findings('VERSION_GREATER_EQUAL "6.5.0"\n') == []
    assert findings("v16.18.70 and 26.18.7\n") == []
    assert findings("find_package(Qt6 6.18.6 EXACT)\n", {"6.18.6"}) == []
    files = {f.relative_to(ROOT).as_posix() for f in scanned_files()}
    for expected in (".github/workflows/release.yml", "CMakeLists.txt", "cmake/DecenzaQtVersion.cmake",
                     "tests/CMakeLists.txt", "tools/qtmodules-wasm/build-ci.sh", "build.sh"):
        assert expected in files, f"not scanned: {expected}"
    assert "qt-version.txt" not in files
    for dated in DATED:
        assert dated in files, f"DATED names a file the scan does not reach: {dated}"
    print("check_qt_version_pins: self-test OK")


def main() -> int:
    if "--self-test" in sys.argv[1:]:
        self_test()
        return 0
    version = qt_version.pinned()
    files = scanned_files()
    if not files:
        print("check_qt_version_pins: scanned no files; the file selection is broken", file=sys.stderr)
        return 1
    bad = []
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        for line, found in findings(path.read_text(encoding="utf-8", errors="replace"), DATED.get(rel, set())):
            bad.append(f"{rel}:{line}: {found}")
    if bad:
        print(f"check_qt_version_pins: a Qt version is written outside qt-version.txt (now {version}). "
              "Read it through cmake/DecenzaQtVersion.cmake, scripts/qt_version.py or the release "
              "workflow's needs.plan.outputs.qt_version; a dated reference goes in DATED:", file=sys.stderr)
        for entry in bad:
            print(f"  {entry}", file=sys.stderr)
        return 1
    print(f"check_qt_version_pins: OK — no Qt version outside qt-version.txt ({version}), "
          f"{len(files)} files scanned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
