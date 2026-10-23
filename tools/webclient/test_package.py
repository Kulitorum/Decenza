"""Stdlib tests for Wasm bundle staging and native-side verification."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import random
import tempfile
import unittest
from unittest.mock import patch


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts" / "package_webclient.py"
SPEC = importlib.util.spec_from_file_location("package_webclient", SCRIPT_PATH)
assert SPEC and SPEC.loader
package_webclient = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_webclient)


class PackageWebClientTests(unittest.TestCase):
    package_id = "a" * 64

    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.bundle = self.root / "bundle"
        self.bundle.mkdir()
        self.header = self.root / "decenza_package_identity.h"
        self.header.write_text(
            f'#pragma once\n#define DECENZA_PACKAGE_ID "{self.package_id}"\n',
            encoding="utf-8",
        )
        contents = {
            "index.html": b"<!doctype html><title>Decenza</title>",
            "decenza-web.js": b"console.log('web client');",
            # Repetitive and above the threshold so the deterministic .gz
            # sidecar and its manifest metadata are exercised.
            "decenza-web.wasm": b"wasm-fixture-" * 12000,
            "qtloader.js": b"globalThis.qtLoad = function () {};",
            "qt-dialog-accessibility.js": b"// accessibility bridge",
        }
        for name, content in contents.items():
            (self.bundle / name).write_bytes(content)
        self.contents = contents
        self.deployment = self.root / "deployment"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def pack(self) -> dict:
        return package_webclient.package_bundle(self.bundle, self.header, self.deployment)

    def test_package_manifest_and_install_verify_exact_bundle(self) -> None:
        manifest = self.pack()

        self.assertEqual(manifest["packageId"], self.package_id)
        self.assertEqual(set(manifest["files"]), set(package_webclient.BASE_FILES))
        wasm_entry = manifest["files"]["decenza-web.wasm"]
        self.assertEqual(wasm_entry["size"], len(self.contents["decenza-web.wasm"]))
        self.assertEqual(wasm_entry["sha256"], hashlib.sha256(self.contents["decenza-web.wasm"]).hexdigest())
        self.assertIn("gzipSize", wasm_entry)
        self.assertIn("gzipSha256", wasm_entry)
        self.assertTrue((self.deployment / "decenza-web.wasm.gz").is_file())
        self.assertNotIn("gzipSize", manifest["files"]["index.html"])
        self.assertFalse((self.deployment / "index.html.gz").exists())

        installed = self.root / "installed-client"
        verified = package_webclient.install_bundle(self.deployment, self.package_id, installed)
        self.assertEqual(verified, manifest)
        for name, content in self.contents.items():
            self.assertEqual((installed / name).read_bytes(), content)
        self.assertEqual(
            (installed / "decenza-web.wasm.gz").read_bytes(),
            (self.deployment / "decenza-web.wasm.gz").read_bytes(),
        )

    def test_gzip_is_skipped_when_larger_than_asset_or_over_runtime_limit(self) -> None:
        self.assertTrue(package_webclient._should_include_gzip(101, 99, 100))
        self.assertFalse(package_webclient._should_include_gzip(100, 100, 100))
        self.assertFalse(package_webclient._should_include_gzip(101, 100, 99))

        # Exercise the package decision with a small per-file limit and a
        # deterministic incompressible input, without a full-size Wasm fixture.
        javascript = random.Random(4271).randbytes(70 * 1024)
        (self.bundle / "decenza-web.js").write_bytes(javascript)
        original_limit_for = package_webclient._limit_for

        def small_javascript_limit(filename: str) -> int:
            return len(javascript) if filename == "decenza-web.js" else original_limit_for(filename)

        with patch.object(package_webclient, "_limit_for", side_effect=small_javascript_limit):
            manifest = self.pack()

        self.assertNotIn("gzipSize", manifest["files"]["decenza-web.js"])
        self.assertFalse((self.deployment / "decenza-web.js.gz").exists())

    def test_missing_input_does_not_replace_previous_deployment(self) -> None:
        self.pack()
        prior_manifest = (self.deployment / "package.json").read_bytes()
        (self.bundle / "qtloader.js").unlink()

        with self.assertRaises(package_webclient.PackageError):
            self.pack()

        self.assertEqual((self.deployment / "package.json").read_bytes(), prior_manifest)
        self.assertTrue((self.deployment / "decenza-web.wasm.gz").is_file())

    def test_install_rejects_wrong_package_id_and_tampered_asset_atomically(self) -> None:
        self.pack()
        installed = self.root / "installed-client"
        installed.mkdir()
        (installed / "old-version.txt").write_text("keep", encoding="utf-8")

        with self.assertRaises(package_webclient.PackageError):
            package_webclient.install_bundle(self.deployment, "b" * 64, installed)
        self.assertEqual((installed / "old-version.txt").read_text(encoding="utf-8"), "keep")

        (self.deployment / "decenza-web.js").write_bytes(b"changed")
        with self.assertRaises(package_webclient.PackageError):
            package_webclient.install_bundle(self.deployment, self.package_id, installed)
        self.assertEqual((installed / "old-version.txt").read_text(encoding="utf-8"), "keep")

    def test_install_rejects_bad_gzip_even_with_matching_manifest_hash(self) -> None:
        manifest = self.pack()
        gzip_path = self.deployment / "decenza-web.wasm.gz"
        gzip_path.write_bytes(b"not a gzip stream")
        entry = manifest["files"]["decenza-web.wasm"]
        entry["gzipSize"] = gzip_path.stat().st_size
        entry["gzipSha256"] = hashlib.sha256(gzip_path.read_bytes()).hexdigest()
        (self.deployment / "package.json").write_text(json.dumps(manifest), encoding="utf-8")

        with self.assertRaises(package_webclient.PackageError):
            package_webclient.install_bundle(self.deployment, self.package_id, self.root / "install")

    def test_install_rejects_manifest_shape_and_unlisted_files(self) -> None:
        self.pack()
        manifest_path = self.deployment / "package.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["unexpected"] = True
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(package_webclient.PackageError):
            package_webclient.install_bundle(self.deployment, self.package_id, self.root / "install-a")

        manifest.pop("unexpected")
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        (self.deployment / "unlisted.txt").write_text("no", encoding="utf-8")
        with self.assertRaises(package_webclient.PackageError):
            package_webclient.install_bundle(self.deployment, self.package_id, self.root / "install-b")


if __name__ == "__main__":
    unittest.main()
