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
There are TWO coverage sets, because the rules do not all generalise the same way:

  COVERED_GLOBS      all rules. Files wholly about one subsystem, where "use the
                     helper" is always the right instruction.
  MARKER_ONLY_GLOBS  rules 2 and 5 only. Files that HOST a subsystem's lines beside
                     unrelated code — main.cpp and SAW's three. Rule 1 there produced
                     over a hundred "violations" that were lines with no subsystem to
                     belong to, and a check reporting that many non-defects is one
                     people switch off.

  1. No bare qDebug/qInfo/qWarning/qCritical. Log through the subsystem's helper,
     which is the only place the marker and tier are applied. Suppressible per line
     with a `// log-marker-exempt: <reason>` comment on or above the call.
  2. No REGISTERED marker typed into a log message. `HELPER("[Scale] ...")` means the
     marker was applied twice, once by hand and once by the helper — the
     `[Scale] [BLE DecentScaleWifi] …` shape.
  3. Every marker literal a helper header applies is one the registry declares, so a
     helper cannot quietly invent a subsystem. The header set is derived from the
     tree, not listed — see helper_headers().
  4. Every bracketed marker literal in qml/ is one the registry declares. The two
     on-screen log views name their subsystems as plain QML strings, so a rename
     would otherwise pass rules 1-3 and leave a view silently empty.
  5. No UNREGISTERED bracketed token opening a log message. `HELPER("[R2-diag] …")`
     passes rules 1 and 2 — it uses the helper, and the token is not a registered
     marker — yet it advertises a subsystem query that returns an incomplete answer
     while looking exactly like one that works.

     This block previously documented rule 5's absence as a known hole ("caught by
     nothing here"), and kept saying so after the rule existed. Two descriptions of
     one policy, one of them false, is the drift this script exists to catch — worth
     leaving on the record, since the stale text read as a deliberate decision.

  Every marker literal must also carry a DECENZA_LOG_SUBSYSTEMS row; see
  registered_markers(). Without one the marker works everywhere in the code and is
  still missing from debug_get_log's description, so nobody knows to search for it.

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
    # Wholly about HDS firmware updates: manifest fetch, eligibility, release
    # notes, and dispatching the start command. Covered because "the Update
    # button never appeared" is answered only by whether the manifest check ran
    # and what it said — and it shipped with four hand-typed "[Scale][HDS Update]"
    # prefixes on bare qWarnings, which no registered marker would have matched.
    "src/core/hdsfirmwareupdatecontroller.cpp",
    # Wholly about the Android multicast lock: every line in it is that lock's
    # lifecycle or its failure to be taken. Worth covering because whether the
    # lock was held is the FIRST question a "browse ran and found nothing" report
    # has to answer, and it shipped with five hand-typed "[MulticastLock]"
    # prefixes that no registered marker would ever match.
    "src/network/multicastlock.cpp",
    # Wholly about mDNS/DNS-SD: socket setup, query/retransmit, record parsing,
    # both backends. Every line is a [Network] line. It carried a hand-typed
    # "[MdnsResolver]" prefix that the registry never knew about, so a reader
    # pulling the [Network] story out of a submitted log got everything about the
    # WiFi scale EXCEPT why its name would not resolve — which is the half that
    # usually explains the other.
    "src/network/mdnsresolver.cpp",
    "src/core/settings_hardware.cpp",
    # Wholly about the coffee-bag detail pipeline: canonical search, the product
    # URL's state, archive recovery, page extraction and the image cache. Covered
    # rather than marker-only because every log line in it is a [BeanBase] line —
    # and it shipped with TWO hand-rolled prefixes ("BeanBaseClient:" and
    # "BeanBase:") across twelve calls, so no single grep returned the story.
    "src/network/beanbaseclient.cpp",
    # Wholly about sensor calibration: every line in it is either a refused
    # correction and why, or a correction being applied with the pair it was
    # computed from. That is the whole answer to "why can I not apply a
    # correction", so "use the helper" is always right here.
    "src/controllers/sensorcalibrationcontroller.cpp",
    # Wholly about the screensaver: every log line in it is a screensaver line.
    "src/screensaver/screensavervideomanager.cpp",
    # Wholly about equipment packages: every log line in it is an [Equipment] line
    # (identity edits and their fork/merge/in-place decision, package CRUD, the
    # enrichment-fork heal, the equipment migration and device import).
    "src/history/equipmentstorage.cpp",
    # Wholly about the MCP server: sessions, the HTTP/SSE transport, tool
    # dispatch, remote access and the tunnel. Every log line in src/mcp is an
    # [MCP] line, so "use the helper" is always the right instruction here. The
    # tool files are included deliberately — a query failure inside shots_list is
    # still something an assistant's user reports as "my AI can't see my shots".
    "src/mcp/**/*.cpp",
    # Wholly about the weather/sun-time fetches, and wholly about the update
    # check. Both carried a hand-typed "WeatherManager: " / "UpdateChecker: "
    # prefix that no registered marker matched, which is exactly why they were
    # invisible to a per-marker analysis of a submitted log while being two of
    # its largest repeaters. Covered so a future bare qDebug in either cannot
    # re-open that hole.
    "src/weather/weathermanager.cpp",
    "src/core/updatechecker.cpp",
    # Wholly about screen-reader announcements and the TTS engine behind them:
    # every line is either the route an announcement took or the setup that
    # decides which routes exist. Covered because "TalkBack says nothing on this
    # screen" is answered ONLY by these lines, and the file shipped with two
    # hand-rolled conventions that had already drifted apart — eight "[a11y] "
    # bare-qInfo lines and six "AccessibilityManager: " ones — so neither a
    # marker filter nor a single grep returned the whole story.
    "src/core/accessibilitymanager.cpp",
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
# fonts, translations, TTS and accessibility. Applying rule 1 there produced 118
# "violations" that were mostly lines with no subsystem to belong to — a check
# reporting a hundred non-defects is one people switch off, which is how the previous
# generation of this convention died.
#
# What DOES hold everywhere is the marker invariant: if you write a bracketed prefix,
# it must be a registered marker applied by its helper. That is what rules 2 and 5
# enforce, and it is what caught the real finds here — seven device lines under a
# hand-typed "[USB Scale]"/"[BLE DE1]" that no [Scale]/[DE1] search returned, and two
# lines that applied [Refractometer] twice.
MARKER_ONLY_GLOBS = [
    # Hosts the [Equipment] migration lines (the enrichment-fork heal) beside the
    # whole schema-migration chain and the shot CRUD, none of which is equipment.
    "src/history/shothistorystorage.cpp",
    # Hosts the [Equipment] grinder census beside every other history query, which
    # is why it is here and not in COVERED_GLOBS: the file's bare qWarning calls
    # are query failures belonging to no subsystem at all.
    "src/history/shothistorystorage_queries.cpp",
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
    # Added after the fact: this file gained [SAW] lines in the same change that
    # added these rules and was left out of both sets, so rules 2 and 5 did not run
    # on the file carrying the [SAW][HotWater] narrative. The check for "does every
    # file using a helper appear in a glob" is one nobody ran, because the script
    # has no way to ask it of itself — worth remembering before adding a subsystem.
    "src/machine/machinestate.cpp",
    # Both found by rule 6 below, on its first run, having been given [Font] lines
    # in this same change and added to no glob — which is the whole argument for
    # rule 6 existing rather than for adding files by hand.
    "src/core/settings_theme.cpp",
    "src/screensaver/iosbrightness.mm",
    # Hosts the [BeanBase][FindPage] decline line beside the advisor, the
    # conversation store and every provider dispatch, none of which is the bag
    # pipeline — marker-only for exactly that reason.
    "src/ai/aimanager.cpp",
    # Hosts two subsystems' lines: [Equipment][Migration] (the constructor's
    # adoption of the package migration 35/36 healed) and [DE1][SettingsDrift]
    # (the ShotSettings resend ladder in onShotSettingsReported). Both sit beside
    # a constructor wiring shot history, bags, recipes, profiles, the DE1 and the
    # scales, and ~110 bare qDebug calls belonging to no subsystem at all. Rule 1
    # has no answer for those, which is exactly the main.cpp case above.
    "src/controllers/maincontroller.cpp",
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
# A bracketed marker in QML. Matches BOTH shapes this rule has to cover, which
# is the part that took two tries to get right.
#
# TWO shapes, and a pattern that covers only one of them is the recurring bug
# here — it has now been written wrong in both directions:
#
#   "[DE1]"                      a log VIEW's filter (SubsystemLogView.markers)
#   console.log("[DE1] woke")    a log CALL
#
# The original `r'"\[([A-Z][A-Za-z0-9]*)\]"'` required the closing quote to sit
# immediately after the `]`, so it matched the filter form and nothing else: rule
# 4 saw 7 tokens in qml/ where there were 59, and 46 call sites under 12
# unregistered names ([AutoSleep], [Keyboard], [Background], [CustomEditorPopup]
# and friends) were invisible.
#
# The first fix replaced that quote with `[ :]`, which INVERTED the coverage
# rather than widening it — the 46 calls were caught and the three filter sites
# at SettingsConnectionsTab.qml:765/:941/:1961 went blind, i.e. the exact sites
# the rule-4 preamble above says the rule exists for. Caught in review by
# injecting an unregistered marker into a bare filter string and watching the
# gate pass.
#
# So: a LOOKAHEAD, which consumes nothing and admits a space, a colon, or the
# closing quote. That keeps the token anchored to a whole word rather than a
# prefix of a longer one while covering both shapes at once.
#
# The opening class is `["'`]` and the lookahead accepts the same three, because
# single-quoted strings and template literals are ordinary QML/JS. Neither occurs
# in the tree today; being anchored to a double quote alone was the same "cannot
# match the shape it is written for" hazard one edit away from mattering.
#
# Hyphens are allowed inside the token so a hand-rolled "[R2-diag]" is CAUGHT
# rather than skipped for not looking like an identifier.
QML_MARKER_RE = re.compile(r'''["'`]\[([A-Z][A-Za-z0-9-]*)\](?=[ :"'`])''')

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
    """Parse the registry: #define DECENZA_LOG_MARKER_<NAME> "<Token>".

    Also enforce that every literal has a DECENZA_LOG_SUBSYSTEMS row. The two are
    separate lists in logtags.h and only the #defines are load-bearing for
    compilation, so a marker declared without a row works perfectly at runtime —
    and is simply ABSENT from debug_get_log's tool description, which is built
    from the rows. logtags.h states that exact hazard ("a marker the description
    does not mention is invisible to the assistant that would have used it"), and
    the errors this script already emits tell people to add the row, which reads
    as a promise that the row is checked. It was not. Now it is.
    """
    text = LOGTAGS.read_text(encoding="utf-8")
    pairs = re.findall(r'#define\s+DECENZA_LOG_MARKER_([A-Z0-9_]+)\s+"([^"]+)"', text)
    if not pairs:
        sys.exit(f"error: no DECENZA_LOG_MARKER_* definitions found in {LOGTAGS}. "
                 "The registry moved or its shape changed; fix this parser rather "
                 "than deleting the check.")

    # Rows look like:  X(DECENZA_LOG_MARKER_SCALE,  \n  "description…")
    described = set(re.findall(r'X\(\s*DECENZA_LOG_MARKER_([A-Z0-9_]+)\s*,', text))
    undescribed = [name for name, _ in pairs if name not in described]
    if undescribed:
        listed = ", ".join(f"DECENZA_LOG_MARKER_{n}" for n in undescribed)
        sys.exit(f"error: {listed} defined in {LOGTAGS} but missing from "
                 "DECENZA_LOG_SUBSYSTEMS. The marker would work everywhere in the "
                 "code and still never appear in debug_get_log's description, so "
                 "nobody would know to search for it. Add a row with a description "
                 "written for someone who has never read this code.")

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


def statement_starts(lines):
    """For each line index, the text of the logical STATEMENT it belongs to.

    Rule 5 has to know whether a bracketed literal sits inside a log call, and a
    log call is very often split across lines:

        FONT_WARN_STDERR("Bundled",
            QStringLiteral("[R2-diag] …"));

    Matching per physical line, the second line carries the prefix but no log
    call, so the rule saw nothing and the prefix passed. That was not a corner
    case — 16 call sites in the scanned files already put their message literal
    on a continuation line, and renaming the Font/Network helpers to their longer
    *_STDERR forms pushed more of them onto two lines, so the rule was quietly
    weakened by an edit in the same change that added it.

    Accumulate lines until parentheses balance, and give every line of the
    statement the whole statement's text.
    """
    result = [""] * len(lines)
    start = 0
    depth = 0
    buf = []
    for i, line in enumerate(lines):
        code = line.split("//", 1)[0]
        buf.append(code)
        depth += code.count("(") - code.count(")")
        if depth <= 0:
            joined = " ".join(buf)
            for j in range(start, i + 1):
                result[j] = joined
            start = i + 1
            depth = 0
            buf = []
    if buf:  # unbalanced tail — treat what we have as one statement
        joined = " ".join(buf)
        for j in range(start, len(lines)):
            result[j] = joined
    return result


def main():
    markers = registered_markers()
    tokens = set(markers.values())
    inline_prefix_re = build_inline_prefix_re(tokens)
    failures = []

    for rel, path, all_rules in covered_files():
        raw = path.read_text(encoding="utf-8")
        lines = strip_block_comments(raw).splitlines()
        statements = statement_starts(lines)

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
            #
            # Like rule 5, this requires a log call in the STATEMENT: the rule is
            # "a marker typed into a log MESSAGE", and without that check any
            # string literal naming a marker trips it. That is not hypothetical —
            # mcpresources.cpp builds debug_get_log's tool description, which
            # necessarily quotes "[Scale][BLE AcaiaScale] tare sent" to teach an
            # assistant what a marker looks like, and warns that "[Scale]" under
            # regex is a character class. Prose ABOUT markers is the one thing a
            # marker-checking script must not flag.
            m = inline_prefix_re.search(code)
            if (m and LOG_CALL_RE.search(statements[n - 1])
                    and not EXEMPT_RE.search(line)):
                inner = m.group(1)
                failures.append(
                    f"{rel}:{n}: message starts with the registered marker \"[{inner}]\" typed "
                    f"by hand. The helper already applies it, so this produces it twice — the "
                    f"\"[Scale] [BLE DecentScaleWifi] …\" shape. Drop it and let the helper's "
                    f"source tag name the source.")

            # Rule 5: an UNREGISTERED bracketed token opening a log message. Rule 2
            # already covered the registered ones with a better message, so skip
            # those here rather than reporting one line twice.
            # The log call is looked for in the whole STATEMENT, not this line —
            # see statement_starts(). The bracket itself is still located on this
            # line, so the reported line number stays the useful one.
            m5 = LEADING_BRACKET_RE.search(code)
            if (m5 and m5.group(1) not in tokens
                    and LOG_CALL_RE.search(statements[n - 1])
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

    # Rule 6: a file that USES a logging helper must be in a glob set.
    #
    # The globs are hand-maintained lists, and nothing made them keep up with the
    # code. machinestate.cpp gained [SAW] lines in the same change that wrote these
    # rules and was added to neither set, so rules 2 and 5 did not run on the file
    # carrying the [SAW][HotWater] narrative — found by review, not by the gate.
    # Adding that one file back would have fixed the instance and left the class.
    #
    # Including a helper header is the signal: it is what a file does when it joins
    # a subsystem, it cannot be done by accident, and it is exactly the moment the
    # file starts being able to violate rules 2 and 5.
    covered_set = {rel for rel, _, _ in covered_files()}
    helper_set = set(helper_headers())
    # logtags.h is the REGISTRY, not a helper. Files include it to read the
    # subsystem table (mcpresources.cpp builds the MCP tool description from it),
    # which is not logging and must not drag them into the rules.
    include_signal = {h for h in helper_set if Path(h).name != "logtags.h"}
    helper_includes = re.compile(
        r'#include\s+"(?:[^"]*/)?(' + "|".join(
            re.escape(Path(h).name) for h in sorted(include_signal)) + r')"')
    for path in sorted(REPO.glob("src/**/*")):
        if path.suffix not in (".cpp", ".mm"):
            continue
        rel = path.relative_to(REPO).as_posix()
        if rel in covered_set or rel in helper_set:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        m = helper_includes.search(text)
        if m:
            failures.append(
                f"{rel}: includes the logging helper \"{m.group(1)}\" but is in neither "
                f"COVERED_GLOBS nor MARKER_ONLY_GLOBS in scripts/check_log_markers.py, so "
                f"the marker rules do not run on it. Add it to MARKER_ONLY_GLOBS if it hosts "
                f"a subsystem's lines beside unrelated code, or to COVERED_GLOBS if every log "
                f"line in it belongs to one subsystem.")

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
