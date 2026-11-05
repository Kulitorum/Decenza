#!/usr/bin/env python3
"""Run qmllint over the QML tree and report what it found, per category and per file.

WHY IT READS JSON, NEVER THE TEXT OUTPUT
----------------------------------------
qmllint's text output loses findings. Each file's messages are buffered until the file is done
(qqmljslinter.cpp:685, flushed at :617), and a function whose type propagation re-runs (typically a
loop) clears the WHOLE buffer (qqmljslogger.cpp:411-418 -> qcoloroutput.cpp:64-67). The message
list behind --json is untouched. On Qt 6.12.0 that hid 60 findings in this tree, four of them
ReferenceErrors that shipped (PostShotReviewPage `pa`, #2176's log). Upstream: QTBUG-151969, fixed
by Gerrit 768762 for 6.12.1; JSON stays because it names every file linted, so coverage is proven.

WHAT IT IS FOR
--------------
Two jobs, deliberately in one place so they cannot disagree:

  --report    human-readable summary: categories, worst files, top identifiers.
  --check     the gate. Compares against the baseline and exits non-zero on a regression.

THE `unqualified` CATEGORY IS SPECIAL
-------------------------------------
Every other category is enforced globally: present in the exemption list or the build fails.
`unqualified` is enforced per file instead, because its residue (delegate and file-scope names whose
remedy is `pragma ComponentBehavior: Bound`) was cleared file by file. The baseline holds the counts.

So `unqualified` is enforced PER FILE: files at zero are locked at zero, files above it carry a
ceiling that may only fall. Suppressing the category outright is never an option — it is the
only automated detector for an undeclared QML identifier, which compiles clean and throws only
when its binding is first evaluated. That exact failure shipped in 2.0.1 (fixed by PR Kulitorum/Decenza#1661).
"""

# PEP 604 annotations (`Path | None`) are used below and are 3.10+ syntax, but CMake's
# Python3_EXECUTABLE on macOS is Xcode's python3.9 — which evaluates annotations eagerly and
# raises TypeError at import, before argparse ever runs. Deferring them keeps the CMake targets
# working on the oldest interpreter that can be picked to run this.
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import tempfile
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "qml-diagnostics-baseline.json"

# Categories not yet cleared, with the count each covers. Any category absent here fails the gate
# on its first occurrence. HAND-EDITED AND ONLY EVER LOSES ENTRIES: it is not in the baseline
# because a regenerated exemption list would re-bless whatever the tool reported that day.
# Correcting a measurement is the one reason a count may go up; a regression never is.
# `unqualified` is never eligible (see the module docstring).
#
# Empty: every category is at zero. Two fixes from clearing them that the docs do not hold:
#   equality-type-coercion: a `property url` reaches JS as a UrlObject whose default
#     virtualIsEqualTo is false, so `!==` on it is always true. Compare `.toString()`.
#   duplicate-property-binding: `NumberAnimation on X` overrides X's initialiser, which was dead.
CATEGORY_EXEMPTIONS: dict[str, int] = {}

IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

# QML that is deliberately outside the module. Kept to exactly the files that have a reason,
# because the check this suppresses — a .qml on disk that nothing bundles or lints — is one of
# the failure modes this gate exists to catch, and a loose pattern here would switch it off.
# These belong to the Wasm Decenza module, registered and linted by src/webclient/CMakeLists.txt
# in the Wasm build. They must not be bundled into the native Decenza module.
NOT_IN_MODULE_BY_DESIGN: set[str] = {
    "qml/webclient/Main.qml",
}

# The two QML modules built from this tree. The Wasm one (src/webclient/CMakeLists.txt) shares
# most sources but resolves them against its own type registry, so it is linted separately against
# the same baseline. It stages copies at Decenza/<path under its source root>, native at
# Decenza/qml/<path>. The registry and not-in-module checks are native-only: the Wasm registry has
# a different singleton set, and most of the tree is legitimately absent from that module.
MODULES = {
    "native": {"rsp": "Decenza.rsp", "copy_roots": {"qml": "Decenza/qml"}, "whole_tree": True},
    "wasm": {"rsp": "decenza-web.rsp",
             "copy_roots": {"qml": "Decenza", "tools/webclient": "Decenza"}, "whole_tree": False},
}

def relative_to_repo(path: str) -> str:
    """The repo-relative key for a path from qmllint's JSON or Qt's response file.

    The ONE normaliser: both sides of every coverage comparison go through it. resolve() makes it
    agree on drive-letter case and symlinked checkouts, where a string prefix test did not, and a
    mismatch there filed every finding under a key no file had, so the tree read as clean.
    """
    p = Path(path)
    if not p.is_absolute():
        return path.replace("\\", "/")
    resolved = p.resolve()
    return resolved.relative_to(REPO).as_posix() if resolved.is_relative_to(REPO) else resolved.as_posix()


