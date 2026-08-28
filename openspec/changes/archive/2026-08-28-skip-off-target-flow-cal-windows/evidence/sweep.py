#!/usr/bin/env python3
"""Contribution rate and ideal spread for the candidate rule, per dataset.

Sweeps the candidate's flatness knobs so the trade-off is visible: a stricter
rule samples one operating point but throws more shots away, and a shot that
contributes nothing slows convergence rather than biasing it.

Usage: sweep.py <label>=<glob> [...]
"""

import glob
import importlib
import os
import statistics
import sys


def run(paths, min_dur, spread, tol):
    os.environ["CAND_MIN_DURATION"] = str(min_dur)
    os.environ["FLOW_SPREAD"] = str(spread)
    os.environ["TARGET_TOLERANCE"] = str(tol)
    sim = importlib.reload(importlib.import_module("flowcal_sim"))

    cur, cand, cur_mf, cand_mf = [], [], [], []
    for path in paths:
        shot = sim.load_shot(path)
        segments = sim.qualifying_samples(shot)
        for picker, scorer, vals, flows in (
                (sim.pick_current, sim.ideal_current, cur, cur_mf),
                (sim.pick_candidate, sim.ideal_candidate, cand, cand_mf)):
            w = picker(segments)
            if w is None:
                continue
            ideal, _ = scorer(w, sim.frame_target(shot, w), 1.0)
            if ideal is not None:
                vals.append(ideal)
                flows.append(w["meanMachineFlow"])
    return cur, cand, cur_mf, cand_mf


def describe(vals, flows, total):
    if not vals:
        return "%2d/%2d  %-8s %-8s %s" % (0, total, "-", "-", "-")
    spread = (max(vals) - min(vals)) / statistics.median(vals)
    flow_range = "%.2f-%.2f" % (min(flows), max(flows))
    return "%2d/%2d  %-8.3f %-8.1f %s" % (
        len(vals), total, statistics.median(vals), spread * 100, flow_range)


def main(specs):
    datasets = []
    for spec in specs:
        label, _, pattern = spec.partition("=")
        datasets.append((label, sorted(glob.glob(pattern))))

    configs = [(1.5, 0.10), (3.0, 0.07), (3.0, 0.05), (5.0, 0.05), (5.0, 0.03)]
    for label, paths in datasets:
        print("\n=== %s (%d shots) ===" % (label, len(paths)))
        print("%-22s %s" % ("rule", "n/total  median   spread%  flow range"))
        cur, cand, cur_mf, cand_mf = run(paths, 1.5, 0.10, 0.10)
        print("%-22s %s" % ("current", describe(cur, cur_mf, len(paths))))
        for min_dur, spread in configs:
            _, cand, _, cand_mf = run(paths, min_dur, spread, 0.10)
            print("%-22s %s" % ("candidate %.1fs/%d%%" % (min_dur, spread * 100),
                                describe(cand, cand_mf, len(paths))))


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    main(sys.argv[1:])
