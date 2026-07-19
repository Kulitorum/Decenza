#!/usr/bin/env python3
"""Measure the -Wall -Wextra backlog on one platform.

Re-runs every first-party translation unit from a Ninja compdb with
-Wall -Wextra -fsyntax-only and tallies diagnostic classes ([-Wfoo]).
Third-party (_deps) TUs are excluded: the warning options in CMakeLists.txt
sit after the FetchContent blocks, so third-party code never inherits them.

Not a CI check, and deliberately not wired to one — a warning count reported
into a log has no forcing function. This exists to build the initial
exemption list once per platform; enforcement is -Werror in CMakeLists.txt.

Usage:
    ninja -C <build-dir> -t compdb > /tmp/cdb.json
    python3 scripts/measure_warnings.py /tmp/cdb.json 8 /tmp/warning-lines.txt

Args: <compdb.json> <parallel-jobs> <output-file-for-raw-warning-lines>
"""
import json, re, shlex, subprocess, sys, collections
from concurrent.futures import ThreadPoolExecutor

CDB = sys.argv[1]
JOBS = int(sys.argv[2])
OUT = sys.argv[3]

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
print(f"{len(uniq)} unique first-party TUs (of {len(entries)} compdb entries)", file=sys.stderr)

def run(e):
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
    out += ["-fsyntax-only", "-Wall", "-Wextra", "-fno-caret-diagnostics",
            "-fno-color-diagnostics", e["file"]]
    try:
        r = subprocess.run(out, capture_output=True, text=True,
                           cwd=e["directory"], timeout=300)
        return e["file"], r.stderr
    except Exception as ex:  # noqa: BLE001 - measurement script, report and continue
        return e["file"], f"SWEEP-ERROR: {ex}\n"

classes, per_file, lines, errors = collections.Counter(), collections.defaultdict(set), [], 0
with ThreadPoolExecutor(max_workers=JOBS) as ex:
    for fname, err in ex.map(run, uniq):
        if err.startswith("SWEEP-ERROR"):
            errors += 1
            continue
        for m in re.finditer(r"warning: .*?\[(-W[\w#=+-]+)\]", err):
            classes[m.group(1)] += 1
            per_file[m.group(1)].add(fname)
        lines += [l for l in err.splitlines() if "warning:" in l]

print("\n== Diagnostic classes: count, distinct files ==")
for cls, n in classes.most_common():
    print(f"{n:6d}  {len(per_file[cls]):4d} files  {cls}")
print(f"\ntotal warnings: {sum(classes.values())}  classes: {len(classes)}  sweep errors: {errors}")
open(OUT, "w").write("\n".join(lines))
