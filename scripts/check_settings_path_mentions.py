#!/usr/bin/env python3
"""Fail if user-facing text sends the user to a Settings tab that does not exist.

The "Shot Stopped" dialog told users to "go to Settings → Bluetooth" long after that tab had
been renamed Connections, so the one recovery step it offered led nowhere. Nothing catches that
kind of drift: the string compiles, translates and renders fine.

This reads every non-comment line of qml/, src/ and resources/ai/ for "Settings" followed by an
arrow (→, \\u2192, ->, >, &gt;, &rarr;) and checks that what follows starts with the English
name of a real tab, as SettingsTabs.qml declares it. "System Settings > ..." (the operating
system's settings app) is not ours and is skipped.

`--self-test` runs the matcher against inline fixtures.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABS_QML = os.path.join(ROOT, "qml", "components", "SettingsTabs.qml")
# resources/profiles: bundled profiles' notes are rendered to users (ProfileEditorPage), and the
# calibration profiles route there with "Settings → Calibration".
SCAN = [("qml", (".qml", ".js")), ("src", (".cpp", ".h")), (os.path.join("resources", "ai"), (".md",)),
        (os.path.join("resources", "profiles"), (".json",))]

TAB_FALLBACK = re.compile(r'\bfallback:\s*"([^"]+)"')
# A → is only ever a path, so any target after it is checked (a lowercase one is simply not a
# tab). ASCII arrows need spaces on both sides and a capitalised target, so C++ such as
# `QPointer<Settings> guard` is not read as a mention.
MENTION = re.compile(r'(?<!System )\bSettings(?:\s*(?:→|\\u2192)\s*([^"\n<\s][^"\n<]{0,39})'
                     r'|\s+(?:->|&gt;|&rarr;|>)\s+([A-Z][^"\n<]{0,39}))')
COMMENT = re.compile(r'^\s*(//|/\*|\*|#)')


def tab_names(tabs_text):
    # Longest first, so "Shot Upload" is tried before a shorter name that prefixes it.
    return sorted(set(TAB_FALLBACK.findall(tabs_text)), key=len, reverse=True)


def bad_mentions(text, names):
    """Yield (line_number, mention) for every mention whose target is not a tab name."""
    for n, line in enumerate(text.splitlines(), 1):
        if COMMENT.match(line):
            continue
        for m in MENTION.finditer(line):
            target = m.group(1) or m.group(2)
            if not any(re.match(re.escape(name) + r'(?![A-Za-z])', target) for name in names):
                yield n, "Settings -> " + target.strip()


SELF_TEST = [
    ('"Go to Settings \\u2192 Bluetooth and tap Forget"', 1),
    ('"Go to Settings → Connections and tap Forget"', 0),
    ('"Settings > Machine > Theme Mode"', 0),
    ('"Settings &gt; AI &gt; MCP Server"', 0),
    ('"Turn on caching in Settings → Screensaver to start"', 0),
    ('"denied for this app (System Settings > Privacy & Security"', 0),
    ('// Settings → Firmware tab would show this', 0),
    ('"Open Settings -> Shot Upload"', 0),
    ('"Open Settings -> Shot Uploads"', 1),
    ('QPointer<Settings> settingsGuard(m_settings);', 0),
    ('"Set city in Settings \\u2192 Options"', 1),
    ('"Go to Settings → bluetooth"', 1),
    ('"Go to Settings \\u2192 connections"', 1),
    ('"notes": "Find this in Settings → Calibration → Flow Calibration."', 0),
    ('"notes": "Find this in Settings → Calibrations."', 1),
]


def self_test() -> int:
    names = ["Connections", "Machine", "AI", "Screensaver", "Shot Upload", "Calibration"]
    failed = 0
    for text, expected in SELF_TEST:
        got = len(list(bad_mentions(text, sorted(names, key=len, reverse=True))))
        if got != expected:
            failed += 1
            print(f"self-test FAILED: expected {expected}, got {got}: {text!r}")
    print(f"self-test: {len(SELF_TEST) - failed}/{len(SELF_TEST)} passed")
    return 1 if failed else 0


def main() -> int:
    with open(TABS_QML, encoding="utf-8") as f:
        names = tab_names(f.read())
    found = []
    for rel, exts in SCAN:
        for dirpath, _, files in os.walk(os.path.join(ROOT, rel)):
            for fn in files:
                if not fn.endswith(exts):
                    continue
                path = os.path.join(dirpath, fn)
                with open(path, encoding="utf-8", errors="replace") as f:
                    for n, mention in bad_mentions(f.read(), names):
                        found.append(f"{os.path.relpath(path, ROOT)}:{n}: {mention}")
    for line in found:
        print(line)
    if found:
        print(f"\n{len(found)} mention(s) of a Settings tab that does not exist. Tabs: {', '.join(sorted(names))}.")
        return 1
    print(f"Every \"Settings -> ...\" mention names a real tab ({len(names)} tabs).")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else main())
