#!/usr/bin/env python3
"""The MCP tool surface is a budget. This is what enforces it.

`tools/list` is sent in full to every client on every connection, and real clients
truncate it. ChatGPT exposed 87 of the app's 97 tools, silently — the user could not
call `get_flow_calibration` or `set_flow_calibration` because those two happened to
fall off, while `clear_flow_calibration` from the same trio survived. Nothing in the
app reported a problem, because nothing in the app had one.

Two things caused it and both are checked here.

  1. SIZE. The listing carried a base64 SVG icon per tool: ~216 KB against ~32 KB of
     descriptions, 87% of the payload, and 41 of 97 tools shipped the SAME 2292-byte
     generic fallback because their name prefix was not in the icon map. None of that
     was visible in review — it was one call to a helper. Hence the `data:` rule
     below, which is about payload rather than icons specifically.

  2. COUNT. Every feature added tools; none folded them in. The merges that took the
     surface from 97 to 65 buy nothing if the next twenty features add one each.

The four limits are deliberately in ONE place (LIMITS below) so tightening them after
a measurement is a one-line edit rather than an archaeology exercise.

Build-free by design: this parses the registration sites as text, needs no Qt and no
compile, and runs in well under a second. That is what lets it sit in the per-PR
text-invariants job rather than behind a build.

Usage: python3 scripts/check_mcp_tool_budget.py [--verbose]
Exit code 1 on any violation.
"""

import re
import sys
from pathlib import Path

# --- The budget. One edit, one place. ---------------------------------------
LIMITS = {
    # Tool count. 65 today; the headroom is for features, not for un-merged families.
    "max_tools": 80,
    # Per-tool description. Enough to CHOOSE the tool and fill its arguments; the rest
    # goes to resources/ai/tools/<topic>.md, served by get_agent_file(topic).
    "max_description_chars": 500,
    # Per-property description inside an input schema.
    "max_property_description_chars": 120,
    # Rough estimate of the whole tools/list payload (see estimate_payload_bytes).
    # ~64 KB today, against ~270 KB before the icons came out and the families
    # merged. Set so that 80 in-budget tools still fit: the COUNT limit is what a
    # new feature should run into, and this one exists to catch a payload that
    # grows without the count growing — which is exactly how the icons got in.
    "max_payload_bytes": 85 * 1024,
}

# mcpresources.cpp is in the list because registerDebugTools() lives there and
# registers `debug_get_log` — a tool, in a file named for resources. Missing it made
# the count read 65 when the server reported 66.
TOOLS_GLOBS = ("src/mcp/mcptools_*.cpp", "src/mcp/mcpresources.cpp")

# A registerTool / registerAsyncTool / registerActionTool call: name, then the
# description as one or more adjacent string literals.
REGISTRATION = re.compile(
    # The description is one or more adjacent literals, optionally continued with
    # `+ identifier` — a runtime-composed description. `debug_get_log` does exactly
    # that, and an earlier version of this pattern (which required the literal run to
    # end at a comma) silently skipped the tool entirely: the count read 65 while the
    # server reported 66, and the biggest description in the app — 7024 characters,
    # 9% of the whole payload — was invisible to the check meant to bound it.
    r'register(?:Async|Action)?Tool\(\s*\n?\s*"([a-z_0-9]+)",\s*((?:"(?:[^"\\]|\\.)*"\s*)+)(,|\+\s*\w+)',
    re.S,
)
PROPERTY_DESCRIPTION = re.compile(r'\{"description", ((?:"(?:[^"\\]|\\.)*"\s*)+)\}')
# A data: URI reachable from a tool listing. The icons were introduced as one helper
# call and nothing about their size showed up at the call site.
DATA_URI = re.compile(r'"data:[a-z]+/')