def find_qmllint() -> str:
    for c in (
        Path.home() / "Qt/6.12.0/macos/bin/qmllint",
        Path("C:/Qt/6.12.0/msvc2022_64/bin/qmllint.exe"),
    ):
        if c.exists():
            return str(c)
    from shutil import which

    found = which("qmllint")
    if not found:
        sys.exit("qmllint not found. Pass --qmllint <path>.")
    return found


def default_import_path() -> str:
    """Find a build directory containing the generated `Decenza` module.

    CMake passes --import-path explicitly, so this only serves someone running the script by
    hand. Naming a specific build directory here would be wrong for everyone whose kit name
    differs, so find one instead: any directory holding Decenza/qmldir, most recently written
    first. Returns a plausible-looking path when there is no candidate, so the caller's
    "build first" error names something concrete rather than an empty string.
    """
    candidates = sorted(
        (p.parent.parent for p in REPO.glob("build*/**/Decenza/qmldir")),
        key=lambda d: d.stat().st_mtime,
        reverse=True,
    )
    return str(candidates[0].relative_to(REPO)) if candidates else "build"


def qml_files() -> list[str]:
    # .as_posix(), never str(): on Windows str() yields backslashes while every key
    # recovered from qmllint output is normalised to forward slashes by
    # relative_to_repo(). Mismatched keys mean zero warnings attach to any file and
    # the whole tree reads as clean — a silently green gate on a supported platform.
    return sorted(p.relative_to(REPO).as_posix() for p in (REPO / "qml").rglob("*.qml"))


def response_file(import_path: str, module: str = "native") -> Path | None:
    """Qt's own generated qmllint argument file, if the build produced one.

    `qt_add_qml_module` emits <build>/.rcc/qmllint/Decenza.rsp holding the exact flags and
    file list Qt's built-in `Decenza_qmllint` target uses. Prefer it over hand-assembled
    arguments: it is generated from the module definition, so it cannot disagree with how the
    module is actually built, and it carries whatever resource and import flags this Qt version
    needs. Guessing at those instead is how a lint run ends up reporting failures that are
    really just a missing flag.
    """
    rsp = REPO / import_path / ".rcc" / "qmllint" / MODULES[module]["rsp"]
    return rsp if rsp.is_file() else None


def split_response(rsp: Path) -> tuple[list[str], list[str]]:
    """Split Qt's response file into (flags, .qml paths).

    The flags have to be reused verbatim — they are what makes the run match how the module is
    really compiled — but the file list has to be splittable, for the reason in run().
    """
    flags, files = [], []
    for line in rsp.read_text().split("\n"):
        entry = line.strip()
        if not entry:
            continue
        # .js/.mjs count as inputs, not flags. Qt lints the module's JavaScript too, and treating
        # those paths as flags would append them to EVERY batch — their warnings counted once
        # per batch, and their per-file ceilings inflated by however many batches ran.
        (files if entry.endswith((".qml", ".js", ".mjs")) else flags).append(entry)
    return flags, files


def files_from_response(rsp: Path) -> list[str]:
    """The .qml/.js paths Qt's response file actually lints, repo-relative."""
    return sorted(relative_to_repo(entry) for entry in split_response(rsp)[1])


# The JSON revisions this reader has been checked against. A new one fails loudly: a schema change
# that keeps the keys but changes their meaning (0-based columns, relative filenames) would
# otherwise be read silently.
KNOWN_JSON_REVISIONS = {6}


