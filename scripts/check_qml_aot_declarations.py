#!/usr/bin/env python3
"""Reject two QML declaration shapes that make Qt 6.12 silently drop AOT code.

Qt validates each compiled unit's AOT lookups when it loads, and on a mismatch runs
the whole calling unit interpreted, with no message
(qtdeclarative/src/qml/jsruntime/qv4executablecompilationunit.cpp:153-177).
qmlcachegen and the run time disagree on two shapes:

  1. A function with typed parameters and no return annotation. qmlcachegen
     records `void`, the run time `QVariant` (only an explicit `: void` stays
     `void`, qtdeclarative 44fa9625e7). Write `function f(x: int): void {`.
  2. A signal declared above a property in the root object. qmlcachegen numbers
     property-change signals where each property is declared, the run time puts
     them all first, so every signal above a property gets two different indices.

Both compile, lint and pass tests that do not set QV4_FAIL_ON_INVALID_AOT=1.
See docs/CLAUDE_MD/QML_GOTCHAS.md. Exit code 1 on any finding, or if no files were
scanned. `--self-test` runs the built-in fixtures.
"""

import pathlib
import re
import sys

from source_lexer import code_only

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCAN = [ROOT / "qml", ROOT / "tests" / "qml", ROOT / "tools" / "webclient"]

FUNCTION = re.compile(r"\bfunction\s+(\w+)\s*\(")
MEMBER = re.compile(r"^\s*(?:(?:readonly|required|default|final)\s+)*(property|signal)\b")


def find_problems(text):
    code = code_only(text, js=True)
    problems = []
    for m in FUNCTION.finditer(code):
        i, depth = m.end(), 1
        while i < len(code) and depth:
            depth += {"(": 1, ")": -1}.get(code[i], 0)
            i += 1
        params = code[m.end():i - 1]
        if ":" in params and not code[i:].lstrip().startswith(":"):
            line = code.count("\n", 0, m.start()) + 1
            problems.append((line, f"function {m.group(1)}() has typed parameters but no "
                                   "return annotation; add one (`: void` if it returns nothing)"))
    depth, first_signal, late = 0, None, []
    for number, line in enumerate(code.split("\n"), 1):
        if depth == 1:
            m = MEMBER.match(line)
            if m and m.group(1) == "signal" and first_signal is None:
                first_signal = number
            elif m and m.group(1) == "property" and first_signal is not None:
                late.append(number)
        depth += line.count("{") - line.count("}")
    if late:
        problems.append((first_signal, f"signal declared above {len(late)} propert"
                         f"{'y' if len(late) == 1 else 'ies'} (first on line {late[0]}); "
                         "declare properties before signals"))
    return problems


def main():
    paths = sorted(p for d in SCAN if d.exists() for p in d.rglob("*.qml"))
    if not paths:
        print("check_qml_aot_declarations: no QML files found")
        return 1
    findings = []
    for path in paths:
        for line, message in find_problems(path.read_text(encoding="utf-8")):
            findings.append(f"{path.relative_to(ROOT)}:{line}: {message}")
    for finding in findings:
        print(finding)
    if findings:
        print(f"check_qml_aot_declarations: {len(findings)} finding(s). Each can make Qt 6.12 run "
              "every calling unit interpreted; see docs/CLAUDE_MD/QML_GOTCHAS.md.")
        return 1
    print(f"check_qml_aot_declarations: OK ({len(paths)} files)")
    return 0


def self_test():
    must_flag = {
        "typed param, no return": "Item {\n    function f(x: int) {\n    }\n}\n",
        "multi-line params": "Item {\n    function f(a: int,\n               b: var) {\n    }\n}\n",
        "signal above property": "Item {\n    signal done()\n    property int x: 0\n}\n",
        "signal above readonly": "Item {\n    signal done()\n    readonly property int x: 0\n}\n",
        # ExpandableTextArea.qml: the quotes and `}` in these regexes once hid every later member.
        "after regex literals": "Item {\n    signal done()\n    function f(t) {\n"
                                "        return t.replace(/https?:\\/\\/[^\\s<>\"']+/g, \"\")"
                                ".replace(/[.,;:!?\\])}]+$/, \"\")\n    }\n    property int x: 0\n}\n",
    }
    must_pass = {
        "return annotated": "Item {\n    function f(x: int): void {\n    }\n}\n",
        "untyped function": "Item {\n    function f(x) { return { a: 1 } }\n}\n",
        "properties first": "Item {\n    property int x: 0\n    signal done()\n}\n",
        "nested object order": "Item {\n    property int x\n    Item {\n        signal s()\n        property int y\n    }\n}\n",
        "commented out": "Item {\n    // signal done()\n    property int x\n    /* function f(a: int) {} */\n}\n",
        "colon in string": "Item {\n    signal s()\n    function f() { g(\"a: b\") }\n}\n",
        "url in string": "Item {\n    property string u: \"http://x/{\"\n    signal s()\n}\n",
        "template literal": "Item {\n    property string t: `a: ${x} }`\n    signal s()\n}\n",
    }
    failed = [name for name, src in must_flag.items() if not find_problems(src)]
    failed += [name for name, src in must_pass.items() if find_problems(src)]
    if failed:
        print("check_qml_aot_declarations self-test FAILED:\n  " + "\n  ".join(failed))
        return 1
    print(f"check_qml_aot_declarations self-test: OK - {len(must_flag)} flagged, "
          f"{len(must_pass)} passed")
    return 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv else main())
