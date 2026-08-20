This lands as **two pull requests**. The first is sections 0-4 and the parts of
9-10 that cover them: legacy conformance, era detection, the modern read path,
`server/discover`, cacheable results, deterministic resource ordering. Nothing
in it is advertised, so the modern half ships dark. The second is sections 5-8:
the rate limiter, the confirmation gate, `subscriptions/listen`, and finally
`2026-07-28` in `supportedProtocolVersions()`.

## 0. Legacy conformance — brought to green, not merely measured

Sequenced FIRST, before any modern code exists. Two reasons, and the second is
the one that matters: a legacy fix landed after the modern path exists cannot be
told apart from a modern regression, and the whole change rests on being able to
say legacy did not move.

We have never run a conformance suite against this server. Four revisions are
advertised in `supportedProtocolVersions()`, and advertising a revision is a
claim to implement it — a claim nothing has ever checked.

- [ ] 0.1 Run `npx @modelcontextprotocol/conformance server --url <url>` against the app as it is on `main`, for every advertised revision (`2025-11-25`, `2025-06-18`, `2025-03-26`, `2024-11-05`). Capture the full output before changing a line
- [ ] 0.2 Triage every failure into one of two piles, in writing: **defect** (we are wrong and did not mean to be) or **deliberate deviation** (we are wrong and there is a written reason at the site)
- [ ] 0.3 Fix the defects. This is the part that changes legacy behaviour on purpose, and the reason the "byte-identical" goal is now stated as "identical except where it disagreed with the spec it claims to implement" — see design.md
- [ ] 0.4 For each deliberate deviation, **re-derive the reason rather than defending it**. Several are documented in `mcpserver.cpp` and at least one must survive: the auto-recovery branch is more permissive than the spec on purpose, because `mcp-remote` cannot re-initialize itself and 404ing it leaves a client "permanently broken until restart". Conformance flagging that is conformance being right about the spec and wrong about the client, and the deviation stays
- [ ] 0.5 Known candidates to expect in the flagged pile, so they are recognised rather than rediscovered: the auto-recovery branch; reaper- and eviction-terminated sessions not being tombstoned (`m_terminatedSessions` documents why only DELETE records); no `Last-Event-ID` replay (a MAY); SSE event IDs not encoding their originating stream (a 2025-11-25 SHOULD, deliberately unmet); batch accepted at every revision rather than only `2025-03-26`
- [ ] 0.6 Record the verdict for every deviation kept — one line each, at the site, saying conformance flags it and why it stays. A deviation nobody re-derives is how a bad decision keeps getting re-approved
- [ ] 0.7 Re-run all four revisions and record the result. Anything still red is either a kept deviation with a written reason or an unfinished task; there is no third pile
- [x] 0.7a Baseline checked in at `tests/conformance/expected-failures.yaml`, listing only the two piles that cannot pass: the suite's fixture surface (it drives a reference server exposing `test_*` tools and `test://` resources) and capabilities we never declare (`prompts`, `completions`, `logging`). Run with `--expected-failures` and the result becomes green-or-regression rather than a score. It also fails on a STALE entry — one listed that has started passing — so the file cannot rot
- [x] 0.7b Fixture tools were considered and rejected: two of the fourteen would exercise existing code, the rest mean building content kinds we never emit, a progress feature we lack, and Sampling/Elicitation, which we declined and 2026-07-28 deprecates. They would also consume the `tools/list` budget every real client fetches and truncates
- [ ] 0.8 `tst_mcpserver_protocol.cpp` and `tst_mcpserver_session.cpp` may legitimately change **here and only here**. A test asserting behaviour the spec contradicts was asserting our bug. Name the conformance requirement that drove each edit in the commit; an edit with no such citation is the thing 2.5 forbids

## 1. Era-independent wins, shippable on their own

Neither needs a modern client to exist, and both have a payoff today. Land
first so the rest is not holding them hostage.

