"""Stdlib tests for Wasm bundle staging and native-side verification."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import random
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts" / "package_webclient.py"
SPEC = importlib.util.spec_from_file_location("package_webclient", SCRIPT_PATH)
assert SPEC and SPEC.loader
package_webclient = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package_webclient)
BUILD_SPEC = importlib.util.spec_from_file_location("build_webclient", REPOSITORY_ROOT / "scripts/build_webclient.py")
assert BUILD_SPEC and BUILD_SPEC.loader
build_webclient = importlib.util.module_from_spec(BUILD_SPEC)
with patch.dict(sys.modules, {"package_webclient": package_webclient}):
    BUILD_SPEC.loader.exec_module(build_webclient)
REAL_BROTLI_COMMAND = package_webclient._brotli_command

# Speaks the subset of the brotli CLI the packager uses, with zlib standing in for
# Brotli, so the suite does not depend on the tool being installed. A real build
# runs the real tool on every package step.
FAKE_BROTLI = """
import sys, zlib
arguments = sys.argv[1:]
source = open(arguments[-1], "rb").read()
if "--decompress" in arguments:
    sys.stdout.buffer.write(zlib.decompress(source))
else:
    output = next(a.split("=", 1)[1] for a in arguments if a.startswith("--output="))
    open(output, "wb").write(zlib.compress(source, 9))
