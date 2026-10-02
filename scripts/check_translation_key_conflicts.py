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

# A Tr whose key is switched by a condition. Tr's key and fallback are separate bindings, so on
# every flip its text evaluates once with the new key and the old fallback, which the registry
# reads as a reworded string. Use one translate() call per branch instead.
TR_OPEN = re.compile(r'\bTr\s*\{')
SWITCHED_KEY = re.compile(r'\bkey\s*:(?!\s*")[^\n;]*(\?|\n\s*\?)')

def switched_tr_keys(text, line_of):
    for m in TR_OPEN.finditer(text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        k = SWITCHED_KEY.search(text, m.end(), i)
        if k:
            yield line_of(k.start())

def main() -> int:
    seen = collections.defaultdict(lambda: collections.defaultdict(list))
    switched = []
    for path in sorted(glob.glob("qml/**/*.qml", recursive=True)):
        text = io.open(path, encoding="utf-8").read()
        line_of = lambda pos: text.count("\n", 0, pos) + 1
        switched += [f"{path}:{n}" for n in switched_tr_keys(text, line_of)]

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
    if switched:
        print(f"\n{len(switched)} Tr with a condition-switched key (use one translate() per "
              "branch; Tr's key and fallback update separately):")
        for w in switched:
            print(f"      {w}")
    if not conflicts:
        print("No key is used with more than one English string.")
        return 1 if switched else 0

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
