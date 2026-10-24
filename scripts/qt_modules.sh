#!/usr/bin/env bash
# Prints the Qt add-on modules for the given profiles from .github/qt-modules.txt, space-separated,
# for install-qt-action's `modules:` input. Usage: qt_modules.sh <profile>...
# An unknown profile is an error, so a typo cannot install a short list silently.
set -euo pipefail
list="$(dirname "$0")/../.github/qt-modules.txt"
[ "$#" -gt 0 ] || { echo "usage: $0 <profile>..." >&2; exit 2; }
awk -v want="$*" '
    BEGIN { n = split(want, w, " ") }
    /^#/ || NF == 0 { next }
    {
        for (i = 2; i <= NF; i++) known[$i] = 1
        for (i = 2; i <= NF; i++) for (j = 1; j <= n; j++)
            if ($i == w[j]) { out = out (out == "" ? "" : " ") $1; next }
    }
    END {
        for (j = 1; j <= n; j++) if (!(w[j] in known)) {
            print "qt_modules.sh: unknown profile \"" w[j] "\"" > "/dev/stderr"; exit 1
        }
        print out
    }' "$list"
