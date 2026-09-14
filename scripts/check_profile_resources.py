#!/usr/bin/env python3
"""No .qrc in the repo may list a bundled profile.

The profile resource list is generated at configure time from
resources/profiles/*.json (CMakeLists.txt, "Bundled profiles"), so the
directory is the only source of truth. This check exists because the list
used to be kept by hand, twice: resources.qrc for the app and profiles.qrc for
the tests, and #1833 added Adaptive v3 to the second only — nine profiles
reached every test binary and no release. A hand-written entry anywhere would
be that second copy again.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENTRY = re.compile(r"<file[^>]*>\s*[^<]*profiles/[^<]+\.json\s*</file>")


def main() -> int:
    hits: list[str] = []
    for qrc in sorted(ROOT.rglob("*.qrc")):
        if "build" in qrc.parts:
            continue
        for entry in ENTRY.findall(qrc.read_text(encoding="utf-8")):
            hits.append(f"{qrc.relative_to(ROOT)}: {entry.strip()}")
    if hits:
        print("check_profile_resources: FAIL — bundled profiles are listed by CMake from the directory, never by hand")
        for h in hits:
            print("  " + h)
        return 1
    count = len(list((ROOT / "resources" / "profiles").glob("*.json")))
    print(f"check_profile_resources: OK — {count} bundled profiles, no hand-written list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