- [x] 1.1 `McpToolRegistry::listTools` sorts by tool name — **already done**, and by `(tier, name)` rather than name alone, which is stronger. Landed with the tool-budget change for a different reason (a truncating client should always lose the same niche tail); the cache-stability argument below is a second reason for the same code. Nothing to do
- [ ] 1.2 Same for `McpResourceRegistry::listResources`, which is still raw `QHash` order. The registry is a `QHash` and Qt randomizes the hash seed per process (`qtbase/src/corelib/tools/qhash.cpp:121-127`, `:178`), so the order is stable within a run and different across restarts — the worst shape for a client cache, because nothing looks wrong until two runs are compared
- [ ] 1.3 Test: list resources twice across two `McpResourceRegistry` instances, assert identical order. Must be seen RED — and note that a single-instance test CANNOT fail, because the seed is per process. Tools are already covered by the ordering assertions that came with the tier sort
- [ ] 1.4 `ttlMs` + `cacheScope` on `tools/list`, `resources/list`, `resources/read`. **Required fields in the modern era, not optional** — the revision defines a `CacheableResult` interface and every one of those results carries it. (`prompts/list` and `resources/templates/list` are in the same list; we serve neither)
- [ ] 1.5 A listing that reflects the caller's access level is NOT `"public"` — 96 tools filtered by `accessLevel` means a shared cache would serve one caller another's tool set
- [ ] 1.6 Gate both on the negotiated version so legacy clients that pre-date these fields do not receive them

## 1b. Build against the schema, not against this document

- [ ] 1b.1 Pull `schema/2026-07-28/schema.json` from `modelcontextprotocol/modelcontextprotocol` and work from it. Where it and these artifacts disagree, it wins — the proposal and design were drafted from the revision's prose
- [ ] 1b.2 Read `signal-slot/qtmcp` (Qt-native, GPL-3.0-only arm matches ours) and `GopherSecurity/gopher-mcp` (Apache-2.0) for how they shaped era detection and `subscriptions/listen`. Reference only — neither is adopted, and the reason is in design.md
- [ ] 1b.3 Do NOT add an MCP library as a dependency. There is no official C++ SDK, and the third-party ones own the listener and HTTP layer this server cannot delegate

## 2. Era detection

- [ ] 2.1 Add the era discriminator in `handleHttpRequest`, before anything else runs. Decisive signal is the modern-only header set (`Mcp-Method`, `Mcp-Name`) plus `_meta.io.modelcontextprotocol/protocolVersion` in the body
- [ ] 2.2 `MCP-Protocol-Version` alone is NOT a discriminator — legacy has sent it since 2025-06-18. Write that at the site; it is the mistake this decision exists to prevent
- [ ] 2.3 Ambiguous → legacy. The failure modes are asymmetric: mis-routing legacy to modern breaks a working client with no recovery, while mis-routing modern to legacy produces the error the modern client's own detection is specified to fall back from
- [ ] 2.4 Test: every existing legacy shape still routes to legacy — `initialize`, a post-handshake `tools/call`, a notification, a batch array, a bare GET for SSE
- [ ] 2.5 **From this section onward, `tst_mcpserver_protocol.cpp` and `tst_mcpserver_session.cpp` must pass UNTOUCHED.** A diff to either is evidence the legacy path moved, not that a test needed updating. Two carve-outs, both deliberate and both stated where they occur: section 0, where a conformance defect may prove a test was asserting our bug, and section 6, which changes the confirmation mechanism on purpose. Outside those two, the rule is absolute — and note that section 0 lands first precisely so its edits are already in the baseline this rule is measured against

## 3. Modern request path — read-category only

