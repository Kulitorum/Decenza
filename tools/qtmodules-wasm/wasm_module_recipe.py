"""The identity of a reusable Qt Wasm module package, shared by package-ci.py and
verify-ci-package.py so the writer and the checker cannot disagree."""

from __future__ import annotations

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import qt_version  # noqa: E402

QT_VERSION = qt_version.pinned()
EMSCRIPTEN_VERSION = "5.0.5"
TARGET = "wasm_singlethread"
PREFIXES = ("qtmultimedia-wasm-install", "qtro-wasm-install")
PACKAGE_FILES = {
    "qtmultimedia-wasm-install/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake",
    "qtro-wasm-install/lib/cmake/Qt6RemoteObjects/Qt6RemoteObjectsConfig.cmake",
}

# Manifest fields a reused package must match exactly.
IDENTITY = {
    "formatVersion": 1,
    "qtVersion": QT_VERSION,
    "emscriptenVersion": EMSCRIPTEN_VERSION,
    "target": TARGET,
    "modules": ["qtmultimedia", "qtremoteobjects"],
    "qtSources": {
        "version": QT_VERSION,
        "qtmultimedia": "https://code.qt.io/qt/qtmultimedia.git",
        "qtremoteobjects": "https://code.qt.io/qt/qtremoteobjects.git",
    },
}


def archive_name(recipe_key: str) -> str:
    recipe_hash = recipe_key.rsplit("-", 1)[-1]
    return f"qt-wasm-modules-{QT_VERSION}-emscripten-{EMSCRIPTEN_VERSION}-{TARGET}-recipe-{recipe_hash}.tar.gz"
