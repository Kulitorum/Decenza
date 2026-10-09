#!/usr/bin/env python3
"""Fail if a settings card is missing from the settings search index, or the index points nowhere.

SettingsPage scrolls to a search result by finding the card whose objectName equals the
entry's cardId (SettingsPage.findChildByObjectName). So an objectName on a card is the card's
search identity, and SettingsSearchIndex.js is the only list of what search can find.

Three cards had an objectName and no entry when this was written -- temperatureUnit,
sensorCalibration and steamHealth -- so searching "fahrenheit", "celsius" or "units" found
nothing. An entry whose cardId matches no card is the opposite failure: the search result opens
the tab and highlights nothing.

What is read, all with comments stripped:
  * SettingsTabs.qml: every object in the `tabs` array, `id` and `source` in any order.
  * Each tab's QML, and every component file under qml/ it instantiates (transitively): their
    objectName string literals; any non-literal objectName is reported, since search cannot
    target it. findChildByObjectName reaches a card inside an instantiated component, so
    those are cards of the tab too. Cards on debug-only tabs need no entry, and an entry
    pointing at one is reported (the tab is hidden in release builds). Duplicate tab ids are
    reported, since SettingsTabs.indexOf() would only ever find the first.
  * SettingsSearchDialog.qml: every cardId it filters out for some builds must still be an
    indexed card, or the filter has gone stale.
  * SettingsSearchIndex.js: only the array getSearchEntries() returns at its top level. Every
    object in it must carry a `keywords` array (SettingsSearchDialog joins it unconditionally)
    and a title and description, and route somewhere -- tabId plus cardId, or an externalRoute
    that SettingsPage handles. A routing key given twice is reported: JavaScript keeps the
    last value, so a check that read the first would vouch for the wrong pair.

`--self-test` runs the checks against inline fixtures.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABS_QML = os.path.join(ROOT, "qml", "components", "SettingsTabs.qml")
INDEX_JS = os.path.join(ROOT, "qml", "components", "SettingsSearchIndex.js")
SETTINGS_PAGE = os.path.join(ROOT, "qml", "pages", "SettingsPage.qml")
SEARCH_DIALOG = os.path.join(ROOT, "qml", "pages", "settings", "SettingsSearchDialog.qml")
PAGES_DIR = os.path.join(ROOT, "qml", "pages")

STRING = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
# Strings are matched first so that "//" or "/*" inside one is kept, not taken for a comment.
STRING_OR_COMMENT = re.compile(STRING + r'|//[^\n]*|/\*.*?\*/', re.S)
STRING_OR_BRACKET = re.compile(STRING + r'|[\[\]{}]')
OBJECT_NAME = re.compile(r'\bobjectName\s*:\s*')
LITERAL_VALUE = re.compile(r'(' + STRING + r')\s*(?:;|\n|\}|$)')
DIALOG_DROPPED_CARD = re.compile(r'\bcardId\s*!==\s*"([^"]+)"')
HANDLED_ROUTE = re.compile(r'\bexternalRoute\s*===\s*"([^"]+)"')
PROP = r'\b{}\s*:\s*"([^"]*)"'


def strip_comments(text):
    return STRING_OR_COMMENT.sub(lambda m: m.group(0) if m.group(0)[0] in "\"'" else " ", text)


def matching_close(text, open_index):
    """Index just past the bracket that closes the one at open_index; None if unbalanced."""
    stack = []
    for m in STRING_OR_BRACKET.finditer(text, open_index):
        tok = m.group(0)
        if tok in "[{":
            stack.append(tok)
        elif tok in "]}":
            if not stack or "[{"["]}".index(tok)] != stack.pop():
                return None
            if not stack:
                return m.end()
    return None


def top_level_objects(array_text):
    """The `{...}` objects directly inside an array literal (not ones nested deeper)."""
    out, depth = [], 0
    for m in STRING_OR_BRACKET.finditer(array_text):
        tok = m.group(0)
        if tok == "{" and depth == 1:
            end = matching_close(array_text, m.start())
            if end is None:
                return None
            out.append(array_text[m.start():end])
        if tok in "[{":
            depth += 1
        elif tok in "]}":
            depth -= 1
    return out


def array_after(text, pattern):
    """The array literal that starts at the first '[' after `pattern` matches."""
    m = re.search(pattern, text)
    if not m:
        return None
    start = text.find("[", m.end() - 1)
    end = matching_close(text, start) if start >= 0 else None
    return text[start:end] if end else None


def returned_entries(js):
    """The array getSearchEntries() returns from its own body, not from a nested function."""
    m = re.search(r'\bfunction\s+getSearchEntries\s*\([^)]*\)\s*\{', js)
    if not m:
        return None
    body_end = matching_close(js, m.end() - 1)
    if body_end is None:
        return None
    body = js[m.end() - 1:body_end]
    depth = 0
    for t in re.finditer(STRING + r'|[{}]|\breturn\s*\[', body):
        tok = t.group(0)
        if tok == "{":
            depth += 1
        elif tok == "}":
            depth -= 1
        elif tok.startswith("return") and depth == 1:
            start = t.end() - 1
            end = matching_close(body, start)
            return body[start:end] if end else None
    return None


INSTANTIATION = re.compile(r'\b([A-Z][A-Za-z0-9_]*)\s*\{')


def tab_card_names(src, read_tab, components, out):
    """objectName literals in a tab and in every component file it instantiates, transitively,
    once per instantiation (a component used twice declares its cards twice).
    components: {TypeName: callable returning that file's QML text}."""
    memo, active = {}, set()

    def expand(qml, label):
        names = card_names(qml, label, out)
        for type_name in INSTANTIATION.findall(qml):
            if type_name not in components or type_name in active:
                continue  # not a project component, or a recursive use
            if type_name not in memo:
                active.add(type_name)
                memo[type_name] = expand(strip_comments(components[type_name]()), type_name + ".qml")
                active.discard(type_name)
            names = names + memo[type_name]
        return names

    return expand(strip_comments(read_tab(src)), src)


def card_names(qml, src, out):
    """objectName literals in a tab's QML; any other objectName binding is reported."""
    names = []
    for m in OBJECT_NAME.finditer(qml):
        lit = LITERAL_VALUE.match(qml, m.end())
        if lit:
            names.append(lit.group(1)[1:-1])
        else:
            line = qml[m.start():qml.find("\n", m.start())].strip()
            out.append(f'non-literal objectName in {src} (search cannot target it): {line[:80]}')
    return names


def problems(tabs_text, index_text, read_tab, handled_routes, dropped_cards=(), components=None):
    """Return a list of human-readable problems. read_tab(source) -> QML text of that tab.
    dropped_cards: cardIds SettingsSearchDialog filters out at runtime in some builds.
    components: {TypeName: callable returning its QML} for the component files under qml/."""
    components = components or {}
    out = []
    tab_array = array_after(strip_comments(tabs_text), r'\btabs\s*:\s*\[')
    tab_objects = top_level_objects(tab_array) if tab_array else None
    if not tab_objects:
        return ["could not read the `tabs` array in SettingsTabs.qml"]
    tabs, debug_only = {}, set()
    for obj in tab_objects:
        tid, src = re.search(PROP.format("id"), obj), re.search(PROP.format("source"), obj)
        if not tid or not src:
            out.append(f'tab record without both id and source: {" ".join(obj.split())[:80]}')
            continue
        if tid.group(1) in tabs:
            out.append(f'tab id "{tid.group(1)}" is declared twice in SettingsTabs.qml')
            continue
        tabs[tid.group(1)] = src.group(1)
        if re.search(r'\bdebugOnly\s*:\s*true\b', obj):
            debug_only.add(tid.group(1))

    cards = {}
    for tab_id, src in tabs.items():
        names = tab_card_names(src, read_tab, components, out)
        # findChildByObjectName returns the first match, so a second card with the same name
        # can never be the search target.
        for dup in sorted({n for n in names if names.count(n) > 1}):
            out.append(f'objectName "{dup}" is used by more than one card in {src}')
        cards[tab_id] = set(names)

    entries_array = returned_entries(strip_comments(index_text))
    entries = top_level_objects(entries_array) if entries_array else None
    if entries is None:
        return out + ["could not find the array getSearchEntries() returns at its top level"]

    indexed = set()
    for obj in entries:
        short = " ".join(obj.split())[:80]
        # SettingsSearchDialog reads all three; a missing keywords array throws on the first query.
        missing = [field for field, pattern in (("keywords array", r'\bkeywords\s*:\s*\['),
                                                ("title", r'\btitle\s*:'),
                                                ("description", r'\bdescription\s*:'))
                   if not re.search(pattern, obj)]
        if missing:
            out.append(f'index entry without {", ".join(missing)}: {short}')
        doubled = [key for key in ("tabId", "cardId", "externalRoute")
                   if len(re.findall(r'\b' + key + r'\s*:', obj)) > 1]
        if doubled:
            out.append(f'index entry sets {", ".join(doubled)} more than once: {short}')
            continue
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
        if tab_id in debug_only:
            # SettingsTabs hides debug-only tabs in release, so SettingsPage finds no tab for it.
            out.append(f'index entry {tab_id}/{card_id} targets a debug-only tab: a dead result in release builds')
        elif tab_id not in tabs:
            out.append(f'index entry names unknown tab "{tab_id}" (card "{card_id}")')
        elif card_id and card_id not in cards[tab_id]:
            out.append(f'index entry {tab_id}/{card_id} matches no objectName in {tabs[tab_id]}')
    indexed_cards = {card for _, card in indexed}
    for card in dropped_cards:
        if card not in indexed_cards:
            out.append(f'SettingsSearchDialog filters out cardId "{card}", which no index entry has: the filter is stale')
    for tab_id, names in sorted(cards.items()):
        if tab_id in debug_only:
            continue
        for name in sorted(names):
            if (tab_id, name) not in indexed:
                out.append(f'card {tab_id}/{name} ({tabs[tab_id]}) has no SettingsSearchIndex.js entry')
    return out


TABS_A = 'Item { readonly property var tabs: [\n { id: "a", key: "k", source: "A.qml" }\n ] }'
KW = 'title: tr("t", "T"), description: tr("d", "D"), keywords: ["k"]'


def index(*entries, before="", helper=""):
    body = ",\n".join(entries)
    return (f'{helper}function getSearchEntries(tr) {{\n if (!tr) tr = function(k, f) {{ return f }}\n'
            f'{before} return [\n{body}\n ]\n}}')


SELF_TEST = [
    # (tabs, index, {source: tab text}, expected problem count)
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}'), {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}'),
     {"A.qml": 'Rectangle { objectName: "one" } Rectangle { objectName: "two" }'}, 1),
    (TABS_A, index(f'{{ tabId: "a", cardId: "gone", {KW} }}'), {"A.qml": ''}, 1),
    (TABS_A, index(f'{{ tabId: "b", cardId: "one", {KW} }}'), {"A.qml": ''}, 1),
    # An entry with an empty cardId opens the tab without scrolling; that is allowed.
    (TABS_A, index(f'{{ tabId: "a", cardId: "", {KW} }}'), {"A.qml": ''}, 0),
    # Property order does not matter.
    (TABS_A, index(f'{{ title: tr("x", "X"), cardId: "gone", {KW}, tabId: "a" }}'), {"A.qml": ''}, 1),
    (TABS_A, index(f'{{ title: tr("x", "X"), cardId: "one", {KW}, tabId: "a" }}'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # Two cards with one objectName: the second can never be found.
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}'),
     {"A.qml": 'Rectangle { objectName: "one" } Rectangle { objectName: "one" }'}, 1),
    # A returned object that routes nowhere is reported, not skipped.
    (TABS_A, index(f'{{ tabId: "a", title: tr("x", "X"), {KW} }}'), {"A.qml": ''}, 1),
    (TABS_A, index(f'{{ title: tr("x", "X"), {KW} }}'), {"A.qml": ''}, 1),
    # External routes must be ones SettingsPage handles.
    (TABS_A, index(f'{{ externalRoute: "profileSelector", {KW} }}'), {"A.qml": ''}, 0),
    (TABS_A, index(f'{{ externalRoute: "profileSelecter", {KW} }}'), {"A.qml": ''}, 1),
    # Every entry needs a keywords array, a title and a description (one problem per entry).
    (TABS_A, index('{ tabId: "a", cardId: "one" }'), {"A.qml": 'Rectangle { objectName: "one" }'}, 1),
    (TABS_A, index('{ tabId: "a", cardId: "one", title: tr("t", "T"), keywords: ["k"] }'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 1),
    (TABS_A, index('{ externalRoute: "profileSelector", keyword: ["k"] }'), {"A.qml": ''}, 1),
    # A commented-out entry does not count: the card it covered is reported as missing.
    (TABS_A, index(f'// {{ tabId: "a", cardId: "one", {KW} }},\n {{ tabId: "a", cardId: "", {KW} }}'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 1),
    (TABS_A, index(f'/* {{ tabId: "a", cardId: "one", {KW} }}, */ {{ tabId: "a", cardId: "", {KW} }}'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 1),
    # A commented-out card does not count either: its entry now points nowhere.
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}'),
     {"A.qml": '// Rectangle { objectName: "one" }'}, 1),
    # "//" inside a string is text, not a comment.
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", title: tr("u", "see https://x.y"), {KW} }}'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # Objects outside the returned array are not entries...
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}', before=' var ex = { tabId: "zz", cardId: "no" }\n'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # ...nor is an array returned by a helper inside or before getSearchEntries.
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}',
                   before=' function h() { return [ { tabId: "zz", cardId: "no" } ] }\n'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}',
                   helper='function h() { return [ { tabId: "zz", cardId: "no" } ] }\n'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    # Tab records: any property order, across lines, and a broken record is reported.
    ('Item { readonly property var tabs: [\n { source: "A.qml",\n   key: "k",\n   id: "a" }\n ] }',
     index(f'{{ tabId: "a", cardId: "one", {KW} }}'), {"A.qml": 'Rectangle { objectName: "one" }'}, 0),
    ('Item { readonly property var tabs: [\n { id: "a", key: "k" }\n ] }',
     index(f'{{ tabId: "a", cardId: "", {KW} }}'), {}, 2),
    # A duplicate tab id is reported (indexOf() finds only the first).
    ('Item { readonly property var tabs: [\n { id: "a", source: "A.qml" },\n { id: "a", source: "B.qml" }\n ] }',
     index(f'{{ tabId: "a", cardId: "", {KW} }}'), {"A.qml": '', "B.qml": ''}, 1),
    # A routing key given twice: JS keeps the last, so the entry is reported, not trusted.
    (TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW}, cardId: "stale" }}'),
     {"A.qml": 'Rectangle { objectName: "one" }'}, 2),
    # objectName in single quotes is a card too; a computed one is reported.
    (TABS_A, index(f'{{ tabId: "a", cardId: "", {KW} }}'), {"A.qml": "Rectangle { objectName: 'one' }"}, 1),
    (TABS_A, index(f'{{ tabId: "a", cardId: "", {KW} }}'), {"A.qml": 'Rectangle { objectName: "card_" + id }'}, 1),
    # Debug-only tabs: their cards need no entry, and an entry pointing at one is dead in release.
    ('Item { readonly property var tabs: [\n { id: "a", source: "A.qml" },\n { id: "d", source: "D.qml", debugOnly: true }\n ] }',
     index(f'{{ tabId: "a", cardId: "", {KW} }}'), {"A.qml": '', "D.qml": 'Rectangle { objectName: "dbg" }'}, 0),
    ('Item { readonly property var tabs: [\n { id: "a", source: "A.qml" },\n { id: "d", source: "D.qml", debugOnly: true }\n ] }',
     index(f'{{ tabId: "d", cardId: "dbg", {KW} }}'), {"A.qml": '', "D.qml": 'Rectangle { objectName: "dbg" }'}, 1),
]

