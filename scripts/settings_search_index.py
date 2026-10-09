#!/usr/bin/env python3
"""Generate the settings-search index from the settings cards, and fail on anything search misses.

Every card on a settings tab is a `SettingsCard`, and every adjustment on a card (a switch,
field, button, ...) is its own search result. This script reads the QML source of each tab,
checks both rules, and writes `qml/components/SettingsSearchEntries.js`.

There is no type information here (a qmllint plugin would have it, but the Qt-signed qmllint
cannot load one on macOS), so every object type that appears on a tab must be classified in
TYPE_CLASSES. An unclassified type is an error: deciding whether a new control is an adjustment
is a choice someone has to make, never a silent default. A card component's own controls (the
switch inside UploadDestinationCard) are covered by its card's result.

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
        "SettingsActionRow", "UploadAccountSection", "UploadMissingShots", "PortalDevicePanel",
        "LayoutEditorZone", "LibraryPanel", "ColorEditor", "SubsystemLogView",
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
RESULT_CLASSES = (ADJUSTMENT, COMPOSITE, VIEW)
OPAQUE_CLASSES = (ADJUSTMENT, COMPOSITE, VIEW, OVERLAY)   # their subtree is not walked

# A MouseArea, TapHandler or ColoredIcon (a Button often used as a plain icon) is an adjustment
# only if it reacts to the user.
CLICK_HANDLERS = {"onClicked", "onPressed", "onReleased", "onDoubleClicked", "onPressAndHold",
                  "onTapped", "onSingleTapped", "onDoubleTapped", "onLongPressed", "onToggled",
                  "onCheckedChanged", "onActivated", "onPressedChanged"}
TEXT_CONTROLS = ("TextEdit", "TextInput", "TextArea", "StyledTextField")

# SettingsSearchLocator reads titles at runtime from these property arrays; the scanner reads
# them from there, so the two cannot disagree.
LOCATOR_CPP = "src/core/settingssearch.cpp"

# Files outside Settings that host a search result (`SettingsSearch.route`).
EXTERNAL_HOSTS = ("qml/pages/ProfileSelectorPage.qml",)

STRING_RE = r'"((?:[^"\\]|\\.)*)"'
TR_CALL_RE = re.compile(r'^\s*TranslationManager\.translate\(\s*' + STRING_RE + r'\s*,\s*'
                        + STRING_RE + r'\s*\)')
TR_ID_RE = re.compile(r'^\s*([A-Za-z_]\w*)\.text\s*$')
STRING_LITERAL_RE = re.compile(r'^\s*' + STRING_RE + r'\s*$')
PLACEHOLDER_RE = re.compile(r"%\d")
# A platform or build test must go through SettingsSearchRegistry so search can read it.
RAW_CONDITION_RE = re.compile(r"Qt\.platform\.os|Settings\.app\.(simulatorAvailable|isDebugBuild)")
IS_AVAILABLE_RE = re.compile(r'SettingsSearchRegistry\.isAvailable\(\s*"(\w+)"\s*\)')


def title_props(src, errors):
    """The names a title is read from, strongest first."""
    def array(name):
        m = re.search(name + r"\[\]\s*=\s*\{([^}]*)\}", src)
        return tuple(re.findall(r'"(\w+)"', m.group(1))) if m else ()
    names, texts = array("kNameProperties"), array("kTextProperties")
    if not names or not texts:
        errors.append(f"{LOCATOR_CPP}: cannot find the kNameProperties/kTextProperties arrays")
    return ("SettingsSearch.title",) + names + ("Accessible.name",) + texts


def strip_comments(src):
    """Source with // and /* */ comments blanked, string literals kept."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c in "\"'`":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


@dataclass(frozen=True)
class TrText:
    key: str
    fallback: str


@dataclass(frozen=True)
class Tab:
    id: str
    source: str
    debug_only: bool = False


def entry(kind, title, keywords, availability, *, tab=None, card_id=None, card_title=None,
          desc=None, route=None):
    """One index record. Every kind is built here, so the format lives in one place."""
    e = {"externalRoute": route} if route is not None else {"tabId": tab.id, "cardId": card_id}
    e.update({"kind": kind, "key": title.key, "fallback": title.fallback})
    if card_title:
        e.update({"cardKey": card_title.key, "cardFallback": card_title.fallback})
    if desc:
        e.update({"descKey": desc.key, "descFallback": desc.fallback})
    e.update({"keywords": keywords, "availability": availability})
    return e


