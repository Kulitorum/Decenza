## Why

Both major cloud MCP connectors open a fresh MCP session on nearly every request — they re-run `initialize` and never echo the `Mcp-Session-Id` header back. Live logs confirm this for **both** `client="openai-mcp"` (ChatGPT) and `client="Anthropic/ClaudeAI"` (the claude.ai connector); the Claude connector is in fact more stateless still, never even opening an SSE stream. This per-request re-initialization is a documented (and repeatedly-regressed) departure from the MCP Streamable HTTP transport rules. Because Decenza allocates and retains a durable session slot for every `initialize`, a short burst of activity from either connector — and especially both at once — fills the 8-slot pool, after which every subsequent request is rejected with `-32000 "Too many sessions"` until the 5-minute orphan reaper catches up. The MCP ecosystem's converged best practice is *stateless-first*: only hold per-client session state for clients that actually need server→client push (an open SSE stream); everything else should be served without consuming a durable slot.

## What Changes

- Make the tool-calling POST path effectively **stateless**: a header-less `initialize` that never opens an SSE stream no longer pins a durable session slot, so per-call re-initializing clients (ChatGPT) cannot exhaust the pool.
- Scope the `MaxSessions` concurrency cap to **stateful (SSE-backed) sessions only** — the sessions that genuinely require retained server-side state for notifications. Ephemeral POST-only sessions are not counted and are reaped promptly.
- Preserve full stateful behavior for clients that subscribe via SSE (the dial-in push flow using `decenza://shots/recent` etc.): they keep a durable session, their capabilities, and their subscriptions.
- Keep all existing access-level, confirmation, origin, protocol-version, and rate-limit gates unchanged — this is a session-lifecycle change only, not a security-surface change.
- No breaking changes to any tool, resource, or the wire protocol; MCP clients continue to speak the same handshake.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `mcp-server`: Add requirements defining session lifecycle so that only SSE-backed sessions are stateful and counted toward the concurrency limit, while header-less non-subscribing clients are served statelessly and cannot exhaust the session pool.

## Impact

- **Code**: `src/mcp/mcpserver.cpp` / `mcpserver.h` — session creation/lookup (`findOrCreateSession`, `createSession`, `handleHttpRequest`), the `MaxSessions` accounting, and the orphan/expiry reaping logic. `src/mcp/mcpsession.*` may gain a stateful/ephemeral distinction (e.g. keyed on whether an SSE stream was established).
- **Behavior**: Fixes the "Too many sessions" wedge observed when the ChatGPT (`openai-mcp`) and/or claude.ai (`Anthropic/ClaudeAI`) connectors churn sessions — individually or, worse, together. LAN `mcp-remote` / Claude Desktop, which holds a persistent SSE stream, keeps full stateful behavior.
- **Docs**: `docs/CLAUDE_MD/MCP_SERVER.md` session/limits section updated to describe stateless handling of non-SSE clients.
- **Risk to verify**: some reports indicate ChatGPT may trigger an auth flow against a fully stateless server. Because Decenza's remote surface is no-auth (capability URL), the change must be validated live through the ChatGPT connector to confirm it still connects without demanding OAuth.
- **Tests**: MCP session unit tests (session reuse, orphan reaping, cap enforcement) updated to cover the stateful-vs-ephemeral split.