def run(qmllint: str, import_path: str, files: list[str], rsp: Path | None,
        raw_out: str | None, batch: int, timeout: int) -> list[dict]:
    """Lint the module and return qmllint's per-file JSON entries; abort rather than under-report.

    One process unless --batch. qmllint's exit status is checked, and read_json() proves every
    file has an entry: a run that stops early would otherwise record each unreached file as clean.
    """
    if rsp:
        flags, all_files = split_response(rsp)
    else:
        flags, all_files = ["-I", import_path], list(files)

    # Opt-in categories (file-selector imports hide types from the tooling; a function used before
    # its declaration). --ignore-settings: qmllint otherwise applies any .qmllint.ini from each
    # file's directory up to the filesystem root, so a stray one above the checkout could disable a
    # category while the gate printed "passed" (qqmltoolingsettings.cpp:139-185).
    flags += ["--import-file-selector=warning", "--function-used-before-declaration=warning",
              "--ignore-settings"]

    # Both lists come from the same .rsp, so only the count can differ.
    if rsp and len(all_files) != len(files):
        sys.exit(
            f"internal: the response file lists {len(all_files)} file(s) but {len(files)} are "
            f"being accounted for. These must match — a file linted but not counted is invisible, "
            f"and a file counted but not linted records as clean."
        )
    work = Path(tempfile.mkdtemp(prefix="qmllint_"))
    log_path = work / "qmllint.log"  # --json silences qmllint's text (qqmljslinter.cpp:688-689); this keeps its own errors
    size = batch if batch > 0 else len(all_files)
    chunks = [all_files[i:i + size] for i in range(0, len(all_files), size)]
    entries: list[dict] = []
    revisions: set[int] = set()
    with log_path.open("w") as fh:
        for n, chunk in enumerate(chunks, 1):
            json_path = work / f"batch{n}.json"
            what = f"batch {n}/{len(chunks)}"
            try:
                proc = subprocess.run(
                    [qmllint, *flags, "--json", str(json_path), *chunk],
                    cwd=REPO,
                    stdout=fh,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=timeout,
                )
            except subprocess.TimeoutExpired:
                sys.exit(
                    f"qmllint exceeded {timeout}s on {what} "
                    f"({chunk[0] if len(chunk) == 1 else f'{len(chunk)} files from {chunk[0]}'}).\n"
                    f"Historically this was a qmllint bug rather than a hung machine: merge() in "
                    f"QQmlJSRegisterContent was tree-recursive, and one layout item drove the "
                    f"linter to a 313 GB footprint and an OOM kill. Qt 6.11.2 memoizes it "
                    f"(Gerrit 757430), so a timeout on 6.11.2 or later is something new.\n"
                    f"Report it and name the file. Do NOT respond by excluding the file: a file "
                    f"the linter never reached emits no warnings and records as clean."
                )
            fh.flush()
            # Non-zero also means an error-severity finding or a parse failure, with the JSON
            # complete (tools/qmllint/main.cpp:564-565, :612); warnings exit 0. So read the JSON
            # first: findings fail the gate as findings, and only a missing or partial JSON is a
            # tool failure.
            batch_entries, revision, problem = read_json(json_path, chunk)
            if problem:
                why = ("" if proc.returncode == 0 else
                       f"qmllint {'killed by signal ' + str(-proc.returncode) if proc.returncode < 0 else 'exited ' + str(proc.returncode)}. ")
                sys.exit(
                    f"{what}: {why}{problem}\n"
                    f"Only {len(entries)} of {len(all_files)} files were accounted for, so no count "
                    f"from this run can be trusted. qmllint's own output: {log_path}"
                )
            entries += batch_entries
            revisions.add(revision)
            print(f"\r  linted {len(entries)}/{len(all_files)} files", end="", file=sys.stderr,
                  flush=True)
    print(file=sys.stderr)
    shutil.rmtree(work, ignore_errors=True)
    if raw_out:
        # --from-raw re-validates the revision, so it must survive the merge of the batches.
        if len(revisions) != 1:
            sys.exit(f"internal: batches reported JSON revisions {sorted(revisions)}; cannot write "
                     f"one --raw-out file for them.")
        Path(raw_out).write_text(json.dumps({"revision": revisions.pop(), "files": entries}) + "\n")
    return entries


def read_json(path: Path, expected: list[str]) -> tuple[list[dict], int, str]:
    """(entries, revision, "") for qmllint --json output with exactly one entry per expected
    file, else ([], 0, why). A file the linter never reached has no entry, and would otherwise
    record as clean.
    """
    try:
        doc = json.loads(path.read_text())
        entries = doc["files"]
        for e in entries:  # touch every field tally() reads, so a schema change fails here
            e["filename"]
            for w in e["warnings"]:
                w["id"], w["type"], w["message"]
    except (OSError, ValueError, KeyError, TypeError) as e:
        return [], 0, f"cannot read qmllint JSON at {path} ({e!r})."
    if doc.get("revision") not in KNOWN_JSON_REVISIONS:
        return [], 0, (f"qmllint JSON revision {doc.get('revision')!r} is not one this reader has been "
                    f"checked against ({sorted(KNOWN_JSON_REVISIONS)}). Re-verify tally() and "
                    f"qml_qualify.py against it, then add it to KNOWN_JSON_REVISIONS.")
    want = Counter(relative_to_repo(f) for f in expected)
    got = Counter(relative_to_repo(e["filename"]) for e in entries)
    if want != got:
        missing, extra = sorted((want - got).elements()), sorted((got - want).elements())
        return [], 0, (
            f"qmllint's JSON does not match the files it was given — "
            f"{len(missing)} missing, {len(extra)} unexpected.\n"
            + "".join(f"  missing: {f}\n" for f in missing[:10])
            + "".join(f"  unexpected: {f}\n" for f in extra[:10])
            + "A missing file was never analysed and would record as clean.")
    # Open/parse failures also clear `success`, as findings do; with no finding to explain it,
    # the file was not linted.
    unexplained = [e["filename"] for e in entries if not e.get("success", True) and not e["warnings"]]
    if unexplained:
        return [], 0, (f"qmllint reported failure with no diagnostic for "
                    f"{', '.join(unexplained[:5])}. Those files were not linted.")
    return entries, doc["revision"], ""


def load_json(path: Path, expected: list[str], what: str) -> list[dict]:
    entries, _, problem = read_json(path, expected)
    if problem:
        sys.exit(f"{what}: {problem}")
    return entries


def format_finding(path: str, w: dict) -> str:
    where = f"{path}:{w['line']}:{w.get('column', 0)}" if "line" in w else path
    return f"{w['type'].capitalize()}: {where}: {' '.join(w['message'].split())} [{w['id']}]"