class FileScan:
    """Checks and harvest for one QML file."""

    def __init__(self, path, root, errors, titles):
        self.path, self.root, self.errors, self.titles = path, root, errors, titles
        self.ids = {o.id: o for o in walk(root) if o.id}

    def error(self, line, msg):
        self.errors.append(f"{self.path}:{line}: {msg}")

    def unescape(self, s, line):
        try:
            return json.loads('"' + s + '"')
        except ValueError:
            raise ScanError(self.path, line, f"cannot read the string literal \"{s}\"; write it "
                                             f"with JSON-compatible escapes")

    def literal_text(self, binding, exact=True):
        """A translated string written as a literal: translate("k", "f") or a Tr's id.text.
        exact=False accepts a translated prefix of a longer name (`+ ": " + value`), never a
        method call on it (`.arg(v)` makes a title no item will carry)."""
        m = TR_CALL_RE.match(binding.value)
        if m:
            rest = binding.value[m.end():].strip()
            if (exact and rest) or rest.startswith("."):
                return None
            text = TrText(self.unescape(m.group(1), binding.line), self.unescape(m.group(2), binding.line))
        else:
            m = TR_ID_RE.match(binding.value)
            if not (m and m.group(1) in self.ids and self.ids[m.group(1)].type == "Tr"):
                return None
            tr = self.ids[m.group(1)]
            k, f = tr.prop("key"), tr.prop("fallback")
            km = k and STRING_LITERAL_RE.match(k.value)
            fm = f and STRING_LITERAL_RE.match(f.value)
            if not (km and fm):
                return None
            text = TrText(self.unescape(km.group(1), k.line), self.unescape(fm.group(1), f.line))
        return None if PLACEHOLDER_RE.search(text.fallback) else text

    def literal_string(self, obj, name, required=False):
        b = obj.prop(name)
        if b is None:
            if required:
                self.error(obj.line, f"{obj.type} needs a literal {name}")
            return ""
        m = STRING_LITERAL_RE.match(b.value)
        if not m or (required and not m.group(1)):
            self.error(b.line, f"{name} must be a non-empty string literal" if required
                       else f"{name} must be a string literal")
            return ""
        return self.unescape(m.group(1), b.line)

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
        return [self.unescape(i, b.line) for i in items]

    def literal_desc(self, obj, name):
        b = obj.prop(name)
        if b is None:
            return None
        d = self.literal_text(b)
        if d is None:
            self.error(b.line, f"{name} must be a literal TranslationManager.translate(...)")
        return d

    def title_of(self, obj):
        for name in self.titles:
            b = obj.prop(name)
            if b is None:
                continue
            text = self.literal_text(b, exact=(name == "SettingsSearch.title"))
            if text is None:
                self.error(b.line, f"{obj.type}: cannot read a search title from `{name}` (it is "
                                   f"built at runtime or holds a %1 placeholder); add "
                                   f"`SettingsSearch.title: TranslationManager.translate(...)`")
            return text
        self.error(obj.line, f"{obj.type} has no title for settings search; add "
                             f"`SettingsSearch.title: TranslationManager.translate(...)`")
        return None

    def conditions_of(self, obj):
        """The SettingsSearchRegistry conditions obj's own `visible:` depends on."""
        b = obj.prop("visible")
        if b is None:
            return []
        if RAW_CONDITION_RE.search(b.value):
            self.error(b.line, "a platform or build test in `visible:`: use "
                               "SettingsSearchRegistry.isAvailable(\"...\") so search hides it too")
        return IS_AVAILABLE_RE.findall(b.value)

    def classify(self, obj):
        if getattr(obj.prop("SettingsSearch.overlay"), "value", "").strip() == "true":
            return OVERLAY
        # A childless helper that is never shown (a clipboard TextEdit) is never a result. A
        # hidden container is still walked: it may be shown imperatively.
        if not obj.children and getattr(obj.prop("visible"), "value", "").strip() == "false":
            return STRUCTURAL
        if obj.type in ("MouseArea", "TapHandler", "ColoredIcon") or obj.type in TEXT_CONTROLS:
            if CLICK_HANDLERS & obj.bindings.keys():
                return ADJUSTMENT
            if obj.type not in TEXT_CONTROLS:
                return STRUCTURAL
        # A read-only text control (a selectable serial number) displays; it does not adjust.
        if obj.type in TEXT_CONTROLS and getattr(obj.prop("readOnly"), "value", "").strip() == "true":
            return STRUCTURAL
        if obj.type == "Loader":
            # Inline content is walked as a child; a Loader of an unknown `source` is one result.
            return STRUCTURAL if obj.children else VIEW
        cls = CLASS_OF.get(obj.type)
        if cls is None:
            self.error(obj.line, f"unclassified type `{obj.type}`: add it to TYPE_CLASSES in "
                                 f"scripts/settings_search_index.py")
        return cls


