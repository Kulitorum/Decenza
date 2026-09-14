#!/usr/bin/env python3
"""Keep resources/profiles.qrc the one complete list of bundled profiles.

Two lists drifted for two months: resources.qrc carried its own copy of the
profile entries for the app, profiles.qrc fed the tests, and #1833 added
Adaptive v3 to the second only. Nine profiles reached every test binary and no
release. Three checks, all text, no build:

  1. every resources/profiles/*.json is listed in profiles.qrc
  2. every profiles.qrc entry exists on disk
  3. no other .qrc under resources/ lists a profiles/ entry

Exit 1 with the offending names on any failure.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "resources"
PROFILES_QRC = RES / "profiles.qrc"
ENTRY = re.compile(r"<file[^>]*>\s*(profiles/[^<]+\.json)\s*</file>")


def main() -> int:
    listed = set(ENTRY.findall(PROFILES_QRC.read_text(encoding="utf-8")))
    on_disk = {f"profiles/{p.name}" for p in (RES / "profiles").glob("*.json")}
    failures: list[str] = []

    for missing in sorted(on_disk - listed):
        failures.append(f"not in profiles.qrc: {missing}")
    for stale in sorted(listed - on_disk):
        failures.append(f"listed but not on disk: {stale}")
    for qrc in sorted(RES.glob("*.qrc")):
        if qrc == PROFILES_QRC:
            continue
        for entry in ENTRY.findall(qrc.read_text(encoding="utf-8")):
            failures.append(f"{qrc.name} lists {entry}; only profiles.qrc may")

    if failures:
        print("check_profile_resources: FAIL")
        for f in failures:
            print("  " + f)
        return 1
    print(f"check_profile_resources: OK — {len(listed)} bundled profiles, one list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
