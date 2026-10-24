#!/usr/bin/env bash
# Prints the Qt add-on modules for the given profiles from .github/qt-modules.txt, space-separated,
# for install-qt-action's `modules:` input. Usage: qt_modules.sh <profile>...
# Fails, rather than printing a short list, on an unknown profile in the request or the file, a
# malformed row, or an empty result.
set -euo pipefail
list="$(dirname "$0")/../.github/qt-modules.txt"
[ "$#" -gt 0 ] || { echo "usage: $0 <profile>..." >&2; exit 2; }
awk -v want="$*" '
    function fail(msg) { print "qt_modules.sh: " FILENAME ":" FNR ": " msg > "/dev/stderr"; bad = 1; exit 1 }
    BEGIN { n = split(want, w, " ") }
    { sub(/\r$/, "") }
    /^[ \t]*#/ || NF == 0 { next }
    $1 == "profiles" {
        if (np) fail("second profiles line")
        for (i = 2; i <= NF; i++) { valid[$i] = 1; np++ }
        next
    }
    {
        if (!np) fail("module row before the profiles line")
        if ($1 !~ /^qt[a-z0-9]+$/) fail("\"" $1 "\" is not a Qt module name")
        if (NF < 2) fail($1 " names no profile")
        hit = 0
        for (i = 2; i <= NF; i++) {
            if (!($i in valid)) fail("unknown profile \"" $i "\" on " $1)
            for (j = 1; j <= n; j++) if ($i == w[j]) hit = 1
        }
        if (hit) out = out (out == "" ? "" : " ") $1
    }
    END {
        if (bad) exit 1
        if (!np) { print "qt_modules.sh: no profiles line in " FILENAME > "/dev/stderr"; exit 1 }
        for (j = 1; j <= n; j++) if (!(w[j] in valid)) {
            print "qt_modules.sh: unknown profile \"" w[j] "\"" > "/dev/stderr"; exit 1
        }
        if (out == "") { print "qt_modules.sh: no modules for: " want > "/dev/stderr"; exit 1 }
        print out
    }' "$list"
