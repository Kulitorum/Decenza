#!/usr/bin/env python3
"""Generate the settings-search index from the settings cards, and fail on anything search misses.

Every card on a settings tab is a `SettingsCard`, and every adjustment on a card (a switch,
field, button, ...) is its own search result. This script reads the QML source of each tab,
checks both rules, and writes `qml/components/SettingsSearchEntries.js`.

There is no type information here (a qmllint plugin would have it, but the Qt-signed qmllint
cannot load one on macOS), so every object type that appears on a tab must be classified in
TYPE_CLASSES. An unclassified type is an error: deciding whether a new control is an adjustment
is a choice someone has to make, never a silent default.

    settings_search_index.py              regenerate; exit 1 if the file changed or on any error
    settings_search_index.py --check      exit 1 on an error or a stale file; never writes
    settings_search_index.py --self-test  run the inline fixtures
"""
import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TABS_QML = "qml/components/SettingsTabs.qml"
REGISTRY_QML = "qml/components/SettingsSearchRegistry.qml"
SETTINGS_PAGE_QML = "qml/pages/SettingsPage.qml"
OUTPUT_JS = "qml/components/SettingsSearchEntries.js"


class ScanError(Exception):
    def __init__(self, path, line, msg):
        super().__init__(f"{path}:{line}: {msg}")


# ---------------------------------------------------------------------------------------------
# Tokenizer. Comments are dropped; strings, template literals and regex literals become single
# tokens so a brace or quote inside one never reaches the parser.
# ---------------------------------------------------------------------------------------------

@dataclass
class Tok:
    kind: str      # ident, string, number, punct, regex, template
    text: str
    line: int
    start: int
    end: int
    nl_before: bool


PUNCT3 = ("===", "!==", "...", ">>>", "**=", "<<=", ">>=", "&&=", "||=", "??=")
PUNCT2 = ("==", "!=", "<=", ">=", "&&", "||", "??", "?.", "=>", "++", "--", "+=", "-=", "*=",
          "/=", "%=", "&=", "|=", "^=", "<<", ">>", "**")
# After one of these (or at the start), a `/` begins a regex literal rather than a division.
REGEX_AFTER = set("(,=:[!&|?{};+-*%<>~^") | {"return", "typeof", "case", "in", "of", "else",
                                             "&&", "||", "??", "==", "===", "!=", "!==", "=>"}


def tokenize(src, path):
    toks = []
    i, n, line = 0, len(src), 1
    nl = True

    def skip_string(j, quote):
        nonlocal line
        j += 1
        while j < n:
            c = src[j]
            if c == "\\":
                j += 2
                continue
            if c == "\n":
                if quote != "`":
                    raise ScanError(path, line, "unterminated string")
                line += 1
            if quote == "`" and c == "$" and j + 1 < n and src[j + 1] == "{":
                j = skip_braced(j + 1)
                continue
            if c == quote:
                return j + 1
            j += 1
        raise ScanError(path, line, "unterminated string")

    def skip_braced(j):
        # j at '{' of a template substitution; returns index after the matching '}'.
        nonlocal line
        depth = 0
        while j < n:
            c = src[j]
            if c in "\"'`":
                j = skip_string(j, c)
                continue
            if c == "\n":
                line += 1
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return j + 1
            j += 1
        raise ScanError(path, line, "unterminated template substitution")

    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            nl = True
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j < 0:
                raise ScanError(path, line, "unterminated comment")
            line += src.count("\n", i, j)
            i = j + 2
            continue
        start, sline = i, line
        if c in "\"'`":
            i = skip_string(i, c)
            kind = "template" if c == "`" else "string"
        elif c.isalpha() or c in "_$":
            while i < n and (src[i].isalnum() or src[i] in "_$"):
                i += 1
            kind = "ident"
        elif c.isdigit() or (c == "." and i + 1 < n and src[i + 1].isdigit()):
            while i < n and (src[i].isalnum() or src[i] in "._"):
                i += 1
            kind = "number"
        elif c == "/" and (not toks or toks[-1].text in REGEX_AFTER):
            j, in_class = i + 1, False
            while j < n:
                d = src[j]
                if d == "\\":
                    j += 2
                    continue
                if d == "\n":
                    raise ScanError(path, line, "unterminated regex literal")
                if d == "[":
                    in_class = True
                elif d == "]":
                    in_class = False
                elif d == "/" and not in_class:
                    break
                j += 1
            i = j + 1
            while i < n and src[i].isalpha():
                i += 1
            kind = "regex"
        else:
            kind = "punct"
            for cand in PUNCT3 + PUNCT2:
                if src.startswith(cand, i):
                    i += len(cand)
                    break
            else:
                i += 1
        toks.append(Tok(kind, src[start:i], sline, start, i, nl))
        nl = False
    return toks


