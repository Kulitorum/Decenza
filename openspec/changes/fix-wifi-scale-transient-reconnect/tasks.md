## 1. Error classification in the WiFi driver

- [x] 1.1 Add a private helper to `DecentScaleWifi` that classifies a `QAbstractSocket::SocketError` as transient (`NetworkError`, `SocketTimeoutError`, `HostNotFoundError`) or wrong-host (everything else, incl. `ConnectionRefusedError`), with a comment recording that the discriminator is "did any peer answer", not "did the attempt fail". **Corrected during apply**: `ConnectionRefusedError` was originally listed as transient, which contradicted the design's own discriminator — a TCP RST proves a host is up and refused the port, which is evidence the address was reassigned. Existing tests `cachedIpFailureEvictsCacheBeforeFallback` and `cachedIpFastFailsToHostnameFallback` both rely on that classification.
- [x] 1.2 In `onError`, gate the existing cached-IP eviction block on the wrong-host classification so a transient error no longer calls `m_ipCacheUpdate(hostname, QString())`
- [x] 1.3 On a transient error, end the attempt without dialling the hostname: stop the recognition timer, leave `m_triedHostnameFallback` untouched, and let the disconnect propagate through `setConnected(false)`
- [x] 1.4 Replace the shared `Cached IP ... unreachable — evicting cache` log line with two distinct messages; the transient one must state that the cached IP was retained and the retry deferred to the app-level loop
- [x] 1.5 Verify the existing `503` early-return still short-circuits ahead of the new classification and is unaffected

## 2. Fresh resolution on the retry after a transient failure

- [x] 2.1 Add an event-based flag on `DecentScaleWifi` set when an attempt ends transiently and cleared in `onRecognizedAsHds`; no timer
- [x] 2.2 In `connectToHost`, when the flag is set, bypass both the `preferredIp` and cached-IP short-circuits and route to `attemptHostname()` so the attempt re-resolves
- [x] 2.3 Confirm the `m_resolveGeneration` guard still drops a superseded in-flight lookup when a retry arrives while a previous resolve is outstanding
- [x] 2.4 Confirm the cached IP is still present after a transient failure (retained, not evicted) and is used again once a connection succeeds

## 3. Tests

- [x] 3.1 Read `docs/CLAUDE_MD/TESTING.md` before writing any test; the warning rules are strict and a test that emits WARN lines is not shippable
- [x] 3.2 Extend `tests/tst_decentscalewifi.cpp`: a transient socket error on a cached-IP attempt preserves the cache and does not dial the hostname within the same cycle
- [x] 3.3 Test that a wrong-host failure (handshake/protocol error) still evicts the cache and falls back to the hostname, so the router-403 case is not regressed
- [x] 3.4 Test that a recognition timeout still evicts and falls back — unchanged behaviour
- [x] 3.5 Test that the retry following a transient failure takes the re-resolving hostname path rather than the cached IP
- [x] 3.6 Test that a transient failure does not emit `recognitionFailed`, so the manual-path `physicalScale.reset()` teardown is not triggered

## 4. Verification against real hardware

- [ ] 4.1 Reproduce the original failure: confirm the instant sub-millisecond `Host unreachable (code 7)` with `local=<unbound>` still appears in the log and is now classified transient
- [ ] 4.2 Confirm the scale reconnects with no manual rescan, and record which backoff step (5 s / 30 s / 60 s) actually succeeds
- [x] 4.3 Resolve the open question from `design.md`: determine whether the mDNS re-resolve measurably clears the OS reject route or whether the backoff delay alone accounts for recovery; if it contributes nothing, drop the claim from the spec rather than leaving an unverified mechanism documented
- [ ] 4.4 Sanity-check the classification against a real DHCP reassignment (point the cached IP at another host on the LAN) and confirm recovery still happens, one backoff step later than before

## 5. Full suite and review

