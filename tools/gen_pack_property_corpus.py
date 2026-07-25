#!/usr/bin/env python3
"""Generate a randomised profile corpus and pack it with de1app's own packer.

The byte-for-byte test in tst_recipeeditorparity compares eight real profiles.
Eight profiles is not the quantisation space: they exercise the values their
authors happened to choose, which cluster on round numbers and miss every
encoder boundary. This generates profiles that deliberately sit ON those
boundaries, packs each through de1app's real `de1_packed_shot`, and commits the
result as goldens so the C++ side can diff without a Tcl interpreter.

Encoders under stress, and why each boundary matters:

  U8P4    setpoints, exit thresholds, limiters. 1/16 steps, so x.03125 offsets
          land exactly on a rounding tie.
  U8P1    temperature. 1/10 steps; .x5 values are ties.
  F8_1_7  frame duration. A custom split format: below 12.75 it stores
          round(v*10); at or above, round(v) with bit 7 set. The switchover is
          the single most likely place for two implementations to disagree, and
          no stock profile sits near it.
  U10P0   volume. Integer with a bit-10 marker; 1023/1024 is the wrap point.

The first two profiles are PINNED: their frames carry the critical boundary
values explicitly, one per frame, so coverage of the switchover and the rounding
ties is guaranteed by construction rather than by the seed happening to draw
them. The rest are random. That distinction matters at this corpus size — with a
hundred-odd profiles you can trust the dice, with two dozen you cannot, and the
whole point is the boundaries.

Deterministic: fixed seed, so re-running reproduces the corpus exactly and a
diff in the goldens means a real change in de1app, not churn.

Usage:  python3 tools/gen_pack_property_corpus.py <de1plus-dir> [count]
"""

import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "tests", "data", "pack_property")
ORACLE = os.path.join(HERE, "de1app_pack_oracle.tcl")

SEED = 20260725


def boundary_values(rng, lo, hi, step):
    """A value chosen to stress `step` quantisation: on a tie, just under, just
    over, or plain random. Ties are where two rounding implementations diverge."""
    kind = rng.randrange(4)
    n = rng.uniform(lo, hi)
    if kind == 0:                      # exact multiple
        return round(round(n / step) * step, 6)
    if kind == 1:                      # exact tie (half a step up)
        return round(round(n / step) * step + step / 2, 6)
    if kind == 2:                      # a hair below a tie
        return round(round(n / step) * step + step / 2 - 1e-4, 6)
    return round(n, 4)                 # unconstrained


def duration(rng):
    """Frame seconds, weighted onto the F8_1_7 switchover at 12.75."""
    kind = rng.randrange(6)
    if kind == 0:
        return rng.choice([12.74, 12.75, 12.76, 12.7, 12.8])
    if kind == 1:
        return rng.choice([0, 0.1, 0.05, 1.0, 127.0, 126.9, 127.4])
    if kind == 2:
        return round(rng.uniform(0, 12.75), 2)
    if kind == 3:
        return round(rng.uniform(12.75, 127), 2)
    return round(rng.uniform(0, 60), 1)


def volume(rng):
    return rng.choice([0, 1, 100, 1022, 1023, rng.randrange(0, 1024)])


def make_frame(rng, idx):
    pump = rng.choice(["flow", "pressure"])
    exit_if = rng.randrange(2)
    exit_type = rng.choice(["pressure_over", "pressure_under", "flow_over", "flow_under"])
    f = {
        "name": "F%d" % idx,
        "temperature": boundary_values(rng, 20, 105, 0.1),
        "sensor": rng.choice(["coffee", "water"]),
        "pump": pump,
        "transition": rng.choice(["fast", "smooth"]),
        "pressure": boundary_values(rng, 0, 12, 1 / 16),
        "flow": boundary_values(rng, 0, 10, 1 / 16),
        "seconds": duration(rng),
        "volume": volume(rng),
        "weight": rng.choice([0, round(rng.uniform(0, 10), 1)]),
        "exit_if": exit_if,
        "exit_type": exit_type,
        "exit_pressure_over": boundary_values(rng, 0, 12, 1 / 16),
        "exit_pressure_under": boundary_values(rng, 0, 12, 1 / 16),
        "exit_flow_over": boundary_values(rng, 0, 10, 1 / 16),
        "exit_flow_under": boundary_values(rng, 0, 10, 1 / 16),
        "max_flow_or_pressure": rng.choice([0, boundary_values(rng, 0, 12, 1 / 16)]),
        "max_flow_or_pressure_range": boundary_values(rng, 0, 2, 1 / 16),
    }
    return f


def frame_to_tcl(f):
    parts = []
    for k, v in f.items():
        parts.append("%s %s" % (k, v))
    return "{" + " ".join(parts) + "}"