ROOT_TYPE_RE = re.compile(r"^\s*([A-Z][\w.]*)\s*\{", re.M)


def card_types():
    """SettingsCard and every component rooted in one (UploadDestinationCard): each instance on
    a tab is a card, declaring its own searchId and title. Maps the type to its definition file."""
    roots = {}
    for f in (REPO / "qml").rglob("*.qml"):
        m = ROOT_TYPE_RE.search(strip_comments(re.sub(r"^\s*(import|pragma)\b.*$", "",
                                                      f.read_text(encoding="utf-8"), flags=re.M)))
        if m:
            roots[f.stem] = (m.group(1), f.relative_to(REPO).as_posix())
    types = {"SettingsCard": "qml/components/SettingsCard.qml"}
    grew = True
    while grew:
        grew = False
        for stem, (root, path) in roots.items():
            if stem not in types and root in types:
                types[stem] = path
                grew = True
    return types


class Index:
    def __init__(self, tabs_src=None, registry_src=None, page_src=None, locator_src=None):
        def read(path, given):
            return given if given is not None else (REPO / path).read_text(encoding="utf-8")
        self.errors = []
        self.entries = []
        self.card_types = card_types()
        self.titles = title_props(read(LOCATOR_CPP, locator_src), self.errors)
        self.tabs = parse_tabs(read(TABS_QML, tabs_src), self.errors)
        self.conditions, self.routes = parse_registry(read(REGISTRY_QML, registry_src), self.errors)
        check_route_arms(read(SETTINGS_PAGE_QML, page_src), self.routes, self.errors)
        self.card_ids = {}       # (tab id, searchId) -> line
        self.consumed = set()    # objects whose SettingsSearch.* bindings were read

    def scan_tab(self, tab, root, path):
        fs = FileScan(path, root, self.errors, self.titles)
        self.walk_tab(fs, tab, root, conds=[])
        self.check_unread_declarations(fs, root)

    def walk_tab(self, fs, tab, obj, conds):
        if obj.type in self.card_types:
            self.scan_card(fs, tab, obj, conds)
            return
        conds = conds + fs.conditions_of(obj)
        cls = fs.classify(obj)
        if obj.type == "Rectangle" and "Theme.cardBackgroundColor" in getattr(obj.prop("color"), "value", ""):
            fs.error(obj.line, "a card-styled Rectangle on a settings tab: use SettingsCard")
        elif cls in RESULT_CLASSES:
            fs.error(obj.line, f"{obj.type} outside any SettingsCard cannot be a search result")
        if cls == OVERLAY:
            self.check_no_card_inside(fs, obj)
        if cls in OPAQUE_CLASSES:
            return
        for c in obj.children:
            self.walk_tab(fs, tab, c, conds)

    def check_no_card_inside(self, fs, overlay):
        for o in walk(overlay):
            if o is not overlay and o.type in self.card_types:
                fs.error(o.line, f"{o.type} inside an overlay: search cannot navigate into it")

    def scan_card(self, fs, tab, card, conds):
        self.consumed.add(id(card))
        if card.prop("visible") is not None:
            fs.error(card.prop("visible").line, "SettingsCard: set `shown`, not `visible`, so "
                                                "availability still applies")
        card_id = fs.literal_string(card, "searchId", required=True)
        if card_id:
            key = (tab.id, card_id)
            if key in self.card_ids:
                fs.error(card.line, f"searchId \"{card_id}\" used twice on this tab (also line "
                                    f"{self.card_ids[key]}); search would only reach the first")
            self.card_ids.setdefault(key, card.line)
        title_b = card.prop("title")
        title = fs.literal_text(title_b) if title_b else None
        if title is None:
            fs.error(card.line, "SettingsCard title must be a literal TranslationManager.translate(...)")
            return
        card_conds = self.availability(fs, card.prop("availability"), tab, conds)
        self.entries.append(entry("card", title, fs.literal_list(card, "keywords"), card_conds,
                                  tab=tab, card_id=card_id, desc=fs.literal_desc(card, "description")))
        seen = {title.fallback.casefold(): card.line}
        for obj, obj_conds in self.card_results(fs, card, card_conds):
            self.consumed.add(id(obj))
            t = fs.title_of(obj)
            if t is None:
                continue
            folded = t.fallback.casefold()
            if folded in seen:
                if seen[folded] != card.line:
                    fs.error(obj.line, f"duplicate search title \"{t.fallback}\" on card "
                                       f"\"{card_id}\" (also line {seen[folded]}); give one a "
                                       f"distinct SettingsSearch.title")
                elif any(k in obj.bindings for k in ("SettingsSearch.keywords", "SettingsSearch.description")):
                    fs.error(obj.line, f"titled like its card, so the card result stands for it and "
                                       f"its SettingsSearch keywords/description would be dropped: "
                                       f"move them to the card or give it a distinct title")
                continue
            seen[folded] = obj.line
            self.entries.append(entry("adjustment", t, fs.literal_list(obj, "SettingsSearch.keywords"),
                                      self.availability(fs, None, tab, obj_conds), tab=tab,
                                      card_id=card_id, card_title=title,
                                      desc=fs.literal_desc(obj, "SettingsSearch.description")))

    def card_results(self, fs, obj, conds):
        """(result, the conditions its visibility depends on) for every result under obj."""
        for c in obj.children:
            if c.type in self.card_types:
                fs.error(c.line, "SettingsCard nested in another SettingsCard")
                continue
            c_conds = conds + fs.conditions_of(c)
            cls = fs.classify(c)
            if cls in RESULT_CLASSES:
                yield c, c_conds
            if cls == OVERLAY:
                self.check_no_card_inside(fs, c)
            if cls in OPAQUE_CLASSES:
                continue
            yield from self.card_results(fs, c, c_conds)

    def availability(self, fs, binding, tab, inherited):
        """Every condition an entry needs: its tab's, its card's, and its own row's."""
        conds = []
        if binding is not None:
            m = STRING_LITERAL_RE.match(binding.value)
            if not m:
                fs.error(binding.line, "availability must be a string literal")
            else:
                conds.append(fs.unescape(m.group(1), binding.line))
        for c in list(inherited) + conds:
            if c not in self.conditions:
                fs.error(getattr(binding, "line", 0), f"unknown condition \"{c}\"; the conditions are "
                                                      f"{sorted(self.conditions)} ({REGISTRY_QML})")
        merged = []
        for c in list(inherited) + conds + (["debug"] if tab.debug_only else []):
            if c not in merged:
                merged.append(c)
        return merged

    def check_unread_declarations(self, fs, root):
        for obj in walk(root):
            if id(obj) in self.consumed:
                continue
            for k, b in obj.bindings.items():
                if k.startswith("SettingsSearch.") and k != "SettingsSearch.overlay":
                    fs.error(b.line, f"`{k}` here is never read: only a card's control, view or "
                                     f"component is a search result")

    def scan_external(self, path, root):
        fs = FileScan(path, root, self.errors, self.titles)
        for obj in walk(root):
            if obj.type in self.card_types:
                fs.error(obj.line, "SettingsCard outside a settings tab")
            route_b = obj.prop("SettingsSearch.route")
            if route_b is None:
                continue
            self.consumed.add(id(obj))
            route = fs.literal_string(obj, "SettingsSearch.route", required=True)
            if route and route not in self.routes:
                fs.error(route_b.line, f"unknown route \"{route}\"; the routes are {self.routes} "
                                       f"({REGISTRY_QML})")
            t_b = obj.prop("SettingsSearch.title")
            t = fs.literal_text(t_b) if t_b else None
            if t is None:
                fs.error(obj.line, "a SettingsSearch.route needs a literal SettingsSearch.title")
                continue
            self.entries.append(entry("external", t, fs.literal_list(obj, "SettingsSearch.keywords"), [],
                                      route=route, desc=fs.literal_desc(obj, "SettingsSearch.description")))
        self.check_unread_declarations(fs, root)

    def scan_card_components(self):
        """A card component's own controls are covered by its card's result, but every type in
        it is still classified, so a new kind of control there is a decision too."""
        for path in sorted(set(self.card_types.values()) - {self.card_types["SettingsCard"]}):
            root = parse_qml(path)
            fs = FileScan(path, root, self.errors, self.titles)
            for obj in list(walk(root))[1:]:
                fs.classify(obj)


