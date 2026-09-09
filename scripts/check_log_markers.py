#!/usr/bin/env python3
"""Enforce registered logging across first-party runtime C++, QML/JS and native bridges.

Raw output must use a shared formatter, with explicit registry ownership at the
emitter. Inline exemptions require a reason (bootstrap, terminal sink, crash-safe
writer); Runtime capture never excuses a first-party bypass. Registered tokens
and catalog rows are derived from logtags.h. No compiler, Qt, or network needed.
Run --self-test to exercise negative fixtures through this same checker.
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGTAGS = REPO / "src/core/logtags.h"

# All first-party runtime source, including mixed-owner files and dormant bridges.
COVERED_GLOBS = [
    "src/**/*.cpp", "src/**/*.h", "src/**/*.mm", "src/**/*.m",
    "ios/**/*.mm", "ios/**/*.m", "android/**/*.java", "android/**/*.kt",
]
MARKER_ONLY_GLOBS = []  # retained for callers of covered_files(); no runtime exemptions

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
QML_GLOBS = ["qml/**/*.qml", "qml/**/*.js"]
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

BARE_LOG_RE = re.compile(
    r"\b(?:q(?:Debug|Info|Warning|Critical|Fatal|CDebug|CInfo|CWarning|CCritical|CFatal|ErrnoWarning)|"
    r"console\.(?:log|debug|info|warn|error)|(?:android\.util\.)?Log\.(?:[dviwe]|println|wtf)|"
    r"NSLogv?|os_log(?:_debug|_info|_error|_fault|_with_type)?|__android_log_print|__android_log_write|fprintf|fputs|printf|puts)\s*\(")
EXEMPT_RE = re.compile(r"log-marker-exempt:\s*[^\s].{10,}")

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
LEADING_BRACKET_RE = re.compile(r'''["'`]\[([A-Za-z][A-Za-z0-9 _./-]*)\]''')

# A logging call: a bare Qt one, or any subsystem helper macro (FOO_LOG, SCALE_WARN,
# SAW_INFO_STDERR, DECENZA_SUBSYS_LOG…). Derived from the naming convention rather
# than listed, for the same reason helper_headers() is derived.
LOG_CALL_RE = re.compile(
    r"\bq(Debug|Info|Warning|Critical|Fatal)\s*\(|\b[A-Z][A-Z0-9_]*_(LOG|INFO|WARN|DEBUG|ERROR|FATAL)[A-Z0-9_]*\s*\(")

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
    # All rules apply to every first-party runtime file.
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


SOURCE_TOKEN_RE = re.compile(
    r'''R"([^\s()\\]*)\(.*?\)\1"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/''', re.S)

def code_only(text):
    return SOURCE_TOKEN_RE.sub(
        lambda m: "".join("\n" if c == "\n" else " " for c in m.group()), text)

def helper_arguments(text, markers, rel):
    failures = []
    # Registry identifiers in the generic C++ stream alias are as strongly
    # checked as the literal-marker aliases in subsystem headers.
    for m in re.finditer(r"\bDIAG_(?:C?DEBUG|INFO|C?WARN|ERROR|FATAL)\s*\(\s*([^,]+),", text):
        owner = m.group(1).strip()
        is_macro_parameter = owner == "owner" and text[text.rfind("\n", 0, m.start()) + 1:m.start()].lstrip().startswith("#define")
        if not is_macro_parameter and owner not in markers:
            failures.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: unknown diagnostic owner {owner}")
    for m in re.finditer(r'''\b(?:WebDebugLogger\.(?:debug|info|warn|error)|DiagnosticLog\.(?:[dviwe]|println|wtf))\s*\(\s*([^,]+),''', text):
        owner = m.group(1).strip().strip('"')
        if owner not in markers.values():
            failures.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: diagnostic owner must be a registered literal: {owner}")
    return failures


def main():
    markers = registered_markers()
    tokens = set(markers.values())
    inline_prefix_re = build_inline_prefix_re(tokens)
    failures = []

    for rel, path, all_rules in covered_files():
        raw = path.read_text(encoding="utf-8")
        lines = strip_block_comments(raw).splitlines()
        bare_lines = code_only(raw).splitlines()
        failures.extend(helper_arguments(raw, markers, rel))
        statements = statement_starts(lines)

        for n, line in enumerate(lines, 1):
            code = line.split("//", 1)[0]
            if not code.strip():
                continue

            # Exempt only the physical call line, never neighboring emitters.
            if all_rules and BARE_LOG_RE.search(bare_lines[n - 1]):
                context = lines[n - 1]
                if not EXEMPT_RE.search(context):
                    failures.append(
                        f"{rel}:{n}: bare {BARE_LOG_RE.search(bare_lines[n - 1]).group(0)} — log through "
                        f"this subsystem's helper so the marker and tier are applied in one "
                        f"place. If this is a crash-safe writer, bootstrap fallback, or terminal sink, append "
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
        raw = path.read_text(encoding="utf-8")
        failures.extend(helper_arguments(raw, markers, rel))
        for n, bare in enumerate(code_only(raw).splitlines(), 1):
            if BARE_LOG_RE.search(bare):
                failures.append(f"{rel}:{n}: raw QML/JS logging bypasses WebDebugLogger's shared formatter")
        for n, line in enumerate(raw.splitlines(), 1):
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
    # Guard future changes to the source roots. Historically, hand-maintained
    # per-file lists did not keep up with the
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
                f"the marker rules do not run on it. Extend COVERED_GLOBS to include its first-party source root.")

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


def self_test():
    """Run the production checker against isolated positive/negative source trees."""
    import contextlib
    import io
    import tempfile
    global REPO, LOGTAGS
    original_repo, original_tags = REPO, LOGTAGS
    # Preserve the real registry/helpers so fixtures also test registry derivation.
    helpers = {name: (REPO / name).read_text() for name in (
        "src/core/logtags.h", "src/core/diagnosticlogging.h")}
    cases = [
        ("src/mixed.cpp", 'void f() { printf("bare\\n"); }', False),
        ("src/main.cpp", 'void f() { qDebug() << "bare"; }', False),
        ("src/core/mixed.h", 'void f() { qWarning() << prefix << "x"; }', False),
        ("src/mixed.cpp", 'void f() { qInfo() << "[lowercase] x"; }', False),
        ("src/mixed.cpp", 'void f() { DIAG_DEBUG(UNKNOWN, "x") << "x"; }', False),
        ("src/mixed.cpp", 'void f() { DIAG_DEBUG(owner, "x") << "x"; }', False),
        ("src/mixed.cpp", 'void f() { DIAG_DEBUG(APP, "x") << "[unregistered] x"; }', False),
        ("qml/View.qml", 'Item { Component.onCompleted: console.warn("x") }', False),
        ("qml/shared.js", 'function f() { console.log(prefix + "x") }', False),
        ("qml/View.qml", 'Item { Component.onCompleted: WebDebugLogger.warn(owner, "x", "x") }', False),
        ("android/Bridge.java", 'void f() { Log.w("x", "bad"); }', False),
        ("android/Bridge.java", 'void f() { Log.println(5, "x", "bad"); }', False),
        ("ios/Bridge.mm", 'void f() { NSLog(@"bad"); }', False),
        ("src/mixed.cpp", 'void f() { DIAG_WARN(APP, "x") << "valid"; }', True),
        ("qml/View.qml", 'Item { Component.onCompleted: WebDebugLogger.warn("App", "x", "ok") }', True),
        ("android/Bridge.java", 'void f() { DiagnosticLog.w("App", "x", "ok"); }', True),
        ("src/crash.cpp", 'void f() { fprintf(stderr, "crash"); } // log-marker-exempt: crash-safe writer cannot reenter Qt', True),
        ("src/mixed.cpp", '// log-marker-exempt: crash-safe writer cannot reenter Qt\nvoid f() { qDebug() << "not exempt"; }', False),
    ]
    try:
        for rel, source, accepted in cases:
            with tempfile.TemporaryDirectory(prefix="log-marker-fixture-") as directory:
                REPO = Path(directory)
                LOGTAGS = REPO / "src/core/logtags.h"
                for name, text in {**helpers, rel: source}.items():
                    path = REPO / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(text)
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    result = main()
                if (result == 0) != accepted:
                    raise AssertionError(f"fixture {rel}: {source}\n{output.getvalue()}")
    finally:
        REPO, LOGTAGS = original_repo, original_tags
    print(f"OK: {len(cases)} marker-gate fixtures")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv else main())