- [ ] 3.1 Route a modern request to the shared dispatch layer (`handleJsonRpc` and below) without creating a session. Only the envelope forks: version resolution, session lookup, response framing
- [ ] 3.2 If a fix has to be made in two places, the fork is in the wrong place — say so at the fork
- [ ] 3.3 Read protocol version per request; reject a request whose header and body disagree (`-32020`)
- [ ] 3.4 `UnsupportedProtocolVersionError` (`-32022`) carrying `supported` and `requested`
- [ ] 3.5 Resource-not-found is `-32602` in the modern era; `-32002` stays for legacy. Both correct, per revision
- [ ] 3.6 **Refuse control- and settings-category tools on the modern path** until section 5 lands. Structural, so the unsafe state cannot be reached by forgetting
- [ ] 3.7 Stamp `resultType` on every modern result. `"complete"` always — `"input_required"` is MRTR, which is a non-goal. Required by the revision on ALL results, so put it where a result cannot leave without it rather than at each handler
- [ ] 3.8 Emit `io.modelcontextprotocol/serverInfo` in each modern result's `_meta`. Same identity the legacy handshake reports (`McpSurfaceVersion` + app version); a stateless client has no handshake to learn it from
- [ ] 3.9 `ping`, `logging/setLevel` and `notifications/roots/list_changed` are removed in the modern era — they reach the shared dispatch as unknown methods. Legacy keeps all three, including `ping`'s exemption from the not-initialized check in `resolveSessionForMessage`, which is legacy-only by construction
- [ ] 3.10 Tests: a modern read tool works; a modern control tool is refused; header/body mismatch is rejected; an unsupported version returns the supported list; every modern result carries `resultType`; `ping` is unknown to a modern caller and still served to a legacy one

## 4. `server/discover`

- [ ] 4.1 Implement it — a server MUST, a client MAY call it
- [ ] 4.2 Test BOTH client routes: discovery up front, and inline invocation that hits `-32022` and retries. The second is the likelier one and would be the easier to leave uncovered
- [ ] 4.3 Assert the advertised list and the served versions are the same list, not two lists that happen to agree today

## 5. Session-independent rate limiting — the gate

- [x] 5.1 Key decided: **peer address**, both routes, per-minute window. Not the remote-access token — there is one token for the whole app, so it would produce a global limiter for the entire remote route. Reasoning in design.md
- [ ] 5.2 Implement per-caller, not global — one caller must not be able to starve another, which is what the per-session counter was shaped to avoid. `McpRemoteAccess::failedTokenOverLimit` is the existing per-peer-address per-minute window in this codebase; follow its shape rather than inventing a second one, and extract if the two turn out to be the same thing
- [ ] 5.3 Open control/settings categories on the modern path only once 5.1-5.2 are done
- [ ] 5.4 Tests: a caller over the limit is refused; a second caller is unaffected
- [ ] 5.5 Leave `McpSession::controlCallCount()` alone — legacy still uses it

## 6. Confirmation-gated tools in the modern era

Decided: the gate gets its own identity and **legacy moves onto it too** — no
parallel mechanism. Refusing modern callers was considered and rejected as a
destination: the end state of this plan is legacy dropped, and on that day
refusal would make `machine_start_*` unreachable to every client. Reasoning in
design.md.

- [ ] 6.1 `PendingConfirmation` mints its own `confirmationId`. `confirmationRequested` carries it, QML echoes it back, `confirmationResolved` matches on it. Note what this replaces: the `sessionId` there is not a session lookup and never was — nothing calls `findSession` on it
- [ ] 6.2 `sessionId` stays on the struct but narrows to what it honestly is — which legacy session owns this, for the response header and for the three reaper/DELETE abandon sites. Empty for a modern caller, and the response for one carries no `Mcp-Session-Id`
- [ ] 6.3 Update `qml/main.qml` (the `confirmationResolved` call sites) in the same edit. The parameter is renamed, not just re-typed; a stale `sessionId` name there would read as a session for the next person
- [ ] 6.4 Abandon the pending confirmation on `QTcpSocket::disconnected`, both eras. Event-based, not a timer. It is the only backstop the modern era can have, and for legacy it replaces "noticed up to 30 minutes later by the session reaper" with "noticed immediately"
- [ ] 6.5 A confirmation-gated tool must NEVER be served without confirmation. That invariant does not move
- [ ] 6.6 Test: a modern caller's confirmation round-trips and the tool runs; a modern caller's confirmation is abandoned when its socket drops; a legacy caller's behaviour is unchanged end to end
- [ ] 6.7 `tst_mcpserver_session.cpp` may need updating here — this section deliberately changes a mechanism legacy uses. Say so in the PR rather than editing it quietly. The untouched-tests rule of 2.5 applies in full to the first PR

## 7. `subscriptions/listen`

Sequenced last on purpose: it is the only piece needing new socket handling,
and a modern client without it degrades to polling rather than breaking.

