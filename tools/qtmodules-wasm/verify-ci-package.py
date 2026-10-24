#!/usr/bin/env python3
"""Verify a reusable Qt Wasm module artifact and install its prefixes."""

import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import tempfile


EXPECTED = {
    "formatVersion": 1,
    "qtVersion": "6.12.0",
    "emscriptenVersion": "5.0.5",
    "target": "wasm_singlethread",
    "modules": ["qtmultimedia", "qtremoteobjects"],
    "qtSources": {
        "version": "6.12.0",
        "qtmultimedia": "https://code.qt.io/qt/qtmultimedia.git",
        "qtremoteobjects": "https://code.qt.io/qt/qtremoteobjects.git",
    },
}
REQUIRED = {
    "qtmultimedia-wasm-install/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake",
    "qtro-wasm-install/lib/cmake/Qt6RemoteObjects/Qt6RemoteObjectsConfig.cmake",
    "manifest.json",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <artifact-directory> <recipe-cache-key> <build-directory>", file=sys.stderr)
        return 2

    artifact_dir = Path(sys.argv[1]).resolve()
    recipe_key = sys.argv[2]
    build_dir = Path(sys.argv[3]).resolve()
    archives = list(artifact_dir.glob("qt-wasm-modules-*.tar.gz"))
    if len(archives) != 1:
        raise SystemExit(f"Expected one Qt Wasm module archive in {artifact_dir}; found {len(archives)}")
    archive_path = archives[0]
    recipe_hash = recipe_key.rsplit("-", 1)[-1]
    expected_filename = f"qt-wasm-modules-6.12.0-emscripten-5.0.5-wasm_singlethread-recipe-{recipe_hash}.tar.gz"
    if archive_path.name != expected_filename:
        raise SystemExit("Qt Wasm module archive filename does not match the expected recipe identity")
    checksum_path = artifact_dir / f"{archive_path.name}.sha256"
    manifest_path = artifact_dir / f"{archive_path.name}.manifest.json"
    if not checksum_path.is_file() or not manifest_path.is_file():
        raise SystemExit("Qt Wasm module artifact is missing its checksum or manifest sidecar")

    expected_digest, listed_name = checksum_path.read_text(encoding="ascii").split()[:2]
    if listed_name != archive_path.name or sha256(archive_path) != expected_digest:
        raise SystemExit("Qt Wasm module archive checksum validation failed")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if any(manifest.get(key) != value for key, value in EXPECTED.items()):
        raise SystemExit("Qt Wasm module package has incompatible Qt, Emscripten, target, or format metadata")
    if manifest.get("recipeCacheKey") != recipe_key:
        raise SystemExit("Qt Wasm module package recipe key does not match this workflow")

    with tarfile.open(archive_path, "r:gz") as archive:
        members = archive.getmembers()
        names = {member.name for member in members}
        missing = REQUIRED - names
        if missing:
            raise SystemExit(f"Qt Wasm module archive is missing required files: {', '.join(sorted(missing))}")
        for member in members:
            name = PurePosixPath(member.name)
            if name.is_absolute() or ".." in name.parts:
                raise SystemExit(f"Unsafe path in Qt Wasm module archive: {member.name}")
        embedded = json.load(archive.extractfile("manifest.json"))
        if embedded != manifest:
            raise SystemExit("Embedded Qt Wasm module manifest does not match its sidecar")
        with tempfile.TemporaryDirectory(prefix="qt-wasm-verify-") as temp_name:
            extracted = Path(temp_name)
            archive.extractall(path=extracted, filter="data")
            for prefix in ("qtmultimedia-wasm-install", "qtro-wasm-install"):
                source = extracted / prefix
                destination = build_dir / prefix
                if not source.is_dir():
                    raise SystemExit(f"Qt Wasm module archive has no {prefix} install prefix")
                shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=True)
            notices = extracted / "notices"
            if not notices.is_dir():
                raise SystemExit("Qt Wasm module archive is missing Qt source license notices")
            shutil.copytree(notices, build_dir / "qt-wasm-module-notices", dirs_exist_ok=True)

    for relative in REQUIRED - {"manifest.json"}:
        if not (build_dir / relative).is_file():
            raise SystemExit(f"Installed Qt Wasm package is missing {relative}")
    print(f"Verified and installed reusable Qt Wasm modules from {archive_path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