def check_route_arms(page_src, routes, errors):
    page = strip_comments(page_src)
    for route in routes:
        if f'externalRoute === "{route}"' not in page:
            errors.append(f"{SETTINGS_PAGE_QML}: no `externalRoute === \"{route}\"` arm for the "
                          f"route {REGISTRY_QML} declares")


def check_no_stray_cards(files, card_types, errors):
    """files: {repo-relative path: QML source} of everything that is neither a tab nor a host."""
    card_re = re.compile(r"(?<![\w.])(" + "|".join(sorted(card_types)) + r")\s*\{")
    definitions = set(card_types.values())
    for rel, src in sorted(files.items()):
        if rel not in definitions and card_re.search(strip_comments(src)):
            errors.append(f"{rel}: SettingsCard outside a settings tab; search cannot navigate to it")


def parse_tabs(src, errors):
    m = re.search(r"readonly property var tabs:\s*\[(.*?)\n\s*\]", strip_comments(src), re.S)
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
        tabs.append(Tab(tid.group(1), "qml/pages/" + src_m.group(1), bool(dbg and dbg.group(1) == "true")))
    ids = [t.id for t in tabs]
    for dup in sorted({i for i in ids if ids.count(i) > 1}):
        errors.append(f"{TABS_QML}: tab id \"{dup}\" declared twice")
    return tabs


