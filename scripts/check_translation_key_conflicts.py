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

# Mirror the THREE patterns scanAllStrings() uses, including its nearest-fallback-within-200-
# characters pairing. An earlier version of this script only matched labelKey/translationKey on
# a single line and so reported a clean tree while the running app was logging conflicts for
# settings.tab.languageAccess and three others -- they use the plain key:/fallback: form, spread
# across lines. A checker that is narrower than the thing it checks gives false assurance.
DIRECT = re.compile(r'translate\s*\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')
KEY_ANY = re.compile(r'\b(?:labelKey|translationKey|key)\s*:\s*"([^"]+)"')
FB_ANY = re.compile(r'\b(?:labelFallback|translationFallback|fallback)\s*:\s*"((?:[^"\\]|\\.)*)"')

def main() -> int:
    seen = collections.defaultdict(lambda: collections.defaultdict(list))
    for path in sorted(glob.glob("qml/**/*.qml", recursive=True)):
        text = io.open(path, encoding="utf-8").read()
        line_of = lambda pos: text.count("\n", 0, pos) + 1

        for m in DIRECT.finditer(text):
            seen[m.group(1)][m.group(2)].append(f"{path}:{line_of(m.start())}")

        # key -> nearest following fallback within 200 characters, as scanAllStrings does.
        fallbacks = [(m.start(), m.group(1)) for m in FB_ANY.finditer(text)]
        for km in KEY_ANY.finditer(text):
            for fpos, ftext in fallbacks:
                if fpos > km.start() and fpos - km.start() < 200:
                    seen[km.group(1)][ftext].append(f"{path}:{line_of(km.start())}")
                    break

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
