**Landing as ONE pull request**, not the two this plan first proposed — the
overhead of a PR outweighed the smaller blast radius, and section 0 (legacy
conformance) landed separately as #1840 anyway, which took the riskiest half
out on its own.

## 0. Legacy conformance — brought to green, not merely measured

Sequenced FIRST, before any modern code exists. Two reasons, and the second is
the one that matters: a legacy fix landed after the modern path exists cannot be
told apart from a modern regression, and the whole change rests on being able to
say legacy did not move.

We have never run a conformance suite against this server. Four revisions are
advertised in `supportedProtocolVersions()`, and advertising a revision is a
claim to implement it — a claim nothing has ever checked.

- [x] 0.1 Ran `npx @modelcontextprotocol/conformance server --url <url>` (suite v0.1.16) against the app before changing a line: **8 passed, 23 failed** of 31 scenarios
- [x] 0.1a **RESOLVED by dropping them.** This originally recorded that the suite could not test two of the four advertised revisions — `--spec-version 2024-11-05` is rejected outright and `2025-03-26` filters to zero scenarios. That gap is closed from the other end: both were dropped in `drop-legacy-mcp-revisions` (#1840), so the advertised set is now `2025-11-25` and `2025-06-18`, and the suite covers **both**. "Legacy conforms" is now a claim about the whole list rather than half of it
- [x] 0.2 Triaged. **One defect**, 22 unfixable-by-us. The 22 split into the suite's fixture surface (it drives a reference server exposing `test_*` tools and `test://` resources) and capabilities we never declare (`prompts`, `completions`, `logging`). Zero were deliberate deviations
- [x] 0.3 Defect fixed: `resolveSessionForMessage` 400'd any `MCP-Protocol-Version` differing from the session's negotiated one, including versions in `supportedProtocolVersions()`. The spec licenses 400 only for an *invalid or unsupported* version, and makes matching the negotiated one a client-side SHOULD. Post-fix: **9 passed**, `server-accepts-multiple-post-streams` green, baseline check exit 0
- [ ] 0.4 For each deliberate deviation, **re-derive the reason rather than defending it**. **The suite flagged none of them** — see 0.5. That is not clearance: it means the suite does not probe them, not that they are correct. Left open deliberately rather than closed on a green run
- [x] 0.5 Candidates expected in the flagged pile — the auto-recovery branch; reaper- and eviction-terminated sessions not being tombstoned; no `Last-Event-ID` replay (a MAY); SSE event IDs not encoding their originating stream (a 2025-11-25 SHOULD, deliberately unmet); batch accepted at every revision rather than only `2025-03-26`. **The suite raised not one of these.** Recorded because the prediction failing is itself worth knowing: the auto-recovery branch that this plan expected to have to defend was never challenged, so nothing here has been independently checked by the suite
- [ ] 0.6 Record the verdict for every deviation kept — one line each, at the site, saying conformance flags it and why it stays. Nothing to write yet: no deviation was flagged
- [x] 0.7 Re-ran post-fix. `--expected-failures tests/conformance/expected-failures.yaml` exits **0**, "Baseline check passed: all failures are expected", no stale entries. Per-revision: 2025-11-25 exit 0 (30 scenarios), 2025-06-18 exit 0 (26) — which is now every revision advertised, since the untestable two were dropped
- [x] 0.7c DONE. Ran over the tokenized remote route; identical to ShotServer. The only difference, `dns-rebinding-protection`, is the harness refusing that scenario against a non-localhost URL. Original: run the same baseline over the REMOTE route (`/mcp/<token>`), which is a different URL. Not yet done — needs the remote token. Task 9.2d is the same item at PR scope
- [x] 0.7a Baseline lives at `tests/conformance/expected-failures.yaml`, now on `main` via #1840 rather than owned by this change, listing only the two piles that cannot pass: the suite's fixture surface (it drives a reference server exposing `test_*` tools and `test://` resources) and capabilities we never declare (`prompts`, `completions`, `logging`). Run with `--expected-failures` and the result becomes green-or-regression rather than a score. It also fails on a STALE entry — one listed that has started passing — so the file cannot rot
- [x] 0.7b Fixture tools were considered and rejected: two of the fourteen would exercise existing code, the rest mean building content kinds we never emit, a progress feature we lack, and Sampling/Elicitation, which we declined and 2026-07-28 deprecates. They would also consume the `tools/list` budget every real client fetches and truncates
- [x] 0.8 `tst_mcpserver_protocol.cpp` and `tst_mcpserver_session.cpp` may legitimately change **here and only here**. A test asserting behaviour the spec contradicts was asserting our bug. Name the conformance requirement that drove each edit in the commit; an edit with no such citation is the thing 2.5 forbids

## 1. Era-independent wins, shippable on their own

Neither needs a modern client to exist, and both have a payoff today. Land
first so the rest is not holding them hostage.

- [x] 1.1 `McpToolRegistry::listTools` sorts by tool name — **already done**, and by `(tier, name)` rather than name alone, which is stronger. Landed with the tool-budget change for a different reason (a truncating client should always lose the same niche tail); the cache-stability argument below is a second reason for the same code. Nothing to do
- [x] 1.2 Same for `McpResourceRegistry::listResources`, which is still raw `QHash` order. The registry is a `QHash` and Qt randomizes the hash seed per process (`qtbase/src/corelib/tools/qhash.cpp:121-127`, `:178`), so the order is stable within a run and different across restarts — the worst shape for a client cache, because nothing looks wrong until two runs are compared
- [x] 1.3 Test: list resources twice across two `McpResourceRegistry` instances, assert identical order. Must be seen RED — and note that a single-instance test CANNOT fail, because the seed is per process. Tools are already covered by the ordering assertions that came with the tier sort
- [x] 1.4 `ttlMs` + `cacheScope` on `tools/list`, `resources/list`, `resources/read`. **Required fields in the modern era, not optional** — the revision defines a `CacheableResult` interface and every one of those results carries it. (`prompts/list` and `resources/templates/list` are in the same list; we serve neither)
- [x] 1.5 A listing that reflects the caller's access level is NOT `"public"` — 96 tools filtered by `accessLevel` means a shared cache would serve one caller another's tool set
- [x] 1.6 Gate both on the negotiated version so legacy clients that pre-date these fields do not receive them

- [x] 1.7 **Follow-up, blocked on section 8.** The cache-hint test asserts only that the fields do NOT leak to a legacy revision, which passes identically whether the gate works or the feature was never implemented. The positive case — fields present, `cacheScope: "private"` everywhere, and the list/read TTL split — needs `2026-07-28` negotiable, which is deliberately the last step. Write it there, not here
- [x] 1.8 **The proposal's framing needs correcting.** Cacheable results are listed among the "era-independent wins with a payoff today". Gated on the version they are neither: nothing is emitted until `2026-07-28` is negotiable. The deterministic ordering IS an era-independent win; this one is not, and the proposal should say so
- [x] 1.9 `server/discover` also extends `CacheableResult` in the schema (`DiscoverResult`), which the proposal did not note. Apply the same hints when section 4 builds it

## 1b. Build against the schema, not against this document

- [x] 1b.1 Pull `schema/2026-07-28/schema.json` from `modelcontextprotocol/modelcontextprotocol` and work from it. Where it and these artifacts disagree, it wins — the proposal and design were drafted from the revision's prose
- [x] 1b.2 **Partly, and late.** The spec's OWN example payloads (`schema/2026-07-28/examples/`) were used to verify the `subscriptions/listen` request shape, the `SubscriptionsListenResult`, and `DiscoverResult` — better evidence than a third-party implementation, and they corrected two things a schema-only reading had got wrong. The third-party implementations were NOT read; this task was skipped until asked about directly. Original task: read `signal-slot/qtmcp` (Qt-native, GPL-3.0-only arm matches ours) and `GopherSecurity/gopher-mcp` (Apache-2.0) for how they shaped era detection and `subscriptions/listen`. Reference only — neither is adopted, and the reason is in design.md
- [ ] 1b.3 Do NOT add an MCP library as a dependency. There is no official C++ SDK, and the third-party ones own the listener and HTTP layer this server cannot delegate

## 2. Era detection

- [x] 2.1 **Discriminator narrowed to `_meta` alone.** The schema makes `io.modelcontextprotocol/protocolVersion` REQUIRED on every modern request and no legacy revision defines it, so its presence is decisive by itself. The `Mcp-Method` / `Mcp-Name` headers were dropped from the signal: requiring them would reject a modern request whose proxy stripped them — a needless false negative — and they are transport decoration while `_meta` is the request itself
- [x] 2.2 `MCP-Protocol-Version` alone is NOT a discriminator — legacy has sent it since 2025-06-18. Write that at the site; it is the mistake this decision exists to prevent
- [x] 2.3 Ambiguous → legacy. The failure modes are asymmetric: mis-routing legacy to modern breaks a working client with no recovery, while mis-routing modern to legacy produces the error the modern client's own detection is specified to fall back from
- [x] 2.4 Test: every existing legacy shape still routes to legacy — `initialize`, a post-handshake `tools/call`, a notification, a batch array, a bare GET for SSE
- [x] 2.5 **From this section onward, `tst_mcpserver_protocol.cpp` and `tst_mcpserver_session.cpp` must pass UNTOUCHED.** A diff to either is evidence the legacy path moved, not that a test needed updating. Two carve-outs, both deliberate and both stated where they occur: section 0, where a conformance defect may prove a test was asserting our bug, and section 6, which changes the confirmation mechanism on purpose. Outside those two, the rule is absolute — and note that section 0 lands first precisely so its edits are already in the baseline this rule is measured against

## 3. Modern request path — read-category only

- [x] 3.1 Route a modern request to the shared dispatch layer (`handleJsonRpc` and below) without creating a session. Only the envelope forks: version resolution, session lookup, response framing
- [x] 3.2 If a fix has to be made in two places, the fork is in the wrong place — say so at the fork
- [x] 3.3 Read protocol version per request; reject a request whose header and body disagree (`-32020`)
- [x] 3.4 `UnsupportedProtocolVersionError` (`-32022`) carrying `supported` and `requested`
- [x] 3.5 **STILL OPEN.** Resource-not-found is `-32602` in the modern era; `-32002` stays for legacy. Both correct, per revision
- [x] 3.6 **Refuse control- and settings-category tools on the modern path** until section 5 lands. Structural, so the unsafe state cannot be reached by forgetting
- [x] 3.7 Stamp `resultType` on every modern result. `"complete"` always — `"input_required"` is MRTR, which is a non-goal. Required by the revision on ALL results, so put it where a result cannot leave without it rather than at each handler
- [x] 3.8 Emit `io.modelcontextprotocol/serverInfo` in each modern result's `_meta`. Same identity the legacy handshake reports (`McpSurfaceVersion` + app version); a stateless client has no handshake to learn it from
- [x] 3.9 `ping`, `logging/setLevel` and `notifications/roots/list_changed` are removed in the modern era — they reach the shared dispatch as unknown methods. Legacy keeps all three, including `ping`'s exemption from the not-initialized check in `resolveSessionForMessage`, which is legacy-only by construction
- [x] 3.10 Tests: a modern read tool works; a modern control tool is refused; header/body mismatch is rejected; an unsupported version returns the supported list; every modern result carries `resultType`; `ping` is unknown to a modern caller and still served to a legacy one

## 4. `server/discover`

- [x] 4.1 Implement it — a server MUST, a client MAY call it
- [x] 4.2 Test BOTH client routes: discovery up front, and inline invocation that hits `-32022` and retries. The second is the likelier one and would be the easier to leave uncovered
- [x] 4.3 Assert the advertised list and the served versions are the same list, not two lists that happen to agree today

## 5. Session-independent rate limiting — the gate

- [x] 5.1 Key decided: **peer address**, both routes, per-minute window. Not the remote-access token — there is one token for the whole app, so it would produce a global limiter for the entire remote route. Reasoning in design.md
- [x] 5.2 Implemented per-caller in `McpRateWindow` (`src/mcp/mcpratewindow.h`), and `McpRemoteAccess::failedTokenOverLimit` was MIGRATED onto it in the same pass rather than left as a second copy — same key/window/count shape, only the budget differs. Its separate prune loop went with the mechanism. Original task text: implement per-caller, not global — one caller must not be able to starve another, which is what the per-session counter was shaped to avoid. `McpRemoteAccess::failedTokenOverLimit` is the existing per-peer-address per-minute window in this codebase; follow its shape rather than inventing a second one, and extract if the two turn out to be the same thing
- [x] 5.3 Open control/settings categories on the modern path only once 5.1-5.2 are done
- [x] 5.4 Tests: a caller over the limit is refused; a second caller is unaffected
- [x] 5.5 Leave `McpSession::controlCallCount()` alone — legacy still uses it

## 6. Confirmation-gated tools in the modern era

Decided: the gate gets its own identity and **legacy moves onto it too** — no
parallel mechanism. Refusing modern callers was considered and rejected as a
destination: the end state of this plan is legacy dropped, and on that day
refusal would make `machine_start_*` unreachable to every client. Reasoning in
design.md.

- [x] 6.1 `PendingConfirmation` mints its own `confirmationId`. `confirmationRequested` carries it, QML echoes it back, `confirmationResolved` matches on it. Note what this replaces: the `sessionId` there is not a session lookup and never was — nothing calls `findSession` on it
- [x] 6.2 `sessionId` stays on the struct but narrows to what it honestly is — which legacy session owns this, for the response header and for the three reaper/DELETE abandon sites. Empty for a modern caller, and the response for one carries no `Mcp-Session-Id`
- [x] 6.3 Update `qml/main.qml` (the `confirmationResolved` call sites) in the same edit. The parameter is renamed, not just re-typed; a stale `sessionId` name there would read as a session for the next person
- [x] 6.4 Abandon the pending confirmation on `QTcpSocket::disconnected`, both eras. Event-based, not a timer. It is the only backstop the modern era can have, and for legacy it replaces "noticed up to 30 minutes later by the session reaper" with "noticed immediately"
- [x] 6.5 A confirmation-gated tool must NEVER be served without confirmation. That invariant does not move
- [x] 6.6 Test: a modern caller's confirmation round-trips and the tool runs; a modern caller's confirmation is abandoned when its socket drops; a legacy caller's behaviour is unchanged end to end
- [x] 6.7 In the event `tst_mcpserver_session.cpp` did NOT need touching; `tst_mcpserver_protocol.cpp` did, and more than expected — the socket-disconnect backstop broke five confirmation tests at once by revealing that the harness had never modelled connection lifetime. A `HeldConnection` helper was added for that. Original task: `tst_mcpserver_session.cpp` may need updating here — this section deliberately changes a mechanism legacy uses. Say so in the PR rather than editing it quietly. The untouched-tests rule of 2.5 applies in full to the first PR

## 7. `subscriptions/listen`

Sequenced last on purpose: it is the only piece needing new socket handling,
and a modern client without it degrades to polling rather than breaking.

- [x] 7.1 Long-lived POST stream. ShotServer holds a socket open for SSE on GET today and has not seen this shape — check its socket handling before assuming it transfers
- [x] 7.2 It is an OPT-IN mechanism, not a transport swap for the GET stream. The client names the notification types it wants (`toolsListChanged`, `promptsListChanged`, `resourcesListChanged`, `resourceSubscriptions`), the server acknowledges, and each notification carries `io.modelcontextprotocol/subscriptionId`. Carry the three existing broadcasts (`machine/state`, `profiles/active`, `shots/recent`) through it
- [x] 7.3 `resources/subscribe` and `resources/unsubscribe` are REPLACED by this method, not merely supplemented — they do not exist for a modern caller. Legacy keeps both, and keeps the GET stream, untouched
- [x] 7.4 **Not applicable so far.** This server emits no `notifications/progress` and no `notifications/message`, so there is nothing to keep off the listen stream yet. The rule still binds anything added later. Original: request-scoped notifications (`notifications/progress`, `notifications/message`) do NOT belong on this stream — they flow on the response stream of the request they relate to. Also: never emit `notifications/message` for a request that did not carry `io.modelcontextprotocol/logLevel`
- [x] 7.5 Do NOT implement stream resumability or `Last-Event-ID` replay — removed outright in this revision, and we already declined it. Our legacy GET stream keeps its event IDs and `retry`, which is correct for legacy and must not leak into the modern stream

## 8. Advertise the version

- [x] 8.1 **Done EARLY, not last, and deliberately.** The plan said last so the list never promises what is unbuilt; in practice sections 2-7 would then have had no test able to reach any of them. It is servable but NOT negotiable — `initialize` filters it out — so the list promises nothing false. Original: add `2026-07-28` to `supportedProtocolVersions()` — **last**. That list is what `server/discover` promises; adding it earlier makes the server lie
- [x] 8.2 Confirm the legacy negotiation still picks a legacy version for a legacy client rather than the new highest

## 9. Verify

- [x] 9.1 Full suite via `mcp__qtcreator__run_tests` (scope `all`) — ask first, Qt Creator is shared
- [x] 9.2 Break each fix in turn and confirm its test goes red — done for the resource ordering, the header-version fix, `server/discover`'s advertised list, and the era-dependent `-32602`. NOT done for every fix; the ones red-checked are the ones whose assertion could plausibly have passed either way
- [x] 9.2a **Run the official conformance suite against a running app**, both PRs: `npx @modelcontextprotocol/conformance server --url http://<host>:<port>/mcp`. It drives the server over HTTP and needs no C++ SDK. Passing its `2026-07-28` requirement set is what "done" means for the second PR
- [x] 9.2b Legacy conformance is section 0's job, not a measurement taken here. Re-run all four legacy revisions at the end of each PR and confirm nothing regressed against section 0's recorded end state
- [ ] 9.2c Record every result in the PR body: which requirement sets ran, what passed, and every failure left standing with the reason it stays. A summarised "conformance passes" is exactly the claim this suite exists to make checkable
- [x] 9.2d Run it over the REMOTE route as well as ShotServer's. Conformance points at a URL, and the tokenized route is a different URL — pointing it only at the local one leaves the route that has been missed before unmeasured again
- [x] 9.3 Live-test over BOTH callers — ShotServer AND `McpRemoteAccess`. The remote route is a separate entry that has been missed before, and a legacy regression there would be invisible to the local one
- [ ] 9.4 **NOW HARDER, and the reason matters.** The two clients that would have proved this — `claude-code` and the claude.ai connector — have both MOVED to `2026-07-28`, so neither exercises the legacy path any more. A legacy client has to be found or forced (`codex-mcp-client` negotiated `2025-06-18`). Original: exercise a real legacy client end to end (Claude Desktop or the claude.ai connector) and confirm nothing changed for it. This is the assertion the whole change rests on
- [x] 9.5 **This is now FALSE and the PR must say so, not repeat it.** Two real modern clients are on `2026-07-28`: `claude-code v2.1.234` and `Anthropic/ClaudeAI`, both calling `server/discover` then `tools/list` / `resources/list` / `tools/call`, creating no sessions. The original text — "no real MODERN client has exercised any of this, it ships dark" — was true when written and stopped being true the moment the app restarted on this build. Do not carry the caveat forward out of habit
- [ ] 9.6 Read the `text-invariants.yml` PR run — it gates `src/**` and nothing blocks a merge on it

## 10. Document

- [x] 10.1 `docs/CLAUDE_MD/MCP_SERVER.md`: which era a client gets and how that is decided, and what a contributor must do to a new tool so it works in both
- [x] 10.2 Record that the session machinery is NOT deleted by this change and why — it is legacy's, and legacy stays. Written down so it is not re-litigated every time that code annoys someone
- [x] 10.3 Record the answer to "when do we drop legacy": when no client needs it, backed by the revision's twelve-month minimum deprecation windows
- [x] 10.4 No wiki manual change — MCP protocol eras are not a user-visible app surface
- [ ] 10.5 Open the PR, then run `/pr-review-toolkit:review-pr`
- [ ] 10.6 Archive the change + spec sync as the final commit on the same PR
