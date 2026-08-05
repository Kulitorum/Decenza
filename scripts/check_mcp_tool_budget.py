#!/usr/bin/env python3
"""The MCP tool surface is a budget. This is what enforces it.

`tools/list` is sent in full to every client on every connection, and real clients
truncate it. ChatGPT exposed 87 of the app's 97 tools, silently — the user could not
call `get_flow_calibration` or `set_flow_calibration` because those two happened to
fall off, while `clear_flow_calibration` from the same trio survived. Nothing in the
app reported a problem, because nothing in the app had one.

Two things caused it and both are checked here.

  1. SIZE. The listing carried a base64 SVG icon per tool: 216 KB of a ~312 KB
     payload (measured), and 41 of 97 tools shipped the SAME 2292-byte generic
     fallback because their name prefix was not in the icon map. None of that was
     visible in review — it was one call to a helper, which is also the limit of what
     the `data:` rule below can see: it catches a payload written as a literal in a
     scanned file. The helper path is held by tst_mcpserver_protocol's assertion that
     no tool record contains "data:" anywhere.

  2. COUNT. Every feature added tools; none folded them in. The merges that took the
     surface from 97 to 66 buy nothing if the next twenty features add one each.

The four limits are deliberately in ONE place (LIMITS below) so tightening them after
a measurement is a one-line edit rather than an archaeology exercise.

Build-free by design: this parses the registration sites as text, needs no Qt and no
compile, and runs in well under a second. That is what lets it sit in the per-PR
text-invariants job rather than behind a build.

Usage: python3 scripts/check_mcp_tool_budget.py [--verbose]
Exit code 1 on any violation.
"""

import hashlib
import re
import sys
from pathlib import Path