# --- pinned boundary frames -------------------------------------------------
# One boundary per frame, so a divergence names itself. A DE1 profile takes at
# most 20 frames; ten each keeps both well inside that.

def _frame(idx, **over):
    """A neutral frame with every field at a safe value, overridden per case."""
    f = {
        "name": "P%d" % idx, "temperature": 90.0, "sensor": "coffee",
        "pump": "flow", "transition": "fast", "pressure": 6.0, "flow": 2.0,
        "seconds": 10.0, "volume": 0, "weight": 0, "exit_if": 0,
        "exit_type": "pressure_over", "exit_pressure_over": 0,
        "exit_pressure_under": 0, "exit_flow_over": 0, "exit_flow_under": 0,
        "max_flow_or_pressure": 0, "max_flow_or_pressure_range": 0.6,
    }
    f.update(over)
    return f


def pinned_profiles():
    """Two profiles whose frames sit exactly on the encoder boundaries."""
    # F8_1_7 duration: the split format's switchover at 12.75, plus the ends.
    durations = [0, 0.05, 0.1, 12.7, 12.74, 12.75, 12.76, 12.8, 126.9, 127.0]
    a = [_frame(i, seconds=d) for i, d in enumerate(durations)]

    # U8P4 (1/16 steps) ties at +1/32; U8P1 (1/10) ties at .x5; U10P0 wrap.
    u8p4_ties = [0.03125, 0.09375, 1.03125, 6.03125, 9.03125, 12.03125,
                 15.90625, 0.5, 2.53125, 7.96875]
    u8p1_ties = [20.05, 32.45, 55.55, 78.85, 90.05, 93.05, 96.55, 99.95,
                 104.95, 105.0]
    volumes   = [0, 1, 99, 100, 512, 1021, 1022, 1023, 777, 256]
    b = [_frame(i, pressure=p, temperature=t, volume=v,
                max_flow_or_pressure=p, exit_pressure_over=p, exit_flow_over=p,
                pump="pressure")
         for i, (p, t, v) in enumerate(zip(u8p4_ties, u8p1_ties, volumes))]
    return [a, b]


def profile_text(frames, i):
    lines = [
        "advanced_shot {%s}" % " ".join(frame_to_tcl(f) for f in frames),
        "profile_title {PropTest %03d}" % i,
        "settings_profile_type settings_2c",
        "author property-test",
        "beverage_type espresso",
        "espresso_temperature %s" % frames[0]["temperature"],
        "final_desired_shot_weight_advanced 36",
        "final_desired_shot_volume_advanced_count_start 0",
        "maximum_flow_range_advanced 0.6",
        "maximum_pressure_range_advanced 0.6",
        "tank_desired_water_temperature 0",
    ]
    return "\n".join(lines) + "\n"


def make_profile(rng, i):
    nframes = rng.randrange(1, 11)          # DE1 accepts up to 20; 1..10 is plenty
    frames = [make_frame(rng, n) for n in range(nframes)]
    lines = [
        "advanced_shot {%s}" % " ".join(frame_to_tcl(f) for f in frames),
        "profile_title {PropTest %03d}" % i,
        "settings_profile_type settings_2c",
        "author property-test",
        "beverage_type espresso",
        "espresso_temperature %s" % frames[0]["temperature"],
        "final_desired_shot_weight_advanced 36",
        "final_desired_shot_volume_advanced_count_start %d" % rng.randrange(0, nframes + 1),
        "maximum_flow_range_advanced 0.6",
        "maximum_pressure_range_advanced 0.6",
        "tank_desired_water_temperature 0",
    ]
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    de1plus = os.path.expanduser(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 24

    os.makedirs(OUT, exist_ok=True)
    for stale in os.listdir(OUT):
        if stale.endswith((".tcl", ".txt")):
            os.remove(os.path.join(OUT, stale))

    rng = random.Random(SEED)
    pinned = pinned_profiles()
    ok = 0
    for i in range(count):
        name = "prop_%03d" % i
        tcl_path = os.path.join(OUT, name + ".tcl")
        with open(tcl_path, "w") as fh:
            # The first two are the explicit boundary profiles; see above.
            fh.write(profile_text(pinned[i], i) if i < len(pinned)
                     else make_profile(rng, i))

        res = subprocess.run(["tclsh", ORACLE, de1plus, tcl_path],
                             capture_output=True, text=True)
        if res.returncode != 0 or not res.stdout.strip():
            # A profile de1app itself cannot pack is not a useful fixture.
            print("  skip %s: %s" % (name, res.stderr.strip().splitlines()[:1]))
            os.remove(tcl_path)
            continue
        with open(os.path.join(OUT, name + ".txt"), "w") as fh:
            fh.write(res.stdout)
        ok += 1

    print("generated %d/%d profiles with de1app goldens in %s" % (ok, count, OUT))


if __name__ == "__main__":
    main()
