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
  - `DE1Simulator:`, 37 sites, all at DEBUG. On a simulator session the entire DE1
    view was EMPTY — the machine the page is about was the one thing it could not
    report — and nothing in the tree said so.

A grep finds both in milliseconds. Guidance did not.

WHAT IT ENFORCES
----------------
Within the covered files (see COVERED_GLOBS):

  1. No bare qDebug/qInfo/qWarning/qCritical. Log through the subsystem's helper,
     which is the only place the marker and tier are applied. Suppressible per line
     with a `// log-marker-exempt: <reason>` comment on or above the call.
  2. No REGISTERED marker typed into a log message. `HELPER("[Scale] ...")` means the
     marker was applied twice, once by hand and once by the helper — the
     `[Scale] [BLE DecentScaleWifi] …` shape. Note the narrowness: an UNregistered
     prefix (`"[Foo] ..."`) is deliberately not flagged, because `[M]` is a DE1
     protocol byte and `[observe]` is a mode qualifier. The consequence is real
     though — `HELPER("[R2-diag] foo")` would pass rule 1 (it uses the helper) and
     rule 2 (unregistered), so a hand-rolled prefix inside a helper call is caught
     by nothing here. Rule 1 caught the historical cases only because they sat on
     bare qDebug calls, which is a fact about the past, not an invariant.
  3. Every marker literal a helper header applies is one the registry declares, so a
     helper cannot quietly invent a subsystem. The header set is derived from the
     tree, not listed — see helper_headers().
  4. Every bracketed marker literal in qml/ is one the registry declares. The two
     on-screen log views name their subsystems as plain QML strings, so a rename
     would otherwise pass rules 1-3 and leave a view silently empty.

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
    # Both already log through SCALE_*, so the rationale above does not describe
    # them — they have somewhere to log to. wifiscalediscovery.cpp especially:
    # it is the home of the "[DecentScaleWifi]" family that logtags.h cites as the
    # original sin, and leaving it uncovered means a future bare qDebug there is
    # the one place nobody would think to check.
    "src/network/wifiscalediscovery.cpp",
    "src/core/settings_hardware.cpp",
]

# Files that HOST a registered subsystem's lines alongside unrelated code.
#
# Rules 2 and 5 apply (do not fake a marker); rule 1 does not (not every line here
# belongs to a marked subsystem, so "route it through a helper" has no answer).
#
# The distinction is real, not a concession. COVERED_GLOBS above are files that are
# WHOLLY about their subsystem — every log line in src/ble/scales/acaiascale.cpp is a
# scale line, so "use the helper" is always the right instruction. main.cpp is not
# like that: it drives the scale and refractometer reconnect ladders AND initialises
# fonts, translations, TTS and accessibility. Applying rule 1 there produced 112
# "violations" that were mostly lines with no subsystem to belong to — a check
# reporting a hundred non-defects is one people switch off, which is how the previous
# generation of this convention died.
#
# What DOES hold everywhere is the marker invariant: if you write a bracketed prefix,
# it must be a registered marker applied by its helper. That is what rules 2 and 5
# enforce, and it is what caught the real finds here — six device lines under a
# hand-typed "[USB Scale]"/"[BLE DE1]" that no [Scale]/[DE1] search returned, and two
# lines that applied [Refractometer] twice.
MARKER_ONLY_GLOBS = [
    # Drives both reconnect ladders through BLEManager's public tier helpers, so a
    # device subsystem's most-asked-about narrative is written here, in a file that is
    # not about logging at all.
    "src/main.cpp",
    # SAW's own files, now that [SAW] is registered and has a helper header. Each
    # also carries unrelated lines (frame transitions, flow calibration), which is
    # why they are here rather than in COVERED_GLOBS.
    "src/controllers/shottimingcontroller.cpp",
    "src/core/settings_calibration.cpp",
    "src/machine/weightprocessor.cpp",
]

