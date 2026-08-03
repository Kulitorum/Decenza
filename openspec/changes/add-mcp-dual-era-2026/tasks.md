## 1. Era-independent wins, shippable on their own

Neither needs a modern client to exist, and both have a payoff today. Land
first so the rest is not holding them hostage.

- [ ] 1.1 `McpToolRegistry::listTools` sorts by tool name. The registry is a `QHash` and Qt randomizes the hash seed per process (`qtbase/src/corelib/tools/qhash.cpp:121-127`, `:178`), so the order is stable within a run and different across restarts — the worst shape for a client cache, because nothing looks wrong until two runs are compared
- [ ] 1.2 Same for `McpResourceRegistry::listResources`
- [ ] 1.3 Test: list twice across two `McpServer` instances, assert identical order. Must be seen RED — and note that a single-instance test CANNOT fail, because the seed is per process
- [ ] 1.4 `ttlMs` + `cacheScope` on `tools/list`, `resources/list`, `resources/read`
- [ ] 1.5 A listing that reflects the caller's access level is NOT `"public"` — 96 tools filtered by `accessLevel` means a shared cache would serve one caller another's tool set
- [ ] 1.6 Gate both on the negotiated version so legacy clients that pre-date these fields do not receive them

## 2. Era detection

- [ ] 2.1 Add the era discriminator in `handleHttpRequest`, before anything else runs. Decisive signal is the modern-only header set (`Mcp-Method`, `Mcp-Name`) plus `_meta.io.modelcontextprotocol/protocolVersion` in the body
- [ ] 2.2 `MCP-Protocol-Version` alone is NOT a discriminator — legacy has sent it since 2025-06-18. Write that at the site; it is the mistake this decision exists to prevent
- [ ] 2.3 Ambiguous → legacy. The failure modes are asymmetric: mis-routing legacy to modern breaks a working client with no recovery, while mis-routing modern to legacy produces the error the modern client's own detection is specified to fall back from
- [ ] 2.4 Test: every existing legacy shape still routes to legacy — `initialize`, a post-handshake `tools/call`, a notification, a batch array, a bare GET for SSE
- [ ] 2.5 **The existing `tst_mcpserver_protocol.cpp` and `tst_mcpserver_session.cpp` must pass UNTOUCHED.** A diff to either is evidence the legacy path moved, not that a test needed updating

## 3. Modern request path — read-category only

- [ ] 3.1 Route a modern request to the shared dispatch layer (`handleJsonRpc` and below) without creating a session. Only the envelope forks: version resolution, session lookup, response framing
- [ ] 3.2 If a fix has to be made in two places, the fork is in the wrong place — say so at the fork
- [ ] 3.3 Read protocol version per request; reject a request whose header and body disagree (`-32020`)
- [ ] 3.4 `UnsupportedProtocolVersionError` (`-32022`) carrying `supported` and `requested`
- [ ] 3.5 Resource-not-found is `-32602` in the modern era; `-32002` stays for legacy. Both correct, per revision
- [ ] 3.6 **Refuse control- and settings-category tools on the modern path** until section 5 lands. Structural, so the unsafe state cannot be reached by forgetting
- [ ] 3.7 Tests: a modern read tool works; a modern control tool is refused; header/body mismatch is rejected; an unsupported version returns the supported list

## 4. `server/discover`

- [ ] 4.1 Implement it — a server MUST, a client MAY call it
- [ ] 4.2 Test BOTH client routes: discovery up front, and inline invocation that hits `-32022` and retries. The second is the likelier one and would be the easier to leave uncovered
- [ ] 4.3 Assert the advertised list and the served versions are the same list, not two lists that happen to agree today

## 5. Session-independent rate limiting — the gate

- [ ] 5.1 Decide the key. Remote route has a token and is unambiguous; ShotServer's route has the peer address, which collapses a whole LAN behind one NAT to a single key. Decide against real client shapes, not in the abstract (design Open Question)
- [ ] 5.2 Implement per-caller, not global — one caller must not be able to starve another, which is what the per-session counter was shaped to avoid
- [ ] 5.3 Open control/settings categories on the modern path only once 5.1-5.2 are done
- [ ] 5.4 Tests: a caller over the limit is refused; a second caller is unaffected
- [ ] 5.5 Leave `McpSession::controlCallCount()` alone — legacy still uses it

## 6. Confirmation-gated tools in the modern era

- [ ] 6.1 `m_pendingConfirmation` stores a `sessionId` to route the answer back, and a modern caller has none. Either refuse those tools (defensible — they are the `machine_start_*` family) or give the gate a request-scoped handle
- [ ] 6.2 Whichever is chosen, a confirmation-gated tool must NEVER be served without confirmation. Refused with an error saying why is acceptable; executed is not
- [ ] 6.3 Test the refusal explicitly — this is the one place where "not implemented yet" and "silently ungated" look the same from outside

## 7. `subscriptions/listen`

Sequenced last on purpose: it is the only piece needing new socket handling,
and a modern client without it degrades to polling rather than breaking.

- [ ] 7.1 Long-lived POST stream. ShotServer holds a socket open for SSE on GET today and has not seen this shape — check its socket handling before assuming it transfers
- [ ] 7.2 Carry the three existing broadcasts (`machine/state`, `profiles/active`, `shots/recent`)
- [ ] 7.3 Legacy GET stream untouched
- [ ] 7.4 Do NOT implement stream resumability or `Last-Event-ID` replay — removed outright in this revision, and we already declined it

## 8. Advertise the version

- [ ] 8.1 Add `2026-07-28` to `supportedProtocolVersions()` — **last**. That list is what `server/discover` promises; adding it earlier makes the server lie
- [ ] 8.2 Confirm the legacy negotiation still picks a legacy version for a legacy client rather than the new highest

## 9. Verify

- [ ] 9.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask first, Qt Creator is shared
- [ ] 9.2 Break each fix in turn and confirm its test goes red
- [ ] 9.3 Live-test over BOTH callers — ShotServer AND `McpRemoteAccess`. The remote route is a separate entry that has been missed before, and a legacy regression there would be invisible to the local one
- [ ] 9.4 Exercise a real legacy client end to end (Claude Desktop or the claude.ai connector) and confirm nothing changed for it. This is the assertion the whole change rests on
- [ ] 9.5 Be honest in the PR body that no real MODERN client has exercised any of this — it ships dark, and the test suite is the only evidence. Do not describe it the way the legacy path can now be described
- [ ] 9.6 Read the `text-invariants.yml` PR run — it gates `src/**` and nothing blocks a merge on it

## 10. Document

- [ ] 10.1 `docs/CLAUDE_MD/MCP_SERVER.md`: which era a client gets and how that is decided, and what a contributor must do to a new tool so it works in both
- [ ] 10.2 Record that the session machinery is NOT deleted by this change and why — it is legacy's, and legacy stays. Written down so it is not re-litigated every time that code annoys someone
- [ ] 10.3 Record the answer to "when do we drop legacy": when no client needs it, backed by the revision's twelve-month minimum deprecation windows
- [ ] 10.4 No wiki manual change — MCP protocol eras are not a user-visible app surface
- [ ] 10.5 Open the PR, then run `/pr-review-toolkit:review-pr`
- [ ] 10.6 Archive the change + spec sync as the final commit on the same PR
