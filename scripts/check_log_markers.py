#!/usr/bin/env python3
"""Every covered subsystem's log lines carry a registered marker, applied by a helper.

WHY THIS IS CHECKED AND NOT JUST DOCUMENTED
-------------------------------------------
The markers exist so that one grep over a user-submitted log returns a whole
subsystem's story. That property is only worth anything if it holds for EVERY line:
a single site that hand-rolls its own prefix is invisible to the search, and the
reader cannot know it was missed — the log looks complete.

That is not hypothetical. While the markers were being introduced, nine separate
hand-rolled prefix families were found in code that had already been "converted",
and every one of them was found by a person reading a running app's log, never by
the tree. Two of the worst:

  - `[R2-diag]`, 21 sites. A debug-session prefix that no registered marker matched,
    so a [Refractometer] search returned the driver's packets but NOT the
    connect/churn story those lines were added to explain.
  - `DE1Simulator:`, 40 sites, all at DEBUG. On a simulator session the entire DE1
    view was EMPTY — the machine the page is about was the one thing it could not
    report — and nothing in the tree said so.

A grep finds both in milliseconds. Guidance did not.

WHAT IT ENFORCES
----------------
Within the covered files (see COVERED_GLOBS):

  1. No bare qDebug/qInfo/qWarning/qCritical. Log through the subsystem's helper,
     which is the only place the marker and tier are applied. Suppressible per line
     with a `// log-marker-exempt: <reason>` comment on or above the call.
  2. No hand-rolled bracketed prefix inside a log message. `qFn() << "[Foo] ..."` or
     `HELPER("[Foo] ...")` means the marker was typed at the call site, which is how
     `[Scale] [BLE DecentScaleWifi] …` double prefixes and `[R2-diag]` happened.
  3. Every marker literal a helper header applies is one the registry declares, so a
     helper cannot quietly invent a fourth subsystem.

The registry is PARSED from src/core/logtags.h, never restated here. A copy of the
marker list in this script would be one more thing to drift, which is the exact
failure mode the script exists to prevent.

No Qt, no compiler, no build: pure text over the source, so it can run as a
per-PR gate (see .github/workflows/text-invariants.yml).
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGTAGS = REPO / "src/core/logtags.h"

# Files whose logging must go through a marked helper. Deliberately narrow: these are
# the subsystems that have helpers and a registry entry. Widen it when a subsystem is
# added to the registry, not before — a glob that covers code with nowhere to log to
# would only teach people to add exemptions.
COVERED_GLOBS = [
    "src/ble/**/*.cpp",
    "src/ble/**/*.mm",
    "src/usb/**/*.cpp",
    "src/simulator/de1simulator.cpp",
    "src/simulator/simulatedscale.cpp",
]

# Helper headers define the macros; they are allowed to name markers and to contain
# the qFn tokens the macros expand to.
HELPER_HEADERS = {
    "src/core/logtags.h",
    "src/ble/de1logging.h",
    "src/ble/scales/scalelogging.h",
    "src/ble/refractometers/refractometerlogging.h",
}

BARE_LOG_RE = re.compile(r"\bq(Debug|Info|Warning|Critical)\s*\(\s*\)")
EXEMPT_RE = re.compile(r"log-marker-exempt")

# A REGISTERED marker token typed at the start of a log message string. Built from
# the registry at runtime (see build_inline_prefix_re) so it cannot drift from it.
#
# Deliberately narrow to registered tokens rather than "any bracketed prefix". The
# broad version had two false positives in the real tree and both were instructive:
# `m_probeBuffer.contains("[M]")` is a DE1 protocol response byte, not a log message
# at all, and `warn("[observe] …")` is a mode qualifier on a line whose marker the
# helper already supplied. Neither is the defect. The defect is naming a SUBSYSTEM at
# the call site, which is what produced "[Scale] [BLE DecentScaleWifi] …" — a marker
# applied twice, once by hand and once by the helper.
#
# Hand-rolled prefixes that are NOT registered markers (`[R2-diag]`, `DE1Simulator:`)
# are caught by rule 1 instead: they lived on bare qDebug calls, which is how they
# escaped the marker in the first place.
def build_inline_prefix_re(tokens):
    alt = "|".join(re.escape(t) for t in sorted(tokens))
    return re.compile(r'"\s*\[(' + alt + r')\]')

# Where a marker literal is applied by a helper: DECENZA_LOG_MARKER_<NAME>.
MARKER_USE_RE = re.compile(r"\bDECENZA_LOG_MARKER_([A-Z0-9_]+)\b")


def registered_markers():
    """Parse the registry: #define DECENZA_LOG_MARKER_<NAME> "<Token>"."""
    text = LOGTAGS.read_text(encoding="utf-8")
    pairs = re.findall(r'#define\s+DECENZA_LOG_MARKER_([A-Z0-9_]+)\s+"([^"]+)"', text)
    if not pairs:
        sys.exit(f"error: no DECENZA_LOG_MARKER_* definitions found in {LOGTAGS}. "
                 "The registry moved or its shape changed; fix this parser rather "
                 "than deleting the check.")
    return {name: token for name, token in pairs}


