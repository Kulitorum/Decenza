#!/usr/bin/env python3
"""Compare Decenza's shipped built-in profiles against what de1app itself builds.

    python3 tools/de1app_oracle/compare_builtins.py <de1app-checkout> [--verbose]

For every .tcl in tests/data/de1app_profiles/, this runs de1app's OWN frame
builders (via de1app_frames.tcl, which sources de1app's profile.tcl verbatim)
and diffs the result against resources/profiles/<name>.json.

Why this exists, when profile_sync already compares against the .tcl: the .tcl
is the INPUT to de1app, not its output. For a simple profile de1app throws the
stored advanced_shot away and rebuilds the frames from the scalars, so the only
way to know what de1app actually brews is to run its builders. Doing that found
three divergences the whole C++ suite missed, including two profiles we brewed
6 C colder than de1app did.

Differences are split by whether the DE1 acts on them:

  SHOT      the machine does something different - a real portability break
  INACTIVE  the axis the frame's pump does not use (flow on a pressure frame).
            The DE1 ignores it, so the coffee is identical, but a byte-level
            reader still sees a value de1app never wrote.

Exit status is 1 if any SHOT difference is found, so this can gate a sync.
"""
import json
import os
import re
import subprocess
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ORACLE = os.path.join(HERE, "de1app_frames.tcl")
CORPUS = os.path.join(ROOT, "tests", "data", "de1app_profiles")
BUILTINS = os.path.join(ROOT, "resources", "profiles")

# Values within this of each other encode identically at our precision.
EPS = 0.051


def title_to_filename(title):
    """Mirror of Profile::titleToFilename (src/profile/profile.cpp)."""
    decomposed = unicodedata.normalize("NFD", title)
    decomposed = "".join(c for c in decomposed if not 0x300 <= ord(c) <= 0x36F)
    out = "".join(c.lower() if c.isalnum() else "_" for c in decomposed)
    while "__" in out:
        out = out.replace("__", "_")
    return out.strip("_")


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def de1app_frames(de1app, tcl_path):
    """Run de1app's builders. Returns (frames, count_start) or raises."""
    r = subprocess.run([
        "tclsh", ORACLE, de1app, tcl_path,
    ], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr.strip().splitlines()[0] if r.stderr else "tclsh failed")
    frames, count_start = [], None
    for line in r.stdout.splitlines():
        if line.startswith("COUNTSTART\t"):
            count_start = line.split("\t")[1]
        elif line.startswith("FRAME\t"):
            frames.append(dict(kv.split("=", 1) for kv in line.split("\t")[1:]))
    return frames, count_start