# ---------------------------------------------------------------------------------------------
# Parser: the QML object structure. JS (binding values, function bodies, handlers) is kept as
# raw source text; only object declarations are descended into.
# ---------------------------------------------------------------------------------------------

@dataclass
class Binding:
    name: str
    value: str       # raw source text, or "" for an object value
    line: int
    obj: "QmlObject | None" = None


@dataclass
class QmlObject:
    type: str
    line: int
    path: str
    bindings: dict = field(default_factory=dict)   # name -> Binding (last wins, as in QML)
    children: list = field(default_factory=list)   # nested objects, in source order
    id: str = ""

    def prop(self, name):
        return self.bindings.get(name)


# Continuation: an expression does not end at a newline next to one of these.
CONTINUES = {"?", ":", ".", "?.", "+", "-", "*", "/", "%", "&&", "||", "??", "==", "===", "!=",
             "!==", "<", ">", "<=", ">=", "=", "=>", ",", "&", "|", "^", "(", "[", "{"}


class Parser:
    def __init__(self, src, path):
        self.src, self.path = src, path
        self.toks = tokenize(src, path)
        self.i = 0

    def peek(self, k=0):
        j = self.i + k
        return self.toks[j] if j < len(self.toks) else None

    def fail(self, msg, tok=None):
        tok = tok or self.peek() or self.toks[-1]
        raise ScanError(self.path, tok.line, msg)

    def expect(self, text):
        t = self.peek()
        if not t or t.text != text:
            self.fail(f"expected '{text}', found '{t.text if t else 'end of file'}'")
        self.i += 1
        return t

    def parse_file(self):
        while self.peek() and self.peek().text in ("import", "pragma"):
            line = self.peek().line
            while self.peek() and self.peek().line == line:
                self.i += 1
        root = self.parse_object()
        if self.peek():
            self.fail("unexpected content after the root object")
        return root

    def qualified_name(self):
        parts = [self.expect_ident().text]
        while self.peek() and self.peek().text == "." and self.peek(1) and self.peek(1).kind == "ident":
            self.i += 1
            parts.append(self.expect_ident().text)
        return ".".join(parts)

    def expect_ident(self):
        t = self.peek()
        if not t or t.kind != "ident":
            self.fail("expected an identifier")
        self.i += 1
        return t

    def at_object_start(self):
        # Type { | Module.Type { | Type on prop {
        j = self.i
        t = self.peek()
        if not t or t.kind != "ident":
            return False
        while True:
            t = self.toks[j] if j < len(self.toks) else None
            if not t or t.kind != "ident":
                return False
            last = t.text
            j += 1
            nxt = self.toks[j] if j < len(self.toks) else None
            if nxt and nxt.text == "." and j + 1 < len(self.toks) and self.toks[j + 1].kind == "ident":
                j += 1
                continue
            break
        if not last[:1].isupper():
            return False
        nxt = self.toks[j] if j < len(self.toks) else None
        if nxt and nxt.text == "{":
            return True
        return bool(nxt and nxt.text == "on" and j + 2 < len(self.toks)
                    and self.toks[j + 1].kind == "ident" and self.toks[j + 2].text == "{")

    def parse_object(self):
        start = self.peek()
        type_name = self.qualified_name()
        if self.peek() and self.peek().text == "on":
            self.i += 2  # `on prop`
        obj = QmlObject(type_name, start.line, self.path)
        self.expect("{")
        self.parse_body(obj, prefix="")
        return obj

    def parse_body(self, obj, prefix):
        while True:
            t = self.peek()
            if t is None:
                self.fail("unterminated object body", self.toks[-1])
            if t.text == "}":
                self.i += 1
                return
            if t.text == ";":
                self.i += 1
                continue
            if t.text in ("component", "enum") and self.peek(1) and self.peek(1).kind == "ident":
                self.fail(f"'{t.text}' declarations are not supported on settings tabs")
            is_binding = self.peek(1) is not None and self.peek(1).text == ":"
            if t.text in ("default", "required", "readonly", "property") and not is_binding:
                self.parse_property_decl(obj)
                continue
            if t.text == "signal" and not is_binding:
                self.i += 2
                if self.peek() and self.peek().text == "(":
                    self.skip_balanced()
                continue
            if t.text == "function" or (t.text == "async" and self.peek(1) and self.peek(1).text == "function"):
                while self.peek() and self.peek().text != "{":
                    self.i += 1
                self.skip_balanced()
                continue
            if self.at_object_start():
                obj.children.append(self.parse_object())
                continue
            if t.kind != "ident":
                self.fail(f"unexpected '{t.text}' in object body")
            name = prefix + self.qualified_name()
            nxt = self.peek()
            if nxt and nxt.text == "{":
                # grouped property block: `anchors { fill: parent }`
                self.i += 1
                self.parse_body(obj, prefix=name + ".")
                continue
            self.expect(":")
            self.parse_value(obj, name, t.line)

    def parse_property_decl(self, obj):
        line = self.peek().line
        while self.peek() and self.peek().text in ("default", "required", "readonly"):
            self.i += 1
        self.expect("property")
        self.expect_ident()                       # type
        if self.peek() and self.peek().text == "<":
            self.i += 1
            self.expect_ident()
            self.expect(">")
        name = self.expect_ident().text
        if self.peek() and self.peek().text == ":":
            self.i += 1
            self.parse_value(obj, name, line)

    def parse_value(self, obj, name, line):
        if self.at_object_start():
            child = self.parse_object()
            obj.bindings[name] = Binding(name, "", line, child)
            obj.children.append(child)
            return
        t = self.peek()
        if t and t.text == "[" and self.is_object_list():
            self.i += 1
            while self.peek() and self.peek().text != "]":
                if self.peek().text == ",":
                    self.i += 1
                    continue
                obj.children.append(self.parse_object())
            self.expect("]")
            obj.bindings[name] = Binding(name, "", line)
            return
        value = self.parse_expression()
        if name == "id":
            obj.id = value.strip()
        obj.bindings[name] = Binding(name, value, line)

    def is_object_list(self):
        save = self.i
        self.i += 1
        ok = self.at_object_start()
        self.i = save
        return ok

    def skip_balanced(self):
        # at an opening bracket; skip to just past its match
        pairs = {"(": ")", "[": "]", "{": "}"}
        stack = []
        while True:
            t = self.peek()
            if t is None:
                self.fail("unbalanced brackets", self.toks[-1])
            self.i += 1
            if t.text in pairs:
                stack.append(pairs[t.text])
            elif t.text in (")", "]", "}"):
                if not stack or stack.pop() != t.text:
                    self.fail(f"mismatched '{t.text}'", t)
                if not stack:
                    return

    def parse_expression(self):
        first = self.peek()
        if first is None:
            self.fail("expected a value")
        end = first.end
        while True:
            t = self.peek()
            if t is None or t.text in ("}", ";", "]", ")"):
                break
            if t is not first and t.nl_before:
                prev = self.toks[self.i - 1]
                if prev.text not in CONTINUES and t.text not in CONTINUES:
                    break
            if t.text in ("(", "[", "{"):
                self.skip_balanced()
            else:
                self.i += 1
            end = self.toks[self.i - 1].end
        return self.src[first.start:end]