- [ ] 7.1 Long-lived POST stream. ShotServer holds a socket open for SSE on GET today and has not seen this shape — check its socket handling before assuming it transfers
- [ ] 7.2 It is an OPT-IN mechanism, not a transport swap for the GET stream. The client names the notification types it wants (`toolsListChanged`, `promptsListChanged`, `resourcesListChanged`, `resourceSubscriptions`), the server acknowledges, and each notification carries `io.modelcontextprotocol/subscriptionId`. Carry the three existing broadcasts (`machine/state`, `profiles/active`, `shots/recent`) through it
- [ ] 7.3 `resources/subscribe` and `resources/unsubscribe` are REPLACED by this method, not merely supplemented — they do not exist for a modern caller. Legacy keeps both, and keeps the GET stream, untouched
- [ ] 7.4 Request-scoped notifications (`notifications/progress`, `notifications/message`) do NOT belong on this stream — they flow on the response stream of the request they relate to. Also: never emit `notifications/message` for a request that did not carry `io.modelcontextprotocol/logLevel`
- [ ] 7.5 Do NOT implement stream resumability or `Last-Event-ID` replay — removed outright in this revision, and we already declined it. Our legacy GET stream keeps its event IDs and `retry`, which is correct for legacy and must not leak into the modern stream

## 8. Advertise the version

- [ ] 8.1 Add `2026-07-28` to `supportedProtocolVersions()` — **last**. That list is what `server/discover` promises; adding it earlier makes the server lie
- [ ] 8.2 Confirm the legacy negotiation still picks a legacy version for a legacy client rather than the new highest

## 9. Verify

- [ ] 9.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask first, Qt Creator is shared
- [ ] 9.2 Break each fix in turn and confirm its test goes red
- [ ] 9.2a **Run the official conformance suite against a running app**, both PRs: `npx @modelcontextprotocol/conformance server --url http://<host>:<port>/mcp`. It drives the server over HTTP and needs no C++ SDK. Passing its `2026-07-28` requirement set is what "done" means for the second PR
- [ ] 9.2b Legacy conformance is section 0's job, not a measurement taken here. Re-run all four legacy revisions at the end of each PR and confirm nothing regressed against section 0's recorded end state
- [ ] 9.2c Record every result in the PR body: which requirement sets ran, what passed, and every failure left standing with the reason it stays. A summarised "conformance passes" is exactly the claim this suite exists to make checkable
- [ ] 9.2d Run it over the REMOTE route as well as ShotServer's. Conformance points at a URL, and the tokenized route is a different URL — pointing it only at the local one leaves the route that has been missed before unmeasured again
- [ ] 9.3 Live-test over BOTH callers — ShotServer AND `McpRemoteAccess`. The remote route is a separate entry that has been missed before, and a legacy regression there would be invisible to the local one
- [ ] 9.4 Exercise a real legacy client end to end (Claude Desktop or the claude.ai connector) and confirm nothing changed for it. This is the assertion the whole change rests on
- [ ] 9.5 Be honest in the PR body that no real MODERN client has exercised any of this — it ships dark. The conformance suite is stronger evidence than our own tests, since ours only re-assert our own reading of the spec, but it is still not a real client. Do not describe this the way the legacy path can now be described
- [ ] 9.6 Read the `text-invariants.yml` PR run — it gates `src/**` and nothing blocks a merge on it

## 10. Document

- [ ] 10.1 `docs/CLAUDE_MD/MCP_SERVER.md`: which era a client gets and how that is decided, and what a contributor must do to a new tool so it works in both
- [ ] 10.2 Record that the session machinery is NOT deleted by this change and why — it is legacy's, and legacy stays. Written down so it is not re-litigated every time that code annoys someone
- [ ] 10.3 Record the answer to "when do we drop legacy": when no client needs it, backed by the revision's twelve-month minimum deprecation windows
- [ ] 10.4 No wiki manual change — MCP protocol eras are not a user-visible app surface
- [ ] 10.5 Open the PR, then run `/pr-review-toolkit:review-pr`
- [ ] 10.6 Archive the change + spec sync as the final commit on the same PR
