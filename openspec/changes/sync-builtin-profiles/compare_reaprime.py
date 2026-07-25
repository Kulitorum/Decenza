#!/usr/bin/env python3
"""Compare Decenza's built-in profiles against reaprime's bundled set.

This is the script that produced audit.md. It exists because the `--reaprime` leg
of tools/profile_sync.cpp was deliberately not built (design D3) — so re-running
the comparison is a manual step, and without this file that step would be a
re-derivation rather than a re-run.

    python3 compare_reaprime.py <decenza_profiles_dir> <reaprime_defaultProfiles_dir>

Pin the reaprime side to a commit rather than a working tree, or the numbers are
not reproducible:

    git -C <reaprime> archive <commit> assets/defaultProfiles | tar -x -C /tmp/rea
    python3 compare_reaprime.py resources/profiles /tmp/rea/assets/defaultProfiles

EQUIVALENCE is machine-observable, per the spec requirement "Cross-app profile
equivalence is machine-observable": absent/""/0 are one value, the axis a frame's
pump does not drive is ignored (the DE1 ignores it too), a zero-value limiter
equals an absent one, and numbers compare numerically. A structural diff instead
reports 55 of 63 differing and buries the real findings under encoding noise.
"""
import json, glob, os, sys, collections

# de1app profile types whose frames are DERIVED from scalars, not read from the
# stored advanced_shot array. de1app's save_profile writes that array out of the
# GLOBAL ::settings, so for these types it routinely describes another profile.
LEGACY_TYPES = {"settings_2a", "settings_2b"}

# Scalars that decide when the shot STOPS. Frames alone are not the comparison:
# two copies can carry identical frames and still stop at different weights.
SHOT_SCALARS = ["target_weight", "target_volume", "target_volume_count_start",
                "tank_temperature", "beverage_type", "type"]


def load(d):
    out = {}
    for p in sorted(glob.glob(os.path.join(d, "*.json"))):
        if os.path.basename(p) == "manifest.json":
            continue
        j = json.load(open(p))
        title = j.get("title")
        if title is None:
            print(f"  !! no title: {p}", file=sys.stderr)
            continue
        if title in out:
            # Titles are the join key (design D2 — filenames differ between apps),
            # so a collision makes the comparison ambiguous rather than merely odd.
            print(f"  !! DUPLICATE TITLE {title!r} in {d}", file=sys.stderr)
        out[title] = (os.path.basename(p), j)
    return out


def num(v):
    if v is None or v == "":
        return 0.0
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def same(a, b, tol=1e-3):
    na, nb = num(a), num(b)
    if na is not None and nb is not None:
        return abs(na - nb) <= tol
    return (a or None) == (b or None)


def norm_frame(s):
    """A frame reduced to what the DE1 acts on."""
    pump = (s.get("pump") or "").lower()
    f = {"name": s.get("name"), "pump": pump,
         "transition": s.get("transition"), "sensor": s.get("sensor")}
    for k in ("temperature", "seconds", "volume", "weight"):
        f[k] = round(num(s.get(k)) or 0.0, 3)
    # The pump drives one axis; the other is ignored by the machine, so the two
    # apps' differing encodings of it ("" vs 0.00) are not a divergence.
    f["flow"] = round(num(s.get("flow")) or 0.0, 3) if pump == "flow" else None
    f["pressure"] = round(num(s.get("pressure")) or 0.0, 3) if pump == "pressure" else None
    e = s.get("exit")
    f["exit"] = None if not e else (e.get("type"), e.get("condition"),
                                    round(num(e.get("value")) or 0.0, 3))
    lim = s.get("limiter") or {}
    lv = round(num(lim.get("value")) or 0.0, 3)
    f["limiter"] = None if lv == 0 else (lv, round(num(lim.get("range")) or 0.0, 3))
    return f


def compare(dec, rea):
    common = sorted(set(dec) & set(rea))
    rows, encoding = [], collections.Counter()
    for t in common:
        dj, rj = dec[t][1], rea[t][1]
        ds, rs = dj.get("steps", []), rj.get("steps", [])

        # Encoding-class tally: what a structural diff would have called a difference.
        for a, b in zip(ds, rs):
            if (a.get("weight") is None) != (b.get("weight") is None) and \
               same(a.get("weight"), b.get("weight")):
                encoding["omitted zero weight"] += 1
            if bool(a.get("limiter")) != bool(b.get("limiter")):
                encoding["zero-value limiter"] += 1
            for axis in ("flow", "pressure"):
                if (a.get(axis) == "") != (b.get(axis) == ""):
                    encoding["inactive-axis encoding"] += 1

        scalars = [(k, dj.get(k), rj.get(k)) for k in SHOT_SCALARS
                   if not same(dj.get(k), rj.get(k))]
        if len(ds) != len(rs):
            rows.append((t, "frame-count", f"{len(ds)} vs {len(rs)}", scalars))
            continue
        diffs = [(i, k, norm_frame(a)[k], norm_frame(b)[k])
                 for i, (a, b) in enumerate(zip(ds, rs))
                 for k in norm_frame(a) if norm_frame(a)[k] != norm_frame(b)[k]]
        if diffs:
            rows.append((t, "frame-value", diffs, scalars))
        elif scalars:
            rows.append((t, "scalar-only", None, scalars))
        else:
            rows.append((t, "equivalent", None, []))
    return common, rows, encoding


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    dec, rea = load(sys.argv[1]), load(sys.argv[2])
    common, rows, encoding = compare(dec, rea)

    print(f"Decenza titles : {len(dec)}")
    print(f"reaprime titles: {len(rea)}")
    print(f"common         : {len(common)}   "
          f"(Decenza-only {len(set(dec)-set(rea))}, reaprime-only {len(set(rea)-set(dec))})")

    tally = collections.Counter(v for _, v, _, _ in rows)
    print("\nVERDICTS:", dict(tally))
    print("\nENCODING CLASSES (rows a structural diff would have flagged):")
    for k, v in encoding.most_common():
        print(f"   {k:26} {v}")

    print("\nDIVERGENT:")
    for t, verdict, detail, scalars in rows:
        if verdict == "equivalent":
            continue
        legacy = dec[t][1].get("legacy_profile_type")
        print(f"\n  {t!r}  [{verdict}]  de1app type: {legacy}")
        if verdict == "frame-count":
            print(f"      frames: decenza {detail}")
        elif verdict == "frame-value":
            for i, k, a, b in detail[:8]:
                print(f"      frame {i} {k}: decenza={a!r} reaprime={b!r}")
        for k, a, b in scalars:
            print(f"      scalar {k}: decenza={a!r} reaprime={b!r}")

    print("\nEQUIVALENT (%d):" % tally["equivalent"])
    for t, v, _, _ in rows:
        if v == "equivalent":
            print("   ", t)
    return 0


if __name__ == "__main__":
    sys.exit(main())