def parse_qml(path):
    return Parser((REPO / path).read_text(encoding="utf-8"), path).parse_file()


def walk(obj):
    yield obj
    for c in obj.children:
        yield from walk(c)


# ---------------------------------------------------------------------------------------------
# Classification. Every object type that may appear on a settings tab, and what it means for
# search. An unlisted type is an error (see the module docstring).
# ---------------------------------------------------------------------------------------------

ADJUSTMENT = "adjustment"   # its own search result; needs a title; its subtree is part of it
COMPOSITE = "composite"     # a component holding adjustments: one result, title from the instance
VIEW = "view"               # dynamic content: one result for the view, delegates not walked
OVERLAY = "overlay"         # content on the overlay, reached via the control that opens it
STRUCTURAL = "structural"   # layout and display only

TYPE_CLASSES = {
    ADJUSTMENT: {
        "AccessibleButton", "AccessibleMouseArea", "StyledSwitch", "StyledTextField",
        "StyledComboBox", "StyledIconButton", "ValueInput", "ExpandableTextArea", "ColorSwatch",
        "TextArea", "TextInput", "TextEdit", "ComboBox", "Switch", "ItemDelegate",
    },
    COMPOSITE: {
        "SettingsActionRow", "UploadDestinationCard", "UploadAccountSection", "UploadMissingShots",
        "PortalDevicePanel", "LayoutEditorZone", "LibraryPanel", "ColorEditor", "SubsystemLogView",
    },
    VIEW: {"Repeater", "ListView"},
    OVERLAY: {
        "DecenzaDialog", "Dialog", "Popup", "FileDialog", "FolderDialog", "DatePickerDialog",
        "SelectionDialog", "DeviceMigrationDialog", "CustomEditorPopup", "ScreensaverEditorPopup",
        "ZoneOptionsPopup", "ReadoutOptionsPopup", "SleepEditorPopup", "BackgroundPickerDialog",
        "TranslationErrorToast",
    },
    STRUCTURAL: {
        "Text", "Tr", "Rectangle", "Item", "Image", "RowLayout", "ColumnLayout", "GridLayout",
        "Row", "Column", "Flow", "Flickable", "ScrollView", "ScrollBar", "KeyboardAwareContainer",
        "Connections", "Timer", "QtObject", "ProgressBar", "BusyIndicator", "Gradient",
        "GradientStop", "Behavior", "NumberAnimation", "SequentialAnimation", "PauseAnimation",
        "ListModel", "ListElement", "LayoutPreview", "DecentUploadStatus", "QrCode",
        "ShotMapScreensaver", "ScrollDownIndicator",
    },
}
CLASS_OF = {t: c for c, types in TYPE_CLASSES.items() for t in types}

