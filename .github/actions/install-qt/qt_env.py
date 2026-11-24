"""Record the environment install-qt-action sets, and replay it on a cache hit.

  qt_env.py snapshot <before.json>
  qt_env.py record <before.json> <qt-dir>     # after the action ran
  qt_env.py replay <qt-dir>                   # instead of running it

What the action sets is in its main.ts (v4): QT_* and QML2_IMPORT_PATH outright,
IQTA_TOOLS for tools (:835), LD_LIBRARY_PATH and PKG_CONFIG_PATH by appending
(:905-910), and PATH by prepending bin/ and tool directories (:830, :903).
"""
import json
import os
import sys

SET = ["QT_ROOT_DIR", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QT_HOST_PATH", "IQTA_TOOLS"]
APPEND = ["LD_LIBRARY_PATH", "PKG_CONFIG_PATH"]
RECORD = ".decenza-qt-env.json"


def under(path, root):
    norm = lambda p: os.path.normcase(os.path.abspath(p))
    return norm(path).startswith(norm(root) + os.sep)


def snapshot(out):
    with open(out, "w") as f:
        json.dump({k: os.environ.get(k) for k in SET + APPEND + ["PATH"]}, f)


def record(before_file, qt_dir):
    with open(before_file) as f:
        before = json.load(f)
    rec = {"set": {}, "append": {}, "path": []}
    for k in SET:
        v = os.environ.get(k)
        if v is not None and v != before.get(k):
            rec["set"][k] = v
    if "QT_ROOT_DIR" not in rec["set"]:
        sys.exit("::error::install-qt-action set no new QT_ROOT_DIR; nothing to record")
    for k in APPEND:
        old, new = before.get(k) or "", os.environ.get(k) or ""
        if new == old:
            continue
        if old and not new.startswith(old + os.pathsep):
            sys.exit(f"::error::{k} was rewritten, not appended to; cannot record it")
        rec["append"][k] = new[len(old) + 1:] if old else new
    old_path = set((before.get("PATH") or "").split(os.pathsep))
    rec["path"] = [p for p in os.environ["PATH"].split(os.pathsep)
                   if p and p not in old_path and under(p, qt_dir)]
    with open(os.path.join(qt_dir, RECORD), "w") as f:
        json.dump(rec, f, indent=1)
    print(json.dumps(rec, indent=1))


def replay(qt_dir):
    with open(os.path.join(qt_dir, RECORD)) as f:
        rec = json.load(f)
    root = rec["set"]["QT_ROOT_DIR"]
    if not os.path.isdir(os.path.join(root, "bin")):
        sys.exit(f"::error::Restored Qt has no {root}/bin; the cache entry is incomplete")
    with open(os.environ["GITHUB_ENV"], "a") as env:
        for k, v in rec["set"].items():
            env.write(f"{k}={v}\n")
        for k, v in rec["append"].items():
            old = os.environ.get(k)
            env.write(f"{k}={old + os.pathsep + v if old else v}\n")
    # Each GITHUB_PATH line is prepended in turn, so write the last one first.
    with open(os.environ["GITHUB_PATH"], "a") as path:
        for p in reversed(rec["path"]):
            path.write(p + "\n")
    print(json.dumps(rec, indent=1))


if __name__ == "__main__":
    cmd, *args = sys.argv[1:]
    {"snapshot": snapshot, "record": record, "replay": replay}[cmd](*args)
