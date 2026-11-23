#!/usr/bin/env python3
"""The Qt version Decenza builds against, from qt-version.txt at the repository root.

Run directly to print it; CI reads it this way.
"""

from __future__ import annotations

from pathlib import Path
import re

VERSION_FILE = Path(__file__).resolve().parent.parent / "qt-version.txt"


def pinned(path: Path = VERSION_FILE) -> str:
    text = path.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", text):
        raise ValueError(f"{path} must hold one Qt version as MAJOR.MINOR.PATCH; got {text!r}")
    return text


if __name__ == "__main__":
    print(pinned())