# A MouseArea, TapHandler or ColoredIcon (a Button often used as a plain icon) is an adjustment
# only if it reacts to the user.
CLICK_HANDLERS = {"onClicked", "onPressed", "onReleased", "onDoubleClicked", "onPressAndHold",
                  "onTapped", "onDoubleTapped", "onLongPressed"}
# Where a title is read from, in order (design D3). SettingsSearchLocator::findRow
# (src/core/settingssearch.cpp) reads the same names at runtime: keep the two in step.
TITLE_PROPS = ("SettingsSearch.title", "accessibleName", "accessibleLabel", "Accessible.name",
               "text", "title", "label")

# Tabs migrated to SettingsCard, whose content outside any card is checked too. Until a tab is
# listed, only its SettingsCards are checked. Becomes every tab when the migration completes.
MIGRATED_TABS: set = {"machine", "calibration", "connections"}

# Files outside Settings that host a search result (`SettingsSearch.route`).
EXTERNAL_HOSTS = ("qml/pages/ProfileSelectorPage.qml",)

STRING_RE = r'"((?:[^"\\]|\\.)*)"'
TR_CALL_RE = re.compile(r'^\s*TranslationManager\.translate\(\s*' + STRING_RE + r'\s*,\s*'
                        + STRING_RE + r'\s*\)')
TR_ID_RE = re.compile(r'^\s*([A-Za-z_]\w*)\.text\s*$')
STRING_LITERAL_RE = re.compile(r'^\s*' + STRING_RE + r'\s*$')


def unescape(s):
    return json.loads('"' + s + '"')


@dataclass
class Text:
    key: str
    fallback: str