"""


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
            # Repetitive and above the threshold so the .br
            # sidecar and its manifest metadata are exercised.
            "decenza-web.wasm": b"wasm-fixture-" * 12000,
            "qtloader.js": b"globalThis.qtLoad = function () {};",
            "qt-dialog-accessibility.js": b"// accessibility bridge",
        }
        for name, content in contents.items():
            (self.bundle / name).write_bytes(content)
        self.contents = contents
        self.deployment = self.root / "deployment"
        self.fake_brotli = self.root / "fake_brotli.py"
        self.fake_brotli.write_text(FAKE_BROTLI, encoding="utf-8")
        brotli = patch.object(package_webclient, "_brotli_command",
                              return_value=[sys.executable, str(self.fake_brotli)])
        brotli.start()
        self.addCleanup(brotli.stop)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def pack(self) -> dict:
        return package_webclient.package_bundle(self.bundle, self.header, self.deployment, 5)

    def test_local_build_finishes_wasm_before_verifying_and_staging(self) -> None:
        configuration = {
            "source": str(REPOSITORY_ROOT), "build": str(self.root / "wasm-build"),
            "bundle": str(self.deployment), "cmake": "cmake", "ninja": "ninja",
            "qt_wasm": str(self.root / "qt-wasm"), "qt_host": str(self.root / "qt-host"),
            "qtro": str(self.root / "qtro"), "multimedia": str(self.root / "multimedia"),
        }
        events = []
        staged = self.root / "embedded"

        def run(command, environment):
            if "--build" in command:
                events.append("build")
                self.pack()
            else:
                events.append("configure")
            self.assertFalse(staged.exists(), "native staging must follow the completed Wasm build")

        install = package_webclient.install_bundle

        def verify(*arguments):
            events.append("verify")
            return install(*arguments)

        with patch.object(build_webclient, "child_environment", return_value={}), \
                patch.object(build_webclient, "ensure_modules"), \
                patch.object(package_webclient, "install_bundle", side_effect=verify):
            build_webclient.build_and_stage(configuration, self.package_id, staged, runner=run)
        self.assertEqual(events, ["configure", "build", "verify"])
        self.assertEqual(set(path.name for path in staged.iterdir()), set(build_webclient.EMBEDDED_FILES))
        self.assertEqual((staged / "qtloader.js.br").read_bytes(), b"")
        # Empty optional slots belong only to native RCC input, never to the
        # verified deployment. A strict re-verification of deployment still passes.
        self.assertFalse((self.deployment / "qtloader.js.br").exists())
        package_webclient._load_manifest(self.deployment, self.package_id)

        # An existing kit delegates source invalidation to Ninja instead of
        # configuring twice. Changed configure options must still be applied.
        cached = dict(argument[2:].split("=", 1)
                      for argument in build_webclient.configure_command(
                          configuration, REPOSITORY_ROOT, Path(configuration["build"]))
                      if argument.startswith("-D"))
        cached.update(CMAKE_HOME_DIRECTORY=str(REPOSITORY_ROOT),
                      DECENZA_QTRO_PREFIX=configuration["qtro"],
                      DECENZA_QTMULTIMEDIA_PREFIX=configuration["multimedia"],
                      DECENZA_WASM_FAST_LINK="OFF")
        for stale in (False, True):
            shutil.rmtree(staged)
            events.clear()
            cache = dict(cached)
            if stale:
                cache["QT_HOST_PATH"] = "old-kit"
            with patch.object(build_webclient, "child_environment", return_value={}), \
                    patch.object(build_webclient, "ensure_modules"), \
                    patch.object(build_webclient, "read_cache", return_value=cache), \
                    patch.object(package_webclient, "install_bundle", side_effect=verify):
                build_webclient.build_and_stage(configuration, self.package_id, staged, runner=run)
            self.assertEqual(events, (["configure"] if stale else []) + ["build", "verify"])

    def test_local_build_rejects_mismatched_output_before_native_staging(self) -> None:
        self.pack()
        configuration = {
            "source": str(REPOSITORY_ROOT), "build": str(self.root / "wasm-build"),
            "bundle": str(self.deployment), "cmake": "cmake", "ninja": "ninja",
            "qt_wasm": str(self.root / "qt-wasm"), "qt_host": str(self.root / "qt-host"),
            "qtro": str(self.root / "qtro"), "multimedia": str(self.root / "multimedia"),
        }
        staged = self.root / "embedded"
        with patch.object(build_webclient, "child_environment", return_value={}), \
                patch.object(build_webclient, "ensure_modules"):
            with self.assertRaises(package_webclient.PackageError):
                build_webclient.build_and_stage(configuration, "b" * 64, staged,
                                                runner=lambda command, environment: None)
        self.assertFalse(staged.exists())

    def test_package_manifest_and_install_verify_exact_bundle(self) -> None:
        manifest = self.pack()

        self.assertEqual(manifest["packageId"], self.package_id)
        self.assertEqual(set(manifest["files"]), set(package_webclient.BASE_FILES))
        wasm_entry = manifest["files"]["decenza-web.wasm"]
        self.assertEqual(wasm_entry["size"], len(self.contents["decenza-web.wasm"]))
        self.assertEqual(wasm_entry["sha256"], hashlib.sha256(self.contents["decenza-web.wasm"]).hexdigest())
        self.assertIn("brSize", wasm_entry)
        self.assertIn("brSha256", wasm_entry)
        self.assertTrue((self.deployment / "decenza-web.wasm.br").is_file())
        self.assertNotIn("brSize", manifest["files"]["index.html"])
        self.assertFalse((self.deployment / "index.html.br").exists())

        installed = self.root / "installed-client"
        verified = package_webclient.install_bundle(self.deployment, self.package_id, installed,
                                                    require_brotli=True)
        self.assertEqual(verified, manifest)
        for name, content in self.contents.items():
            self.assertEqual((installed / name).read_bytes(), content)
        self.assertEqual(
            (installed / "decenza-web.wasm.br").read_bytes(),
            (self.deployment / "decenza-web.wasm.br").read_bytes(),
        )

    def test_brotli_is_skipped_when_larger_than_asset_or_over_runtime_limit(self) -> None:
        self.assertTrue(package_webclient._should_include_brotli(101, 99, 100))
        self.assertFalse(package_webclient._should_include_brotli(100, 100, 100))
        self.assertFalse(package_webclient._should_include_brotli(101, 100, 99))

        # Exercise the package decision with a small per-file limit and a
        # deterministic incompressible input, without a full-size Wasm fixture.
        javascript = random.Random(4271).randbytes(70 * 1024)
        (self.bundle / "decenza-web.js").write_bytes(javascript)
        original_limit_for = package_webclient._limit_for

        def small_javascript_limit(filename: str) -> int:
            return len(javascript) if filename == "decenza-web.js" else original_limit_for(filename)

        with patch.object(package_webclient, "_limit_for", side_effect=small_javascript_limit):
            manifest = self.pack()

        self.assertNotIn("brSize", manifest["files"]["decenza-web.js"])
        self.assertFalse((self.deployment / "decenza-web.js.br").exists())

    def test_missing_input_does_not_replace_previous_deployment(self) -> None:
        self.pack()
        prior_manifest = (self.deployment / "package.json").read_bytes()
        (self.bundle / "qtloader.js").unlink()

        with self.assertRaises(package_webclient.PackageError):
            self.pack()

        self.assertEqual((self.deployment / "package.json").read_bytes(), prior_manifest)
        self.assertTrue((self.deployment / "decenza-web.wasm.br").is_file())

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

    def test_pack_rejects_brotli_output_that_does_not_reproduce_the_asset(self) -> None:
        self.fake_brotli.write_text(FAKE_BROTLI.replace(
            "zlib.compress(source, 9)", "zlib.compress(source[:-1], 9)"), encoding="utf-8")
        with self.assertRaisesRegex(package_webclient.PackageError, "does not reproduce"):
            self.pack()
        self.assertFalse(self.deployment.exists())

    def test_pack_names_the_missing_brotli_tool_and_rejects_bad_quality(self) -> None:
        with patch.object(package_webclient.shutil, "which", return_value=None):
            with self.assertRaisesRegex(package_webclient.PackageError, "brotli command-line tool"):
                REAL_BROTLI_COMMAND()
        with self.assertRaisesRegex(package_webclient.PackageError, "between 0 and 11"):
            package_webclient.package_bundle(self.bundle, self.header, self.deployment, 12)

    def test_install_rejects_brotli_copy_that_differs_from_its_manifest(self) -> None:
        self.pack()
        (self.deployment / "decenza-web.wasm.br").write_bytes(b"not the packaged stream")
        with self.assertRaisesRegex(package_webclient.PackageError, "does not match its manifest"):
            package_webclient.install_bundle(self.deployment, self.package_id, self.root / "install")

    def test_release_install_requires_a_brotli_runtime(self) -> None:
        manifest = self.pack()
        entry = manifest["files"]["decenza-web.wasm"]
        del entry["brSize"], entry["brSha256"]
        (self.deployment / "decenza-web.wasm.br").unlink()
        (self.deployment / "package.json").write_text(json.dumps(manifest), encoding="utf-8")

        package_webclient.install_bundle(self.deployment, self.package_id, self.root / "local")
        with self.assertRaisesRegex(package_webclient.PackageError, "no Brotli copy"):
            package_webclient.install_bundle(self.deployment, self.package_id, self.root / "release",
                                             require_brotli=True)

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
