#!/usr/bin/env python3
"""Fail if one translation key is used with two different English strings.

The string registry maps key -> English, so a key used with two fallbacks has no correct
value: whichever QML site is scanned or rendered last wins. Consequences, all silent:

  * Translators are shown one string while users may be shown the other.
  * The registry's value for that key flips between runs, which is what the AI translator is
    prompted with and what a community upload publishes.

27 keys were in this state when the check was written -- every beanbase.details.* label,
and common.accessibility.dismissDialog across eleven files. Most were a visible label and an
Accessible.name sharing a key, which is a reasonable thing to want and needs two keys.

Two ways to fix a report from this script:
  * The difference is noise (a trailing colon, casing) -- unify on the variant the existing
    translations were made from, or you silently invalidate them.
  * The difference is real (short label vs spoken name) -- give the accessible one its own
    key, suffixed `.accessible`, the convention set by changebeans.form.url.accessible.
"""
import collections, glob, io, re, sys

DIRECT = re.compile(r'translate\s*\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')
PROP = re.compile(r'(labelKey|translationKey)\s*:\s*"([^"]+)"\s*;?\s*'
                  r'(labelFallback|translationFallback)\s*:\s*"((?:[^"\\]|\\.)*)"')

def main() -> int:
    seen = collections.defaultdict(lambda: collections.defaultdict(list))
    for path in sorted(glob.glob("qml/**/*.qml", recursive=True)):
        for lineno, line in enumerate(io.open(path, encoding="utf-8"), 1):
            for m in DIRECT.finditer(line):
                seen[m.group(1)][m.group(2)].append(f"{path}:{lineno}")
            for m in PROP.finditer(line):
                seen[m.group(2)][m.group(4)].append(f"{path}:{lineno}")

    conflicts = {k: v for k, v in seen.items() if len(v) > 1}
    print(f"Checked {len(seen)} translation keys across QML.")
    if not conflicts:
        print("No key is used with more than one English string.")
        return 0

    print(f"\n{len(conflicts)} key(s) used with more than one English string:\n")
    for key, variants in sorted(conflicts.items()):
        print(f"  {key}")
        for text, where in sorted(variants.items()):
            print(f"      {text!r}")
            for w in where:
                print(f"          {w}")
    return 1

if __name__ == "__main__":
    sys.exit(main())