def tally(entries: list[dict]) -> tuple[Counter, dict[str, Counter]]:
    """-> (per-category counts, per-file unqualified identifiers)"""
    categories: Counter = Counter()
    unqualified: dict[str, Counter] = defaultdict(Counter)
    sources: dict[str, list[str]] = {}
    for entry in entries:
        path = relative_to_repo(entry["filename"])
        for w in entry["warnings"]:
            category = w["id"]
            categories[category] += 1
            if category != "unqualified":
                continue
            # Per file even without a location: unqualified is enforced nowhere else.
            if "line" not in w:
                unqualified[path]["<no location>"] += 1
                continue
            # The identifier, for --report. A column counts UTF-16 units, so it can be off by one
            # per astral character earlier on the line; harmless for a label.
            if path not in sources:
                src_file = REPO / path
                sources[path] = (src_file.read_text(errors="replace").split("\n")
                                 if src_file.is_file() else [])
            lines = sources[path]
            src = lines[w["line"] - 1] if 0 < w["line"] <= len(lines) else ""
            start = w.get("column", 0) - 1
            tok = IDENT_RE.match(src[start:]) if 0 <= start < len(src) else None
            unqualified[path][tok.group(0) if tok else "<unparsed>"] += 1
    return categories, unqualified


def build_state(categories: Counter, unqualified: dict, files: list[str]) -> dict:
    counts = {f: sum(unqualified[f].values()) for f in files if unqualified.get(f)}
    return {
        # A generated file that someone will open and try to interpret, so it says what it is.
        "_README": [
            "Generated by scripts/qmllint_report.py --update-baseline. Do not hand-edit.",
            "'clean': files with zero unqualified warnings. They must stay at zero, and a new "
            "QML file joins them automatically — new code never starts with a budget.",
            "'ceilings': files that still have unqualified warnings. These are ceilings, not "
            "budgets: a count may fall, never rise. Most of what is left is delegate and "
            "file-scope identifiers (modelData, root, index, model), whose remedy is "
            "'pragma ComponentBehavior: Bound' plus required properties on delegates — a "
            "semantic change to how a component resolves names, not a lint suppression.",
            "Category exemptions are NOT here. They are hand-edited in the script, because "
            "this file is regenerated and an exemption list that regenerates is not one.",
        ],
        "clean": sorted(f for f in files if f not in counts),
        "ceilings": dict(sorted(counts.items())),
        # Informational only. Category enforcement reads CATEGORY_EXEMPTIONS, never this —
        # see the comment on that block for why a regenerated file must not be the authority.
        "observed_categories": dict(sorted(categories.items())),
    }


def check_fresh(import_path: str, files: list[str], module: str = "native") -> None:
    """Refuse to run against a stale build directory.

    qmllint resolves the `Decenza` module through <import-path>/Decenza/qmldir, whose entries
    point at the build directory's OWN COPY of the QML — not the working tree. So the module
    types it sees are as old as the last build, while the files it analyses are current.

    A stale copy has been measured to change the counts by 4x, so being stale is a hard error.

    Compares CONTENT, not mtime. mtime is wrong in both directions here: the build copies with
    `copy_if_different`, so an unchanged file keeps an older timestamp forever, and any git
    operation that rewrites the working tree (a pull, a branch switch) bumps every source mtime
    without changing a byte. An mtime check reported this very tree stale immediately after a
    successful build.
    """
    roots = MODULES[module]["copy_roots"]
    module_qml = REPO / import_path / roots["qml"]
    if not module_qml.is_dir():
        sys.exit(
            f"No module QML at {module_qml}. Build first — without it qmllint cannot resolve "
            f"Decenza types, and every module reference is reported as unqualified."
        )
    whole_tree = MODULES[module]["whole_tree"]
    stale, missing = [], []
    for f in files:
        root = next((r for r in roots if Path(f).is_relative_to(r)), None)
        copy = REPO / import_path / roots[root] / Path(f).relative_to(root) if root else None
        if copy is None or not copy.exists():
            # Native passes the whole tree: a file outside the module has no copy, and whether
            # that is intended is the unlisted-file check's business. Wasm passes only its own
            # module files, so a missing copy means the mapping or the build is wrong.
            if not whole_tree or f not in NOT_IN_MODULE_BY_DESIGN:
                missing.append(f)
            continue
        if copy.read_bytes() != (REPO / f).read_bytes():
            stale.append(f)
    if stale:
        sys.exit(
            "Build directory is STALE — these files differ from the copy qmllint will resolve\n"
            "module types against, so the counts would not describe this source:\n"
            + "".join(f"  {f}\n" for f in stale[:10])
            + (f"  ... and {len(stale) - 10} more\n" if len(stale) > 10 else "")
            + "Rebuild, then re-run. (--allow-stale to override.)"
        )
    if missing and not whole_tree:
        sys.exit(
            f"{len(missing)} {module} module file(s) have no build copy under {import_path}, so "
            f"freshness cannot be checked: {', '.join(missing[:5])}"
            + (" ..." if len(missing) > 5 else "")
            + "\nBuild the module, or fix MODULES['" + module + "']['copy_roots']."
        )
    if missing:
        print(
            f"note: {len(missing)} QML file(s) are not in the module and are linted without "
            f"module context: {', '.join(missing[:3])}"
            + (" ..." if len(missing) > 3 else ""),
            file=sys.stderr,
        )