def parse_registry(src, errors):
    src = strip_comments(src)
    cm = re.search(r"property var conditions:\s*\(\{(.*?)\}\)", src, re.S)
    rm = re.search(r"property var routes:\s*\[(.*?)\]", src, re.S)
    if not cm or not rm:
        errors.append(f"{REGISTRY_QML}: cannot find the `conditions` object or `routes` array literal")
        return set(), []
    names = re.findall(r'^\s*"(\w+)"\s*:', cm.group(1), re.M)
    return set(names), re.findall(STRING_RE, rm.group(1))


def build_index():
    index = Index()

    def parse(path):
        try:
            return parse_qml(path)
        except ScanError as e:
            index.errors.append(str(e))
        except OSError as e:
            index.errors.append(f"{path}: cannot read: {e.strerror}")
        return None

    for tab in index.tabs:
        root = parse(tab.source)
        if root is not None:
            try:
                index.scan_tab(tab, root, tab.source)
            except ScanError as e:
                index.errors.append(str(e))
    for host in EXTERNAL_HOSTS:
        root = parse(host)
        if root is not None:
            try:
                index.scan_external(host, root)
            except ScanError as e:
                index.errors.append(str(e))
    try:
        index.scan_card_components()
    except ScanError as e:
        index.errors.append(str(e))
    skip = {t.source for t in index.tabs} | set(EXTERNAL_HOSTS)
    others = {p.relative_to(REPO).as_posix(): p.read_text(encoding="utf-8")
              for p in (REPO / "qml").rglob("*.qml") if p.relative_to(REPO).as_posix() not in skip}
    check_no_stray_cards(others, index.card_types, index.errors)
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


# ---------------------------------------------------------------------------------------------
# Self-test.
# ---------------------------------------------------------------------------------------------

CARD = 'SettingsCard {{ searchId: "c"; title: TranslationManager.translate("t.card", "Card")\n{body}\n}}'
SWITCH = 'StyledSwitch {{ accessibleName: TranslationManager.translate("{key}", "{text}") }}'
TR = 'TranslationManager.translate("{}", "{}")'

