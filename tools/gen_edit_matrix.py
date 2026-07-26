#!/usr/bin/env python3
"""Generate the edit-matrix goldens: every editable parameter x every stock profile.

The wire tests answer "given identical frames, do we send identical bytes" — yes.
This answers the question upstream of that, and the one users actually hit:
given the same profile and the same single edit, does Decenza produce the frames
the plugin produces?

For each (profile, parameter) pair it runs de1app's REAL prep + update_* via
tools/de1app_edit_oracle.tcl and records the resulting frames. The C++ side
(tst_recipeeditorapppath) drives the same edit through ProfileManager's
Q_INVOKABLEs and diffs.

Only parameters the plugins actually expose are edited. Decenza's four extra ones
(fillTimeout / fillPressure / fillFlow / infuseEnabled) had no counterpart to
compare against; they have since been removed.

It also emits one COMPOUND case per profile: two parameters changed in
succession, each with its own prep -> update cycle. The single-edit matrix always
starts from a pristine profile, so it cannot see an error that only appears when
a second edit re-derives its parameters from the frames the first one wrote —
which is exactly how AF-1 compounded.

Usage:  python3 tools/gen_edit_matrix.py <de1plus-dir>
"""

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "tests", "data", "edit_matrix")
ORACLE = os.path.join(HERE, "de1app_edit_oracle.tcl")

# Decenza RecipeParams key -> de1app global (per-editor), and the value to set.
# Values are chosen to differ from every stock profile's current value, so the
# edit is always observable rather than accidentally a no-op.
SHARED = [
    ("fillTemperature", "{p}flow_filling_temperature", "87.5"),
    ("infusePressure",  "{p}flow_soaking_pressure",    "5.5"),
    ("infuseTime",      "{p}flow_soaking_seconds",     "42"),
    ("infuseVolume",    "{p}flow_soaking_volume",      "77"),
    ("infuseWeight",    "{p}flow_soaking_weight",      "2.5"),
    ("pourTemperature", "{p}flow_pouring_temperature", "91.5"),
    ("pourFlow",        "{p}flow_pouring_flow",        "2.6"),
    ("pourPressure",    "{p}flow_pouring_pressure",    "7.5"),
]

# (key, [(global, value), ...]) — applied in order, one save each.
COMPOUND = {
    "aflow": ("compound", [("Aflow_pouring_flow", "2.6"), ("ramp_down_enabled", "1")]),
    "dflow": ("compound", [("Dflow_soaking_pressure", "5.5"),
                           ("Dflow_pouring_temperature", "91.5")]),
}

AFLOW_ONLY = [
    ("rampTime",          "Aflow_ramp_updown_seconds", "7"),
    ("rampDownEnabled",   "ramp_down_enabled",         "1"),
    ("rampDownDisabled",  "ramp_down_enabled",         "0"),
    ("flowExtractionUp",  "flow_extraction_up",        "1"),
    ("flowExtractionOff", "flow_extraction_up",        "0"),
    ("secondFillEnabled", "2nd_fill_step",             "1"),
    ("secondFillOff",     "2nd_fill_step",             "0"),
]

FIELDS = ["name", "temperature", "pressure", "flow", "seconds", "volume", "weight",
          "max_flow_or_pressure", "max_flow_or_pressure_range", "exit_if", "exit_type",
          "exit_pressure_over", "exit_pressure_under", "exit_flow_over", "exit_flow_under",
          "pump", "transition", "sensor"]


def parse_frame(line):
    """Tcl dict -> {key: value}. Values may be braced ({Pre Fill}, {$weight}, {})."""
    idx, rest = line.split(" ", 1)
    out = {}
    i = 0
    toks = rest
    while i < len(toks):
        while i < len(toks) and toks[i] == " ":
            i += 1
        j = toks.find(" ", i)
        if j < 0:
            break
        key = toks[i:j]
        i = j + 1
        if i < len(toks) and toks[i] == "{":
            depth = 0
            start = i
            while i < len(toks):
                if toks[i] == "{":
                    depth += 1
                elif toks[i] == "}":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
            val = toks[start + 1:i - 1]
        else:
            j = toks.find(" ", i)
            if j < 0:
                val = toks[i:]
                i = len(toks)
            else:
                val = toks[i:j]
                i = j + 1
        out[key] = val
    return idx, out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    de1plus = os.path.expanduser(sys.argv[1])
    aflow_src = os.path.join(de1plus, "plugins", "A_Flow", "code.tcl")
    dflow_src = os.path.join(de1plus, "plugins", "D_Flow_Espresso_Profile", "plugin.tcl")

    os.makedirs(OUT, exist_ok=True)
    for stale in os.listdir(OUT):
        if stale.endswith(".txt"):
            os.remove(os.path.join(OUT, stale))

    targets = []
    for n in ["dark", "light", "like-dflow", "medium", "very-dark"]:
        targets.append(("aflow", "A-Flow",
                        os.path.join(REPO, "tests/data/de1app_profiles",
                                     "A-Flow____default-%s.tcl" % n),
                        aflow_src, "A-Flow____default-%s" % n))
    for n in ["default", "Q", "La_Pavoni"]:
        targets.append(("dflow", "D-Flow",
                        os.path.join(REPO, "tests/data/dflow_plugin_profiles",
                                     "D-Flow____%s.tcl" % n),
                        dflow_src, "D-Flow____%s" % n))

    written = failed = 0
    for kind, suffix, profile, src, base in targets:
        prefix = "A" if kind == "aflow" else "D"
        edits = [(k, g.format(p=prefix), v) for k, g, v in SHARED]
        if kind == "aflow":
            edits += AFLOW_ONLY

        runs = [(key, [glob, val]) for key, glob, val in edits]
        ckey, cpairs = COMPOUND[kind]
        runs.append((ckey, [x for pair in cpairs for x in pair]))

        for key, argv in runs:
            res = subprocess.run(["tclsh", ORACLE, src, profile, suffix] + argv,
                                 capture_output=True, text=True)
            if res.returncode != 0 or not res.stdout.strip():
                print("  FAIL %-28s %-18s %s" % (base, key,
                                                 res.stderr.strip().splitlines()[:1]))
                failed += 1
                continue

            lines = []
            for ln in res.stdout.strip().splitlines():
                idx, f = parse_frame(ln)
                lines.append("%s\t%s" % (idx, "\t".join(f.get(k, "") for k in FIELDS)))
            with open(os.path.join(OUT, "%s__%s.txt" % (base, key)), "w") as fh:
                fh.write("# fields: idx\t" + "\t".join(FIELDS) + "\n")
                fh.write("# edit: %s -> %s\n" % (key, " ".join(argv)))
                fh.write("\n".join(lines) + "\n")
            written += 1

    print("edit matrix: %d goldens written, %d failed" % (written, failed))


if __name__ == "__main__":
    main()