# Helper headers define the macros; they are allowed to name markers and to contain
# the qFn tokens the macros expand to.
#
# DERIVED, not listed. A hand-maintained list failed the moment it was written: the
# four names originally hard-coded here omitted src/ble/bluetoothlogging.h, the
# fourth subsystem added in the very change that introduced this script — so rule 3,
# whose stated job is "a helper cannot quietly invent a fourth subsystem", could not
# see the fourth subsystem. Anything a reviewer must remember to update is a check
# that silently narrows. A header EARNS review by using the macros, so ask the tree.
def helper_headers():
    found = set()
    for path in REPO.glob("src/**/*.h"):
        text = path.read_text(encoding="utf-8", errors="replace")
        if "DECENZA_LOG_MARKER_" in text or "DECENZA_SUBSYS_LOG" in text:
            found.add(path.relative_to(REPO).as_posix())
    if not found:
        sys.exit("error: no logging helper headers found under src/. The macros were "
                 "renamed or moved; fix this parser rather than deleting the check.")
    return sorted(found)


# Where the two on-screen log views name the subsystems they show. These are plain
# QML string literals, so a marker rename compiles clean, passes rules 1-3, and
# leaves a view permanently empty — presenting as "this subsystem logged nothing",
# the single false answer this whole change exists to stop a reader being given.
# logtags.h calls a marker "API, not an implementation detail"; rule 4 is what makes
# the QML side of that true.
QML_GLOBS = ["qml/**/*.qml"]
QML_MARKER_RE = re.compile(r'"\[([A-Z][A-Za-z0-9]*)\]"')

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
# Hand-rolled prefixes that are NOT registered markers are caught by RULE 5 below.
# They used to be caught by nothing: rule 1 only reaches them when they sit on a bare
# qDebug call, which is a fact about how the historical ones happened to be written
# rather than an invariant. `HELPER("[R2-diag] …")` passed rule 1 (it used the helper)
# and rule 2 (unregistered token), and that hole was documented in prose instead of
# closed.
def build_inline_prefix_re(tokens):
    alt = "|".join(re.escape(t) for t in sorted(tokens))
    return re.compile(r'"\s*\[(' + alt + r')\]')

# Rule 5: an unregistered bracketed token opening a log message.
#
# Why any bracketed prefix is a defect and not merely untidy: `[Subsystem]` is the
# grammar of a registered marker, and a reader cannot tell `[SAW]` from `[Scale]` by
# looking at it. An unregistered one therefore advertises a subsystem query that
# silently returns an incomplete answer, or none — while looking exactly like one that
# works.
#
# Two discriminators, both learned from this rule's own first run, which produced two
# kinds of false positive alongside a real find (a sixth hand-rolled family,
# "[Weight-Worker]", that every other rule had missed):
#
#   1. THE LINE MUST CONTAIN A LOG CALL. `m_probeBuffer.contains("[M]")` is a DE1
#      protocol-response comparison, not a message. Leading position alone does not
#      separate a log message from any other string literal — an earlier draft of this
#      comment claimed it did, and the run disproved it immediately.
#   2. THE TOKEN MUST LOOK LIKE A SUBSYSTEM NAME, i.e. start uppercase. Every
#      registered marker does. `[observe]` is a lowercase mode qualifier sitting after
#      a marker the helper already applied; it impersonates nothing.
#
# Neither is an allowlist, deliberately. An allowlist of permitted tokens would be a
# second registry, free to drift from the first — the exact failure this convention
# exists to prevent.
LEADING_BRACKET_RE = re.compile(r'"\s*\[([A-Z][A-Za-z0-9 _.-]*)\]')

# A logging call: a bare Qt one, or any subsystem helper macro (FOO_LOG, SCALE_WARN,
# SAW_INFO_STDERR, DECENZA_SUBSYS_LOG…). Derived from the naming convention rather
# than listed, for the same reason helper_headers() is derived.
LOG_CALL_RE = re.compile(
    r"\bq(Debug|Info|Warning|Critical)\s*\(|\b[A-Z][A-Z0-9_]*_(LOG|INFO|WARN)[A-Z0-9_]*\s*\(")

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