def literal_text(source: str) -> str:
    """Join C++ adjacent string literals into the text the compiler would produce."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', source)
    return "".join(parts).replace('\\"', '"').replace("\\n", "\n")


def scan(repo_root: Path):
    tools = []          # (name, description, file, line)
    properties = []     # (text, file, line)
    data_uris = []      # (file, line)

    paths = sorted({p for glob in TOOLS_GLOBS for p in repo_root.glob(glob)})
    for path in paths:
        source = path.read_text(encoding="utf-8")
        rel = path.relative_to(repo_root)

        for match in REGISTRATION.finditer(source):
            line = source.count("\n", 0, match.start()) + 1
            composed = match.group(3).startswith("+")
            tools.append((match.group(1), literal_text(match.group(2)), rel, line, composed))

        for match in PROPERTY_DESCRIPTION.finditer(source):
            line = source.count("\n", 0, match.start()) + 1
            properties.append((literal_text(match.group(1)), rel, line))

        for match in DATA_URI.finditer(source):
            line = source.count("\n", 0, match.start()) + 1
            data_uris.append((rel, line))

    return tools, properties, data_uris


def estimate_payload_bytes(tools, properties) -> int:
    """Rough `tools/list` size.

    Per tool: its name, its description, and a flat allowance for the JSON
    scaffolding — the derived `title`, `"inputSchema"`, `"type":"object"`, the
    2020-12 `$schema` URL (60 bytes by itself), `required`, and punctuation.
    Per property: its description plus an allowance for its key, `"type"`, and any
    enum values.

    Deliberately an estimate: the exact number needs a running server, and a check
    that needs the app built cannot run per-PR. The point is to catch a payload that
    has grown by a MULTIPLE — which is what happened — not to audit it to the byte.
    """
    per_tool_scaffolding = 180
    per_property_scaffolding = 55
    # Measured 2026-08-05 against a live tools/list from the running app: this
    # formula read 63.8 KB where the server sent ~70 KB at Full access (77 KB with
    # the "[DISABLED — requires …]" prefixes a lower access level adds). The gap is
    # what a text scan cannot see — the `action` properties the registry injects,
    # nested schema objects — so it is corrected here rather than left as optimism.
    calibration = 1.10
    total = sum(len(name) + len(desc) + per_tool_scaffolding for name, desc, _, _, _ in tools)
    total += sum(len(text) + per_property_scaffolding for text, _, _ in properties)
    return int(total * calibration)


def main() -> int:
    verbose = "--verbose" in sys.argv
    repo_root = Path(__file__).resolve().parent.parent
    tools, properties, data_uris = scan(repo_root)

    violations = []

    if len(tools) > LIMITS["max_tools"]:
        violations.append(
            f"{len(tools)} tools registered, limit {LIMITS['max_tools']}. Merge a same-noun "
            f"family into one tool with an `action` argument (see docs/CLAUDE_MD/MCP_SERVER.md)."
        )

    for name, desc, path, line, composed in tools:
        if composed:
            violations.append(
                f"{path}:{line}: tool `{name}` composes its description at runtime (`+ …`), so "
                f"this check can only see {len(desc)} of its characters. Keep the description a "
                f"plain literal and serve the generated part from the tool's RESPONSE or a "
                f"resources/ai/tools/{name}.md topic."
            )
        if len(desc) > LIMITS["max_description_chars"]:
            violations.append(
                f"{path}:{line}: tool `{name}` description is {len(desc)} chars, limit "
                f"{LIMITS['max_description_chars']}. Keep what a client needs to choose the tool "
                f"and fill its arguments; move the rest to resources/ai/tools/{name}.md and point "
                f"at it with: get_agent_file topic \"{name}\"."
            )

    for text, path, line in properties:
        if len(text) > LIMITS["max_property_description_chars"]:
            violations.append(
                f"{path}:{line}: property description is {len(text)} chars, limit "
                f"{LIMITS['max_property_description_chars']}: \"{text[:60]}...\""
            )

    for path, line in data_uris:
        violations.append(
            f"{path}:{line}: inline data: URI in a tool source. The tool listing must not carry "
            f"binary payloads — per-tool base64 icons were 87% of tools/list and are why clients "
            f"were truncating it."
        )

    payload = estimate_payload_bytes(tools, properties)
    if payload > LIMITS["max_payload_bytes"]:
        violations.append(
            f"estimated tools/list payload is {payload / 1024:.1f} KB, limit "
            f"{LIMITS['max_payload_bytes'] / 1024:.0f} KB."
        )

    if verbose or violations:
        print(
            f"MCP tool budget: {len(tools)} tools, "
            f"{sum(len(d) for _, d, _, _, _ in tools)} description chars, "
            f"~{payload / 1024:.1f} KB estimated tools/list payload"
        )

    if not violations:
        if verbose:
            print("OK — inside budget")
        return 0

    print("\nMCP tool budget violations:\n", file=sys.stderr)
    for violation in violations:
        print(f"  {violation}\n", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