class FileScan:
    """Checks and harvest for one QML file."""

    def __init__(self, path, root, errors):
        self.path, self.root, self.errors = path, root, errors
        self.ids = {o.id: o for o in walk(root) if o.id}

    def error(self, line, msg):
        self.errors.append(f"{self.path}:{line}: {msg}")

    def literal_text(self, binding, exact=True):
        """A translated string written as a literal: translate("k", "f") or a Tr's id.text."""
        m = TR_CALL_RE.match(binding.value)
        if m and (not exact or not binding.value[m.end():].strip()):
            return Text(unescape(m.group(1)), unescape(m.group(2)))
        m = TR_ID_RE.match(binding.value)
        if m and m.group(1) in self.ids and self.ids[m.group(1)].type == "Tr":
            tr = self.ids[m.group(1)]
            k, f = tr.prop("key"), tr.prop("fallback")
            km = k and STRING_LITERAL_RE.match(k.value)
            fm = f and STRING_LITERAL_RE.match(f.value)
            if km and fm:
                return Text(unescape(km.group(1)), unescape(fm.group(1)))
        return None

    def literal_string(self, obj, name):
        b = obj.prop(name)
        if b is None:
            return ""
        m = STRING_LITERAL_RE.match(b.value)
        if not m:
            self.error(b.line, f"{name} must be a string literal")
            return ""
        return unescape(m.group(1))

    def literal_list(self, obj, name):
        b = obj.prop(name)
        if b is None:
            return []
        v = b.value.strip()
        items = re.findall(STRING_RE, v)
        rebuilt = "[" + ", ".join('"' + i + '"' for i in items) + "]"
        if re.sub(r"\s+", "", v) != re.sub(r"\s+", "", rebuilt):
            self.error(b.line, f"{name} must be an array of string literals")
            return []
        return [unescape(i) for i in items]

    def title_of(self, obj):
        for name in TITLE_PROPS:
            b = obj.prop(name)
            if b is None:
                continue
            text = self.literal_text(b, exact=(name == "SettingsSearch.title"))
            if text is None:
                self.error(b.line, f"{obj.type}: cannot read a search title from `{name}`; "
                                   f"add `SettingsSearch.title: TranslationManager.translate(...)`")
            return text
        self.error(obj.line, f"{obj.type} has no title for settings search; add "
                             f"`SettingsSearch.title: TranslationManager.translate(...)`")
        return None

    def classify(self, obj):
        cls = CLASS_OF.get(obj.type)
        if obj.type in ("MouseArea", "TapHandler", "ColoredIcon"):
            return ADJUSTMENT if CLICK_HANDLERS & obj.bindings.keys() else STRUCTURAL
        if obj.type == "Loader":
            # Inline content is walked as a child; a Loader of an unknown `source` is one result.
            return STRUCTURAL if obj.children else VIEW
        if cls is None:
            self.error(obj.line, f"unclassified type `{obj.type}` on a settings tab: add it to "
                                 f"TYPE_CLASSES in scripts/settings_search_index.py")
        return cls