def check_registry_fresh(import_path: str) -> None:
    """Refuse to run when the TYPE REGISTRY is a generation behind the C++ sources.

    check_fresh() above compares the QML, and it cannot see this one. qmllint resolves module
    types through <build>/Decenza/Decenza.qmltypes, which qmltyperegistrar generates from the C++
    QML_* macros. Change a registration in a header, skip the rebuild, and every QML file matches
    its build copy byte for byte while the registry still describes the previous generation — so
    the counts are measured against types that no longer match the source, and check_fresh() says
    nothing because no QML changed.

    That is not hypothetical either, and it is more expensive than the stale-QML case because it
    fails SILENTLY IN THE SAFE-LOOKING DIRECTION. PR Kulitorum/Decenza#1680 regenerated the baseline from a build
    whose registry predated its own C++ changes: the newly-migrated names did not resolve, qmllint
    gave up early on expressions it could not type, and it therefore reported FEWER warnings than
    the tree really produces. Those numbers were written to the baseline as ceilings. Every later
    honest run then failed against targets the tree could not meet, and the failure surfaced on
    the nightly a day later rather than on the machine that caused it.

    Content-based, like check_fresh, for the same reason: any git operation rewrites source
    mtimes, so a timestamp comparison reports a freshly built tree as stale. The check is that
    every singleton the C++ registers is actually IN the registry. A new or renamed registration
    that has not been through qmltyperegistrar is missing from the exports, which is precisely the
    generation skew this catches.
    """
    qmltypes = REPO / import_path / "Decenza" / "Decenza.qmltypes"
    if not qmltypes.exists():
        sys.exit(f"No {qmltypes}. Build first — qmllint cannot resolve Decenza types without it.")
    registry = qmltypes.read_text(errors="replace")

    # A struct/class carrying QML_SINGLETON exports under QML_NAMED_ELEMENT(X) if present, else
    # under the foreign type from QML_FOREIGN(X), else its own name. Same resolution order
    # tst_qmlregistration.cpp uses; this is the cheap textual half of it.
    # The split CAPTURES the class name, because a plain QML_ELEMENT exports under it and there is
    # nowhere else to get it. The first version of this function discarded the name and had no
    # else-branch, so it silently collected 20 of the tree's 27 singletons — every QML_ELEMENT one,
    # including MainController, MachineState and TranslationManager, went unchecked. It passed its
    # negative control because that control used the QML_FOREIGN shape, exercising the branch that
    # worked. A guard that covers 74% of its subject while reporting success is the failure mode
    # this whole file exists to argue against, so see the count assertion below.
    declared: set[str] = set()
    for header in sorted((REPO / "src").rglob("*.h")):
        # This gate uses the native Decenza registry. Browser-only types belong
        # to the separate Wasm module, checked by tst_qmlregistration against
        # that target's qmltypes (moduleForHeader uses the same boundary).
        if header.is_relative_to(REPO / "src" / "webclient"):
            continue
        text = header.read_text(errors="replace")
        if "QML_SINGLETON" not in text:
            continue
        parts = re.split(r"^[ \t]*(?:class|struct)\s+(\w+)", text, flags=re.M)
        # re.split with one group yields [pre, name1, body1, name2, body2, ...].
        for cls, block in zip(parts[1::2], parts[2::2]):
            if not re.search(r"^\s*QML_SINGLETON\s*$", block, flags=re.M):
                continue
            named = re.search(r"QML_NAMED_ELEMENT\(\s*(\w+)\s*\)", block)
            foreign = re.search(r"QML_FOREIGN\(\s*(\w+)\s*\)", block)
            if named:
                declared.add(named.group(1))
            elif foreign:
                declared.add(foreign.group(1))
            else:
                declared.add(cls)
    if not declared:
        sys.exit("Found no QML_SINGLETON declarations under src/. The scan is broken, not the code.")

    # Presence is not enough: the registry lists an `exports:` line for every registered type,
    # singleton or not, so a type being MIGRATED to a singleton would still match on a stale
    # registry — it is already there as an uncreatable type. Require the export and the singleton
    # flag in the SAME component, which is what actually changes when the registration changes.
    stale_types = []
    components = registry.split("    Component {")
    for name in sorted(declared):
        owning = [c for c in components if f'"Decenza/{name} ' in c]
        if not owning:
            stale_types.append((name, "absent from the registry"))
        elif not any("isSingleton: true" in c for c in owning):
            stale_types.append((name, "present, but NOT as a singleton"))
    if stale_types:
        sys.exit(
            "Type registry is STALE — these are registered as QML singletons in C++ but the\n"
            f"registry at {qmltypes} disagrees, so qmllint is resolving against a previous\n"
            "generation and the counts would not describe this source:\n"
            + "".join(f"  {n}: {why}\n" for n, why in stale_types)
            + "Rebuild, then re-run. (--allow-stale to override, but not with --update-baseline.)\n"
            "This is the shape that produced the bad baseline in PR Kulitorum/Decenza#1680: an under-resolving run\n"
            "reports FEWER warnings, which looks like an improvement and ratchets the gate to a\n"
            "target the tree cannot meet."
        )