# (name, QML, the error the rules must produce or None for clean, the entries it must emit as
#  (kind, fallback, availability) or None to skip that check, tab)
TAB = Tab("fixture", "fixture.qml")
DEBUG_TAB = Tab("fixture", "fixture.qml", debug_only=True)
FIXTURES = [
    ("clean card", CARD.format(body=SWITCH.format(key="k.a", text="Alpha")), None,
     [("card", "Card", []), ("adjustment", "Alpha", [])], TAB),
    ("unclassified type", CARD.format(body="FancySlider { }"), "unclassified type `FancySlider`", None, TAB),
    ("card title not literal", 'SettingsCard { searchId: "c"; title: someVar }',
     "SettingsCard title must be a literal", None, TAB),
    ("missing searchId", "SettingsCard { title: " + TR.format("t", "T") + " }", "needs a literal searchId", None, TAB),
    ("empty searchId", 'SettingsCard { searchId: ""; title: ' + TR.format("t", "T") + " }",
     "searchId must be a non-empty string literal", None, TAB),
    ("duplicate searchId", "Item {\n" + CARD.format(body="") + "\n" + CARD.format(body="") + "\n}",
     "searchId \"c\" used twice", None, TAB),
    ("dynamic accessible name", CARD.format(body="StyledSwitch { accessibleName: label }"),
     "cannot read a search title from `accessibleName`", None, TAB),
    (".arg() title: no item carries it", CARD.format(
        body="ValueInput { accessibleName: " + TR.format("k.v", "Level %1") + ".arg(v) }"),
     "cannot read a search title", None, TAB),
    ("%1 placeholder in a title", CARD.format(
        body="ValueInput { SettingsSearch.title: " + TR.format("k.v", "Version %1") + " }"),
     "cannot read a search title", None, TAB),
    ("no title at all", CARD.format(body="StyledSwitch { checked: true }"), "has no title", None, TAB),
    ("duplicate titles", CARD.format(body=SWITCH.format(key="k.a", text="Alpha") + "\n"
                                     + SWITCH.format(key="k.b", text="alpha")),
     "duplicate search title", None, TAB),
    ("visible on a card", 'SettingsCard { searchId: "c"; title: ' + TR.format("t", "T") + '; visible: false }',
     "set `shown`, not `visible`", None, TAB),
    ("card-styled Rectangle", "Item { Rectangle { color: Theme.cardBackgroundColor } }",
     "card-styled Rectangle", None, TAB),
    ("adjustment outside a card", "Item { " + SWITCH.format(key="k", text="K") + " }",
     "outside any SettingsCard", None, TAB),
    ("overlay subtree skipped", CARD.format(body="DecenzaDialog { AccessibleButton { onClicked: x() } }"),
     None, [("card", "Card", [])], TAB),
    ("a card inside an overlay", "Item { DecenzaDialog {\n" + CARD.format(body="") + "\n} }",
     "inside an overlay", None, TAB),
    ("declared overlay", "Item { Rectangle { SettingsSearch.overlay: true; AccessibleButton { onClicked: x() } } }",
     None, [], TAB),
    ("view needs a title", CARD.format(body="Repeater { model: 3; delegate: AccessibleButton { } }"),
     "Repeater has no title", None, TAB),
    ("a component rooted in SettingsCard is a card", 'Item { UploadDestinationCard { searchId: "u"; '
     'title: ' + TR.format("t.u", "Uploads") + ' } }', None, [("card", "Uploads", [])], TAB),
    ("a card component still needs a literal title", 'Item { UploadDestinationCard { searchId: "u"; title: name } }',
     "SettingsCard title must be a literal", None, TAB),
    ("read-only text is display, not a control", CARD.format(body="TextEdit { readOnly: true; text: serial }"),
     None, [("card", "Card", [])], TAB),
    ("read-only text that reacts is a control", CARD.format(body="StyledTextField { readOnly: true; onPressed: pick() }"),
     "has no title", None, TAB),
    ("a hidden childless helper is not a result", "Item { TextEdit { id: helper; visible: false } }", None, [], TAB),
    ("a hidden container is still checked", CARD.format(body="Item { visible: false; FancySlider { } }"),
     "unclassified type `FancySlider`", None, TAB),
    ("a control's internals are part of it", CARD.format(
        body='StyledComboBox { accessibleLabel: ' + TR.format("k.c", "Pick") + '; delegate: ItemDelegate { } }'),
     None, [("card", "Card", []), ("adjustment", "Pick", [])], TAB),
    ("titled view, delegates not walked", CARD.format(
        body='Repeater { SettingsSearch.title: ' + TR.format("k.r", "Rows") + '; delegate: AccessibleButton { } }'),
     None, [("card", "Card", []), ("adjustment", "Rows", [])], TAB),
    ("clicked raw MouseArea", CARD.format(body="MouseArea { onClicked: go() }"), "MouseArea has no title", None, TAB),
    ("TapHandler single tap", CARD.format(body="TapHandler { onSingleTapped: go() }"),
     "TapHandler has no title", None, TAB),
    ("passive MouseArea", CARD.format(body="MouseArea { hoverEnabled: true }"), None, [("card", "Card", [])], TAB),
    ("Tr id as title", CARD.format(body='Tr { id: trX; key: "k.tr"; fallback: "From Tr"; visible: false }\n'
                                        'AccessibleButton { text: trX.text }'),
     None, [("card", "Card", []), ("adjustment", "From Tr", [])], TAB),
    ("translated prefix of a longer name", CARD.format(
        body='ValueInput { accessibleName: ' + TR.format("k.v", "Level") + ' + ": " + value }'),
     None, [("card", "Card", []), ("adjustment", "Level", [])], TAB),
    ("SettingsSearch.title must be exact", CARD.format(
        body='ValueInput { SettingsSearch.title: ' + TR.format("k.v", "Level") + ' + x }'),
     "cannot read a search title from `SettingsSearch.title`", None, TAB),
    ("keywords not literal", 'SettingsCard { searchId: "c"; title: ' + TR.format("t", "T") + '; keywords: words }',
     "must be an array of string literals", None, TAB),
    ("availability not literal", 'SettingsCard { searchId: "c"; title: ' + TR.format("t", "T") + '; availability: os }',
     "availability must be a string literal", None, TAB),
    ("unknown availability", 'SettingsCard { searchId: "c"; title: ' + TR.format("t", "T") + '; availability: "mars" }',
     "unknown condition", None, TAB),
    ("a raw platform test in visible", CARD.format(
        body='RowLayout { visible: Qt.platform.os === "android"; ' + SWITCH.format(key="k", text="K") + " }"),
     "use SettingsSearchRegistry.isAvailable", None, TAB),
    ("a row's condition reaches its result", CARD.format(
        body='RowLayout { visible: SettingsSearchRegistry.isAvailable("windows") && x\n'
             + SWITCH.format(key="k", text="Row") + " }"),
     None, [("card", "Card", []), ("adjustment", "Row", ["windows"])], TAB),
    ("card and row conditions combine", 'SettingsCard { searchId: "c"; title: ' + TR.format("t", "Card")
     + '; availability: "android"\nRowLayout { visible: SettingsSearchRegistry.isAvailable("windows")\n'
     + SWITCH.format(key="k", text="Row") + " } }",
     None, [("card", "Card", ["android"]), ("adjustment", "Row", ["android", "windows"])], TAB),
    ("a debug tab's results are debug-only", CARD.format(body=SWITCH.format(key="k", text="Row")),
     None, [("card", "Card", ["debug"]), ("adjustment", "Row", ["debug"])], DEBUG_TAB),
    ("SettingsSearch keywords and description are emitted", CARD.format(
        body='StyledSwitch { accessibleName: ' + TR.format("k", "Row") + '; SettingsSearch.keywords: ["kw"]; '
             'SettingsSearch.description: ' + TR.format("k.d", "Desc") + ' }'),
     None, [("card", "Card", []), ("adjustment", "Row", [])], TAB),
    ("non-literal SettingsSearch.description", CARD.format(
        body='StyledSwitch { accessibleName: ' + TR.format("k", "Row") + '; SettingsSearch.description: d }'),
     "must be a literal", None, TAB),
    ("SettingsSearch on something that is not a result", CARD.format(
        body='ColumnLayout { SettingsSearch.keywords: ["x"]\n' + SWITCH.format(key="k", text="Row") + " }"),
     "is never read", None, TAB),
    ("titled like its card, with its own keywords", CARD.format(
        body='StyledSwitch { accessibleName: ' + TR.format("k", "Card") + '; SettingsSearch.keywords: ["x"] }'),
     "would be dropped", None, TAB),
    ("same title as its card: one result", CARD.format(body=SWITCH.format(key="k", text="Card")),
     None, [("card", "Card", [])], TAB),
    ("Loader by source needs a title", CARD.format(body='Loader { source: "Panel.qml" }'),
     "Loader has no title", None, TAB),
    ("inline Loader content is walked", CARD.format(body="Loader { sourceComponent: FancySlider { } }"),
     "unclassified type `FancySlider`", None, TAB),
    ("nested card", CARD.format(body=CARD.format(body="")), "nested in another SettingsCard", None, TAB),
    ("an escape JSON cannot read is reported, not a traceback",
     'SettingsCard { searchId: "c"; title: TranslationManager.translate("t", "Don\\\'t") }',
     "cannot read the string literal", None, TAB),
    ("tokenizer: regex, template, `property:` binding", CARD.format(
        body='Item { function f(h) { if (/^#[0-9a-f]{6}$/.test(h)) return `${h}}`; return "}" }\n'
             'NumberAnimation { property: "opacity"; to: 1 } }'), None, [("card", "Card", [])], TAB),
]