class Index:
    def __init__(self):
        self.errors = []
        self.entries = []
        tabs_src = (REPO / TABS_QML).read_text(encoding="utf-8")
        self.tabs = parse_tabs(tabs_src, self.errors)
        reg = (REPO / REGISTRY_QML).read_text(encoding="utf-8")
        self.conditions, self.routes = parse_registry(reg, self.errors)
        page = (REPO / SETTINGS_PAGE_QML).read_text(encoding="utf-8")
        for route in self.routes:
            if f'externalRoute === "{route}"' not in page:
                self.errors.append(f"{SETTINGS_PAGE_QML}: no `externalRoute === \"{route}\"` arm "
                                   f"for the route {REGISTRY_QML} declares")

    def scan_tab(self, tab, root, path):
        fs = FileScan(path, root, self.errors)
        strict = tab["id"] in MIGRATED_TABS
        self.walk_tab(fs, tab, root, card=None, strict=strict)

    def walk_tab(self, fs, tab, obj, card, strict):
        if obj.type == "SettingsCard":
            if card is not None:
                fs.error(obj.line, "SettingsCard nested in another SettingsCard")
            self.scan_card(fs, tab, obj, strict)
            return
        cls = fs.classify(obj)
        if card is None and strict:
            if obj.type == "Rectangle" and "Theme.cardBackgroundColor" in getattr(obj.prop("color"), "value", ""):
                fs.error(obj.line, "a card-styled Rectangle on a settings tab: use SettingsCard")
            elif cls in (ADJUSTMENT, COMPOSITE, VIEW):
                fs.error(obj.line, f"{obj.type} outside any SettingsCard cannot be a search result")
        if cls in (ADJUSTMENT, OVERLAY, VIEW, COMPOSITE):
            return
        for c in obj.children:
            self.walk_tab(fs, tab, c, card, strict)

    def scan_card(self, fs, tab, card, strict):
        if card.prop("visible") is not None:
            fs.error(card.prop("visible").line, "SettingsCard: set `shown`, not `visible`, so "
                                                "availability still applies")
        card_id = fs.literal_string(card, "searchId")
        title_b = card.prop("title")
        title = fs.literal_text(title_b) if title_b else None
        if title_b is None or title is None:
            fs.error(card.line, "SettingsCard title must be a literal TranslationManager.translate(...)")
            return
        desc_b = card.prop("description")
        desc = fs.literal_text(desc_b) if desc_b else None
        if desc_b is not None and desc is None:
            fs.error(desc_b.line, "SettingsCard description must be a literal TranslationManager.translate(...)")
        availability = self.availability(fs, card, card.prop("availability"), tab)
        base = {"tabId": tab["id"], "cardId": card_id}
        self.entries.append({**base, "kind": "card", "key": title.key, "fallback": title.fallback,
                             **desc_fields(desc), "keywords": fs.literal_list(card, "keywords"),
                             "availability": availability})
        seen = {title.fallback.casefold(): card.line}
        for obj in self.card_results(fs, card):
            t = fs.title_of(obj)
            if t is None:
                continue
            folded = t.fallback.casefold()
            if folded in seen:
                if seen[folded] != card.line:
                    fs.error(obj.line, f"duplicate search title \"{t.fallback}\" on card "
                                       f"\"{card_id}\" (also line {seen[folded]}); give one a "
                                       f"distinct SettingsSearch.title")
                continue   # same as the card's own title: the card result covers it
            seen[folded] = obj.line
            d_b = obj.prop("SettingsSearch.description")
            d = fs.literal_text(d_b) if d_b else None
            if d_b is not None and d is None:
                fs.error(d_b.line, "SettingsSearch.description must be a literal TranslationManager.translate(...)")
            own = obj.prop("SettingsSearch.availability")
            self.entries.append({**base, "kind": "adjustment", "key": t.key, "fallback": t.fallback,
                                 "cardKey": title.key, "cardFallback": title.fallback,
                                 **desc_fields(d),
                                 "keywords": fs.literal_list(obj, "SettingsSearch.keywords"),
                                 "availability": self.availability(fs, obj, own, tab) if own else availability})

    def card_results(self, fs, obj):
        for c in obj.children:
            if c.type == "SettingsCard":
                fs.error(c.line, "SettingsCard nested in another SettingsCard")
                continue
            cls = fs.classify(c)
            if cls in (ADJUSTMENT, COMPOSITE, VIEW):
                yield c
            if cls in (ADJUSTMENT, OVERLAY, VIEW, COMPOSITE):
                continue
            yield from self.card_results(fs, c)

    def availability(self, fs, obj, binding, tab):
        value = ""
        if binding is not None:
            m = STRING_LITERAL_RE.match(binding.value)
            if not m:
                fs.error(binding.line, "availability must be a string literal")
            else:
                value = unescape(m.group(1))
                if value not in self.conditions:
                    fs.error(binding.line, f"unknown availability \"{value}\"; the conditions are "
                                           f"{sorted(self.conditions)} ({REGISTRY_QML})")
        if tab.get("debugOnly") and not value:
            value = "debug"
        return value

    def scan_external(self, path, root):
        fs = FileScan(path, root, self.errors)
        for obj in walk(root):
            if obj.type == "SettingsCard":
                fs.error(obj.line, "SettingsCard outside a settings tab")
            route_b = obj.prop("SettingsSearch.route")
            if route_b is None:
                continue
            route = fs.literal_string(obj, "SettingsSearch.route")
            if route and route not in self.routes:
                fs.error(route_b.line, f"unknown route \"{route}\"; the routes are {self.routes} "
                                       f"({REGISTRY_QML})")
            t_b = obj.prop("SettingsSearch.title")
            t = fs.literal_text(t_b) if t_b else None
            if t is None:
                fs.error(obj.line, "a SettingsSearch.route needs a literal SettingsSearch.title")
                continue
            d_b = obj.prop("SettingsSearch.description")
            d = fs.literal_text(d_b) if d_b else None
            self.entries.append({"externalRoute": route, "kind": "external", "key": t.key,
                                 "fallback": t.fallback, **desc_fields(d),
                                 "keywords": fs.literal_list(obj, "SettingsSearch.keywords"),
                                 "availability": ""})

    def check_no_stray_cards(self, tab_paths):
        for p in sorted((REPO / "qml").rglob("*.qml")):
            rel = p.relative_to(REPO).as_posix()
            if rel in tab_paths or rel in EXTERNAL_HOSTS or rel == "qml/components/SettingsCard.qml":
                continue
            if re.search(r"^\s*SettingsCard\s*\{", p.read_text(encoding="utf-8"), re.M):
                self.errors.append(f"{rel}: SettingsCard outside a settings tab; search cannot "
                                   f"navigate to it")


