## Why

Three findings from one overnight window on the maintainer's tablet, all in the remote MCP connector.

**The log cannot name who called.** In Tailscale mode the listener binds loopback and the embedded tsnet node proxies the public Funnel into it, so every remote client arrives as `127.0.0.1`. The rejection line reported that address verbatim. A burst of fifteen unauthorized requests at 02:30 was nearly filed as the maintainer's own testing on the strength of it — the standing heuristic for a loopback source. The same collapse keys the failed-token limiter and the modern era's control-call limiter, so one bucket holds every public caller.

**A rejected caller could write to the log without limit that matters.** The budget was twenty failures per source per minute, each one a warning, plus a suppression line: twenty-one lines a minute, ~30k a day, into a fixed-size buffer that every other subsystem shares. Nothing about attempt #4 is more actionable than attempt #1 — a valid client never fails the token check — so the budget was buying a scanner nineteen extra guesses and nineteen extra lines.

**A bare 404 confirms the service exists.** Authorization here is a capability URL, so a wrong token is a guess; answering it tells the guesser the hostname fronts something worth guessing at again.

## What Changes

- **A tunnel-proxied caller is named `Funnel (public internet)`**, in the connector's own log lines and in the modern rate limiter's key and refusal. Computed once by the listener — the only party that knows whether it is tunnel-proxied — and passed to `McpServer`, so the label a reader sees and the bucket a budget counts cannot drift. Mode C keeps the raw peer address, where it is a real peer.
- **Behind an embedded tunnel, an unauthenticated caller gets no reply at all** and the socket closes: bad token, unterminated headers, oversized request, malformed `Content-Length`. One policy for all four sites. Mode C keeps the bare 404 and its keep-alive, where the reply goes to the user's own reverse proxy. **This does not make the endpoint invisible** — the Funnel edge terminates TLS and serves its own error for a backend that hangs up — and must not be described as if it did. What stops is the app confirming anything.
- **The failed-token budget drops from 20/minute to 3**, and sustained rejection stops costing a line per request: the first few, one transition line, then decimal milestones carrying the running count. Worst case seven lines a minute instead of twenty-one, and a submitted log still separates one stray probe from a scanner at thousands a minute.

## Capabilities

### New Capabilities

_None — every behaviour here refines the existing remote-access capability._

### Modified Capabilities

- `mcp-server`: the refusal given to an unauthenticated remote caller (silence behind a tunnel, 404 in Mode C), and the requirement that a caller be identified by something other than a proxied loopback address.

## Impact

- **Code**: `src/mcp/mcpremoteaccess.{h,cpp}` (source labelling, one refusal policy, smaller budget, bounded logging), `src/mcp/mcpratewindow.h` (`countInWindow`), `src/mcp/mcpserver.{h,cpp}` (caller label threaded to the modern control limiter).
- **Behaviour**: a Mode A client with a bad token now sees a dropped connection where it saw a 404. No change for a client holding a valid token, and none at all in Mode C or on the LAN surface.
- **Logs**: `[MCP][RemoteAccess]` rejection lines change wording and source, and stop repeating per request.
