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

It also emits two BOUNDARY profiles into tests/data/pack_property/. Those carry
the encoder edges explicitly, one per frame — the F8_1_7 switchover at 12.75, the
U8P4 and U8P1 rounding ties, the U10P0 wrap — because no real profile sits on
them and tst_binarycodec only reaches the codec, not the whole-profile pack.

This replaced a 120-profile randomised "property corpus". That corpus found
nothing in its lifetime, and its whole-profile-assembly coverage duplicated what
the 89 stock goldens already provide on real values. The two pinned profiles are
the only part that covered something nothing else did.

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


def frame_to_tcl(f):
    return "{" + " ".join("%s %s" % (k, v) for k, v in f.items()) + "}"


def profile_text(frames, i):
    lines = [
        "advanced_shot {%s}" % " ".join(frame_to_tcl(f) for f in frames),
        "profile_title {Boundary %03d}" % i,
        "settings_profile_type settings_2c",
        "author boundary-fixture",
        "beverage_type espresso",
        "espresso_temperature %s" % frames[0]["temperature"],
        "final_desired_shot_weight_advanced 36",
        "final_desired_shot_volume_advanced_count_start 0",
        "maximum_flow_range_advanced 0.6",
        "maximum_pressure_range_advanced 0.6",
        "tank_desired_water_temperature 0",
    ]
    return "\n".join(lines) + "\n"


def write_boundary_profiles(de1plus):
    out = os.path.join(REPO, "tests", "data", "pack_property")
    os.makedirs(out, exist_ok=True)
    for stale in os.listdir(out):
        if stale.endswith((".tcl", ".txt")):
            os.remove(os.path.join(out, stale))
    for i, frames in enumerate(pinned_profiles()):
        name = "prop_%03d" % i
        tcl = os.path.join(out, name + ".tcl")
        with open(tcl, "w") as fh:
            fh.write(profile_text(frames, i))
        res = subprocess.run(["tclsh", ORACLE, de1plus, tcl],
                             capture_output=True, text=True)
        if res.returncode != 0 or not res.stdout.strip():
            raise SystemExit("boundary profile %s failed to pack: %s"
                             % (name, res.stderr.strip()))
        with open(os.path.join(out, name + ".txt"), "w") as fh:
            fh.write(res.stdout)
    print("boundary profiles: %d written" % len(pinned_profiles()))


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

    write_boundary_profiles(de1plus)
    print("de1app pack corpus: %d goldens written, %d failed (of %d profiles)"
          % (written, failed, len(profiles)))
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
