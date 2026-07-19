#!/usr/bin/env python3
"""Measure the warning backlog on one platform, per diagnostic class.

Re-runs every first-party translation unit from a Ninja compdb with
-fsyntax-only under a chosen warning set, and tallies diagnostic classes
([-Wfoo]) with the number of distinct files each appears in. Third-party
(_deps) TUs are excluded: the warning options in CMakeLists.txt sit after the
FetchContent blocks, so third-party code never inherits them.

This is what produced the per-flag counts quoted in CMakeLists.txt, and it is
committed so those numbers can be re-derived rather than taken on trust. Every
candidate flag is passed in ONE sweep — the class name in each diagnostic says
which flag fired, so N flags cost one pass, not N.

Not a CI check, and deliberately not wired to one: a warning count reported
into a log has no forcing function. Enforcement is -Werror in CMakeLists.txt.

Two things this gets right that a naive version does not, both learned the hard
way on this branch:

  - A TU that FAILS TO COMPILE emits no warnings, and a version that only
    scraped stderr for "warning:" scored it identically to a clean TU. That is
    how a candidate flag was once measured at "0 hits across 567 TUs" when the
    truth was that the flag did not exist on that compiler and every TU had
    died on the command line. Compile failures are counted separately, printed
    loudly, and make the run exit non-zero.

  - A flag one compiler lacks (-Wshorten-64-to-32 is clang-only;
    -Wrange-loop-construct is GCC's) poisons the whole sweep if passed blind.
    Each candidate is probed for acceptance first, and the unsupported ones are
    reported by name instead of silently skewing every count to zero.

Usage:
    ninja -C <build-dir> -t compdb > /tmp/cdb.json
    python3 scripts/measure_warnings.py /tmp/cdb.json 8 /tmp/warning-lines.txt
    python3 scripts/measure_warnings.py /tmp/cdb.json 8 /tmp/lines.txt \\
        -Wshorten-64-to-32 -Wsuggest-override -Wextra-semi

Args: <compdb.json> <parallel-jobs> <output-file> [extra warning flags...]
Exit: 0 if every TU compiled, 1 if any failed (counts are then incomplete).
"""
import json, re, shlex, subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor

CDB = sys.argv[1]
JOBS = int(sys.argv[2])
OUT = sys.argv[3]
EXTRA_FLAGS = sys.argv[4:]

BASE_FLAGS = ["-Wall", "-Wextra"]
COMPILERS = ("clang++", "clang", "c++", "cc", "gcc", "g++")
STRIP_NEXT = {"-o", "-MT", "-MF"}
DROP = {"-c", "-MD", "-MMD"}

entries = json.load(open(CDB))


def usable(e):
    cmd = e.get("command", "").strip()
    f = e.get("file", "")
    if not cmd or not f:
        return False
    if not f.endswith((".cpp", ".cc", ".cxx", ".c", ".mm", ".m")):
        return False
    if "/_deps/" in f or "/_deps/" in e.get("directory", ""):
        return False
    try:
        head = shlex.split(cmd)[0]
    except ValueError:
        return False
    return head.endswith(COMPILERS)


tus = [e for e in entries if usable(e)]
# One entry per source file (autogen TUs repeat across configs)
seen, uniq = set(), []
for e in tus:
    if e["file"] not in seen:
        seen.add(e["file"])
        uniq.append(e)
if not uniq:
    sys.exit("No first-party TUs found in the compdb — wrong build dir?")
print(f"{len(uniq)} unique first-party TUs (of {len(entries)} compdb entries)",
      file=sys.stderr)


def base_command(e):
    """The compdb command with output/dependency/source args stripped."""
    args, out, skip = shlex.split(e["command"]), [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in STRIP_NEXT:
            skip = True
            continue
        if a in DROP or a == e["file"]:
            continue
        out.append(a)
    return out


def probe(flag):
    """True if this compiler accepts `flag`, measured on one real TU.

    -Werror is essential: without it clang merely warns about an unknown -W
    option and exits 0, which would let an inert flag through and report every
    count as zero.
    """
    e = uniq[0]
    cmd = base_command(e) + BASE_FLAGS + [flag, "-Werror", "-fsyntax-only",
                                          e["file"]]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           cwd=e["directory"], timeout=300)
    except Exception:  # noqa: BLE001 - measurement script
        return False
    # The TU may legitimately warn under `flag`, which -Werror turns into a
    # failure. Only an unknown/unsupported *option* disqualifies it.
    return not re.search(r"(unknown warning option|unrecognized command[- ]line "
                         r"option|invalid argument).*" + re.escape(flag),
                         r.stderr) and "-Wunknown-warning-option" not in r.stderr


if EXTRA_FLAGS:
    supported = [f for f in EXTRA_FLAGS if probe(f)]
    rejected = [f for f in EXTRA_FLAGS if f not in supported]
    if rejected:
        print(f"NOT SUPPORTED by this compiler, excluded from the sweep: "
              f"{' '.join(rejected)}", file=sys.stderr)
    if not supported:
        sys.exit("None of the requested flags are supported here — nothing to "
                 "measure. Counts from this compiler would all be zero and "
                 "would mean nothing.")
else:
    supported = []

SWEEP_FLAGS = BASE_FLAGS + supported
print(f"sweeping with: {' '.join(SWEEP_FLAGS)}", file=sys.stderr)


def run(e):
    cmd = base_command(e) + SWEEP_FLAGS + [
        "-fsyntax-only", "-fno-caret-diagnostics", "-fno-color-diagnostics",
        e["file"]]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           cwd=e["directory"], timeout=300)
    except Exception as ex:  # noqa: BLE001 - measurement script
        return e["file"], "", f"sweep error: {ex}"
    # -fsyntax-only exits 0 on a TU that merely warns, so a non-zero code here
    # means the TU did not compile and contributed no warnings to any count.
    if r.returncode != 0:
        first = next((l for l in r.stderr.splitlines() if "error:" in l),
                     r.stderr.strip().splitlines()[:1] or ["(no stderr)"])
        return e["file"], "", first if isinstance(first, str) else first[0]
    return e["file"], r.stderr, None


classes = collections.Counter()
per_file = collections.defaultdict(set)
lines, failures = [], []
with ThreadPoolExecutor(max_workers=JOBS) as ex:
    for fname, err, failure in ex.map(run, uniq):
        if failure is not None:
            failures.append((fname, failure))
            continue
        for m in re.finditer(r"warning: .*?\[(-W[\w#=+-]+)\]", err):
            classes[m.group(1)] += 1
            per_file[m.group(1)].add(fname)
        lines += [l for l in err.splitlines() if "warning:" in l]

compiled = len(uniq) - len(failures)
print("\n== Diagnostic classes: count, distinct files ==")
for cls, n in classes.most_common():
    print(f"{n:6d}  {len(per_file[cls]):4d} files  {cls}")
print(f"\ntotal warnings: {sum(classes.values())}  classes: {len(classes)}")
print(f"TUs compiled: {compiled}/{len(uniq)}")

open(OUT, "w").write("\n".join(lines))

if failures:
    print(f"\n{len(failures)} TU(s) FAILED TO COMPILE. Every count above is a "
          f"lower bound — these files contributed nothing:", file=sys.stderr)
    for fname, why in failures[:20]:
        print(f"  {fname}\n      {why.strip()}", file=sys.stderr)
    if len(failures) > 20:
        print(f"  ... and {len(failures) - 20} more", file=sys.stderr)
    sys.exit(1)
