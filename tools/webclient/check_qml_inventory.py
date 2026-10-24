#!/usr/bin/env python3
"""Check QML/JS-module inventory rows against repository source files.

This is a source-coverage ledger check only. It does not inspect runtime
reachability or behavioral parity. Optional source manifests report inclusion
counts and enforce the ledger's declared native/Wasm inclusion as well as rejecting
QML sources absent from the app source set/ledger.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


APP_HEADING = "## Complete application QML inventory"
NON_APP_HEADING = "### Non-application QML and bootstrap resources"
COVERAGE_HEADING = "### Coverage maintenance and dependency closure"
EXCLUDED_DIRS = {"build", "_build", "node_modules", "vendor", "generated", "CMakeFiles"}
SOURCE_PATH_RE = re.compile(
    r"(?:^|[/\"'])qml/(?!\.\./)([A-Za-z0-9_.+/-]+\.(?:qml|js|mjs))"
    r"(?=$|[\s\"'<])"
)
CHANGE_NAME = "replace-web-interface-with-wasm"


@dataclass(frozen=True)
class LedgerRow:
    path: str
    owner_text: str
    line: int
    inclusion: str = ""


def _table_rows(lines: list[str], first: int, last: int, owner_column: int,
                inclusion_column: Optional[int] = None) -> list[LedgerRow]:
    rows: list[LedgerRow] = []
    for index in range(first, last):
        line = lines[index]
        if not line.lstrip().startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) <= owner_column:
            continue
        match = re.fullmatch(r"`([^`]+)`", cells[0])
        if not match:
            continue
        path = match.group(1)
        if Path(path).suffix.lower() not in {".qml", ".js", ".mjs"}:
            continue
        inclusion = cells[inclusion_column] if inclusion_column is not None and len(cells) > inclusion_column else ""
        rows.append(LedgerRow(path=path, owner_text=cells[owner_column], line=index + 1,
                             inclusion=inclusion))
    return rows


def parse_ledger(path: Path) -> tuple[list[LedgerRow], list[LedgerRow], list[str]]:
    errors: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        return [], [], [f"cannot read inventory ledger {path}: {exc}"]

    try:
        app_start = next(i for i, line in enumerate(lines)
                         if line.strip() == APP_HEADING or line.strip().startswith(APP_HEADING + " ("))
        non_app_start = next(i for i, line in enumerate(lines) if line.strip() == NON_APP_HEADING)
        coverage_start = next(i for i, line in enumerate(lines) if line.strip() == COVERAGE_HEADING)
    except StopIteration:
        return [], [], ["ledger is missing the application/non-application inventory headings"]
    if not (app_start < non_app_start < coverage_start):
        return [], [], ["inventory headings are out of order"]

    # The application table has path, task owner, last source inclusion, and role.
    app_rows = _table_rows(lines, app_start + 1, non_app_start, owner_column=1, inclusion_column=2)
    # The non-app table has path and a disposition/task owner description.
    non_app_rows = _table_rows(lines, non_app_start + 1, coverage_start, owner_column=1)
    if not app_rows:
        errors.append("application inventory table contains no QML/JS rows")
    if not non_app_rows:
        errors.append("non-application inventory table contains no QML/JS rows")
    return app_rows, non_app_rows, errors


def _iter_sources(root: Path, relative_root: str) -> set[str]:
    base = root / relative_root
    if not base.exists():
        return set()
    found: set[str] = set()
    for path in base.rglob("*"):
        if not path.is_file() or any(part in EXCLUDED_DIRS for part in path.relative_to(base).parts):
            continue
        if path.suffix.lower() in {".qml", ".js", ".mjs"}:
            found.add(path.relative_to(root).as_posix())
    return found


def source_sets(root: Path) -> tuple[set[str], set[str]]:
    # Only qml/ is the application denominator. Tests and the explicitly-owned
    # developer webclient probes are classified separately, never silently
    # dropped into the app count. Build trees and vendored directories are
    # excluded by _iter_sources; no other tools/ subtree is scanned.
    app = _iter_sources(root, "qml")
    non_app = _iter_sources(root, "tests")
    non_app |= _iter_sources(root, "tools/webclient")
    non_app |= _iter_sources(root, "resources/webclient")
    return app, non_app


def task_sections(tasks_path: Path) -> set[int]:
    text = tasks_path.read_text(encoding="utf-8")
    return {int(number) for number in re.findall(r"^##\s+(\d+)\.", text, flags=re.MULTILINE)}


def _task_refs(owner_text: str) -> list[int]:
    return [int(number) for number in re.findall(r"(?<![\w.])(\d+)(?:\.\d+)?", owner_text)]


def validate(root: Path, ledger_path: Path, tasks_path: Path) -> tuple[list[str], dict[str, int]]:
    app_rows, non_app_rows, errors = parse_ledger(ledger_path)
    app_sources, non_app_sources = source_sets(root)
    all_rows = app_rows + non_app_rows
    row_counts = Counter(row.path for row in all_rows)
    for source, count in sorted(row_counts.items()):
        if count > 1:
            lines = ", ".join(str(row.line) for row in all_rows if row.path == source)
            errors.append(f"duplicate inventory row {source} (ledger lines {lines})")

    app_ledger = {row.path for row in app_rows}
    non_app_ledger = {row.path for row in non_app_rows}
    for row in app_rows:
        if not row.path.startswith("qml/"):
            errors.append(f"application row {row.path} on line {row.line} is outside qml/")
    for row in non_app_rows:
        if row.path.startswith("qml/"):
            errors.append(f"non-application row {row.path} on line {row.line} is inside qml/")

    for missing in sorted(app_sources - app_ledger):
        errors.append(f"unassigned application source: {missing}")
    for stale in sorted(app_ledger - app_sources):
        errors.append(f"stale application inventory row: {stale}")
    for missing in sorted(non_app_sources - non_app_ledger):
        errors.append(f"unclassified test/tool/bootstrap source: {missing}")
    for stale in sorted(non_app_ledger - non_app_sources):
        errors.append(f"stale non-application inventory row: {stale}")

    try:
        valid_sections = task_sections(tasks_path)
    except OSError as exc:
        errors.append(f"cannot read task groups {tasks_path}: {exc}")
        valid_sections = set()
    for row in all_rows:
        refs = _task_refs(row.owner_text)
        if not refs:
            errors.append(f"inventory row {row.path} on line {row.line} has no task-group reference")
        for ref in refs:
            if ref not in valid_sections:
                errors.append(
                    f"inventory row {row.path} on line {row.line} references missing task group {ref}"
                )

    counts = {
        "app_sources": len(app_sources),
        "app_rows": len(app_rows),
        "non_app_sources": len(non_app_sources),
        "non_app_rows": len(non_app_rows),
        "task_groups": len(valid_sections),
    }
    return errors, counts


def manifest_source_paths(path: Path) -> set[str]:
    """Extract canonical QML-root paths named in a CMake-generated manifest."""
    text = path.read_text(encoding="utf-8")
    return {
        "qml/" + match.group(1)
        for line in text.splitlines()
        for match in SOURCE_PATH_RE.finditer(line.replace("\\", "/"))
        if "../" not in match.group(1)
    }


def expected_manifest_sources(rows: list[LedgerRow], label: str) -> set[str]:
    target = label.casefold()
    if target not in {"native", "wasm"}:
        return set()
    return {row.path for row in rows
            if re.search(r"\b" + target + r"\b", row.inclusion.casefold())}


def manifest_source_check(
    path: Path, app_sources: set[str], app_inventory: set[str],
    expected_sources: Optional[set[str]] = None
) -> tuple[int, int, list[str]]:
    """Enforce declared target inclusion and reject unknown QML source paths."""
    named = manifest_source_paths(path)
    errors: list[str] = []
    for candidate in sorted(named):
        if candidate not in app_sources:
            errors.append(f"manifest names QML source outside app source set: {candidate}")
        elif candidate not in app_inventory:
            errors.append(f"manifest names QML source missing from app inventory: {candidate}")
    for missing in sorted((expected_sources or set()) - named):
        errors.append(f"manifest is missing declared application source: {missing}")
    return len(named & app_sources), len(app_sources), errors


def default_ledger(root: Path) -> Path:
    active = root / "openspec" / "changes" / CHANGE_NAME / "design.md"
    if active.is_file():
        return active
    archive_root = root / "openspec" / "changes" / "archive"
    archived = sorted(archive_root.glob(f"*{CHANGE_NAME}*/design.md"))
    if archived:
        return archived[-1]
    # Return the documented active location so validate() prints one actionable
    # missing-ledger error rather than silently skipping coverage.
    return active


def _write_ledger(path: Path, app_rows: list[tuple[str, str]], non_app_rows: list[tuple[str, str]]) -> None:
    lines = [
        APP_HEADING,
        "| Repository source | Task group owner | Inclusion | Role |",
        "|---|---|---|---|",
    ]
    lines.extend(f"| `{source}` | {owner} | not included | presentation/script |" for source, owner in app_rows)
    lines.extend([
        "",
        NON_APP_HEADING,
        "| File | Disposition / task owner |",
        "|---|---|",
    ])
    lines.extend(f"| `{source}` | {owner} |" for source, owner in non_app_rows)
    lines.extend(["", COVERAGE_HEADING, "Reconcile sources."])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="qml-inventory-self-test-") as temp:
        root = Path(temp)
        (root / "qml/components").mkdir(parents=True)
        (root / "tests/qml").mkdir(parents=True)
        (root / "tools/webclient").mkdir(parents=True)
        (root / "resources/webclient").mkdir(parents=True)
        (root / "qml/components/Card.qml").write_text("Item {}\n", encoding="utf-8")
        (root / "qml/components/Rules.js").write_text(".pragma library\n", encoding="utf-8")
        (root / "qml/components/Search.mjs").write_text("export function search() {}\n", encoding="utf-8")
        (root / "tests/qml/Fixture.qml").write_text("Item {}\n", encoding="utf-8")
        (root / "tools/webclient/Probe.qml").write_text("Item {}\n", encoding="utf-8")
        (root / "resources/webclient/bootstrap.js").write_text("// adapter\n", encoding="utf-8")
        tasks = root / "tasks.md"
        tasks.write_text("## 1. App\n## 16. Shell\n## 20. Settings\n## 26. Shared\n## 27. Tests\n", encoding="utf-8")
        ledger = root / "design.md"
        _write_ledger(
            ledger,
            [("qml/components/Card.qml", "1"), ("qml/components/Rules.js", "1"),
             ("qml/components/Search.mjs", "20")],
            [("tests/qml/Fixture.qml", "Test fixture; 27.2"),
             ("tools/webclient/Probe.qml", "Developer diagnostic; 27.2"),
             ("resources/webclient/bootstrap.js", "Bootstrap adapter; 16 / 26")],
        )
        errors, _ = validate(root, ledger, tasks)
        if errors:
            print("self-test setup unexpectedly failed:", *errors, sep="\n  ", file=sys.stderr)
            return 1
        (root / "qml/components/Unassigned.qml").write_text("Item {}\n", encoding="utf-8")
        errors, _ = validate(root, ledger, tasks)
        if not any("unassigned application source: qml/components/Unassigned.qml" in error
                   for error in errors):
            print("self-test failed: a deliberately unassigned app fixture was not rejected",
                  file=sys.stderr)
            return 1
        manifest = root / "native_raw_qml.qrc"
        manifest.write_text(
            '<file alias="qml/components/Card.qml">/tmp/qml/components/Card.qml</file>\n'
            '<file alias="qml/components/Unknown.qml">/tmp/qml/components/Unknown.qml</file>\n',
            encoding="utf-8",
        )
        app_rows, _, _ = parse_ledger(ledger)
        app_sources, _ = source_sets(root)
        _, _, manifest_errors = manifest_source_check(
            manifest, app_sources, {row.path for row in app_rows}
        )
        if not any(
            "manifest names QML source outside app source set: qml/components/Unknown.qml" in error
            for error in manifest_errors
        ):
            print("self-test failed: an unknown manifest QML source was not rejected",
                  file=sys.stderr)
            return 1
        declaration_text = ledger.read_text(encoding="utf-8")
        for source, owner, inclusion in [
            ("Card.qml", "1", "native + Wasm"),
            ("Rules.js", "1", "native only"),
            ("Search.mjs", "20", "Wasm only"),
        ]:
            declaration_text = declaration_text.replace(
                f"| `qml/components/{source}` | {owner} | not included |",
                f"| `qml/components/{source}` | {owner} | {inclusion} |")
        ledger.write_text(declaration_text, encoding="utf-8")
        declared, _, _ = parse_ledger(ledger)
        if (expected_manifest_sources(declared, "native") != {
                "qml/components/Card.qml", "qml/components/Rules.js"}
                or expected_manifest_sources(declared, "wasm") != {
                    "qml/components/Card.qml", "qml/components/Search.mjs"}):
            print("self-test failed: target inclusion declarations were not parsed", file=sys.stderr)
            return 1
        declared_inventory = {row.path for row in declared}
        manifest.write_text('<file>/tmp/qml/components/Card.qml</file>\n', encoding="utf-8")
        for target, missing in [("native", "Rules.js"), ("wasm", "Search.mjs")]:
            _, _, missing_errors = manifest_source_check(
                manifest, declared_inventory, declared_inventory,
                expected_manifest_sources(declared, target))
            if missing_errors != ["manifest is missing declared application source: qml/components/" + missing]:
                print(f"self-test failed: {target} accepted a missing declared source", file=sys.stderr)
                return 1
        manifest.write_text("<RCC/>\n", encoding="utf-8")
        for target in ["native", "wasm"]:
            _, _, shared_errors = manifest_source_check(
                manifest, declared_inventory, declared_inventory,
                expected_manifest_sources(declared, target))
            if "manifest is missing declared application source: qml/components/Card.qml" not in shared_errors:
                print(f"self-test failed: {target} accepted a missing shared QML source", file=sys.stderr)
                return 1
        manifest.write_text(
            '<file>/tmp/qml/components/Card.qml</file>\n'
            '<file>/tmp/qml/components/Rules.js</file>\n', encoding="utf-8")
        _, _, native_errors = manifest_source_check(
            manifest, declared_inventory, declared_inventory,
            expected_manifest_sources(declared, "native"))
        if native_errors:
            print("self-test failed: a Wasm-only source was required by native", file=sys.stderr)
            return 1
    print("self-test passed: unassigned, unknown and missing declared target sources are rejected")
    return 0


def main(argv: Optional[list[str]] = None) -> int:
    script = Path(__file__).resolve()
    default_root = script.parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=default_root,
                        help="repository root (defaults to this script's repository)")
    parser.add_argument("--ledger", type=Path, default=None,
                        help="design.md containing the complete inventory (defaults to the active change, then its archive)")
    parser.add_argument("--tasks", type=Path, default=None,
                        help="tasks.md defining task groups (defaults beside the ledger)")
    parser.add_argument("--manifest", action="append", default=[], metavar="LABEL=PATH",
                        help="optional source manifest; native/wasm labels enforce declared target inclusion")
    parser.add_argument("--self-test", action="store_true",
                        help="prove unassigned, unknown and missing declared target sources fail")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()

    root = args.root.resolve()
    if args.ledger is None:
        ledger = default_ledger(root)
    else:
        ledger = args.ledger if args.ledger.is_absolute() else root / args.ledger
    tasks = args.tasks or ledger.parent / "tasks.md"
    if not tasks.is_absolute():
        tasks = root / tasks

    errors, counts = validate(root, ledger, tasks)
    print(f"Application QML/JS modules: {counts['app_rows']} ledger rows / {counts['app_sources']} source files")
    print(f"Classified test/tool/bootstrap QML/JS modules: {counts['non_app_rows']} ledger rows / "
          f"{counts['non_app_sources']} source files (excluded from app denominator)")
    if args.manifest:
        app_sources, _ = source_sets(root)
        app_rows, _, _ = parse_ledger(ledger)
        app_inventory = {row.path for row in app_rows}
        for spec in args.manifest:
            if "=" in spec:
                label, raw_path = spec.split("=", 1)
            else:
                raw_path = spec
                label = Path(raw_path).name
            manifest = Path(raw_path)
            if not manifest.is_absolute():
                manifest = root / manifest
            try:
                included, total, manifest_errors = manifest_source_check(
                    manifest, app_sources, app_inventory, expected_manifest_sources(app_rows, label)
                )
                print(f"Manifest {label}: names {included}/{total} application sources "
                      "(source inclusion count only; not runtime parity)")
                errors.extend(f"Manifest {label}: {error}" for error in manifest_errors)
            except OSError as exc:
                errors.append(f"cannot read source manifest {manifest}: {exc}")

    if errors:
        print("QML inventory check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("QML inventory source coverage is consistent; behavior/resource parity is not asserted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