def _errors(fn):
    errors = []
    fn(errors)
    return errors


TABS = 'readonly property var tabs: [\n{{ id: "a", source: "x" }},\n{}\n]'
# (name, check producing errors, the error expected or None, or a callable verdict on the errors)
UNIT_CHECKS = [
    ("duplicate tab id", lambda: _errors(lambda e: parse_tabs(TABS.format('{ id: "a", source: "y" }'), e)),
     "declared twice"),
    ("a commented-out tab is not a tab",
     lambda: [] if [t.id for t in parse_tabs(TABS.format('// { id: "z", source: "z" }'), [])] == ["a"]
     else ["commented-out tab parsed"], None),
    ("a route with no SettingsPage arm", lambda: _errors(lambda e: check_route_arms("", ["r"], e)),
     "no `externalRoute"),
    ("a route arm only in a comment",
     lambda: _errors(lambda e: check_route_arms('// externalRoute === "r"', ["r"], e)), "no `externalRoute"),
    ("a card on a page that is not a tab",
     lambda: _errors(lambda e: check_no_stray_cards({"qml/pages/X.qml": "Item { delegate: SettingsCard { } }"},
                                                    {"SettingsCard": "qml/components/SettingsCard.qml"}, e)),
     "outside a settings tab"),
    ("title properties come from the locator",
     lambda: [] if title_props('kNameProperties[] = {"a"}; kTextProperties[] = {"b", "c"}', [])
     == ("SettingsSearch.title", "a", "Accessible.name", "b", "c") else ["title_props misread"], None),
    ("unknown route", lambda: _scan_external('Item { Item { SettingsSearch.route: "nowhere"; '
                                             'SettingsSearch.title: ' + TR.format("t", "T") + ' } }'),
     "unknown route"),
    ("a route with no literal title", lambda: _scan_external('Item { Item { SettingsSearch.route: "profileSelector" } }'),
     "needs a literal SettingsSearch.title"),
    ("a card on an external host", lambda: _scan_external(
        'Item { SettingsCard { searchId: "c"; title: ' + TR.format("t", "T") + ' } }'), "outside a settings tab"),
    ("locator arrays missing", lambda: _errors(lambda e: title_props("", e)), "cannot find the kNameProperties"),
    ("tabs array missing", lambda: _errors(lambda e: parse_tabs("", e)), "cannot find the `tabs` array"),
    ("a tab without a literal source", lambda: _errors(lambda e: parse_tabs(TABS.format('{ id: "b" }'), e)),
     "without a literal id and source"),
    ("registry tables missing", lambda: _errors(lambda e: parse_registry("", e)), "cannot find the `conditions`"),
]