def _expand(globs):
    seen = {}
    for pattern in globs:
        for path in REPO.glob(pattern):
            seen[path.relative_to(REPO).as_posix()] = path
    return sorted(seen.items())


def covered_files():
    # COVERED_GLOBS matches only .cpp/.mm, so helper headers cannot appear here and
    # need no exclusion. Yields (rel, path, all_rules) — all_rules False for the
    # marker-only set, where rule 1 does not apply (see MARKER_ONLY_GLOBS).
    full = [(rel, path, True) for rel, path in _expand(COVERED_GLOBS)]
    full_rels = {rel for rel, _, _ in full}
    marker_only = [(rel, path, False) for rel, path in _expand(MARKER_ONLY_GLOBS)
                   if rel not in full_rels]
    return sorted(full + marker_only)


def qml_files():
    seen = {}
    for pattern in QML_GLOBS:
        for path in REPO.glob(pattern):
            seen[path.relative_to(REPO).as_posix()] = path
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

    for rel, path, all_rules in covered_files():
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
            if all_rules and BARE_LOG_RE.search(code):
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

            # Rule 5: an UNREGISTERED bracketed token opening a log message. Rule 2
            # already covered the registered ones with a better message, so skip
            # those here rather than reporting one line twice.
            m5 = LEADING_BRACKET_RE.search(code)
            if (m5 and m5.group(1) not in tokens and LOG_CALL_RE.search(code)
                    and not EXEMPT_RE.search(line)):
                inner = m5.group(1)
                failures.append(
                    f"{rel}:{n}: message starts with \"[{inner}]\", which the registry does not "
                    f"declare. A leading bracketed token is the grammar of a subsystem marker, "
                    f"and a reader cannot tell it from a real one — so it advertises a "
                    f"`debug_get_log filter=\"[{inner}]\"` that returns an incomplete answer. "
                    f"Either register it in src/core/logtags.h and give it a helper, or write "
                    f"the prefix so it cannot be mistaken for a marker.")

    # Rule 3: helper headers may only apply registered markers.
    for rel in helper_headers():
        path = REPO / rel
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for name in MARKER_USE_RE.findall(line.split("//", 1)[0]):
                if name not in markers:
                    failures.append(
                        f"{rel}:{n}: DECENZA_LOG_MARKER_{name} is not declared in "
                        f"src/core/logtags.h. Add it to the registry (a literal AND a row in "
                        f"DECENZA_LOG_SUBSYSTEMS) so debug_get_log's description names it.")

    # Rule 4: a bracketed marker literal in QML must be one the registry declares.
    for rel, path in qml_files():
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("//", 1)[0]
            for inner in QML_MARKER_RE.findall(code):
                if inner not in tokens:
                    failures.append(
                        f"{rel}:{n}: \"[{inner}]\" looks like a log marker but the registry "
                        f"does not declare it. A log view asking for an unregistered marker "
                        f"shows NOTHING, and reads as \"this subsystem never logged\" — add "
                        f"it to DECENZA_LOG_SUBSYSTEMS in src/core/logtags.h, or fix the "
                        f"spelling.")

    if failures:
        print("Log-marker invariant violated:\n")
        for f in failures:
            print(f"  {f}\n")
        print(f"{len(failures)} violation(s). See docs/CLAUDE_MD/LOGGING.md.")
        return 1

    # Print what was actually scanned. Three of the four sets are derived from the
    # tree, so a glob that silently matches nothing would otherwise pass as clean.
    print(f"OK: {len(covered_files())} covered file(s) log through marked helpers, "
          f"{len(helper_headers())} helper header(s) apply only registered markers, "
          f"{len(qml_files())} QML file(s) name only registered markers; "
          f"markers registered: {', '.join(sorted(tokens))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