def desc_fields(desc):
    return {"descKey": desc.key, "descFallback": desc.fallback} if desc else {}


def parse_tabs(src, errors):
    m = re.search(r"readonly property var tabs:\s*\[(.*?)\n\s*\]", src, re.S)
    if not m:
        errors.append(f"{TABS_QML}: cannot find the `tabs` array literal")
        return []
    tabs = []
    for rec in re.findall(r"\{([^{}]*)\}", m.group(1)):
        tid = re.search(r'\bid:\s*"([^"]+)"', rec)
        src_m = re.search(r'\bsource:\s*"([^"]+)"', rec)
        dbg = re.search(r"\bdebugOnly:\s*(true|false)", rec)
        if not tid or not src_m:
            errors.append(f"{TABS_QML}: a tab record without a literal id and source: {rec.strip()}")
            continue
        tabs.append({"id": tid.group(1), "source": "qml/pages/" + src_m.group(1),
                     "debugOnly": bool(dbg and dbg.group(1) == "true")})
    ids = [t["id"] for t in tabs]
    for dup in sorted({i for i in ids if ids.count(i) > 1}):
        errors.append(f"{TABS_QML}: tab id \"{dup}\" declared twice")
    return tabs


def parse_registry(src, errors):
    cm = re.search(r"property var conditions:\s*\(\{(.*?)\}\)", src, re.S)
    rm = re.search(r"property var routes:\s*\[(.*?)\]", src, re.S)
    if not cm or not rm:
        errors.append(f"{REGISTRY_QML}: cannot find the `conditions` object or `routes` array literal")
        return set(), []
    return set(re.findall(r'"(\w+)"\s*:', cm.group(1))), re.findall(STRING_RE, rm.group(1))


def build_index():
    index = Index()
    tab_paths = set()
    for tab in index.tabs:
        tab_paths.add(tab["source"])
        try:
            root = parse_qml(tab["source"])
        except ScanError as e:
            index.errors.append(str(e))
            continue
        index.scan_tab(tab, root, tab["source"])
    for host in EXTERNAL_HOSTS:
        try:
            index.scan_external(host, parse_qml(host))
        except ScanError as e:
            index.errors.append(str(e))
    index.check_no_stray_cards(tab_paths)
    return index


def render(entries):
    lines = [".pragma library",
             "// GENERATED by scripts/settings_search_index.py from the settings cards. Do not edit:",
             "// change the card (or its SettingsSearch declaration) and rebuild.",
             "var entries = ["]
    lines += ["    " + json.dumps(e, ensure_ascii=False) + "," for e in entries]
    lines.append("]")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="fail on errors or a stale index; never write")
    ap.add_argument("--self-test", action="store_true", help="run the inline fixtures")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    index = build_index()
    if index.errors:
        print("Settings search index: " + str(len(index.errors)) + " problem(s):", file=sys.stderr)
        for e in index.errors:
            print("  " + e, file=sys.stderr)
        return 1
    out = REPO / OUTPUT_JS
    text = render(index.entries)
    current = out.read_text(encoding="utf-8") if out.exists() else None
    if current == text:
        print(f"Settings search index up to date ({len(index.entries)} entries).")
        return 0
    if args.check:
        print(f"{OUTPUT_JS} is stale: run scripts/settings_search_index.py (a desktop build does) "
              f"and commit the result.", file=sys.stderr)
        return 1
    out.write_text(text, encoding="utf-8")
    print(f"Settings search index regenerated ({len(index.entries)} entries): commit {OUTPUT_JS}.",
          file=sys.stderr)
    return 1


CARD = 'SettingsCard {{ searchId: "c"; title: TranslationManager.translate("t.card", "Card")\n{body}\n}}'
SWITCH = 'StyledSwitch {{ accessibleName: TranslationManager.translate("{key}", "{text}") }}'

