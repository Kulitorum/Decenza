#!/usr/bin/env python3
"""A QML_SINGLETON with a create() factory must not be default-constructible.

WHY THIS EXISTS
---------------
Qt picks how to build a QML_SINGLETON in singletonConstructionMode()
(qtdeclarative/src/qml/qml/qqmlprivate.h:155-167), and the order of its tests is
the whole problem:

    if constexpr (!std::is_same_v<T, WrapperT> && HasSingletonFactory<T, WrapperT>)
        return SingletonConstructionMode::FactoryWrapper;   # QML_FOREIGN, safe
    if constexpr (std::is_default_constructible<T>::value)
        return SingletonConstructionMode::Constructor;      # new T   <-- wins
    if constexpr (HasSingletonFactory<T>::value)
        return SingletonConstructionMode::Factory;          # T::create(q, j)

Default-constructibility is tested BEFORE the factory. So a singleton whose
constructor has a defaulted `parent` gets `new T` at :190 and its create() is
never called — dead code that looks alive, with no diagnostic from the compiler,
qmllint, moc or the test suite.

Decenza shipped that. AccessibilityManager's create() published main.cpp's
instance to QML; Qt ignored it and built its own during engine.load(). Every
QML `AccessibilityManager.announce()` reached Qt's orphan while the MCP server
and the coaching signal were wired to main.cpp's object — two live objects, each
with its own QTextToSpeech. Visible only because the orphan's constructor logged.

WHAT IS SAFE, AND WHY THE CHECK IS NARROW
-----------------------------------------
Not every default-constructible singleton is wrong. A singleton with NO create()
is meant to be built by Qt — that is the normal, correct Constructor mode, and
several here rely on it. The defect is specifically "declares a factory AND is
default-constructible", where the factory silently loses.

QML_FOREIGN wrappers are also safe: T != WrapperT, so the FactoryWrapper branch
is taken first, whatever the foreign type's constructors look like.

This is a text scan, not a compiler. It reads the constructor declarations of a
class that carries QML_SINGLETON and answers "could this be default-constructed".
A private or deleted constructor counts as not-default-constructible, which is
how WebDebugLogger stays correct. Where a class is too tangled to read this way
the script says so and fails rather than guessing — a check that quietly skips
what it cannot parse is how the qmllint gate once reported 103 clean files it had
never analysed.

The per-class static_assert in accessibilitymanager.h is the other half of this,
and the stronger one where it exists: it fails at compile time. This script is
what covers a NEW singleton whose author never knew to write the assert.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "src"

CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+(\w+)\b", re.M)
ACCESS_RE = re.compile(r"^\s*(public|private|protected)\s*:", re.M)


def class_bodies(text: str):
    """Yield (name, body_text) for each class/struct, by brace matching."""
    for m in CLASS_RE.finditer(text):
        name = m.group(1)
        brace = text.find("{", m.end())
        if brace == -1:
            continue
        # A forward declaration ("class QQmlEngine;") has no body of its own.
        # Without this the next brace in the file is some OTHER class's body,
        # which is then attributed to this name — the scan reported fifteen
        # failures for types that are not even declared here.
        semi = text.find(";", m.end())
        if semi != -1 and semi < brace:
            continue
        depth, i = 0, brace
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if depth != 0:
            continue
        yield name, text[brace : i + 1]


def default_constructible(name: str, body: str) -> tuple[bool, str]:
    """Could `T()` compile from outside the class? Returns (verdict, reason)."""
    ctors = []
    for m in re.finditer(r"(?:^|\n)([^\n;{}]*\b" + re.escape(name) + r"\s*\(([^)]*)\))", body):
        decl, args = m.group(1), m.group(2)
        if "~" in decl or "operator" in decl:
            continue
        # Skip a copy/move constructor; it is not a default constructor.
        if re.search(r"\b" + re.escape(name) + r"\s*(?:const)?\s*&", args):
            continue
        ctors.append((m.start(1), decl.strip(), args.strip()))

    if not ctors:
        # No user-declared constructor at all: the implicit one is public.
        return True, "no user-declared constructor (implicit default is public)"

    # Which access section is each constructor in?
    sections = [(m.start(), m.group(1)) for m in ACCESS_RE.finditer(body)]

    def access_at(pos: int) -> str:
        # Start permissive. Every QML_SINGLETON in this tree spells its first
        # access section explicitly (Q_OBJECT forces a `public:` or the author
        # writes one), so "public until told otherwise" matches reality; a class
        # that did rely on the implicit private default would be reported as a
        # false POSITIVE, which is the safe direction for this check to err.
        cur = "public"
        for at, kind in sections:
            if at < pos:
                cur = kind
            else:
                break
        return cur

    for pos, decl, args in ctors:
        if "= delete" in decl:
            continue
        if access_at(pos) != "public":
            continue
        if args == "" or args == "void":
            return True, f"public no-arg constructor: {decl}"
        # Every parameter defaulted => callable with no arguments.
        params = [p for p in re.split(r",(?![^<]*>)", args) if p.strip()]
        if all("=" in p for p in params):
            return True, f"public constructor callable with no arguments: {decl}"

    return False, "no publicly callable no-argument constructor"


def main() -> int:
    problems, checked, names = [], 0, []

    for path in sorted(SRC.rglob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if "QML_SINGLETON" not in text:
            continue
        for name, body in class_bodies(text):
            if not re.search(r"^\s*QML_SINGLETON\b", body, re.M):
                continue
            # QML_FOREIGN takes Qt's FactoryWrapper branch before the
            # default-constructible test, so it cannot hit this defect.
            #
            # Matched as an actual macro invocation, not a substring: the word
            # appears in prose inside MainController's comments, and a substring
            # test silently dropped that class from the check entirely. A
            # false NEGATIVE in a gate is worse than a noisy one — it reports
            # green over exactly the type it failed to look at.
            if re.search(r"^\s*QML_FOREIGN\s*\(", body, re.M):
                continue
            if not re.search(r"static\s+[\w:*<>\s]*\bcreate\s*\(\s*QQmlEngine", body):
                # No factory: Qt is meant to construct it. Correct by design.
                continue
            checked += 1
            names.append(f"{path.relative_to(REPO)}:{name}")
            bad, reason = default_constructible(name, body)
            if bad:
                rel = path.relative_to(REPO)
                problems.append(f"{rel}: {name} declares create() but is default-constructible "
                                f"({reason}). Qt will 'new' it and never call create() "
                                f"(qqmlprivate.h:161-164).")

    if problems:
        sys.stderr.write("QML_SINGLETON construction-mode check FAILED:\n\n")
        for p in problems:
            sys.stderr.write(f"  {p}\n")
        sys.stderr.write(
            "\nA QML_SINGLETON that declares create() must not be default-constructible, or Qt\n"
            "silently builds its own instance and the factory becomes dead code. Remove the\n"
            "default from the constructor's `parent` (and pass it explicitly at the one C++\n"
            "construction site), or make the constructor private.\n")
        return 1

    if checked == 0:
        sys.stderr.write("QML_SINGLETON check analysed 0 types — the scan is broken, not the tree.\n")
        return 1

    print(f"OK: {checked} QML_SINGLETON type(s) with a create() factory are not "
          f"default-constructible, so Qt reaches the factory rather than newing its own.")
    for n in names:
        print(f"  checked: {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
