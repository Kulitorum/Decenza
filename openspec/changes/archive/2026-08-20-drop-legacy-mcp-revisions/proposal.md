## Why

`supportedProtocolVersions()` advertises four MCP revisions. Two of them have
never been asked for, and one of those two is the sole reason a whole dispatch
path exists.

Measured from the device log — 50 `initialize` handshakes across several app
runs, every real client this server has:

| Client | Requests | Count |
|---|---|---|
| `claude-code` v2.1.229–234 | `2025-11-25` | 38 |
| `Anthropic/ClaudeAI` (the claude.ai connector) | `2025-11-25` | 9 |
| `codex-mcp-client` v0.147.0-alpha.6.5 | `2025-06-18` | 3 |

Zero requests for `2025-03-26`. Zero for `2024-11-05`.

The ecosystem agrees: the official conformance suite does not know
`2024-11-05` at all — it is rejected as an unknown `--spec-version`, valid
values being `2025-03-26`, `2025-06-18`, `2025-11-25`, `draft`, `extension` —
and `2025-03-26` selects zero scenarios. So two of the four revisions we
advertise are unverifiable by the protocol's own tooling, and advertising a
revision is a claim to implement it.

`2025-06-18` stays. It is one revision behind, and Codex is on it.

## What Changes

- **`2024-11-05` and `2025-03-26` are removed from
  `supportedProtocolVersions()`.** The supported set becomes `2025-11-25`
  (preferred) and `2025-06-18`.
- **JSON-RPC batch dispatch is deleted.** Batching is required by exactly one
  revision — `2025-03-26`, whose base protocol says implementations "MUST
  support receiving JSON-RPC batches". It does not exist in `2024-11-05`, was
  removed in `2025-06-18`, stays absent from `2025-11-25`, and is still absent
  in `2026-07-28`. With `2025-03-26` gone, no revision this server serves
  defines it. A POST body that is a JSON array is answered with an explicit
  error rather than processed.
- **Version-gated branches whose old arm is now unreachable are collapsed.**
  `instructions` at `initialize`, and `title` on tools and resources, become
  unconditional: every revision still served defines them.
- **The header-absent assumption moves to `2025-06-18`.** The protocol says a
  server receiving no `MCP-Protocol-Version` should assume `2025-03-26`. We
  cannot assume a version we no longer serve, so the session default becomes
  the new floor. This is a deliberate, documented deviation.

## Impact

- `src/mcp/mcpserver.{h,cpp}` — the supported list, `handleJsonRpcBatch` and
  `willDeferResponse` deleted, the array branch in `handleHttpRequest` replaced
  with a refusal, the `instructions` gate collapsed, version-floor constants
  moved to `2025-06-18`.
- `src/mcp/mcpsession.h` — default protocol version.
- `src/mcp/mcptoolregistry.h`, `src/mcp/mcpresourceregistry.h` — the
  `emitTitle` gate collapsed.
- `tests/tst_mcpserver_protocol.cpp` — batch coverage removed, replaced by one
  test that an array body is refused; version tests updated.
- **Wire-visible.** A client requesting a dropped revision is answered with
  `2025-11-25`, the preferred version, and per the protocol should disconnect if
  it cannot speak it. No client observed here does this, but the risk is real
  and is the reason this lands as its own change rather than folded into other
  work.

## Capabilities

### Modified Capabilities

- `mcp-server`: narrows the advertised protocol-version set, moves the
  header-absent assumption, and removes the batch requirement outright.

## Non-Goals

- **Dropping `2025-06-18`.** Codex negotiates it. It is the one drop with a
  known victim.
- **Simplifying the modern (`2026-07-28`) path.** This does not do that and
  should not be sold as doing it: batch is legacy-only and the modern era has
  none, so the dual-era work is not made easier by this in any direct way. What
  it does is remove the most defect-prone code from the file that work is about
  to be added to.
- **Touching the session machinery.** None of it is version-specific.
