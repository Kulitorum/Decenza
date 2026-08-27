#!/usr/bin/env python3
"""Offline replay of auto flow calibration window selection against real shots.

Reads shot JSON captured from the de1 MCP `shots_get_detail(detail="full")`
payloads and replays two window-selection rules over the same curves:

  current   — a faithful port of MainController::computeAutoFlowCalibration()
              (pressure-stability gate, longest qualifying window)
  candidate — same per-sample gates, but the window must also be FLAT in
              machine flow and weight flow, and its mean flow must land near
              the frame's target flow or the shot is skipped

The port is validated by comparing `current` against the window and ideal the
app itself logged for the same shot; the script prints both so a mismatch is
visible rather than assumed.

Not production code — analysis for Kulitorum/Decenza#1872.
"""

import json
import sys

# Constants copied from maincontroller.cpp computeAutoFlowCalibration()
MAX_DPDT = 0.5
MIN_PRESSURE = 1.5
MIN_WEIGHT_FLOW = 0.5
MIN_MACHINE_FLOW = 0.1
MAX_SCALE_GAP = 1.0
MIN_WINDOW_DURATION = 1.5
MIN_WINDOW_SAMPLES = 7
WATER_DENSITY = 0.963
MAX_SAMPLE_RATIO = 2.5
MIN_SAMPLE_RATIO = 0.4
MAX_WINDOW_RATIO = 1.35
MIN_WINDOW_RATIO = 0.75
MIN_WINDOW_START = 10.0
DEVIATION_THRESHOLD = 0.10

# Candidate-rule constants (the thing under test). Overridable from the
# environment so a sweep does not need edits: FLOW_SPREAD, WEIGHT_SPREAD,
# CAND_MIN_DURATION, TARGET_TOLERANCE.
import os

MAX_FLOW_SPREAD = float(os.environ.get("FLOW_SPREAD", 0.10))
MAX_WEIGHT_SPREAD = float(os.environ.get("WEIGHT_SPREAD", 0.20))
CANDIDATE_MIN_DURATION = float(os.environ.get("CAND_MIN_DURATION", 1.5))
TARGET_TOLERANCE = float(os.environ.get("TARGET_TOLERANCE", 0.10))


def load_shot(path):
    """Accepts either an MCP shots_get_detail(full) capture or a Decenza/DYE
    shot export (the `elapsed[] + flow{} + scale{}` shape attached to issues)."""
    raw = json.load(open(path))
    if isinstance(raw, list):
        for entry in raw:
            text = entry["text"]
            if text.lstrip().startswith("{"):
                return json.loads(text)
        raise ValueError("no shot object in " + path)
    if "elapsed" in raw:
        return normalize_export(raw, path)
    return raw  # already normalized (db_extract.py output)


def normalize_export(d, path):
    """Map an exported shot onto the same field names the MCP payload uses."""
    elapsed = d["elapsed"]
    pressure = [{"x": t, "y": v} for t, v in zip(elapsed, d["pressure"]["pressure"])]
    flow = [{"x": t, "y": v} for t, v in zip(elapsed, d["flow"]["flow"])]
    scale = d.get("scale", {})
    wf = [{"x": t, "y": v} for t, v in
          zip(scale.get("weight_arrival", []), scale.get("weight_flow", []))]
    flow_goal = [{"x": t, "y": v} for t, v in zip(elapsed, d["flow"].get("goal", []))]
    name = path.rsplit("/", 1)[-1].replace(".json", "")
    return {
        "id": name,
        "timestampIso": d.get("date", str(d.get("timestamp", ""))),
        "pressure": pressure,
        "flow": flow,
        "weightFlowRate": wf,
        "flowGoal": flow_goal,
        "phases": [],
        "profileJson": json.dumps(d.get("profile", {})),
    }