# Cards inside instantiated components: (tab text, components, index entries, expected).
COMPONENT_TEST = [
    # A card declared inside a component the tab uses needs an entry...
    ('Column { MachineCard { } }', {"MachineCard": 'Rectangle { objectName: "inner" }'},
     [], 1),
    # ...and its entry is valid.
    ('Column { MachineCard { } }', {"MachineCard": 'Rectangle { objectName: "inner" }'},
     ['inner'], 0),
    # Transitively, and a component used twice is one card declared twice.
    ('Column { Outer { } }', {"Outer": 'Item { Inner { } }', "Inner": 'Rectangle { objectName: "deep" }'},
     ['deep'], 0),
    ('Column { MachineCard { } MachineCard { } }', {"MachineCard": 'Rectangle { objectName: "inner" }'},
     ['inner'], 0),
]

# The dialog's per-build filter: (cardIds it drops, expected problem count).
DROPPED_TEST = [(("one",), 0), (("renamed",), 1)]


def self_test() -> int:
    failed = 0
    for tabs, idx, files, expected in SELF_TEST:
        got = problems(tabs, idx, lambda src: files.get(src, ""), {"profileSelector"})
        if len(got) != expected:
            failed += 1
            print(f"self-test FAILED: expected {expected} problem(s), got {got}\n  index: {idx!r}")
    for dropped, expected in DROPPED_TEST:
        got = problems(TABS_A, index(f'{{ tabId: "a", cardId: "one", {KW} }}'),
                       lambda src: 'Rectangle { objectName: "one" }', {"profileSelector"}, dropped)
        if len(got) != expected:
            failed += 1
            print(f"self-test FAILED (dialog filter {dropped}): expected {expected}, got {got}")
    for tab_text, comps, ids, expected in COMPONENT_TEST:
        entries = [f'{{ tabId: "a", cardId: "{i}", {KW} }}' for i in ids] or [f'{{ tabId: "a", cardId: "", {KW} }}']
        got = problems(TABS_A, index(*entries), lambda src: tab_text, {"profileSelector"},
                       (), {k: (lambda v=v: v) for k, v in comps.items()})
        # A component instantiated twice declares its objectName twice in the tab.
        twice = tab_text.count("MachineCard {") > 1
        if len(got) != expected + (1 if twice else 0):
            failed += 1
            print(f"self-test FAILED (components {tab_text!r}): expected {expected + twice}, got {got}")
    total = len(SELF_TEST) + len(DROPPED_TEST) + len(COMPONENT_TEST)
    print(f"self-test: {total - failed}/{total} passed")
    return 1 if failed else 0


def main() -> int:
    def read(path):
        with open(path, encoding="utf-8") as f:
            return f.read()
    routes = set(HANDLED_ROUTE.findall(strip_comments(read(SETTINGS_PAGE))))
    dropped = DIALOG_DROPPED_CARD.findall(strip_comments(read(SEARCH_DIALOG)))
    components = {}
    for dirpath, _, files in os.walk(os.path.join(ROOT, "qml")):
        for fn in files:
            if fn.endswith(".qml"):
                path = os.path.join(dirpath, fn)
                components.setdefault(fn[:-4], lambda path=path: read(path))
    found = problems(read(TABS_QML), read(INDEX_JS),
                     lambda src: read(os.path.join(PAGES_DIR, src)), routes, dropped, components)
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
