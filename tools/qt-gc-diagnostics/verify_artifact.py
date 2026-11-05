#!/usr/bin/env python3
"""Check the Android diagnostic library and its recorded source provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]


def verify(sdk_root=None):
    manifest = json.loads((ROOT / "android/qt-overrides/manifest.json").read_text())
    library = ROOT / "android/qt-overrides" / manifest["library"]
    data = library.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[18:20], "little") != 183:
        raise ValueError("QtQml override must be a little-endian ELF64 AArch64 library")
    if hashlib.sha256(data).hexdigest() != manifest["sha256"]:
        raise ValueError("QtQml override does not match its manifest checksum")
    if manifest["revision_marker"].encode() not in data:
        raise ValueError("QtQml override is missing the required diagnostic revision")
    for forbidden in (b"DECENZA_GC_TEST_MODE", b"GC_TEST_REACHED", b"GC_TEST_SETUP_FAILED"):
        if forbidden in data:
            raise ValueError("QtQml override contains test-only fault injection")
    for patch in manifest["patches"]:
        actual = hashlib.sha256((ROOT / patch["path"]).read_bytes()).hexdigest()
        if actual != patch["sha256"]:
            raise ValueError(f"Patch changed since QtQml was built: {patch['path']}")
    if sdk_root is not None:
        for name, expected in manifest["stock_sdk_sha256"].items():
            actual = hashlib.sha256((sdk_root / "lib" / name).read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError(f"Stock SDK library differs from the verified Qt build: {name}")
    print(f"Verified QtQml {manifest['revision_marker']}; SHA-256 {manifest['sha256']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path)
    args = parser.parse_args()
    try:
        verify(args.sdk_root)
    except (OSError, KeyError, ValueError) as error:
        print(f"QtQml verification failed: {error}", file=sys.stderr)
        sys.exit(1)
