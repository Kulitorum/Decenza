"""Blank comments and literals out of C++/QML/JS source for the text checkers.

code_only() replaces every comment, string, character and template literal (and,
with js=True, every regex literal) with spaces, keeping newlines, so line numbers
and columns still point at the original text. A quote or brace inside a literal,
such as `/[.,;:!?\\])}]+$/` in ExpandableTextArea.qml, otherwise reads as code.
"""

import re

# A `/` after one of these (or at the start) begins a regex literal, not a division.
_REGEX_AFTER_CHARS = set("(,=:[!&|?{};+-*%<>~^")
_REGEX_AFTER_WORDS = {"return", "typeof", "case", "delete", "void", "in", "of",
                      "instanceof", "new", "throw", "yield", "await", "else", "do"}
_RAW_STRING = re.compile(r'R"([^\s()\\]*)\(.*?\)\1"', re.S)
_WORD_BEFORE = re.compile(r"(\w+)\s*$")


def _blank(s):
    return "".join("\n" if c == "\n" else " " for c in s)


def _regex_end(text, i):
    """Index just past the regex literal opening at text[i], or None if it is not one."""
    j, in_class, n = i + 1, False, len(text)
    while j < n:
        c = text[j]
        if c == "\n":
            return None
        if c == "\\":
            j += 2
            continue
        if c == "[":
            in_class = True
        elif c == "]":
            in_class = False
        elif c == "/" and not in_class:
            j += 1
            while j < n and text[j].isalpha():
                j += 1
            return j
        j += 1
    return None


def _starts_regex(code):
    tail = "".join(code[-40:]).rstrip()
    if not tail:
        return True
    if tail[-1] in _REGEX_AFTER_CHARS:
        return True
    word = _WORD_BEFORE.search(tail)
    return bool(word and word.group(1) in _REGEX_AFTER_WORDS)


def code_only(text, js=False):
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif c == "R" and text.startswith('R"', i) and (m := _RAW_STRING.match(text, i)):
            j = m.end()
        elif c in "\"'`" and (c != "`" or js):
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
        elif c == "/" and js and _starts_regex(out) and (end := _regex_end(text, i)):
            j = end
        else:
            out.append(c)
            i += 1
            continue
        out.append(_blank(text[i:j]))
        i = j
    return "".join(out)