def covered_files():
    seen = {}
    for pattern in COVERED_GLOBS:
        for path in REPO.glob(pattern):
            rel = path.relative_to(REPO).as_posix()
            if rel not in HELPER_HEADERS:
                seen[rel] = path
    return sorted(seen.items())


def strip_block_comments(text):
    """Blank out /* */ bodies, preserving line structure so numbers stay right."""
    out = []
    i = 0
    while True:
        start = text.find("/*", i)
        if start < 0:
            out.append(text[i:])
            break
        out.append(text[i:start])
        end = text.find("*/", start + 2)
        if end < 0:
            out.append("\n" * text.count("\n", start))
            break
        out.append("\n" * text.count("\n", start, end))
        i = end + 2
    return "".join(out)


def main():
    markers = registered_markers()
    tokens = set(markers.values())
    inline_prefix_re = build_inline_prefix_re(tokens)
    failures = []

    for rel, path in covered_files():
        raw = path.read_text(encoding="utf-8")
        lines = strip_block_comments(raw).splitlines()

        for n, line in enumerate(lines, 1):
            code = line.split("//", 1)[0]
            if not code.strip():
                continue

            # Rule 1: bare log call. An exemption may sit on the line or in the
            # comment block directly above it. The window is 6 lines because a
            # worthwhile exemption states WHY, and a real reason rarely fits on one
            # line — a 1-line window would push people toward terse, useless reasons
            # or toward giving up and deleting the check.
            if BARE_LOG_RE.search(code):
                context = "\n".join(lines[max(0, n - 7):n])
                if not EXEMPT_RE.search(context):
                    failures.append(
                        f"{rel}:{n}: bare {BARE_LOG_RE.search(code).group(0)} — log through "
                        f"this subsystem's helper so the marker and tier are applied in one "
                        f"place. If this line genuinely cannot (no helper in scope), append "
                        f"`// log-marker-exempt: <reason>`.")

            # Rule 2: a bracketed prefix typed into the message itself.
            m = inline_prefix_re.search(code)
            if m and not EXEMPT_RE.search(line):
                inner = m.group(1)
                failures.append(
                    f"{rel}:{n}: message starts with the registered marker \"[{inner}]\" typed "
                    f"by hand. The helper already applies it, so this produces it twice — the "
                    f"\"[Scale] [BLE DecentScaleWifi] …\" shape. Drop it and let the helper's "
                    f"source tag name the source.")

    # Rule 3: helper headers may only apply registered markers.
    for rel in sorted(HELPER_HEADERS):
        path = REPO / rel
        if not path.exists():
            failures.append(f"{rel}: listed as a helper header but does not exist — "
                            f"update HELPER_HEADERS in {Path(__file__).name}.")
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for name in MARKER_USE_RE.findall(line.split("//", 1)[0]):
                if name not in markers:
                    failures.append(
                        f"{rel}:{n}: DECENZA_LOG_MARKER_{name} is not declared in "
                        f"src/core/logtags.h. Add it to the registry (a literal AND a row in "
                        f"DECENZA_LOG_SUBSYSTEMS) so debug_get_log's description names it.")

    if failures:
        print("Log-marker invariant violated:\n")
        for f in failures:
            print(f"  {f}\n")
        print(f"{len(failures)} violation(s). See docs/CLAUDE_MD/LOGGING.md.")
        return 1

    n_files = len(covered_files())
    print(f"OK: {n_files} covered file(s) log through marked helpers; "
          f"markers registered: {', '.join(sorted(tokens))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
