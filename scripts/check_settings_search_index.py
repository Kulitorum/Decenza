#!/usr/bin/env python3
"""Fail if a settings card is missing from the settings search index, or the index points nowhere.

SettingsPage scrolls to a search result by finding the card whose objectName equals the
entry's cardId (SettingsPage.findChildByObjectName). So an objectName on a card is the card's
search identity, and SettingsSearchIndex.js is the only list of what search can find.

Three cards had an objectName and no entry when this was written -- temperatureUnit,
sensorCalibration and steamHealth -- so searching "fahrenheit", "celsius" or "units" found
nothing. An entry whose cardId matches no card is the opposite failure: the search result opens
the tab and highlights nothing.

`--self-test` runs the checks against inline fixtures.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABS_QML = os.path.join(ROOT, "qml", "components", "SettingsTabs.qml")
INDEX_JS = os.path.join(ROOT, "qml", "components", "SettingsSearchIndex.js")
PAGES_DIR = os.path.join(ROOT, "qml", "pages")

TAB = re.compile(r'\{\s*id:\s*"([^"]+)".*?source:\s*"([^"]+)"')
# One index entry: a brace-delimited object literal. Entries hold arrays and tr(...) calls but
# no nested braces, so a flat match is exact; properties are then read in any order.
ENTRY_OBJECT = re.compile(r'\{[^{}]*\}')
PROP = r'\b{}\s*:\s*"([^"]*)"'
OBJECT_NAME = re.compile(r'\bobjectName:\s*"([^"]+)"')


def problems(tabs_text, index_text, read_tab):
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

    indexed = set()
    for obj in ENTRY_OBJECT.findall(index_text):
        if re.search(r'\bexternalRoute\s*:', obj):
            continue  # routes outside Settings; tabId/cardId are ignored for it
        tab = re.search(PROP.format("tabId"), obj)
        card = re.search(PROP.format("cardId"), obj)
        if not tab and not card:
            continue  # a flat {...} that is not an entry, e.g. a function body
        if not tab or not card:
            out.append(f'index entry without both tabId and cardId: {" ".join(obj.split())[:80]}')
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


SELF_TEST = [
    # (tabs, index, {source: tab text}, expected problem count)
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "a", cardId: "one" }',
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "a", cardId: "one" }',
     {"A.qml": 'Rectangle { objectName: "one" } Rectangle { objectName: "two" }'}, 1),
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "a", cardId: "gone" }',
     {"A.qml": ''}, 1),
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "b", cardId: "one" }',
     {"A.qml": ''}, 1),
    # An entry with an empty cardId opens the tab without scrolling; that is allowed.
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "a", cardId: "" }',
     {"A.qml": ''}, 0),
    # Property order does not matter: a stale entry written title-first is still caught.
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ title: tr("x", "X"), cardId: "gone", keywords: ["a"], tabId: "a" }',
     {"A.qml": ''}, 1),
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ title: tr("x", "X"), cardId: "one", tabId: "a" }',
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # Two cards with one objectName: the second can never be found.
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "a", cardId: "one" }',
     {"A.qml": 'Rectangle { objectName: "one" } Rectangle { objectName: "one" }'}, 1),
    # An entry missing its cardId is reported, not silently skipped.
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ tabId: "a", title: tr("x", "X") }',
     {"A.qml": ''}, 1),
    # External routes leave Settings and are not checked against a tab.
    ('{ id: "a", key: "k", source: "A.qml" }',
     '{ externalRoute: "profileSelector", title: tr("x", "X") }',
     {"A.qml": ''}, 0),
]


def self_test() -> int:
    failed = 0
    for tabs, index, files, expected in SELF_TEST:
        got = problems(tabs, index, lambda src: files.get(src, ""))
        if len(got) != expected:
            failed += 1
            print(f"self-test FAILED: expected {expected} problem(s), got {got}")
    print(f"self-test: {len(SELF_TEST) - failed}/{len(SELF_TEST)} passed")
    return 1 if failed else 0


def main() -> int:
    def read(path):
        with open(path, encoding="utf-8") as f:
            return f.read()
    found = problems(read(TABS_QML), read(INDEX_JS), lambda src: read(os.path.join(PAGES_DIR, src)))
    for p in found:
        print(p)
    if found:
        print(f"\n{len(found)} problem(s). Add an entry to qml/components/SettingsSearchIndex.js "
              "for each card, or fix the cardId.")
        return 1
    print("Every settings card is in the search index, and every index entry finds its card.")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else main())