def qualifying_samples(shot):
    """Every sample that passes the CURRENT per-sample gates, in order.

    Returns a list of segments; each segment is a run of consecutive passing
    samples (a gate failure ends the run, exactly as finishWindow() does).
    """
    pressure = [(p["x"], p["y"]) for p in shot["pressure"]]
    flow = [(p["x"], p["y"]) for p in shot["flow"]]
    wflow = [(p["x"], p["y"]) for p in shot["weightFlowRate"]]
    if len(pressure) < 10 or len(flow) < 10 or not wflow:
        return []

    smoothed = []
    for i, (_, y) in enumerate(pressure):
        if i == 0 or i == len(pressure) - 1:
            smoothed.append(y)
        else:
            smoothed.append((pressure[i - 1][1] + y + pressure[i + 1][1]) / 3.0)

    segments, current = [], []
    wf_cursor = 0
    mf_cursor = 1

    def close():
        nonlocal current
        if current:
            segments.append(current)
            current = []

    for i in range(1, len(pressure)):
        t, p = pressure[i]
        dt = t - pressure[i - 1][0]
        if dt <= 0:
            continue
        dpdt = abs(smoothed[i] - smoothed[i - 1]) / dt
        if t < MIN_WINDOW_START:
            continue
        if dpdt > MAX_DPDT or p < MIN_PRESSURE:
            close()
            continue

        wf, nearest = 0.0, 1e9
        for k in range(wf_cursor, len(wflow)):
            d = abs(wflow[k][0] - t)
            if d < nearest:
                nearest, wf, wf_cursor = d, wflow[k][1], k
            else:
                break
        if nearest > MAX_SCALE_GAP or wf < MIN_WEIGHT_FLOW:
            close()
            continue

        mf = 0.0
        for j in range(mf_cursor, len(flow)):
            if flow[j][0] >= t:
                t0, t1 = flow[j - 1][0], flow[j][0]
                if t1 - t0 > 0:
                    frac = (t - t0) / (t1 - t0)
                    mf = flow[j - 1][1] + frac * (flow[j][1] - flow[j - 1][1])
                else:
                    mf = flow[j][1]
                mf_cursor = j
                break
        if mf < MIN_MACHINE_FLOW:
            close()
            continue

        ratio = mf / wf
        if ratio > MAX_SAMPLE_RATIO or ratio < MIN_SAMPLE_RATIO:
            close()
            continue

        current.append((t, mf, wf))

    close()
    return segments


def window_stats(samples):
    n = len(samples)
    mf = sum(s[1] for s in samples) / n
    wf = sum(s[2] for s in samples) / n
    return {
        "start": samples[0][0],
        "end": samples[-1][0],
        "duration": samples[-1][0] - samples[0][0],
        "samples": n,
        "meanMachineFlow": mf,
        "meanWeightFlow": wf,
        "flowSpread": (max(s[1] for s in samples) - min(s[1] for s in samples)) / mf,
        "weightSpread": (max(s[2] for s in samples) - min(s[2] for s in samples)) / wf,
    }


def pick_current(segments):
    """Longest qualifying window — today's rule."""
    best = None
    for seg in segments:
        if len(seg) < MIN_WINDOW_SAMPLES:
            continue
        w = window_stats(seg)
        if w["duration"] < MIN_WINDOW_DURATION:
            continue
        if best is None or w["duration"] > best["duration"]:
            best = w
    return best


def pick_candidate(segments):
    """Longest window that is also FLAT in both lines."""
    best = None
    for seg in segments:
        n = len(seg)
        for i in range(n):
            for j in range(n, i + MIN_WINDOW_SAMPLES - 1, -1):
                sub = seg[i:j]
                if len(sub) < MIN_WINDOW_SAMPLES:
                    break
                w = window_stats(sub)
                if w["duration"] < CANDIDATE_MIN_DURATION:
                    break
                if w["flowSpread"] > MAX_FLOW_SPREAD or w["weightSpread"] > MAX_WEIGHT_SPREAD:
                    continue
                if best is None or w["duration"] > best["duration"]:
                    best = w
                break  # longest flat window starting at i
    return best


def frame_target(shot, window):
    """Target flow of the frame the window sits in, or None if not flow-controlled."""
    phases = shot.get("phases", [])
    profile = json.loads(shot["profileJson"]) if shot.get("profileJson") else {}
    steps = profile.get("steps", [])
    mid = (window["start"] + window["end"]) / 2.0

    # Export shots carry no phase markers, but they do carry the goal series,
    # which says directly whether the machine was flow-controlled at that moment.
    if not phases and shot.get("flowGoal"):
        goal = 0.0
        for pt in shot["flowGoal"]:
            if pt["x"] <= mid:
                goal = pt["y"]
        if goal < MIN_MACHINE_FLOW:
            return None
        for step in steps:
            if str(step.get("pump", "")).lower() == "flow":
                try:
                    return float(step.get("flow", 0))
                except (TypeError, ValueError):
                    return None
        return None

    frame = None
    for ph in phases:
        if ph["time"] <= mid:
            frame = ph
    if frame is None:
        return None
    idx = frame["frameNumber"]
    if idx >= len(steps):
        return None
    step = steps[idx]
    if str(step.get("pump", "")).lower() != "flow":
        return None
    try:
        return float(step.get("flow", 0))
    except (TypeError, ValueError):
        return None


def ideal_current(window, target, factor):
    """Today's formula choice, including the #1823 reclassification."""
    if target:
        deviation = (target - window["meanMachineFlow"]) / target
        if deviation > DEVIATION_THRESHOLD:
            r = window["meanMachineFlow"] / window["meanWeightFlow"]
            if r > MAX_WINDOW_RATIO or r < MIN_WINDOW_RATIO:
                return None, "reject: window ratio %.3f" % r
            return (factor * window["meanWeightFlow"]
                    / (window["meanMachineFlow"] * WATER_DENSITY)), "reclassified"
        r = (target * WATER_DENSITY) / window["meanWeightFlow"]
        if r > MAX_WINDOW_RATIO or r < MIN_WINDOW_RATIO:
            return None, "reject: flow profile ratio %.3f" % r
        return window["meanWeightFlow"] / (target * WATER_DENSITY), "flow"
    r = window["meanMachineFlow"] / window["meanWeightFlow"]
    if r > MAX_WINDOW_RATIO or r < MIN_WINDOW_RATIO:
        return None, "reject: window ratio %.3f" % r
    return (factor * window["meanWeightFlow"]
            / (window["meanMachineFlow"] * WATER_DENSITY)), "pressure"


