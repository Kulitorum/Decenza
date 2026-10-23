#!/usr/bin/env python3
"""Check generated Qt resource manifests for local QML/JS and qrc resource closure."""
from __future__ import annotations

import argparse
import posixpath
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable

MODULE_SUFFIXES = {".qml", ".js", ".mjs"}
QML_IMPORT = re.compile(r"^\s*import\s+['\"]([^'\"]+)['\"]", re.MULTILINE)
ES_IMPORT = re.compile(
    r"^\s*(?:import\s+(?:[^'\";]*?\s+from\s+)?|export\s+[^'\";]*?\s+from\s+)['\"]([^'\"]+)['\"]",
    re.MULTILINE,
)
QRC_URL_LITERAL = re.compile(r"([\'\"])qrc:/([^\'\"<>]+)\1")


def _resource_entries(path: Path) -> list[tuple[str, Path]]:
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as exc:
        raise ValueError(f"cannot parse resource manifest {path}: {exc}") from exc
    entries: list[tuple[str, Path]] = []
    for group in root.findall("qresource"):
        prefix = group.get("prefix", "/")
        for item in group.findall("file"):
            source_text = (item.text or "").strip()
            if not source_text:
                raise ValueError(f"empty <file> in {path}")
            source = Path(source_text)
            if not source.is_absolute():
                source = path.parent / source
            alias = item.get("alias")
            if not alias:
                alias = source_text if not Path(source_text).is_absolute() else source.name
            resource_path = "/" + posixpath.normpath(
                posixpath.join(prefix, alias)
            ).lstrip("/")
            entries.append((resource_path, source.resolve()))
    return entries


def _local_module_imports(source: Path) -> Iterable[str]:
    text = source.read_text(encoding="utf-8")
    found = set(QML_IMPORT.findall(text))
    found.update(ES_IMPORT.findall(text))
    for specifier in sorted(found):
        if ":" in specifier or not Path(specifier).suffix.lower() in MODULE_SUFFIXES:
            continue
        yield specifier


def active_resource_manifests(resources_dir: Path, target_sources: Path,
                              extra_manifests: Iterable[Path] = ()) -> list[Path]:
    lines = target_sources.read_text(encoding="utf-8").splitlines()
    if len(lines) < 3 or not lines[0].startswith("source-root=") or not lines[1].startswith("build-root="):
        raise ValueError(f"invalid target source manifest: {target_sources}")
    source_root = Path(lines[0].split("=", 1)[1])
    build_root = Path(lines[1].split("=", 1)[1])
    if not source_root.is_absolute() or not build_root.is_absolute():
        raise ValueError(f"target source manifest roots must be absolute: {target_sources}")
    resource_root = resources_dir.resolve()
    extras = {path.resolve() for path in extra_manifests}
    used_extras: set[Path] = set()
    manifests: set[Path] = set()
    for line in lines[2:]:
        text = line.strip()
        if not text:
            continue
        if "$<" in text:
            raise ValueError(f"unresolved target source expression: {text}")
        source = Path(text)
        if source.suffix != ".cpp" or not source.name.startswith("qrc_"):
            continue
        candidates = {source.resolve()} if source.is_absolute() else {
            (build_root / source).resolve(), (source_root / source).resolve()}
        name = source.stem.removeprefix("qrc_") + ".qrc"
        if any(candidate.parent == resource_root for candidate in candidates):
            manifest = resource_root / name
        else:
            matching = {path for path in extras if path.name == name}
            if len(matching) != 1:
                raise ValueError(f"unsupported active resource source: {text}; supply its compiled .qrc")
            manifest = matching.pop()
            used_extras.add(manifest)
        if not manifest.is_file():
            raise ValueError(f"active resource manifest is missing: {manifest}")
        manifests.add(manifest)
    if not manifests:
        raise ValueError(f"target source manifest names no active resources: {target_sources}")
    if extras - used_extras:
        raise ValueError("extra resource manifests are not compiled by the target: "
                         + ", ".join(str(path) for path in sorted(extras - used_extras)))
    return sorted(manifests)


def check(qml_manifest: Path, resource_manifests: Iterable[Path]) -> list[str]:
    errors: list[str] = []
    try:
        qml_entries = _resource_entries(qml_manifest)
        all_entries = list(qml_entries)
        for manifest in resource_manifests:
            if manifest.resolve() == qml_manifest.resolve():
                continue
            all_entries.extend(_resource_entries(manifest))
    except ValueError as exc:
        return [str(exc)]

    module_sources: dict[str, Path] = {}
    for url, source in qml_entries:
        if source.suffix.lower() in MODULE_SUFFIXES:
            module_sources[url] = source
            if not source.is_file():
                errors.append(f"QML manifest source is missing: {url} -> {source}")
    available_modules = set(module_sources)
    for importer_url, source in sorted(module_sources.items()):
        if not source.is_file():
            continue
        try:
            imports = list(_local_module_imports(source))
        except (OSError, UnicodeError) as exc:
            errors.append(f"cannot read module {importer_url} ({source}): {exc}")
            continue
        for specifier in imports:
            target_url = "/" + posixpath.normpath(
                posixpath.join(posixpath.dirname(importer_url), specifier)
            ).lstrip("/")
            if target_url not in available_modules:
                errors.append(
                    f"missing bundled module {target_url} imported by {importer_url}"
                )

    available_resources = {url for url, source in all_entries if source.is_file()}
    for importer_url, source in sorted(module_sources.items()):
        if not source.is_file():
            continue
        try:
            text = source.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"cannot read module {importer_url} ({source}): {exc}")
            continue
        for match in QRC_URL_LITERAL.finditer(text):
            raw_path = match.group(2)
            # A path followed by concatenation (or ending in '/') is a dynamic prefix,
            # not a complete resource lookup; runtime-selected host media is intentionally
            # outside this source-closure check.
            trailing = text[match.end():].lstrip()
            if raw_path.endswith("/") or trailing.startswith("+"):
                continue
            if "…" in raw_path or raw_path.endswith("/..."):
                continue
            url = "/" + posixpath.normpath(raw_path).lstrip("/")
            if url not in available_resources:
                errors.append(f"missing bundled resource qrc:{url} referenced by {importer_url}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qml-manifest", required=True, type=Path,
                        help="generated Qt QML module .qrc manifest for the Wasm target")
    parser.add_argument("--resources-dir", required=True, type=Path,
                        help="generated .qt/rcc directory containing target resource .qrc files")
    parser.add_argument("--target-sources", required=True, type=Path,
                        help="current target SOURCES exported by CMake file(GENERATE)")
    parser.add_argument("--resource-qrc", action="append", default=[], type=Path,
                        help="additional source .qrc compiled directly into the target")
    args = parser.parse_args(argv)
    try:
        manifests = active_resource_manifests(args.resources_dir, args.target_sources, args.resource_qrc)
        if args.qml_manifest.resolve() not in manifests:
            raise ValueError(f"QML module manifest is not compiled by the target: {args.qml_manifest}")
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"Wasm resource source audit failed: {exc}", file=sys.stderr)
        return 1
    errors = check(args.qml_manifest, manifests)
    if errors:
        print("Wasm QML resource closure failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("Wasm QML imports and literal qrc resources are present in the active target resource manifests.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
