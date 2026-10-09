#!/usr/bin/env python3
"""Fail if a settings card is missing from the settings search index, or the index points nowhere.

SettingsPage scrolls to a search result by finding the card whose objectName equals the
entry's cardId (SettingsPage.findChildByObjectName). So an objectName on a card is the card's
search identity, and SettingsSearchIndex.js is the only list of what search can find.

Three cards had an objectName and no entry when this was written -- temperatureUnit,
sensorCalibration and steamHealth -- so searching "fahrenheit", "celsius" or "units" found
nothing. An entry whose cardId matches no card is the opposite failure: the search result opens
the tab and highlights nothing.

Only what getSearchEntries() returns counts: comments are stripped, and only the objects in the
returned array are read. Each must route somewhere -- a tabId and cardId, or an externalRoute
that SettingsPage actually handles.

`--self-test` runs the checks against inline fixtures.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABS_QML = os.path.join(ROOT, "qml", "components", "SettingsTabs.qml")
INDEX_JS = os.path.join(ROOT, "qml", "components", "SettingsSearchIndex.js")
SETTINGS_PAGE = os.path.join(ROOT, "qml", "pages", "SettingsPage.qml")
PAGES_DIR = os.path.join(ROOT, "qml", "pages")

TAB = re.compile(r'\{\s*id:\s*"([^"]+)".*?source:\s*"([^"]+)"')
OBJECT_NAME = re.compile(r'\bobjectName:\s*"([^"]+)"')
HANDLED_ROUTE = re.compile(r'\bexternalRoute\s*===\s*"([^"]+)"')
# Strings are matched first so that "//" or "/*" inside one is kept, not taken for a comment.
STRING_OR_COMMENT = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
# Entries hold arrays and tr(...) calls but no nested braces, so a flat match is exact.
ENTRY_OBJECT = re.compile(r'\{[^{}]*\}')
PROP = r'\b{}\s*:\s*"([^"]*)"'


def strip_comments(text):
    return STRING_OR_COMMENT.sub(lambda m: m.group(0) if m.group(0)[0] in "\"'" else " ", text)


def returned_array(js):
    """The text of the array literal after `return [`, matched bracket-for-bracket."""
    start = js.find("return [")
    if start < 0:
        return None
    depth, i = 0, start + len("return ")
    for m in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[\[\]]', js[i:]):
        tok = m.group(0)
        if tok == "[":
            depth += 1
        elif tok == "]":
            depth -= 1
            if depth == 0:
                return js[i:i + m.end()]
    return None


def problems(tabs_text, index_text, read_tab, handled_routes):
    """Return a list of human-readable problems. read_tab(source) -> QML text of that tab."""
    tabs = dict(TAB.findall(tabs_text))
    out = []
    cards = {}
    for tab_id, src in tabs.items():
        names = OBJECT_NAME.findall(read_tab(src))
        # findChildByObjectName returns the first match, so a second card with the same name
        # can never be the search target.
        for dup in sorted({n for n in names if names.count(n) > 1}):
            out.append(f'objectName "{dup}" is used by more than one card in {src}')
        cards[tab_id] = set(names)

    entries = returned_array(strip_comments(index_text))
    if entries is None:
        return out + ["could not find the array getSearchEntries() returns (`return [ ... ]`)"]

    indexed = set()
    for obj in ENTRY_OBJECT.findall(entries):
        short = " ".join(obj.split())[:80]
        route = re.search(PROP.format("externalRoute"), obj)
        if route:
            if route.group(1) not in handled_routes:
                out.append(f'externalRoute "{route.group(1)}" is not handled by SettingsPage.qml: {short}')
            continue
        tab = re.search(PROP.format("tabId"), obj)
        card = re.search(PROP.format("cardId"), obj)
        if not tab or not card:
            out.append(f'index entry without both tabId and cardId: {short}')
            continue
        indexed.add((tab.group(1), card.group(1)))

    for tab_id, card_id in sorted(indexed):
        if tab_id not in tabs:
            out.append(f'index entry names unknown tab "{tab_id}" (card "{card_id}")')
        elif card_id and card_id not in cards[tab_id]:
            out.append(f'index entry {tab_id}/{card_id} matches no objectName in {tabs[tab_id]}')
    for tab_id, names in sorted(cards.items()):
        for name in sorted(names):
            if (tab_id, name) not in indexed:
                out.append(f'card {tab_id}/{name} ({tabs[tab_id]}) has no SettingsSearchIndex.js entry')
    return out


TABS_A = '{ id: "a", key: "k", source: "A.qml" }'


def index(*entries, before="", after=""):
    body = ",\n".join(entries)
    return f'function getSearchEntries(tr) {{\n if (!tr) tr = function(k, f) {{ return f }}\n{before} return [\n{body}\n ]{after}\n}}'


SELF_TEST = [
    # (index, {source: tab text}, expected problem count)
    (index('{ tabId: "a", cardId: "one" }'), {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    (index('{ tabId: "a", cardId: "one" }'),
     {"A.qml": 'Rectangle { objectName: "one" } Rectangle { objectName: "two" }'}, 1),
    (index('{ tabId: "a", cardId: "gone" }'), {"A.qml": ''}, 1),
    (index('{ tabId: "b", cardId: "one" }'), {"A.qml": ''}, 1),
    # An entry with an empty cardId opens the tab without scrolling; that is allowed.
    (index('{ tabId: "a", cardId: "" }'), {"A.qml": ''}, 0),
    # Property order does not matter: a stale entry written title-first is still caught.
    (index('{ title: tr("x", "X"), cardId: "gone", keywords: ["a"], tabId: "a" }'), {"A.qml": ''}, 1),
    (index('{ title: tr("x", "X"), cardId: "one", tabId: "a" }'), {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # Two cards with one objectName: the second can never be found.
    (index('{ tabId: "a", cardId: "one" }'),
     {"A.qml": 'Rectangle { objectName: "one" } Rectangle { objectName: "one" }'}, 1),
    # A returned object that routes nowhere is reported, not skipped.
    (index('{ tabId: "a", title: tr("x", "X") }'), {"A.qml": ''}, 1),
    (index('{ title: tr("x", "X"), keywords: ["a"] }'), {"A.qml": ''}, 1),
    # External routes must be ones SettingsPage handles.
    (index('{ externalRoute: "profileSelector", title: tr("x", "X") }'), {"A.qml": ''}, 0),
    (index('{ externalRoute: "profileSelecter", title: tr("x", "X") }'), {"A.qml": ''}, 1),
    # A commented-out entry does not count: the card it covered is reported as missing.
    (index('// { tabId: "a", cardId: "one" },\n { tabId: "a", cardId: "" }'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 1),
    (index('/* { tabId: "a", cardId: "one" }, */ { tabId: "a", cardId: "" }'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 1),
    # "//" inside a string is text, not a comment.
    (index('{ tabId: "a", cardId: "one", title: tr("u", "see https://example.com") }'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # Objects outside the returned array (a helper's body, a header example) are not entries.
    (index('{ tabId: "a", cardId: "one" }', before=' var example = { tabId: "zz", cardId: "nope" }\n'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
]


def self_test() -> int:
    failed = 0
    for idx, files, expected in SELF_TEST:
        got = problems(TABS_A, idx, lambda src: files.get(src, ""), {"profileSelector"})
        if len(got) != expected:
            failed += 1
            print(f"self-test FAILED: expected {expected} problem(s), got {got}\n  index: {idx!r}")
    print(f"self-test: {len(SELF_TEST) - failed}/{len(SELF_TEST)} passed")
    return 1 if failed else 0


def main() -> int:
    def read(path):
        with open(path, encoding="utf-8") as f:
            return f.read()
    routes = set(HANDLED_ROUTE.findall(strip_comments(read(SETTINGS_PAGE))))
    found = problems(read(TABS_QML), read(INDEX_JS),
                     lambda src: read(os.path.join(PAGES_DIR, src)), routes)
    for p in found:
        print(p)
    if found:
        print(f"\n{len(found)} problem(s). Add an entry to qml/components/SettingsSearchIndex.js "
              "for each card, or fix the entry.")
        return 1
    print("Every settings card is in the search index, and every index entry finds its card.")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else main())