# --- The budget. One edit, one place. ---------------------------------------
LIMITS = {
    # Tool count. 66 today; the headroom is for features, not for un-merged families.
    "max_tools": 80,
    # Per-tool description. Enough to CHOOSE the tool and fill its arguments; the rest
    # goes to resources/ai/tools/<topic>.md, served by get_agent_file(topic).
    "max_description_chars": 500,
    # Per-property description inside an input schema.
    "max_property_description_chars": 120,
    # The whole tools/list payload: ~72 KB estimated today, 68.4 KB measured live.
    # This bounds DESCRIPTION and property-description growth — it cannot see an
    # icon, which contributes nothing to the formula below, so it is not what would
    # have caught the 216 KB of base64 (that is the `data:` rule and the test).
    # Sized so 80 tools at today's ~1.1 KB each still fit, keeping the COUNT limit
    # the one a new feature runs into first.
    "max_payload_bytes": 95 * 1024,
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
# A merged tool's verbs: syncAction("name", "category", … / asyncAction(same).
# Checked here because the type cannot check them — `category` is a QString, and a
# typo'd one resolves to "deny at every access level", which is a verb that silently
# never works for anybody rather than a compile error. Duplicate names are the same
# shape of problem: the second one becomes dead code that nothing reports.
ACTION_BUILDER = re.compile(r'(?:sync|async)Action\(\s*"([a-z_0-9]+)",\s*"([a-z_]+)"')
ACTION_TOOL_START = re.compile(r'const QVector<McpToolAction>\s+(\w+)\s*\{')
VALID_CATEGORIES = {"read", "control", "settings"}
# `serverInfo.version` and the surface it was recorded against, both in mcpserver.h.
# A client caches the tool list it fetched at initialize and refreshes only on
# reconnect, so a surface that moves without the version moving leaves no way to tell
# a stale session from a live one — which is exactly how a connector came to report
# 97 tools against a server that registers 66.
SURFACE_VERSION = re.compile(r'McpSurfaceVersion\s*=\s*"([^"]+)"')
SURFACE_FINGERPRINT = re.compile(r'McpSurfaceFingerprint\s*=\s*"([^"]+)"')
# Which QVector<McpToolAction> belongs to which tool, so the fingerprint is keyed by
# the tool NAME rather than by a local variable a rename would churn.
ACTION_TOOL_BINDING = re.compile(
    r'registerActionTool\(\s*\n?\s*"([a-z_0-9]+)",.*?\n\s*(\w+)\)?[,;]', re.S)
# McpServer's name-keyed confirmation list, for the tools that are not merged. A name
# left here after its tool became a verb of a merged tool is dead text that reads like
# a live rule — and the next reader trusts it.
CONFIRM_NAME = re.compile(r'toolName == "([a-z_0-9]+)"')
# `get_agent_file topic "x"` in a description promises resources/ai/tools/x.md exists
# AND is listed in resources/ai.qrc. Miss the qrc line and the topic silently does not
# exist: the tool still points at it and the server reports it as the caller's typo.
TOPIC_REFERENCE = re.compile(r'get_agent_file topic \\?"([a-z_0-9]+)\\?"')
# A data: URI reachable from a tool listing. The icons were introduced as one helper
# call and nothing about their size showed up at the call site.
DATA_URI = re.compile(r'"data:[a-z]+/')


def literal_text(source: str) -> str:
    """Join C++ adjacent string literals into the text the compiler would produce."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', source)
    return "".join(parts).replace('\\"', '"').replace("\\n", "\n")


def header_sources(repo_root: Path) -> str:
    """All tool sources concatenated — for patterns that span a registration."""
    return "\n".join(p.read_text(encoding="utf-8")
                     for glob in TOOLS_GLOBS for p in sorted(repo_root.glob(glob)))


def scan(repo_root: Path):
    tools = []          # (name, description, file, line, composed)
    properties = []     # (text, file, line)
    data_uris = []      # (file, line)
    actions = []        # (family, action, category, file, line)

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

        # Attribute each action to the QVector<McpToolAction> block it sits in, so a
        # duplicate is reported against its own family rather than the whole file.
        family_starts = [(m.start(), m.group(1)) for m in ACTION_TOOL_START.finditer(source)]
        for match in ACTION_BUILDER.finditer(source):
            line = source.count("\n", 0, match.start()) + 1
            family = next((name for pos, name in reversed(family_starts)
                           if pos < match.start()), "<unknown>")
            actions.append((family, match.group(1), match.group(2), rel, line))

    return tools, properties, data_uris, actions


def surface_fingerprint(tools, actions, bindings) -> str:
    """A stable digest of what a client would see in `tools/list`.

    Tool names, plus each merged tool's action names keyed by the TOOL — not by the
    C++ variable holding the vector, which a rename would churn for no reason. Not
    the descriptions: prose is edited constantly and a client's cached LIST is what
    goes stale, not its wording.
    """
    by_tool = {}
    for family, action, _category, _path, _line in actions:
        by_tool.setdefault(bindings.get(family, family), set()).add(action)
    lines = sorted(name for name, _d, _p, _l, _c in tools)
    lines += [f"{tool}:{','.join(sorted(verbs))}" for tool, verbs in sorted(by_tool.items())]
    return hashlib.sha256("\n".join(lines).encode()).hexdigest()[:12]


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
    tools, properties, data_uris, actions = scan(repo_root)

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

    for family, action, category, path, line in actions:
        if category not in VALID_CATEGORIES:
            violations.append(
                f"{path}:{line}: action `{family}.{action}` declares category \"{category}\", "
                f"which is not one of {sorted(VALID_CATEGORIES)}. An unrecognised category is "
                f"denied at every access level, so that verb would never work for anyone."
            )

    seen_actions = {}
    for family, action, _category, path, line in actions:
        key = (family, action)
        if key in seen_actions:
            violations.append(
                f"{path}:{line}: action `{action}` is declared twice in `{family}`. The second "
                f"is dead — every reader resolves the first — and nothing reports it at runtime."
            )
        seen_actions[key] = line

    tool_names = {name for name, _desc, _path, _line, _composed in tools}
    server = (repo_root / "src/mcp/mcpserver.cpp").read_text(encoding="utf-8")
    for match in CONFIRM_NAME.finditer(server):
        if match.group(1) in tool_names:
            continue
        line = server.count("\n", 0, match.start()) + 1
        violations.append(
            f"src/mcp/mcpserver.cpp:{line}: the confirmation list names `{match.group(1)}`, which "
            f"is not a registered tool. If it became a verb of a merged tool, its confirmation "
            f"wording belongs on that action; if it is gone, so is this line."
        )

    topic_dir = repo_root / "resources/ai/tools"
    qrc = (repo_root / "resources/ai.qrc").read_text(encoding="utf-8")
    topics_on_disk = {p.stem for p in topic_dir.glob("*.md")}
    for topic in sorted(topics_on_disk):
        if f"ai/tools/{topic}.md" not in qrc:
            violations.append(
                f"resources/ai.qrc: resources/ai/tools/{topic}.md is not listed, so the topic "
                f"does not exist at runtime — get_agent_file reports it as an unknown topic."
            )
    for name, desc, path, line, _composed in tools:
        for match in TOPIC_REFERENCE.finditer(desc):
            if match.group(1) not in topics_on_disk:
                violations.append(
                    f"{path}:{line}: `{name}` points at get_agent_file topic "
                    f"\"{match.group(1)}\", but resources/ai/tools/{match.group(1)}.md does "
                    f"not exist."
                )

    header = (repo_root / "src/mcp/mcpserver.h").read_text(encoding="utf-8")
    bindings = {var: tool for tool, var in ACTION_TOOL_BINDING.findall(header_sources(repo_root))}
    fingerprint = surface_fingerprint(tools, actions, bindings)
    recorded = SURFACE_FINGERPRINT.search(header)
    version = SURFACE_VERSION.search(header)
    if not recorded or not version:
        violations.append("src/mcp/mcpserver.h: McpSurfaceVersion / McpSurfaceFingerprint not found")
    elif recorded.group(1) != fingerprint:
        violations.append(
            f"src/mcp/mcpserver.h: the tool surface changed but McpSurfaceVersion is still "
            f"\"{version.group(1)}\". Bump it and set McpSurfaceFingerprint to \"{fingerprint}\". "
            f"A client caches the tool list from initialize and refreshes only on reconnect, so "
            f"without a version change there is nothing to tell a stale session from a live one."
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
            f"~{payload / 1024:.1f} KB estimated tools/list payload, "
            f"surface {fingerprint}"
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
