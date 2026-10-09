#!/usr/bin/env python3
"""Fail if user-facing text sends the user to a Settings tab that does not exist.

The "Shot Stopped" dialog told users to "go to Settings → Bluetooth" long after that tab had
been renamed Connections, so the one recovery step it offered led nowhere. Nothing catches that
kind of drift: the string compiles, translates and renders fine.

What is scanned: qml/ (.qml, .js) and src/ (.cpp, .h) with comments stripped, plus
resources/ai/ (.md) and resources/profiles/ (.json, whose notes ProfileEditorPage shows).

A mention is "Settings" followed by an arrow: → or \\u2192 (any case, "settings → x" too), or
->, >, &gt;, &rarr; with spaces on both sides and a capitalised target, so C++ such as
`QPointer<Settings> guard` is not read as one. HTML tags and &amp; in the target are undone
first. The target must start with a tab name: the tab fallbacks in SettingsTabs.qml (debug-only
tabs excluded, since release builds have no such tab) plus the "settings.tab.*" labels
SettingsPage.qml shows ("Lang & Access"). Not ours, so skipped: the operating system's or
another screen's settings ("System Settings > …", "Android Settings → …", "Brew Settings"),
and a placeholder target ("Settings → %1"). A mention split across string literals or lines is
not seen.

`--self-test` runs the matcher against inline fixtures.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABS_QML = os.path.join(ROOT, "qml", "components", "SettingsTabs.qml")
SETTINGS_PAGE = os.path.join(ROOT, "qml", "pages", "SettingsPage.qml")
# Code is comment-stripped; .md and .json have no comments to strip (a Markdown "#" is a heading).
SCAN = [("qml", (".qml", ".js"), True), ("src", (".cpp", ".h"), True),
        (os.path.join("resources", "ai"), (".md",), False),
        (os.path.join("resources", "profiles"), (".json",), False)]

STRING_OR_COMMENT = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', re.S)
NOT_OURS = r'(?<!System )(?<!system )(?<!Android )(?<!Brew )(?<!iOS )(?<!iPhone )(?<!iPad )'
MENTION = re.compile(NOT_OURS + r'\b(?:[Ss]ettings\s*(?:→|\\u2192)\s*([^"\n]{1,80})'
                                r'|Settings\s+(?:->|&gt;|&rarr;|>)\s+((?:<[^>]*>)*[A-Z][^"\n]{0,79}))')
TAB_RECORD = re.compile(r'\{[^{}]*\}')
PROP = r'\b{}\s*:\s*"([^"]*)"'
TAB_LABEL = re.compile(r'translate\(\s*"settings\.tab\.[^"]*"\s*,\s*"([^"]+)"\s*\)')


def strip_comments(text):
    """Blank out comments, keeping strings and line numbers."""
    def keep(m):
        tok = m.group(0)
        return tok if tok[0] in "\"'" else re.sub(r'[^\n]', ' ', tok)
    return STRING_OR_COMMENT.sub(keep, text)


def tab_names(tabs_text, page_text=""):
    names = set()
    for rec in TAB_RECORD.findall(tabs_text):
        fb = re.search(PROP.format("fallback"), rec)
        if fb and re.search(PROP.format("id"), rec) and not re.search(r'\bdebugOnly\s*:\s*true\b', rec):
            names.add(fb.group(1))
    names.update(TAB_LABEL.findall(page_text))
    # Longest first, so "Shot Upload" is tried before a shorter name that prefixes it.
    return sorted(names, key=len, reverse=True)


def normalise(target):
    target = re.sub(r'<[^>]*>', '', target)
    return target.replace("&amp;", "&").replace("&nbsp;", " ").strip()


def bad_mentions(text, names):
    """Yield (line_number, mention) for every mention whose target is not a tab name."""
    for n, line in enumerate(text.splitlines(), 1):
        for m in MENTION.finditer(line):
            target = normalise(m.group(1) or m.group(2))
            if re.match(r'%\d', target):
                continue  # filled in at runtime (.arg()); nothing to check here
            if not any(re.match(re.escape(name) + r'(?![A-Za-z])', target) for name in names):
                yield n, "Settings -> " + target[:40]


SELF_TEST = [
    ('"Go to Settings \\u2192 Bluetooth and tap Forget"', 1),
    ('"Go to Settings → Connections and tap Forget"', 0),
    ('"Settings > Machine > Theme Mode"', 0),
    ('"Settings &gt; AI &gt; MCP Server"', 0),
    ('"Turn on caching in Settings → Screensaver to start"', 0),
    ('"denied for this app (System Settings > Privacy & Security"', 0),
    ('"Open Settings -> Shot Upload"', 0),
    ('"Open Settings -> Shot Uploads"', 1),
    ('QPointer<Settings> settingsGuard(m_settings);', 0),
    ('"Set city in Settings \\u2192 Options"', 1),
    ('"Go to Settings → bluetooth"', 1),
    ('"Go to Settings \\u2192 connections"', 1),
    ('"notes": "Find this in Settings → Calibration → Flow Calibration."', 0),
    ('"notes": "Find this in Settings → Calibrations."', 1),
    # Lowercase "settings" before a Unicode arrow is still ours.
    ('"open settings → Bluetooth"', 1),
    # Markup and entities in the target.
    ('"Go to Settings → <b>Bluetooth</b>"', 1),
    ('"Go to Settings → <b>Connections</b>"', 0),
    ('"Settings &gt; History &amp; Data"', 0),
    # Other screens' and the OS's settings are not ours.
    ('"Android Settings → Apps → Decenza"', 0),
    ('"tap Brew Settings → Clear"', 0),
    # A runtime placeholder is not checkable.
    ('tr("Find it in Settings → %1").arg(tab)', 0),
    # The short label the tab button actually shows.
    ('"Settings → Lang & Access"', 0),
    # Debug-only tabs do not exist in release builds.
    ('"Settings → Debug"', 1),
]


def self_test() -> int:
    tabs = ('[ { id: "connections", fallback: "Connections" }, { id: "machine", fallback: "Machine" },'
            ' { id: "ai", fallback: "AI" }, { id: "screensaver", fallback: "Screensaver" },'
            ' { id: "visualizer", fallback: "Shot Upload" }, { id: "calibration", fallback: "Calibration" },'
            ' { id: "historyData", fallback: "History & Data" },'
            ' { id: "languageAccess", fallback: "Language & Access" },'
            ' { id: "debug", fallback: "Debug", debugOnly: true } ]')
    page = 'TranslationManager.translate("settings.tab.languageAccess", "Lang & Access")'
    names = tab_names(tabs, page)
    failed = 0
    for text, expected in SELF_TEST:
        got = len(list(bad_mentions(text, names)))
        if got != expected:
            failed += 1
            print(f"self-test FAILED: expected {expected}, got {got}: {text!r}")
    # Comments are stripped from code; a trailing one is a comment too.
    for text in ('x = 1 // see Settings → Firmware', '/* Settings → Firmware */ y = 2'):
        if list(bad_mentions(strip_comments(text), names)):
            failed += 1
            print(f"self-test FAILED: comment was scanned: {text!r}")
    # ...but a "//" inside a string is text.
    if len(list(bad_mentions(strip_comments('s = "http://x Settings → Bluetooth"'), names))) != 1:
        failed += 1
        print("self-test FAILED: a // inside a string hid a mention")
    total = len(SELF_TEST) + 3
    print(f"self-test: {total - failed}/{total} passed")
    return 1 if failed else 0


def main() -> int:
    def read(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    names = tab_names(read(TABS_QML), read(SETTINGS_PAGE))
    found = []
    for rel, exts, code in SCAN:
        for dirpath, _, files in os.walk(os.path.join(ROOT, rel)):
            for fn in files:
                if not fn.endswith(exts):
                    continue
                path = os.path.join(dirpath, fn)
                text = read(path)
                for n, mention in bad_mentions(strip_comments(text) if code else text, names):
                    found.append(f"{os.path.relpath(path, ROOT)}:{n}: {mention}")
    for line in found:
        print(line)
    if found:
        print(f"\n{len(found)} mention(s) of a Settings tab that does not exist. Tabs: {', '.join(sorted(names))}.")
        return 1
    print(f"Every \"Settings -> ...\" mention names a real tab ({len(names)} tab names).")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else main())
