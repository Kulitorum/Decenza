#!/usr/bin/env python3
"""Package and verify the linked Decenza Wasm deployment bundle."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
from typing import Any
import zlib


BASE_FILES = (
    "index.html",
    "decenza-web.js",
    "decenza-web.wasm",
    "qtloader.js",
    "qt-dialog-accessibility.js",
)
MANIFEST_NAME = "package.json"
WASM_LIMIT = 128 * 1024 * 1024
OTHER_FILE_LIMIT = 10 * 1024 * 1024
GZIP_THRESHOLD = 64 * 1024
HASH_CHUNK_SIZE = 1024 * 1024
PACKAGE_ID_PATTERN = re.compile(r'^#define DECENZA_PACKAGE_ID "([0-9a-f]{64})"$', re.MULTILINE)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class PackageError(ValueError):
    pass


def _regular_file(path: Path, *, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as error:
        raise PackageError(f"{label} is missing or unreadable") from error
    if not stat.S_ISREG(info.st_mode):
        raise PackageError(f"{label} must be a regular file")
    return info


def _hash_file(path: Path, limit: int, *, label: str) -> tuple[int, str]:
    info = _regular_file(path, label=label)
    if info.st_size <= 0 or info.st_size > limit:
        raise PackageError(f"{label} is empty or exceeds its size limit")
    digest = hashlib.sha256()
    total = 0
    try:
        with path.open("rb") as stream:
            while chunk := stream.read(HASH_CHUNK_SIZE):
                total += len(chunk)
                if total > limit:
                    raise PackageError(f"{label} exceeds its size limit")
                digest.update(chunk)
    except OSError as error:
        raise PackageError(f"{label} is unreadable") from error
    if total != info.st_size:
        raise PackageError(f"{label} changed while being read")
    return total, digest.hexdigest()


def _package_id_from_header(header: Path) -> str:
    info = _regular_file(header, label="Package identity header")
    if info.st_size > 4096:
        raise PackageError("Package identity header is too large")
    try:
        text = header.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise PackageError("Package identity header is unreadable") from error
    matches = PACKAGE_ID_PATTERN.findall(text)
    if len(matches) != 1:
        raise PackageError("Package identity header has no valid DECENZA_PACKAGE_ID")
    return matches[0]


def _write_gzip(source: Path, destination: Path) -> tuple[int, str]:
    try:
        with source.open("rb") as source_stream, destination.open("wb") as raw_output:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw_output, mtime=0) as output:
                shutil.copyfileobj(source_stream, output, length=HASH_CHUNK_SIZE)
    except OSError as error:
        raise PackageError(f"Could not create compressed asset {source.name}") from error
    return _hash_file(destination, _limit_for(source.name) + 1024 * 1024,
                      label=f"Compressed asset {destination.name}")


def _limit_for(filename: str) -> int:
    return WASM_LIMIT if filename == "decenza-web.wasm" else OTHER_FILE_LIMIT


def _should_include_gzip(original_size: int, gzip_size: int, asset_limit: int) -> bool:
    return gzip_size < original_size and gzip_size <= asset_limit


def _atomic_replace_directory(staging: Path, destination: Path) -> None:
    destination = destination.absolute()
    backup = destination.with_name(f".{destination.name}.previous-{os.getpid()}")
    if backup.exists():
        if not backup.is_dir() or backup.is_symlink():
            backup.unlink()
        else:
            shutil.rmtree(backup)
    had_destination = destination.exists()
    try:
        if had_destination:
            if not destination.is_dir() or destination.is_symlink():
                raise PackageError(f"{destination.name} must be a directory")
            os.replace(destination, backup)
        os.replace(staging, destination)
    except OSError as error:
        if had_destination and backup.exists() and not destination.exists():
            os.replace(backup, destination)
        raise PackageError(f"Could not atomically replace {destination.name}") from error
    if backup.exists():
        shutil.rmtree(backup)


def _verify_gzip_matches(gzip_path: Path, expected_size: int, expected_digest: str,
                         filename: str) -> None:
    digest = hashlib.sha256()
    total = 0
    try:
        with gzip.open(gzip_path, "rb") as stream:
            while chunk := stream.read(HASH_CHUNK_SIZE):
                total += len(chunk)
                if total > expected_size:
                    raise PackageError(f"Compressed bundle file {gzip_path.name} expands beyond {filename}")
                digest.update(chunk)
    except (OSError, EOFError, zlib.error) as error:
        raise PackageError(f"Compressed bundle file {gzip_path.name} is not valid gzip data") from error
    if total != expected_size or digest.hexdigest() != expected_digest:
        raise PackageError(f"Compressed bundle file {gzip_path.name} does not reproduce {filename}")


def package_bundle(bundle_dir: Path, package_header: Path, output_dir: Path) -> dict[str, Any]:
    bundle_dir = bundle_dir.absolute()
    output_dir = output_dir.absolute()
    package_id = _package_id_from_header(package_header)
    source_paths: dict[str, Path] = {}
    for filename in BASE_FILES:
        path = bundle_dir / filename
        _regular_file(path, label=f"Required bundle file {filename}")
        source_paths[filename] = path

    staging_parent = output_dir.parent
    staging_parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.stage-", dir=staging_parent))
    try:
        manifest_files: dict[str, dict[str, Any]] = {}
        for filename, source in source_paths.items():
            limit = _limit_for(filename)
            size, digest = _hash_file(source, limit, label=f"Bundle file {filename}")
            staged_file = staging / filename
            try:
                with source.open("rb") as input_stream, staged_file.open("wb") as output_stream:
                    shutil.copyfileobj(input_stream, output_stream, length=HASH_CHUNK_SIZE)
            except OSError as error:
                raise PackageError(f"Could not stage bundle file {filename}") from error
            copied_size, copied_digest = _hash_file(staged_file, limit, label=f"Staged file {filename}")
            if (copied_size, copied_digest) != (size, digest):
                raise PackageError(f"Bundle file {filename} changed while being packaged")
            entry: dict[str, Any] = {"size": size, "sha256": digest}
            if size >= GZIP_THRESHOLD and filename.endswith((".js", ".wasm")):
                gzip_path = staging / f"{filename}.gz"
                gzip_size, gzip_digest = _write_gzip(staged_file, gzip_path)
                if _should_include_gzip(size, gzip_size, limit):
                    entry["gzipSize"] = gzip_size
                    entry["gzipSha256"] = gzip_digest
                else:
                    gzip_path.unlink()
            manifest_files[filename] = entry

        manifest = {"packageId": package_id, "files": manifest_files}
        manifest_bytes = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
        (staging / MANIFEST_NAME).write_bytes(manifest_bytes)
        _atomic_replace_directory(staging, output_dir)
        return manifest
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def _json_without_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PackageError("Manifest contains a duplicate JSON key")
        result[key] = value
    return result


def _load_manifest(source_dir: Path, expected_package_id: str) -> tuple[dict[str, Any], dict[str, Path]]:
    if not SHA256_PATTERN.fullmatch(expected_package_id):
        raise PackageError("Expected package ID must be a 64-character lowercase SHA-256")
    manifest_path = source_dir / MANIFEST_NAME
    info = _regular_file(manifest_path, label="Package manifest")
    if info.st_size > 128 * 1024:
        raise PackageError("Package manifest exceeds its size limit")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"),
                              object_pairs_hook=_json_without_duplicate_keys)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError("Package manifest is missing or malformed") from error
    if not isinstance(manifest, dict) or set(manifest) != {"packageId", "files"}:
        raise PackageError("Package manifest has an invalid shape")
    if manifest["packageId"] != expected_package_id:
        raise PackageError("Package ID does not match the native application")
    files = manifest["files"]
    if not isinstance(files, dict) or set(files) != set(BASE_FILES):
        raise PackageError("Package manifest does not contain the complete allowlisted bundle")

    verified_paths: dict[str, Path] = {}
    expected_disk_names = {MANIFEST_NAME}
    for filename in BASE_FILES:
        entry = files[filename]
        if not isinstance(entry, dict) or not {"size", "sha256"}.issubset(entry):
            raise PackageError(f"Manifest entry for {filename} is malformed")
        optional_gzip = {"gzipSize", "gzipSha256"}
        present_gzip = optional_gzip.intersection(entry)
        if present_gzip and present_gzip != optional_gzip:
            raise PackageError(f"Manifest gzip metadata for {filename} is incomplete")
        if set(entry) != {"size", "sha256"} | present_gzip:
            raise PackageError(f"Manifest entry for {filename} has unsupported fields")

        size = entry["size"]
        digest = entry["sha256"]
        if not isinstance(size, int) or isinstance(size, bool) or not 0 < size <= _limit_for(filename):
            raise PackageError(f"Manifest size for {filename} is invalid")
        if not isinstance(digest, str) or not SHA256_PATTERN.fullmatch(digest):
            raise PackageError(f"Manifest hash for {filename} is invalid")
        path = source_dir / filename
        actual_size, actual_digest = _hash_file(path, _limit_for(filename), label=f"Bundle file {filename}")
        if (actual_size, actual_digest) != (size, digest):
            raise PackageError(f"Bundle file {filename} does not match its manifest")
        verified_paths[filename] = path
        expected_disk_names.add(filename)

        if present_gzip:
            if not filename.endswith((".js", ".wasm")) or size < GZIP_THRESHOLD:
                raise PackageError(f"Unexpected compressed sibling for {filename}")
            gzip_size = entry["gzipSize"]
            gzip_digest = entry["gzipSha256"]
            if (not isinstance(gzip_size, int) or isinstance(gzip_size, bool)
                    or not 0 < gzip_size <= _limit_for(filename) + 1024 * 1024):
                raise PackageError(f"Manifest gzip size for {filename} is invalid")
            if not isinstance(gzip_digest, str) or not SHA256_PATTERN.fullmatch(gzip_digest):
                raise PackageError(f"Manifest gzip hash for {filename} is invalid")
            gzip_name = f"{filename}.gz"
            gzip_path = source_dir / gzip_name
            actual_gzip_size, actual_gzip_digest = _hash_file(
                gzip_path, _limit_for(filename) + 1024 * 1024,
                label=f"Compressed bundle file {gzip_name}")
            if (actual_gzip_size, actual_gzip_digest) != (gzip_size, gzip_digest):
                raise PackageError(f"Compressed bundle file {gzip_name} does not match its manifest")
            _verify_gzip_matches(gzip_path, size, digest, filename)
            verified_paths[gzip_name] = gzip_path
            expected_disk_names.add(gzip_name)

    try:
        actual_disk_names = {item.name for item in source_dir.iterdir()}
    except OSError as error:
        raise PackageError("Package directory cannot be read") from error
    if actual_disk_names != expected_disk_names:
        raise PackageError("Package directory contains missing or unlisted files")
    return manifest, verified_paths


def install_bundle(source_dir: Path, expected_package_id: str, output_dir: Path) -> dict[str, Any]:
    source_dir = source_dir.absolute()
    output_dir = output_dir.absolute()
    manifest, verified_paths = _load_manifest(source_dir, expected_package_id)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.stage-", dir=output_dir.parent))
    try:
        for filename, source in verified_paths.items():
            shutil.copyfile(source, staging / filename)
        shutil.copyfile(source_dir / MANIFEST_NAME, staging / MANIFEST_NAME)
        # Verify the staged copy too, so mutation during copy cannot install a
        # different bundle from the one checked above.
        _load_manifest(staging, expected_package_id)
        _atomic_replace_directory(staging, output_dir)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return manifest


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="stage and hash the linked Wasm deployment")
    pack.add_argument("--bundle-dir", type=Path, required=True)
    pack.add_argument("--package-header", type=Path, required=True)
    pack.add_argument("--output-dir", type=Path, required=True)
    install = commands.add_parser("install", help="verify a matching bundle and stage it atomically")
    install.add_argument("--source", type=Path, required=True)
    install.add_argument("--expected-package-id", required=True)
    install.add_argument("--output-dir", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "pack":
            manifest = package_bundle(arguments.bundle_dir, arguments.package_header,
                                      arguments.output_dir)
        else:
            manifest = install_bundle(arguments.source, arguments.expected_package_id,
                                      arguments.output_dir)
    except (PackageError, OSError) as error:
        print(f"webclient packaging error: {error}", file=sys.stderr)
        return 1
    print(f"Packaged Wasm bundle {manifest['packageId']} ({len(manifest['files'])} assets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