def compare(theirs, ours, src):
    """Diff de1app's frames against our JSON. Returns (shot, inactive) lists."""
    frames, count_start = theirs
    steps = ours.get("steps", [])
    shot, inactive = [], []

    if len(frames) != len(steps):
        shot.append("frame count: de1app=%d ours=%d" % (len(frames), len(steps)))

    # settings_to_advanced_list does not derive the count, so for advanced
    # profiles de1app uses the literal from the file.
    if not count_start:
        m = re.search(r"^final_desired_shot_volume_advanced_count_start (\S+)", src, re.M)
        count_start = m.group(1) if m else "0"
    if num(count_start) != num(ours.get("number_of_preinfuse_frames", "0")):
        shot.append("preinfuse count: de1app=%s ours=%s"
                    % (count_start, ours.get("number_of_preinfuse_frames")))

    for i, (a, b) in enumerate(zip(frames, steps)):
        pump = a.get("pump", "")
        if pump != b.get("pump"):
            shot.append("f%d pump: de1app=%s ours=%s" % (i, pump, b.get("pump")))
        # `name` is not sent over BLE, so it cannot change the shot — but it is
        # user-visible in shot history and in exported/Visualizer JSON, and this
        # comparison exists to prove byte-identical output, not merely
        # equivalent brewing. Omitting it hid a real regression: de1app's two
        # builders use DIFFERENT names for the boost frame ("preinfusion temp
        # boost" in pressure_to_advanced_list, "preinfusion boost" in
        # flow_to_advanced_list), and unifying them slipped past this gate.
        for f in ("name", "sensor", "transition"):
            if a.get(f, "") != b.get(f, ""):
                shot.append("f%d %s: de1app=%s ours=%s" % (i, f, a.get(f), b.get(f)))
        # One-sided presence is a REAL difference, not agreement. Requiring both
        # sides to parse before comparing is how 210 weight comparisons silently
        # never ran: our writer omits `weight` when it is zero while de1app's
        # builder always emits the key, so a dropped weight exit — the exact bug
        # ProfileFrame::knownTclKeys() exists to prevent — would read as
        # identical. An absent value is treated as 0.0, which is what both
        # formats mean by it; anything else present on one side only is drift.
        def cmp_num(raw_a, raw_b, label, bucket):
            x, y = num(raw_a), num(raw_b)
            if x is None and y is None:
                return
            if x is None or y is None:
                # Absent means zero in both formats; a non-zero value opposite
                # an absent one is a genuine difference.
                present = y if x is None else x
                if abs(present) > EPS:
                    bucket.append("f%d %s: de1app=%r ours=%r (one side absent)"
                                  % (i, label, raw_a, raw_b))
                return
            if abs(x - y) > EPS:
                bucket.append("f%d %s: de1app=%s ours=%s" % (i, label, x, y))

        for f in ("temperature", "seconds", "volume", "weight"):
            cmp_num(a.get(f), b.get(f), f, shot)

        for axis in ("pressure", "flow"):
            cmp_num(a.get(axis), b.get(axis), axis, shot if pump == axis else inactive)

        ex = b.get("exit") or {}
        if a.get("exit_if") == "1":
            raw = a.get("exit_type", "")
            kind, _, cond = raw.rpartition("_")
            if ex.get("type") != kind or ex.get("condition") != cond:
                shot.append("f%d exit: de1app=%s_%s ours=%s_%s"
                            % (i, kind, cond, ex.get("type"), ex.get("condition")))
            else:
                x, y = num(a.get("exit_%s" % raw)), num(ex.get("value"))
                if x is not None and y is not None and abs(x - y) > EPS:
                    shot.append("f%d exit value: de1app=%s ours=%s" % (i, x, y))
        elif ex:
            shot.append("f%d exit: de1app has none, ours=%s" % (i, ex))

        # Same one-sided rule for the limiter value: de1app setting a limiter we
        # drop entirely (no `limiter` object at all) is a shot difference.
        lim = b.get("limiter") or {}
        cmp_num(a.get("max_flow_or_pressure"), lim.get("value"), "limiter value", shot)
        # The RANGE only means anything while the limiter is on. de1app emits no
        # limiter object at all unless the frame carries both keys, whereas we
        # always emit one — reaprime's parser wants it, and a `value: 0` limiter
        # is how this format spells "off". Comparing the range of an off limiter
        # would report drift on nearly every profile that has no limits at all.
        limiter_on = max(abs(num(a.get("max_flow_or_pressure")) or 0.0),
                         abs(num(lim.get("value")) or 0.0)) > EPS
        if limiter_on:
            cmp_num(a.get("max_flow_or_pressure_range"), lim.get("range"), "limiter range", shot)

    return shot, inactive


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    de1app = os.path.expanduser(sys.argv[1])
    verbose = "--verbose" in sys.argv
    if not os.path.isdir(os.path.join(de1app, "de1plus")):
        print("not a de1app checkout: %s" % de1app, file=sys.stderr)
        return 2

    identical, inactive_only, shot_diff, errors, missing = 0, {}, {}, {}, []

    for tcl in sorted(f for f in os.listdir(CORPUS) if f.endswith(".tcl")):
        path = os.path.join(CORPUS, tcl)
        with open(path, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        m = re.search(r"^profile_title \{?([^}\n]*)", src, re.M)
        title = m.group(1).strip() if m else ""
        jp = os.path.join(BUILTINS, title_to_filename(title) + ".json")
        if not os.path.exists(jp):
            missing.append(tcl)
            continue
        try:
            theirs = de1app_frames(de1app, path)
        except RuntimeError as exc:
            errors[tcl] = str(exc)
            continue
        with open(jp, encoding="utf-8") as fh:
            ours = json.load(fh)

        shot, inactive = compare(theirs, ours, src)
        if shot:
            shot_diff[tcl] = shot
        elif inactive:
            inactive_only[tcl] = inactive
        else:
            identical += 1

    print("identical to de1app's own output : %d" % identical)
    print("differ only on the inactive axis : %d" % len(inactive_only))
    print("differ in ways that change the shot: %d" % len(shot_diff))
    if missing:
        print("no built-in counterpart          : %d %s" % (len(missing), missing))
    if errors:
        print("de1app builder failed            : %d" % len(errors))
        for f, e in sorted(errors.items()):
            print("  %s: %s" % (f, e))

    for label, group in (("SHOT", shot_diff), ("INACTIVE", inactive_only if verbose else {})):
        for f, rows in sorted(group.items()):
            print("\n[%s] %s" % (label, f))
            for r in rows[:12]:
                print("    " + r)

    return 1 if shot_diff or errors else 0


if __name__ == "__main__":
    sys.exit(main())