- [x] 5.1 Run the full test suite via `mcp__qtcreator__run_tests` (scope `all`) — this is the pre-PR gate; there is no PR CI
- [x] 5.2 Fix any pre-existing warnings or failures surfaced in the files touched, rather than excusing them as unrelated
- [x] 5.3 Confirm no wiki manual change is needed; the manual already describes automatic reconnect, which this change makes true. Add a wiki task here if implementation reveals otherwise
- [x] 5.4 Open a PR (do not push to `main`)
- [x] 5.5 Run the automated `/pr-review-toolkit:review-pr` on the PR and address findings
- [ ] 5.6 Archive the change with spec sync as the final commit on the same PR

## 6. Recognition-timer ordering (found during implementation)

- [x] 6.1 Arm the recognition timer BEFORE `open()` in `attemptTarget` — `open()` fails synchronously for an OS-rejected address, so `onError`'s `stop()` was hitting a timer that had not started
- [x] 6.2 Lengthen the cache test past the 5 s recognition window; the original 600 ms wait passed with the bug live and gave false confidence
- [x] 6.3 Fix the fixture's `init()`, which unconditionally *required* a DISCONNECTED warning and so failed any test whose driver never connects; replaced with an allow-not-require filter
- [x] 6.4 Declare each test's warning filter BEFORE the driver it covers — locals destruct in reverse order, so a filter declared after the driver is gone before the driver's teardown warnings fire

## 7. Simulator gate (found during implementation)

- [x] 7.1 Add `BLEManager::setScaleSimulated()` / `m_scaleSimulated`, distinct from the DE1 `m_disabled` flag
- [x] 7.2 Gate `switchScale` and `tryDirectConnectToScale` on the scale simulator, not the DE1 simulator
- [x] 7.3 Stop `setDisabled()` from tearing down a connected physical scale; move that to `setScaleSimulated()`
- [x] 7.4 Wire `setScaleSimulated` from `simulatedScaleEnabled` in `main.cpp`, including the change signal
- [ ] 7.5 Add regression coverage for the gate split (no BLEManager test target exists yet — decide whether to add one or accept manual verification per the QML/manual-testing convention)

## 8. Review findings (from /pr-review-toolkit:review-pr)

- [x] 8.1 CRITICAL: gate `setScaleSimulated` on `simulationMode() && simulatedScaleEnabled()`. Reading the latter alone blocked every real scale on debug iOS/Android/Linux — it defaults to true, while the SimulatedScale object only exists under simulation mode, and the Settings row is hidden in exactly that state
- [x] 8.2 CRITICAL: arm `m_scaleConnectionTimer` in `connectToScale`'s WiFi branch. Without it, tapping a discovered scale that just went unreachable dead-ended forever — no retry, no dialog. The pre-PR immediate fallback had been the only thing covering that path
- [x] 8.3 CRITICAL: `0.0.0.1` is not portable — Linux maps zeronet to EINVAL and Qt maps EINVAL to ConnectionRefusedError, so three tests would have failed the tag-push Linux job. Tests now check the precondition and QSKIP with an explanation
- [x] 8.4 HIGH: remove `HostNotFoundError` from the transient set — it only arrives from a hostname dial, which was already excluded from eviction, so the classification protected nothing and only suppressed the manual-add failure dialog
- [x] 8.5 HIGH: emit `scaleSimulatedChanged` on both edges and re-arm the reconnect ladder on the falling edge; otherwise switching the simulated scale off stranded the real scale until restart
- [x] 8.6 HIGH: `preferredIp` now outranks the re-resolve flag — it is strictly fresher, and discarding it could produce an attempt that opened no socket at all
- [x] 8.7 MEDIUM: stop consuming the re-resolve flag before a resolve succeeds; add `dialCachedIpAfterResolveFailure()` so a deaf-mDNS device dials something every cycle instead of oscillating
- [x] 8.8 MEDIUM: declare `cacheWrites` before `driver` — the driver holds a callback capturing it, reachable from its destructor (ASan use-after-free)
- [x] 8.9 LOW: `Q_DISABLE_COPY_MOVE` on ScopedWarningFilter; `switchScale` -> `connectToSavedScale` (no such symbol existed); drop the wrong "seven sites" count; rewrite the test docstring that now taught the inverse rule; drop the unfalsifiable "45 ms"
- [x] 8.10 LOW: `maincontroller.cpp` pre-shot scale-missing abort gated on `isScaleSimulated()` rather than `isDisabled()` — same DE1/scale conflation this change exists to end
- [x] 8.11 Correct two comment claims that the Qt source disproved: EHOSTDOWN is NOT mapped to NetworkError (falls through to UnknownSocketError), and ETIMEDOUT maps to NetworkError rather than SocketTimeoutError
- [ ] 8.12 Verify the behavioural tests on Linux — they now skip rather than fail if the platform disagrees, but the skip has not been observed
- [ ] 8.13 Still no regression coverage for the simulator-gate split (was 7.5); findings 8.1/8.2/8.5 were all found by reading, not by tests