# (name, QML, strict, the error the rule must produce, or None for clean)
FIXTURES = [
    ("clean card", CARD.format(body=SWITCH.format(key="k.a", text="Alpha")), True, None),
    ("unclassified type", CARD.format(body="FancySlider { }"), False, "unclassified type `FancySlider`"),
    ("card title not literal", 'SettingsCard { searchId: "c"; title: someVar }', False,
     "SettingsCard title must be a literal"),
    ("dynamic accessible name", CARD.format(body="StyledSwitch { accessibleName: label }"), False,
     "cannot read a search title from `accessibleName`"),
    ("no title at all", CARD.format(body="StyledSwitch { checked: true }"), False, "has no title"),
    ("duplicate titles", CARD.format(body=SWITCH.format(key="k.a", text="Alpha") + "\n"
                                     + SWITCH.format(key="k.b", text="alpha")), False,
     "duplicate search title"),
    ("visible on a card", 'SettingsCard { searchId: "c"; title: TranslationManager.translate("t", "T"); visible: false }',
     False, "set `shown`, not `visible`"),
    ("card-styled Rectangle", "Item { Rectangle { color: Theme.cardBackgroundColor } }", True,
     "card-styled Rectangle"),
    ("adjustment outside a card", "Item { " + SWITCH.format(key="k", text="K") + " }", True,
     "outside any SettingsCard"),
    ("not checked before migration", "Item { " + SWITCH.format(key="k", text="K") + " }", False, None),
    ("overlay subtree skipped", CARD.format(body="DecenzaDialog { AccessibleButton { onClicked: x() } }"),
     False, None),
    ("view needs a title", CARD.format(body="Repeater { model: 3; delegate: AccessibleButton { } }"),
     False, "Repeater has no title"),
    ("a control's internals are part of it", CARD.format(
        body='StyledComboBox { accessibleLabel: TranslationManager.translate("k.c", "Pick"); '
             'delegate: ItemDelegate { } }'), False, None),
    ("titled view, delegates not walked", CARD.format(
        body='Repeater { SettingsSearch.title: TranslationManager.translate("k.r", "Rows"); '
             'delegate: AccessibleButton { } }'), False, None),
    ("clicked raw MouseArea", CARD.format(body="MouseArea { onClicked: go() }"), False, "MouseArea has no title"),
    ("passive MouseArea", CARD.format(body="MouseArea { hoverEnabled: true }"), False, None),
    ("Tr id as title", CARD.format(body='Tr { id: trX; key: "k.tr"; fallback: "From Tr"; visible: false }\n'
                                        'AccessibleButton { text: trX.text }'), False, None),
    ("translated prefix of a longer name", CARD.format(
        body='ValueInput { accessibleName: TranslationManager.translate("k.v", "Level") + ": " + value }'),
     False, None),
    ("SettingsSearch.title must be exact", CARD.format(
        body='ValueInput { SettingsSearch.title: TranslationManager.translate("k.v", "Level") + x }'),
     False, "cannot read a search title from `SettingsSearch.title`"),
    ("keywords not literal", 'SettingsCard { searchId: "c"; title: TranslationManager.translate("t", "T"); keywords: words }',
     False, "must be an array of string literals"),
    ("unknown availability", 'SettingsCard { searchId: "c"; title: TranslationManager.translate("t", "T"); availability: "mars" }',
     False, "unknown availability"),
    ("Loader by source needs a title", CARD.format(body='Loader { source: "Panel.qml" }'), False,
     "Loader has no title"),
    ("inline Loader content is walked", CARD.format(body="Loader { sourceComponent: FancySlider { } }"), False,
     "unclassified type `FancySlider`"),
    ("nested card", CARD.format(body=CARD.format(body="")), False, "nested in another SettingsCard"),
    ("same title as its card", CARD.format(body=SWITCH.format(key="k", text="Card")), False, None),
    ("tokenizer: regex, template, `property:` binding", CARD.format(
        body='Item { function f(h) { if (/^#[0-9a-f]{6}$/.test(h)) return `${h}}`; return "}" }\n'
             'NumberAnimation { property: "opacity"; to: 1 } }'), False, None),
]


def self_test():
    failures = 0
    for name, src, strict, expected in FIXTURES:
        index = Index()
        index.errors = []
        tab = {"id": "fixture", "source": "fixture.qml", "debugOnly": False}
        if strict:
            MIGRATED_TABS.add("fixture")
        try:
            index.scan_tab(tab, Parser(src, "fixture.qml").parse_file(), "fixture.qml")
        except ScanError as e:
            index.errors.append(str(e))
        MIGRATED_TABS.discard("fixture")
        if expected is None:
            ok = not index.errors
        else:
            ok = any(expected in e for e in index.errors)
        if not ok:
            failures += 1
            print(f"FAIL {name}: expected {expected or 'no error'}, got {index.errors}", file=sys.stderr)
    print(f"settings_search_index self-test: {len(FIXTURES) - failures}/{len(FIXTURES)} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