def cmd_report(state: dict, categories: Counter, unqualified: dict, files: list[str]) -> int:
    total_unq = sum(sum(c.values()) for c in unqualified.values())
    print(f"QML files: {len(files)}")
    print(f"clean (zero unqualified): {len(state['clean'])}  |  dirty: {len(state['ceilings'])}\n")
    print("by category:")
    for cat, n in categories.most_common():
        print(f"  {n:6d}  {cat}")
    print(f"\nworst files ({total_unq} unqualified total):")
    for f, n in Counter(state["ceilings"]).most_common(10):
        print(f"  {n:6d}  {f}")
    ids: Counter = Counter()
    for c in unqualified.values():
        ids.update(c)
    print("\ntop flagged identifiers:")
    for name, n in ids.most_common(15):
        print(f"  {n:6d}  {name}")
    return 0


def cmd_check(state: dict, unlisted: list[str] | None = None) -> int:
    if not BASELINE.exists():
        sys.exit(f"No baseline at {BASELINE}. Run with --update-baseline first.")
    base = json.loads(BASELINE.read_text())
    failures: list[str] = []

    # A .qml that left qt_add_qml_module is neither bundled nor linted. It vanishes from
    # `files`, so every per-file check below silently passes and `improved` counts its whole
    # former ceiling — the gate would print "N improvement(s) ... run --update-baseline" and
    # invite someone to bake the mistake in. It has to fail, not warn.
    for f in unlisted or []:
        failures.append(
            f"{f}: exists but is not in the qt_add_qml_module list, so it is neither linted nor "
            f"bundled.\n    Add it to CMakeLists.txt, or to NOT_IN_MODULE_BY_DESIGN if that is "
            f"deliberate."
        )

    base_clean = set(base["clean"])
    base_ceilings = dict(base["ceilings"])
    now_ceilings = state["ceilings"]

    # A file on the clean list must stay at zero. This is the rule that would have caught the defect PR Kulitorum/Decenza#1661 fixed.
    for f in sorted(base_clean & set(now_ceilings)):
        failures.append(
            f"{f}: {now_ceilings[f]} unqualified warning(s) in a file that had none.\n"
            f"    Make the identifier resolvable. Do not add the file to the baseline."
        )

    # A new file starts clean — new code never gets to carry a backlog.
    known = base_clean | set(base["ceilings"])
    for f in sorted(set(now_ceilings) - known):
        failures.append(f"{f}: new QML file with {now_ceilings[f]} unqualified warning(s); new files must be clean.")

    # Ceilings are ceilings, not budgets.
    for f, limit in sorted(base_ceilings.items()):
        now = now_ceilings.get(f, 0)
        if now > limit:
            failures.append(f"{f}: unqualified rose {limit} -> {now}.")

    # Every other category is enforced globally, against the hand-edited block above rather
    # than against anything the baseline file regenerates.
    ceilings = CATEGORY_EXEMPTIONS
    for cat, n in sorted(state["observed_categories"].items()):
        if cat == "unqualified":
            continue
        if cat not in ceilings:
            failures.append(
                f"category '{cat}': {n} occurrence(s), and the category is not exempt.\n"
                f"    Fix them. Adding an entry to CATEGORY_EXEMPTIONS is for recording an "
                f"existing backlog, not for admitting a new one."
            )
        elif n > ceilings[cat]:
            failures.append(f"category '{cat}' rose {ceilings[cat]} -> {n}.")

    if failures:
        print("QML diagnostics gate FAILED:\n")
        for f in failures:
            print(f"  - {f}")
        print(
            "\nIf you fixed warnings and the counts fell, re-run with --update-baseline "
            "and commit the result."
        )
        return 1

    # State the coverage on the same line as the verdict. "clean: N/N" is the number a
    # reader checks against the module's file count; a gate that passed over fewer files than the
    # module contains has not passed, and the only way anyone notices is if the count is printed.
    analysed = len(state["clean"]) + len(now_ceilings)
    print(f"QML diagnostics gate passed. clean: {len(state['clean'])}/{analysed} "
          f"({analysed} file(s) analysed, the whole module)")
    # Warnings removed. A file that went 90 -> 0 counts 90 here; an earlier version added 1
    # more for it having left the ceilings map, double-counting every fully-cleared file.
    improved = sum(
        max(0, limit - now_ceilings.get(f, 0)) for f, limit in base_ceilings.items()
    )
    if improved:
        print(f"{improved} improvement(s) not yet in the baseline — run --update-baseline to lock them in.")
    # An exemption whose count has reached zero is finished work that nobody has noticed.
    # Say so, or the block keeps its length while its contents mean nothing.
    for cat, limit in sorted(CATEGORY_EXEMPTIONS.items()):
        now = state["observed_categories"].get(cat, 0)
        if now == 0:
            print(f"category '{cat}' is clear — delete its CATEGORY_EXEMPTIONS entry.")
        elif now < limit:
            print(f"category '{cat}' fell {limit} -> {now} — lower its CATEGORY_EXEMPTIONS entry.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--qmllint", default=None)
    ap.add_argument("--import-path", "-I", default=None,
                    help="build directory holding the generated Decenza module "
                         "(default: autodetected)")
    ap.add_argument("--raw-out", default=None, help="also write qmllint's JSON findings here")
    ap.add_argument("--from-raw", default=None,
                    help="read a JSON file written by --raw-out instead of linting")
    ap.add_argument("--timeout", type=int, default=600,
                    help="seconds before a qmllint process is treated as hung (default 600)")
    ap.add_argument("--batch", type=int, default=0,
                    help="files per qmllint process; 0 (the default) means one process for the "
                         "whole tree, which is how Qt's own Decenza_qmllint target runs it. "
                         "BATCHING CHANGES RESULTS: files linted together resolve types from "
                         "their siblings on the command line, so smaller batches invent extra "
                         "import/missing-property warnings (measured: 22 -> 34 and 323 -> 351 "
                         "at --batch 10). It exists only as an escape hatch for a qmllint that "
                         "cannot finish the tree in one process.")
    ap.add_argument("--allow-stale", action="store_true",
                    help="skip the staleness checks, QML and type registry (for debugging only)")
    ap.add_argument("--allow-ceiling-rise", action="store_true",
                    help="permit --update-baseline to write a HIGHER ceiling for a file, or move "
                         "one off the clean list. Relaxing the gate, so it is never implicit; "
                         "record the reason in the change's bugs-found.md.")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--report", action="store_true", help="human-readable summary (default)")
    g.add_argument("--check", action="store_true", help="gate: fail on regression")
    g.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--module", choices=sorted(MODULES), default="native",
                    help="which QML module to lint (default native). The baseline is native's; "
                         "wasm is checked against it and never writes it.")
    args = ap.parse_args()

    if args.module != "native" and args.update_baseline:
        sys.exit("--update-baseline is native-only: the baseline describes the native module, and "
                 "the Wasm module is held to it rather than recorded into it.")

    if args.from_raw and args.update_baseline:
        sys.exit(
            "--update-baseline refuses --from-raw. The staleness checks cannot run against "
            "captured output, so nothing proves it describes this source. "
            "Regenerate the baseline from a live run."
        )
    if args.allow_stale and args.update_baseline:
        sys.exit(
            "--update-baseline refuses --allow-stale. Those two together are precisely how the "
            "PR Kulitorum/Decenza#1680 baseline was written: the freshness checks are the only thing standing between "
            "a build that under-resolves and a set of ceilings the tree cannot meet, and a "
            "baseline is the one output where being wrong is durable — it becomes the target every "
            "later run is measured against. --allow-stale exists for reading a report off a build "
            "you know is behind, not for recording one. Rebuild, then write."
        )

    on_disk = qml_files()
    if not on_disk:
        sys.exit("No QML files found.")
    import_path = args.import_path or default_import_path()
    spec = MODULES[args.module]
    rsp = response_file(import_path, args.module)
    # The hand-built fallback lints .qml only, without --bare or --resource: fine for a --report,
    # never for a verdict. A Qt that renamed the .rsp would otherwise weaken the gate silently.
    if not rsp and (args.check or args.update_baseline or not spec["whole_tree"]):
        sys.exit(f"No {import_path}/.rcc/qmllint/{spec['rsp']}. Build the {args.module} module "
                 f"first; --check and --update-baseline need Qt's own lint arguments.")
    # The module's own file list when it has one; the Wasm module lints a subset of the tree.
    module_files = files_from_response(rsp) if rsp else on_disk
    # --from-raw analyses output captured earlier, so the build directory's state now says
    # nothing about it. Checking freshness would be checking the wrong thing.
    if not args.allow_stale and not args.from_raw:
        check_fresh(import_path, on_disk if spec["whole_tree"] else module_files, args.module)
        # Native only: the Wasm registry exports a different singleton set. A stale one
        # under-reports, but the Wasm leg cannot write the baseline, and its build step depends
        # on the registry (src/webclient/CMakeLists.txt), so an under-report is never recorded.
        if spec["whole_tree"]:
            check_registry_fresh(import_path)

    unlisted: list[str] = []
    if rsp:
        files = module_files
        # A .qml on disk that the module does not list is invisible to qmllint AND missing from
        # the app's resources at runtime — CLAUDE.md calls out forgetting the CMakeLists entry as
        # a known footgun. Without this the file would silently count as clean, so the gate would
        # quietly reward the mistake.
        if spec["whole_tree"]:
            unlisted = sorted(set(on_disk) - set(files) - NOT_IN_MODULE_BY_DESIGN)
        if unlisted:
            print(
                f"warning: {len(unlisted)} QML file(s) exist but are not in the qt_add_qml_module "
                f"list, so they are neither linted nor bundled:\n"
                + "".join(f"  {f}\n" for f in unlisted),
                file=sys.stderr,
            )
    else:
        files = on_disk
        print(
            f"note: no {import_path}/.rcc/qmllint/Decenza.rsp — falling back to a hand-built "
            f"argument list, which may not match how the module is really compiled.",
            file=sys.stderr,
        )
    if args.from_raw:
        # Exactly one entry per module file, or it refuses: a truncated capture would otherwise
        # record every unreached file as clean.
        entries = load_json(Path(args.from_raw), files, args.from_raw)
    else:
        entries = run(args.qmllint or find_qmllint(), import_path, files, rsp,
                      args.raw_out, args.batch, args.timeout)
    categories, unqualified = tally(entries)
    state = build_state(categories, unqualified, files)

    if args.check:
        result = cmd_check(state, unlisted)
        if result:
            # Keep the actual file/line findings in Qt Creator's build output;
            # category totals alone force another lint run to locate a defect.
            print("\nQML findings:")
            for entry in entries:
                for w in entry["warnings"]:
                    print(format_finding(relative_to_repo(entry["filename"]), w))
        return result
    if args.update_baseline:
        # A ceiling that RISES is the one edit this file exists to resist, and until now
        # --update-baseline wrote it without comment — the same keystroke that records a genuine
        # improvement also relaxes the gate, and the output looked identical either way. Raising
        # one is sometimes right (the PR Kulitorum/Decenza#1680 baseline recorded three ceilings the tree could not
        # meet, and correcting them meant raising them), but it is never routine, so it costs a
        # flag and prints what it did.
        rises, drops = [], 0
        if BASELINE.exists():
            try:
                prev = json.loads(BASELINE.read_text())
            except json.JSONDecodeError as e:
                sys.exit(f"{BASELINE} is not valid JSON ({e}). Refusing to write: the rise check "
                         "below can only compare against a baseline it can read, and skipping it "
                         "silently is how an unmeetable ceiling gets recorded.")
            if not isinstance(prev, dict) or "ceilings" not in prev or "clean" not in prev:
                sys.exit(f"{BASELINE} is missing 'ceilings'/'clean'. Refusing to write: with an "
                         "unrecognised schema every file reads as having no history, so no rise "
                         "could ever be detected and the check would pass by being blind.")
            # Indexed, not .get()-with-default — see the schema check just above. A default would
            # make every comparison below vacuous and the write unconditional, which is a guard
            # that silently declines to run: the exact shape this whole change argues against.
            old = prev["ceilings"]
            was_clean = set(prev["clean"])
            for f, n in state["ceilings"].items():
                was = old.get(f)
                if was is None:
                    # Not in ceilings. Either it was clean, or it is a file the baseline has never
                    # seen — and BOTH are rises from zero. The new-file case matters most: the
                    # baseline's own _README promises "new code never starts with a budget", and
                    # cmd_check() enforces it, so letting --update-baseline hand a brand-new file
                    # a ceiling silently is the one edit that quietly repeals that rule.
                    rises.append((f, 0, n))
                elif n > was:
                    rises.append((f, was, n))
                elif n < was:
                    drops += 1
            # A file that reached zero leaves state["ceilings"] entirely, so counting drops only
            # over the surviving entries understates exactly the work worth reporting.
            drops += sum(1 for f in old if f not in state["ceilings"])
        if rises and not args.allow_ceiling_rise:
            sys.exit(
                "refusing to write: this would RAISE %d ceiling(s), which relaxes the gate:\n"
                % len(rises)
                + "".join(f"  {f}: {was} -> {now}\n" for f, was, now in rises)
                + "\nA rise is not always wrong — a baseline measured against a stale build can\n"
                "record a target the tree cannot meet, and correcting it means raising it. But it\n"
                "is never routine, so say so explicitly:\n"
                "  --update-baseline --allow-ceiling-rise\n"
                "and record why in the change's bugs-found.md. If you did NOT expect a rise, the\n"
                "likely cause is a stale build: rebuild and re-run before writing anything."
            )
        BASELINE.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n")
        print(
            f"Baseline written: {len(state['clean'])} clean, "
            f"{len(state['ceilings'])} with ceilings, {len(state['observed_categories'])} categories."
        )
        if rises:
            print(f"RAISED {len(rises)} ceiling(s) (--allow-ceiling-rise): "
                  + ", ".join(f"{f} {was}->{now}" for f, was, now in rises))
        if drops:
            print(f"lowered {drops} ceiling(s).")
        return 0
    return cmd_report(state, categories, unqualified, files)


if __name__ == "__main__":
    sys.exit(main())
