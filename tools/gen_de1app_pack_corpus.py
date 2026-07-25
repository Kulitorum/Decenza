#!/usr/bin/env python3
"""Pack EVERY de1app stock profile with de1app's own packer and save the bytes.

The eight-profile wire test answers "do D-Flow and A-Flow reach the machine as
the same bytes". This answers the much broader question that actually bounds
regression risk: does EVERY profile de1app ships reach the machine as the same
bytes from Decenza?

That matters because the recipe-editor repairs touch code on the load and save
path that every profile passes through, while the parity suite's fixtures are
eight recipe profiles. The other ~80 are advanced, pressure and flow profiles
that no recipe-editor test covers at all — exactly where a repair could break
something that was working and nothing would notice.

It reuses tools/de1app_pack_oracle.tcl unchanged: all 89 stock profiles carry an
`advanced_shot` list, which is the only thing the oracle needs, so there is no
new oracle and no new shim.

Goldens land next to the existing eight in tests/data/de1app_packed/, one .txt
per source .tcl, and are committed so the test runs with no Tcl interpreter.
Regenerate on any de1app bump.

Usage:  python3 tools/gen_de1app_pack_corpus.py <de1plus-dir>
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = os.path.join(REPO, "tests", "data", "de1app_profiles")
OUT = os.path.join(REPO, "tests", "data", "de1app_packed")
ORACLE = os.path.join(HERE, "de1app_pack_oracle.tcl")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    de1plus = os.path.expanduser(sys.argv[1])

    os.makedirs(OUT, exist_ok=True)
    profiles = sorted(f for f in os.listdir(SRC) if f.endswith(".tcl"))
    if not profiles:
        raise SystemExit("no profiles in %s" % SRC)

    written = failed = 0
    for tcl in profiles:
        res = subprocess.run(["tclsh", ORACLE, de1plus, os.path.join(SRC, tcl)],
                             capture_output=True, text=True)
        if res.returncode != 0 or not res.stdout.strip():
            first = (res.stderr.strip().splitlines() or [""])[0]
            print("  FAIL %-52s %s" % (tcl, first))
            failed += 1
            continue
        with open(os.path.join(OUT, tcl[:-4] + ".txt"), "w") as fh:
            fh.write(res.stdout)
        written += 1

    print("de1app pack corpus: %d goldens written, %d failed (of %d profiles)"
          % (written, failed, len(profiles)))
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