def ideal_candidate(window, target, factor):
    """Flat window + must be at the frame's operating point, else skip."""
    if target:
        deviation = abs(target - window["meanMachineFlow"]) / target
        if deviation > TARGET_TOLERANCE:
            return None, "skip: off target by %.1f%%" % (deviation * 100)
        r = (target * WATER_DENSITY) / window["meanWeightFlow"]
        if r > MAX_WINDOW_RATIO or r < MIN_WINDOW_RATIO:
            return None, "reject: flow profile ratio %.3f" % r
        return window["meanWeightFlow"] / (target * WATER_DENSITY), "flow"
    r = window["meanMachineFlow"] / window["meanWeightFlow"]
    if r > MAX_WINDOW_RATIO or r < MIN_WINDOW_RATIO:
        return None, "reject: window ratio %.3f" % r
    return (factor * window["meanWeightFlow"]
            / (window["meanMachineFlow"] * WATER_DENSITY)), "pressure"


BATCH_SIZE = 5
EMA_ALPHA = 0.5
CHANGE_THRESHOLD = 0.03


def replay(shots, start_c, picker, scorer, label):
    """Walk the shots in order, feeding ideals into the batch/median/EMA update."""
    c = start_c
    pending = []
    rows = []
    updates = []
    for shot in shots:
        window = picker(qualifying_samples(shot))
        if window is None:
            rows.append((shot["id"], None, None, "no qualifying window", c))
            continue
        target = frame_target(shot, window)
        ideal, mode = scorer(window, target, c)
        rows.append((shot["id"], window, ideal, mode, c))
        if ideal is None:
            continue
        pending.append(max(0.5, min(2.7, ideal)))
        if len(pending) < BATCH_SIZE:
            continue
        ordered = sorted(pending)
        n = len(ordered)
        median = (ordered[n // 2 - 1] + ordered[n // 2]) / 2.0 if n % 2 == 0 else ordered[n // 2]
        new_c = EMA_ALPHA * median + (1 - EMA_ALPHA) * c
        if abs(new_c - c) / c > CHANGE_THRESHOLD:
            updates.append((shot["id"], c, new_c, median))
            c = new_c
        else:
            updates.append((shot["id"], c, c, median))
        pending = []
    return rows, updates, c, pending


def main(paths, start_c):
    shots = sorted((load_shot(p) for p in paths), key=lambda s: s["timestampIso"])

    cur_rows, cur_updates, cur_c, cur_pending = replay(
        shots, start_c, pick_current, ideal_current, "current")
    cand_rows, cand_updates, cand_c, cand_pending = replay(
        shots, start_c, pick_candidate, ideal_candidate, "candidate")

    print("%-6s %-11s | %-26s %-7s %-11s | %-26s %-7s %-11s" % (
        "shot", "date", "CURRENT window", "ideal", "mode",
        "CANDIDATE window", "ideal", "mode"))
    print("-" * 130)
    for (sid, w1, i1, m1, _), (_, w2, i2, m2, _) in zip(cur_rows, cand_rows):
        date = next(str(s["timestampIso"])[5:16] for s in shots if s["id"] == sid)

        def cell(w, i, m):
            if w is None:
                return "%-26s %-7s %-11s" % ("(none)", "-", m[:11])
            desc = "%.1f-%.1fs n=%-3d mf=%.2f" % (
                w["start"], w["end"], w["samples"], w["meanMachineFlow"])
            return "%-26s %-7s %-11s" % (desc, ("%.3f" % i) if i else "-", m[:11])

        print("%-6s %-11s | %s | %s" % (sid, date, cell(w1, i1, m1), cell(w2, i2, m2)))

    for name, updates, final, pending in (("CURRENT", cur_updates, cur_c, cur_pending),
                                          ("CANDIDATE", cand_updates, cand_c, cand_pending)):
        print("\n%s: start C=%.4f" % (name, start_c))
        for sid, before, after, median in updates:
            verb = "->" if abs(after - before) > 1e-9 else "== (under 3%, held)"
            print("  batch closed at shot %s: median %.4f  C %.4f %s %.4f"
                  % (sid, median, before, verb, after))
        print("  final C=%.4f  (%d ideal(s) pending in an unfinished batch)"
              % (final, len(pending)))


if __name__ == "__main__":
    start = float(sys.argv[1])
    main(sys.argv[2:], start)
