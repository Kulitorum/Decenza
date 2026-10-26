"""Run qmlcachegen, but replace its outputs only when their bytes change.

qmlcachegen commits its output unconditionally (QSaveFile + Truncate,
qtdeclarative/src/qmlcompiler/qqmljscompiler.cpp:584-588, 711 in 6.12.0),
so Ninja's restat on Qt's edges never sees an unchanged output and every
dependent object recompiles. Leaving an identical output untouched lets
restat prune that compile.

usage: python3 qmlcachegen_stable.py <real-qmlcachegen> <qmlcachegen args...>
"""
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    real, args = sys.argv[1], sys.argv[2:]
    if "-o" not in args:
        return subprocess.call([real] + args)
    i = args.index("-o")
    out = os.path.abspath(args[i + 1])
    out_dir = os.path.dirname(out)
    os.makedirs(out_dir, exist_ok=True)
    # Same directory keeps os.replace atomic; same basename keeps qmlcachegen's
    # mode, which it picks from the output name (qmlcache_loader.cpp -> loader).
    tmp_dir = tempfile.mkdtemp(prefix=".stable-", dir=out_dir)
    try:
        tmp_out = os.path.join(tmp_dir, os.path.basename(out))
        rc = subprocess.call([real] + args[:i + 1] + [tmp_out] + args[i + 2:])
        if rc != 0:
            return rc
        # Side outputs (e.g. <out>.aotstats from --dump-aot-stats) sit beside -o.
        for name in os.listdir(tmp_dir):
            produced = os.path.join(tmp_dir, name)
            target = os.path.join(out_dir, name)
            if not _same_bytes(produced, target):
                os.replace(produced, target)
        return 0
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


def _same_bytes(a, b):
    try:
        if os.path.getsize(a) != os.path.getsize(b):
            return False
        with open(a, "rb") as fa, open(b, "rb") as fb:
            return fa.read() == fb.read()
    except OSError:
        return False


if __name__ == "__main__":
    sys.exit(main())
