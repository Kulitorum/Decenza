#!/usr/bin/env python3
"""Extract the D-Flow plugin's three stock profiles into test fixtures.

D-Flow ships no .tcl files — all three profiles live inside plugin.tcl and are written
out at plugin start. The parity suite needs them as files, so this transcribes them.

See tests/data/dflow_plugin_profiles/README.md for provenance and the caveat about the
`default` profile being reconstructed rather than copied verbatim.

Usage:  python3 tools/extract_dflow_profiles.py [path-to-plugin.tcl]
"""

import os
import re
import sys

DEFAULT_SRC = os.path.expanduser(
    "~/Development/GitHub/de1app/de1plus/plugins/D_Flow_Espresso_Profile/plugin.tcl")
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "tests", "data", "dflow_plugin_profiles")


def balanced(text, start):
    """Content inside the brace group beginning at `start` (which must index a '{')."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
    raise ValueError("unbalanced braces from offset %d" % start)


def extract_verbatim(src, var):
    """La Pavoni / Q: `append <var> {...}` — the blob is the file content, copied as-is."""
    m = re.search(r'append\s+%s\s+\{' % re.escape(var), src)
    if not m:
        raise SystemExit("could not find `append %s {` — plugin layout changed" % var)
    return balanced(src, m.end() - 1).strip() + "\n"


def extract_default(src):
    """default: transcribe set_Dflow_default's ::settings assignments in source order.

    de1app's save_profile serialises the settings array as `<key> <value>` lines, so this
    reproduces what it would write for the keys the proc sets. Keys it never touches are
    absent here — see the README caveat.
    """
    m = re.search(r'proc set_Dflow_default \{\} \{', src)
    if not m:
        raise SystemExit("could not find `proc set_Dflow_default` — plugin layout changed")
    proc = balanced(src, m.end() - 1)

    lines = []
    for assign in re.finditer(r'set\s+::settings\((\w+)\)\s+', proc):
        rest = proc[assign.end():]
        if rest.startswith('{'):
            value = "{" + balanced(rest, 0) + "}"
        else:
            value = rest.split('\n', 1)[0].strip()
        lines.append("%s %s" % (assign.group(1), value))

    # The caller sets profile_title immediately before invoking set_Dflow_default, so the
    # saved file carries it even though the proc itself does not.
    lines.append("profile_title {D-Flow / default}")
    return "\n".join(lines) + "\n"


def main():
    src_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    if not os.path.exists(src_path):
        raise SystemExit("plugin source not found: %s" % src_path)
    src = open(src_path, encoding='utf-8').read()

    out_dir = os.path.normpath(OUT_DIR)
    os.makedirs(out_dir, exist_ok=True)

    written = {
        "D-Flow____La_Pavoni.tcl": extract_verbatim(src, "La_Pavoni_data"),
        "D-Flow____Q.tcl": extract_verbatim(src, "Q_data"),
        "D-Flow____default.tcl": extract_default(src),
    }
    for name, body in written.items():
        with open(os.path.join(out_dir, name), 'w', encoding='utf-8') as fh:
            fh.write(body)
        print("wrote %-28s %5d bytes" % (name, len(body)))


if __name__ == "__main__":
    main()
