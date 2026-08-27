#!/usr/bin/env python3
"""Extract per-machine calibration facts from submitted debug logs.

For every auto-flow-cal window the app logged, recover:

    k  = C * weightFlow / (machineFlow * density)

which is the machine's flow-sensor error — what the multiplier SHOULD converge
to. Then compare each machine's actually-converged multiplier against k and
against sqrt(k).

The point of the comparison: today's flow-branch update assigns a ratio as an
absolute multiplier (`C_new = w / (target * density)`). On a machine that
servos its calibrated flow, that iteration's fixed point is sqrt(k), not k — so
if the defect is real, converged multipliers should cluster on sqrt(k) across
unrelated machines, with the error against k changing SIGN either side of k=1.

Usage: logscan.py <dir-with-one-subdir-per-user>
"""

import glob
import os
import re
import statistics
import sys

RHO = 0.963
WINDOW = re.compile(r"steady window found.*?meanMachineFlow=\s*([\d.]+)\s*"
                    r"meanWeightFlow=\s*([\d.]+).*?currentFactor=\s*([\d.]+)")
MODE = re.compile(r'window mode=\s*(\w+).*?(?:target=([\d.]+))?\s*$')
UPDATED = re.compile(r'updated\s+"([^"]+)"\s+from\s+([\d.]+)\s+to\s+([\d.]+)')


def scan(path):
    windows, updates = [], []
    pending = None
    for raw in open(path, errors="replace"):
        line = raw.strip()
        m = WINDOW.search(line)
        if m:
            pending = (float(m.group(1)), float(m.group(2)), float(m.group(3)))
            continue
        if pending and "window mode=" in line:
            mode = "flow" if "flow" in line.split("window mode=")[1][:12] else "pressure"
            tgt = re.search(r"target=([\d.]+)", line)
            windows.append((pending[0], pending[1], pending[2], mode,
                            float(tgt.group(1)) if tgt else None))
            pending = None
            continue
        u = UPDATED.search(line)
        if u:
            updates.append((u.group(1), float(u.group(2)), float(u.group(3))))
    return windows, updates


def main(root):
    print("%-14s %-8s %-26s %-9s %-9s %-9s %-9s" % (
        "user", "windows", "on-target flow windows", "k", "sqrt(k)", "C now", "C vs sqrt(k)"))
    print("-" * 100)
    for d in sorted(glob.glob(os.path.join(root, "*"))):
        if not os.path.isdir(d):
            continue
        wins, ups = [], []
        for f in glob.glob(os.path.join(d, "**", "*"), recursive=True):
            if os.path.isfile(f):
                w, u = scan(f)
                wins += w
                ups += u
        if not wins:
            continue
        # k is only meaningful where the pour held its target: off-target
        # windows measure the sensor at a flow the profile never poured at.
        ont = [(mf, wf, c) for mf, wf, c, mode, t in wins
               if mode == "flow" and t and abs(t - mf) / t <= 0.10]
        ks = [c * wf / (mf * RHO) for mf, wf, c in ont]
        latest_c = ups[-1][2] if ups else (wins[-1][2] if wins else None)
        name = os.path.basename(d)
        if not ks:
            print("%-14s %-8d %-26s %-9s %-9s %-9.4f %s" % (
                name, len(wins), "none (never held target)", "-", "-",
                latest_c or 0, "-"))
            continue
        k = statistics.median(ks)
        rt = k ** 0.5
        print("%-14s %-8d %-26d %-9.3f %-9.3f %-9.4f %+.1f%%" % (
            name, len(wins), len(ont), k, rt, latest_c, (latest_c / rt - 1) * 100))


if __name__ == "__main__":
    main(sys.argv[1])