## 9. Simplification pass (independent review)

Prompted by the maintainer asking whether the change had grown too complicated for the task. An
independent review was run on the question and **overrode the proposal to delete the re-resolve
feature** — deleting it would have shipped a regression against `main`.

- [x] 9.1 Establish what the re-resolve actually buys. Deleting it is NOT safe: when DHCP moves the
      scale, the vacated address usually goes dark rather than being reassigned; dark answers
      nothing, so it classifies transient, so the cache is retained — and the driver would re-dial a
      dead address forever with no other correction path. On `main` any error evicted, so this
      self-corrected. Root-cause fix 1.2 opens that hole and `m_retryShouldReresolve` closes it
- [x] 9.2 Trim the accretion instead: remove `m_evictedIpThisCycle`, `m_cacheFallbackDial`, the
      `isCacheFallback` parameter, the `onError` gate restructure and early-return, the
      `onRecognitionTimeout` eviction block, and the per-cycle resets. Keep the sticky flag plus the
      plain cached-IP fallback
- [x] 9.3 `m_evictedIpThisCycle` did not do what its comment claimed — cleared per cycle, never
      written back, and `onRecognizedAsHds` only caches on a hostname target, so a successful
      last-resort dial could never restore the address. The dead-end it named is pre-existing on
      `main`, not introduced here. Removed rather than repaired
- [x] 9.4 Re-base the `m_retryShouldReresolve` rationale on ADDRESS FRESHNESS, which is verifiable
      by inspection, instead of the unmeasured "mDNS clears the OS negative route" mechanism. For a
      `.local` name the responder is the scale itself, so during an unreachability window the
      resolve usually fails too — recovery there comes from the backoff delay
- [x] 9.5 Close 4.3 without hardware, as it permitted: drop the unverified mechanism claim from the
      spec rather than leave it documented as fact
- [x] 9.6 Correct four factual errors that would otherwise be archived as the permanent record:
      `EHOSTDOWN` listed as transient in the spec delta and `design.md` (Qt maps it to
      `UnknownSocketError`); `HostNotFoundError` still transient in `design.md`; connection-refused
      still transient in `proposal.md`; `proposal.md` claiming edits to
      `src/network/wifiscalediscovery.{h,cpp}` and the `main.cpp` retry path that were never made
- [x] 9.7 Add spec scenarios for the DHCP-moved-and-dark case and the resolve-failure fallback —
      both normative behaviour with no scenario previously
- [x] 9.8 Add `resolveFailureFallsBackToCachedIp`. This path had no coverage because every other
      test uses a `127.0.0.1:<port>` hostname, which is not `.local`, so `attemptHostname()` dials
      directly and never runs a resolution that can fail
- [ ] 9.9 Residual, unchanged by this pass: 4.1/4.2/4.4 ship unverified on real hardware, and the
      transient classification is exercised in tests only via `0.0.0.1:80` (see 8.3/8.12). Its
      real-world behaviour rests on the maintainer's device logs for the `NetworkError` case and on
      reading for the rest
