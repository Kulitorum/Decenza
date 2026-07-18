#!/usr/bin/env python3
"""Rename the bundled UI font's family from Roboto to Decenza Sans.

WHY: registering a bundled font under a widely-distributed family name makes
family-name lookup ambiguous. Windows machines commonly have a Roboto installed
by Chrome/Adobe; when two fonts claim "Roboto", shaping and rasterization can
resolve to different files -- producing a wrong glyph for a ligature ("Profile"
rendering as "Proule") and advance widths that are not ours (#1537, #1469).
A family name nothing else can claim removes the ambiguity structurally.

Preserves the existing family SHAPE exactly -- Regular/Bold stay a RIBBI pair
(ID1 shared, ID2 = Regular/Bold); Light/Medium stay separate families linked by
typographic family (ID16/ID17), which is how Google ships them. Only the names
change, never the structure, so font selection behaves exactly as before.

Name IDs rewritten: 1 (Family), 3 (Unique ID), 4 (Full name), 6 (PostScript),
16 (Typographic Family). ID2/ID17 (subfamily) are left alone -- they carry the
weight, not the family.

Usage:  python3 tools/rename_bundled_font.py            # rewrite in place
        python3 tools/rename_bundled_font.py --verify   # check only, no writes

Requires fonttools (pip install fonttools). Re-run after any font update.
"""
import glob, os, sys
from fontTools.ttLib import TTFont

OLD, NEW = "Roboto", "Decenza Sans"
NEW_PS = "DecenzaSans"                     # PostScript names may not contain spaces
FAMILY_IDS = (1, 3, 4, 16)
PS_IDS = (6,)

def rename(path, verify=False):
    f = TTFont(path)
    changed = []
    for rec in f["name"].names:
        if rec.nameID not in FAMILY_IDS + PS_IDS:
            continue
        old = rec.toUnicode()
        if OLD not in old:
            continue
        new = old.replace(OLD, NEW_PS if rec.nameID in PS_IDS else NEW)
        if rec.nameID in PS_IDS:
            new = new.replace(" ", "")
        changed.append((rec.nameID, old, new))
        if not verify:
            rec.string = new
    if not verify and changed:
        f.save(path)
    return changed

def main():
    verify = "--verify" in sys.argv
    files = sorted(glob.glob("resources/fonts/*.ttf"))
    if not files:
        sys.exit("no fonts found under resources/fonts/ -- run from the repo root")
    for p in files:
        changed = rename(p, verify)
        print(f"{os.path.basename(p)}: {len(changed)} record(s)"
              f"{' would change' if verify else ' renamed'}")
        for nid, old, new in changed:
            print(f"    ID{nid}: {old!r} -> {new!r}")

if __name__ == "__main__":
    main()