def _scan_external(src):
    index = Index()
    index.errors = []
    index.scan_external("fixture.qml", Parser(src, "fixture.qml").parse_file())
    return index.errors


def self_test():
    failures = 0
    for name, src, expected, want_entries, tab in FIXTURES:
        index = Index()
        index.errors = []
        try:
            index.scan_tab(tab, Parser(src, "fixture.qml").parse_file(), "fixture.qml")
        except ScanError as e:
            index.errors.append(str(e))
        got = [(e["kind"], e["fallback"], e["availability"]) for e in index.entries]
        ok = (not index.errors) if expected is None else any(expected in e for e in index.errors)
        if ok and want_entries is not None and got != want_entries:
            ok = False
            index.errors.append(f"entries {got}, expected {want_entries}")
        if ok and name.startswith("SettingsSearch keywords"):
            adj = index.entries[1]
            ok = adj["keywords"] == ["kw"] and adj.get("descFallback") == "Desc"
        if not ok:
            failures += 1
            print(f"FAIL {name}: expected {expected or 'no error'}, got {index.errors}", file=sys.stderr)
    for name, check, expected in UNIT_CHECKS:
        errors = check()
        ok = (not errors) if expected is None else any(expected in e for e in errors)
        if not ok:
            failures += 1
            print(f"FAIL {name}: expected {expected or 'no error'}, got {errors}", file=sys.stderr)
    total = len(FIXTURES) + len(UNIT_CHECKS)
    print(f"settings_search_index self-test: {total - failures}/{total} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
