#!/usr/bin/env python3
"""Package the CI-built Qt Wasm module installs for independent reuse."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tarfile
import tempfile
from datetime import datetime, timezone


QT_VERSION = "6.12.0"
EMSCRIPTEN_VERSION = "5.0.5"
TARGET = "wasm_singlethread"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_tar_info(info: tarfile.TarInfo) -> tarfile.TarInfo:
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    return info


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <output-directory> <recipe-cache-key>", file=sys.stderr)
        return 2

    repo_root = Path(__file__).resolve().parents[2]
    output_dir = Path(sys.argv[1]).resolve()
    recipe_key = sys.argv[2]
    recipe_hash = recipe_key.rsplit("-", 1)[-1]
    if not re.fullmatch(r"[0-9a-f]{64}", recipe_hash):
        raise SystemExit("The module recipe key must end with its 64-character SHA-256 identity")
    source_root_value = os.environ.get("QT_WASM_SOURCES", "")
    source_root = Path(source_root_value) if source_root_value else None
    if source_root is not None and not source_root.is_dir():
        source_root = None

    installs = {
        "qtmultimedia-wasm-install": repo_root / "build/qtmultimedia-wasm-install",
        "qtro-wasm-install": repo_root / "build/qtro-wasm-install",
    }
    required = {
        "qtmultimedia-wasm-install/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake",
        "qtro-wasm-install/lib/cmake/Qt6RemoteObjects/Qt6RemoteObjectsConfig.cmake",
    }
    for name, path in installs.items():
        if not path.is_dir():
            raise SystemExit(f"Missing Qt module install prefix: {path}")
    for relative in required:
        prefix, package_file = relative.split("/", 1)
        if not (installs[prefix] / package_file).is_file():
            raise SystemExit(f"Missing installed Qt package file: {relative}")

    installed_bytes = sum(item.stat().st_size for prefix in installs.values()
                          for item in prefix.rglob("*") if item.is_file())
    if installed_bytes > 200 * 1024 * 1024:
        raise SystemExit("Installed Qt Wasm modules exceed the 200 MiB package allowance")

    output_dir.mkdir(parents=True, exist_ok=True)
    filename = f"qt-wasm-modules-{QT_VERSION}-emscripten-{EMSCRIPTEN_VERSION}-{TARGET}-recipe-{recipe_hash}.tar.gz"
    archive_path = output_dir / filename
    manifest_path = output_dir / f"{filename}.manifest.json"
    checksum_path = output_dir / f"{filename}.sha256"

    with tempfile.TemporaryDirectory(prefix="qt-wasm-package-") as temp_name:
        stage = Path(temp_name)
        for name, source in installs.items():
            shutil.copytree(source, stage / name, symlinks=True)

        notices = stage / "notices"
        if source_root is not None:
            notices.mkdir()
            root_license = source_root / "LICENSE.md"
            if root_license.is_file():
                shutil.copy2(root_license, notices / "Qt-LICENSE.md")
            for module in ("qtmultimedia", "qtremoteobjects"):
                module_source = source_root / module
                license_dir = module_source / "LICENSES"
                if not license_dir.is_dir():
                    raise SystemExit(f"Missing Qt module license notices: {license_dir}")
                target = notices / module
                shutil.copytree(license_dir, target / "LICENSES")
                for notice_name in ("LICENSE.md", "licenseRule.json"):
                    notice_file = module_source / notice_name
                    if notice_file.is_file():
                        shutil.copy2(notice_file, target / notice_name)
        else:
            previous_notices = repo_root / "build/qt-wasm-module-notices"
            if not previous_notices.is_dir():
                raise SystemExit("Qt source notices are unavailable; set QT_WASM_SOURCES or reuse a verified module artifact")
            shutil.copytree(previous_notices, notices)

        manifest = {
            "formatVersion": 1,
            "qtVersion": QT_VERSION,
            "emscriptenVersion": EMSCRIPTEN_VERSION,
            "target": TARGET,
            "modules": ["qtmultimedia", "qtremoteobjects"],
            "recipeCacheKey": recipe_key,
            "qtSources": {
                "version": QT_VERSION,
                "qtmultimedia": "https://code.qt.io/qt/qtmultimedia.git",
                "qtremoteobjects": "https://code.qt.io/qt/qtremoteobjects.git",
            },
            "recipeRepositoryRevision": os.environ.get("GITHUB_SHA", ""),
            "packagedAtUtc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "installPrefixes": list(installs),
            "licenseNotices": "notices/ and the per-module SPDX files under each install prefix's sbom/ directory",
        }
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

        with archive_path.open("wb") as raw:
            import gzip

            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w|") as archive:
                    for item in sorted(stage.rglob("*"), key=lambda path: path.relative_to(stage).as_posix()):
                        archive.add(item, arcname=item.relative_to(stage).as_posix(), recursive=False, filter=normalize_tar_info)

    digest = sha256(archive_path)
    checksum_path.write_text(f"{digest}  {filename}\n", encoding="ascii")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    if sha256(archive_path) != checksum_path.read_text(encoding="ascii").split()[0]:
        raise SystemExit("Qt Wasm module archive checksum validation failed")
    with tarfile.open(archive_path, "r:gz") as archive:
        names = set(archive.getnames())
        for relative in required | {"manifest.json"}:
            if relative not in names:
                raise SystemExit(f"Qt Wasm module archive validation failed: missing {relative}")
        embedded_manifest = json.load(archive.extractfile("manifest.json"))
        if embedded_manifest != manifest:
            raise SystemExit("Embedded Qt Wasm module manifest does not match the sidecar")

    print(f"Created and verified {archive_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
