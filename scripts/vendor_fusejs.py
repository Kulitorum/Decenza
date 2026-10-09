#!/usr/bin/env python3
"""Vendor Fuse.js into the QML module, rewritten so Qt's JS engine (QV4) can parse it.

QV4 in Qt 6.12 does not support ES2018 object spread/rest (`{...a}`, `({a, ...b}) =>`):
loading the stock dist/fuse.mjs fails with "Unexpected token `...'" at each site.
Array spread and rest parameters are ES2015 and parse fine, so only the object
forms are rewritten, each to the Object.assign equivalent.

Usage:
    npm pack fuse.js@7.5.0 && tar -xzf fuse.js-7.5.0.tgz
    python3 scripts/vendor_fusejs.py package/dist/fuse.mjs package/LICENSE

Fails if the input is not the expected upstream file or if any rewrite does not apply
exactly once, so a version bump is a deliberate edit of UPSTREAM_SHA256 and the table.
"""
import hashlib
import sys
from pathlib import Path

VERSION = "7.5.0"
UPSTREAM_SHA256 = "cfb7f9c5b0572b6db1900633c5d573f6ef7885e5ac2adccc99ae757b38cbbf1f"
OUT_DIR = Path(__file__).resolve().parent.parent / "qml" / "third_party" / "fuse"

REWRITES = [
    ("const Config = Object.freeze({\n\t...BasicOptions,\n\t...MatchOptions,\n"
     "\t...FuzzyOptions,\n\t...AdvancedOptions\n});",
     "const Config = Object.freeze(Object.assign({}, BasicOptions, MatchOptions, "
     "FuzzyOptions, AdvancedOptions));"),
    ("keys: this.keys.map(({ getFn, ...key }) => key),",
     "keys: this.keys.map((k) => { const key = Object.assign({}, k); delete key.getFn; "
     "return key; }),"),
    ("this.options = {\n\t\t\t...Config,\n\t\t\t...options\n\t\t};",
     "this.options = Object.assign({}, Config, options);"),
    ("{\n\t\t\t...this.options,\n\t\t\t_invertedIndex: this._invertedIndex\n\t\t}",
     "Object.assign({}, this.options, { _invertedIndex: this._invertedIndex })"),
    ("createSearcher(pattern, {\n\t\t...Config,\n\t\t...options\n\t})",
     "createSearcher(pattern, Object.assign({}, Config, options))"),
]

HEADER = (f"// Fuse.js v{VERSION} (Apache-2.0, see LICENSE beside this file), vendored by\n"
          f"// scripts/vendor_fusejs.py: object spread rewritten to Object.assign for QV4.\n"
          f"// Do not edit by hand; re-run the script.\n")


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, licence = Path(sys.argv[1]), Path(sys.argv[2])
    data = src.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != UPSTREAM_SHA256:
        sys.exit(f"{src}: sha256 {digest} is not Fuse.js {VERSION} ({UPSTREAM_SHA256})")
    text = data.decode("utf-8")
    for old, new in REWRITES:
        if text.count(old) != 1:
            sys.exit(f"rewrite did not apply exactly once: {old[:60]!r}")
        text = text.replace(old, new)
    if "{\n\t\t...\n" in text or "{ ..." in text or "\t...Config" in text:
        sys.exit("object spread left after rewriting")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / "fuse.mjs").write_text(HEADER + text, encoding="utf-8")
    (OUT_DIR / "LICENSE").write_bytes(licence.read_bytes())
    print(f"wrote {OUT_DIR / 'fuse.mjs'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
