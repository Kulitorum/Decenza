#!/usr/bin/env python3
"""Compare candidate calibration CRITERIA, not just window rules.

Two different questions get called "flow calibration":

  DELIVERY  ideal = weightFlow / (targetFlow * density)
            "make the machine pour the flow the profile asked for"
            (today's flow branch)

  SENSOR    ideal = C * weightFlow / (machineFlow * density)
            "make reported flow equal true flow" — the criterion Decent's
            Graphical Flow Calibrator implements, where the operator nudges the
            multiplier until the flow curve overlays the weight-flow curve
            (today's pressure/achieved-flow branch)

They coincide only when the pour actually holds target. This script reports, per
dataset, what each criterion would converge to and whether it is stable.

The SENSOR criterion's fixed point is w/mf == density, i.e. the two curves
overlay. It is a runaway ONLY if w/mf does not respond to C (the v2 bug); if
w/mf moves as 1/C, it converges in one step. That is testable wherever the same
machine has shots at two different C values, so this script tests it.

Usage: criteria.py  (paths are wired in below)
"""

import glob
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import flowcal_sim as sim

RHO = 0.963
TOOLS = ("/Users/jeffreyh/.claude/projects/-Users-jeffreyh-Development-GitHub-Decenza/"
         "cb290e9e-ec27-4141-8d57-db3cb3536be4/tool-results/mcp-de1-shots_get_detail-*.txt")
SCRATCH = ("/private/tmp/claude-501/-Users-jeffreyh-Development-GitHub-Decenza/"
           "cb290e9e-ec27-4141-8d57-db3cb3536be4/scratchpad")

# C per shot. Known from the app's own logs for this repo's DE1 and for the
# #1872 reporter; unknown for user3 (his exported DB carries no currentFactor
# line), so his rows are reported as CHANGE FACTORS instead of absolute ideals.
JEFF_C = {1043: 0.918313, 1044: 0.918313, 1045: 0.918313, 1049: 0.918313,
          1053: 0.918313, 1057: 0.918313, 1058: 0.918313, 1059: 0.918313,
          1060: 0.918313, 1061: 0.918313, 1062: 0.918313, 1063: 0.918313,
          1065: 0.87949, 1066: 0.87949, 1067: 0.87949, 1068: 0.87949,
          1069: 0.87949, 1070: 0.87949, 1071: 0.87949, 1072: 0.87949,
          850: 1.0050852, 851: 1.0050852, 852: 1.0050852}
REPORTER_C = {"shot551": 1.35, "shot556": 1.1697, "shot25": 1.01, "shot541": 1.09312}


def windows(paths, c_lookup):
    """(label, C, target, mf, w) for every shot with a qualifying window."""
    out = []
    for p in sorted(paths):
        shot = sim.load_shot(p)
        w = sim.pick_current(sim.qualifying_samples(shot))
        if w is None:
            continue
        target = sim.frame_target(shot, w)
        key = shot["id"]
        c = c_lookup(key) if callable(c_lookup) else c_lookup.get(key)
        out.append((key, c, target, w["meanMachineFlow"], w["meanWeightFlow"]))
    return out


def on_target(rows, tol=0.10):
    return [r for r in rows if r[2] and abs(r[2] - r[3]) / r[2] <= tol]


def report(label, rows):
    ont = on_target(rows)
    print("\n=== %s — %d shots, %d on-target ===" % (label, len(rows), len(ont)))
    if not ont:
        print("  no on-target windows")
        return

    delivery = [w / (t * RHO) for _, _, t, _, w in ont]
    overlay_factor = [(w / mf) / RHO for _, _, _, mf, w in ont]   # ideal / C
    print("  DELIVERY criterion  -> C converges to      %.3f  (spread %.3f-%.3f)"
          % (statistics.median(delivery), min(delivery), max(delivery)))
    print("  SENSOR criterion    -> C multiplied by     %.3f  each update"
          % statistics.median(overlay_factor))
    known = [(c, mf, w) for _, c, _, mf, w in ont if c]
    if known:
        sensor = [c * w / (mf * RHO) for c, mf, w in known]
        print("                         i.e. C converges to %.3f  (spread %.3f-%.3f)"
              % (statistics.median(sensor), min(sensor), max(sensor)))

    # Is the SENSOR criterion stable? Its ideal is C-invariant iff w/mf moves
    # as 1/C. Group by distinct C to check directly.
    by_c = {}
    for _, c, _, mf, w in ont:
        if c:
            by_c.setdefault(round(c, 4), []).append(c * w / (mf * RHO))
    if len(by_c) > 1:
        print("  C-invariance of the SENSOR ideal (the v2-runaway test):")
        for c in sorted(by_c):
            v = by_c[c]
            print("    at C=%.4f  ideal median %.3f  (n=%d)" % (c, statistics.median(v), len(v)))


def main():
    report("this repo's DE1", windows(glob.glob(TOOLS), JEFF_C))
    report("user3 D-Flow (well dialled)",
           windows(glob.glob(SCRATCH + "/fred/dflow/*.json"), lambda k: None))
    report("user3 lever / pressure",
           windows(glob.glob(SCRATCH + "/fred/cremina/*.json"), lambda k: None))
    rep = windows(glob.glob(SCRATCH + "/u1872/*.json"), REPORTER_C) + \
          windows(glob.glob(SCRATCH + "/u1823/*.json"), REPORTER_C)
    report("#1872 reporter", rep)


if __name__ == "__main__":
    main()
