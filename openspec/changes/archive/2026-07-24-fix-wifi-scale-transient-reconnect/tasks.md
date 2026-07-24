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

- [x] 4.1 Reproduce the original failure: confirm the instant sub-millisecond `Host unreachable (code 7)` with `local=<unbound>` still appears in the log and is now classified transient
- [x] 4.2 Confirm the scale reconnects with no manual rescan, and record which backoff step (5 s / 30 s / 60 s) actually succeeds
- [x] 4.3 Resolve the open question from `design.md`: determine whether the mDNS re-resolve measurably clears the OS reject route or whether the backoff delay alone accounts for recovery; if it contributes nothing, drop the claim from the spec rather than leaving an unverified mechanism documented
- [ ] 4.4 Sanity-check the classification against a real DHCP reassignment (point the cached IP at another host on the LAN) and confirm recovery still happens, one backoff step later than before

## 5. Full suite and review

- [x] 5.1 Run the full test suite via `mcp__qtcreator__run_tests` (scope `all`) — this is the pre-PR gate; there is no PR CI
- [x] 5.2 Fix any pre-existing warnings or failures surfaced in the files touched, rather than excusing them as unrelated
- [x] 5.3 Confirm no wiki manual change is needed; the manual already describes automatic reconnect, which this change makes true. Add a wiki task here if implementation reveals otherwise
- [x] 5.4 Open a PR (do not push to `main`)
- [x] 5.5 Run the automated `/pr-review-toolkit:review-pr` on the PR and address findings
- [x] 5.6 Archive the change with spec sync as the final commit on the same PR

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

## 10. Third review round + hardware verification

- [x] 10.1 Move `m_triedHostnameFallback = true` off `attemptHostname()`'s entry onto the two
      resolved-IP dial sites. Setting it on entry made the resolve-failure fallback dial exempt from
      BOTH eviction gates, so a cached IP that DHCP had handed to a live host (a printer answering
      403) could be re-dialled every cycle and never evicted while resolution kept failing. Found
      independently by two reviewers. This supersedes the `m_cacheFallbackDial` approach added in
      round 2 and deleted in the section-9 trim — the same bug, fixed by removing state rather than
      adding it
- [x] 10.2 Add `savedScaleIsSimulated()` to `connectToSavedScale()`'s bail list. Guarding only at
      `tryDirectConnectToScale` was wrong: `connectToSavedScale` emits `disconnectScaleRequested`
      BEFORE it funnels there, so selecting "Simulated Scale" from Known Devices tore down a working
      real scale and connected nothing — the exact failure that function's own comment says the bail
      list exists to prevent
- [x] 10.3 Both `sim:` guards now use `appendScaleLog` rather than `qDebug`. The sibling guard in
      `connectToSavedScale` already did; as written the always-on background path was the invisible
      one, which is backwards
- [x] 10.4 Replace five copies of the `usb:` ladder-exclusion check with one
      `scaleAddressIsLadderDialable()` predicate that also excludes `sim:`. Stops the ladder arming
      against an address it can only ever no-op on, and means the new guard log cannot repeat every
      60 s
- [x] 10.5 Narrow `resolveFailureFallsBackToCachedIp`'s warning filter to the two resolution-failure
      messages actually expected, per TESTING.md's narrow-pattern rule
- [x] 10.6 Correct `design.md`, which still argued the OS-reject-route mechanism as primary and still
      listed 4.3 as open — missed when `spec.md` and `tasks.md` were updated in the section-9 trim.
      Correct `proposal.md`'s Impact list, which omitted `blemanager.{h,cpp}` and `maincontroller.cpp`
      entirely. Correct the stale `connectToHost` and `isScaleSimulated` docstrings
- [x] 10.7 HARDWARE VERIFICATION (4.1, 4.2). With the macOS LAN block resolved, device logs confirm:
      the transient classification fires on the real `Host unreachable (code 7)` (108 occurrences),
      the cached IP is retained every time (no eviction line in 24,709 log lines), the retry is
      deferred to the ladder and re-resolves at ~60 s intervals, and the driver never gives up or
      emits `recognitionFailed`. Separately, with `simulationMode` ON the WiFi scale now connects
      while `tryDirectConnectToDE1` remains correctly disabled — root cause 3 confirmed fixed
- [x] 10.8 FIXED, reframed. The reviewer's literal suggestion (scope/drop the `m_disabled`
      short-circuit in `isBluetoothAvailable()`) would have REGRESSED this change: that short-circuit
      is the only reason a WiFi scale connects in simulator mode without triggering a macOS Bluetooth
      permission prompt for a scale that uses no Bluetooth. Investigating it surfaced the real,
      release-affecting bug underneath: `tryDirectConnectToScale` and `connectToSavedScale` gated
      ALL transports on `isBluetoothAvailable()` before dispatching, so a WiFi (or USB) scale could
      not reconnect while the user had Bluetooth turned off — nonsensical, since neither uses the BT
      radio, and squarely the recovery this change exists to provide. Fix: the Bluetooth-availability
      gate now applies to the BLE path only. In `tryDirectConnectToScale` it moved below the wifi:/usb:
      branches; in `connectToSavedScale` the wifi: case skips it (usb: already exited above). Verified
      the BLE path is still fully gated — the passive scan and direct `connectToDevice` both sit past
      the relocated gate, and nothing radio-dependent bypasses it. The `m_disabled` short-circuit
      stays as-is; it is load-bearing for the WiFi-in-simulator path
- [ ] 10.9 NOT FIXED, recorded: `BLEManager` tracks the in-flight WiFi target in one unqueued slot
      (`m_pendingWifiHostname` / `m_manualWifiConnect`), shared between foreground user actions and
      the background ladder, so a ladder tick can silently discard a manual "Add WiFi Scale" attempt
      and suppress both its success and failure signals. Pre-existing, but this change widens the
      race from one DNS resolve to the full ~20 s connection window. Needs an attempt-identity token
      mirroring `m_resolveGeneration`
- [ ] 10.10 NOT FIXED, recorded: no idle liveness watchdog. Nothing but `onDisconnected` calls
      `setConnected(false)`, so if Qt ever failed to follow `errorOccurred` with `disconnected`, a
      stuck-true flag would permanently defeat the ladder. No reproduction; the reviewer could not
      point at a failing run. Speculative hardening, deliberately not added
- [ ] 10.11 NOT FIXED, recorded: still no test coverage for the `BLEManager` simulator-gate split. No
      existing test target links `blemanager.cpp`, so this needs a new target stood up against its
      full dependency graph — not the five-line addition it first appeared to be
- [x] 10.12 Observed on hardware, pre-existing, now FIXED here (see section 11): BLEManager's saved scale at
      startup can be a BLE "Decent Scale" with an all-zero MAC even when `scaleAddress` is
      `wifi:hds.local`, burning ~4 s of BLE radio on an impossible device before the WiFi scale
      connects. This change is what lets it be dialled (the simulator gate no longer blocks it), and
      `savedScaleIsSimulated()` does not catch it — the address has no `sim:` prefix. Track separately

## 11. macOS BLE device identity (found from hardware logs, folded in on request)

Chasing the zero-MAC direct wake in 10.12 turned up a real, pre-existing defect. Folded into this
change at the maintainer's request rather than tracked separately.

- [x] 11.1 `getDeviceIdentifier()` and `deviceIdentifiersMatch()` in `blemanager.h` guarded on
      `#ifdef Q_OS_IOS`, but Qt's Bluetooth backend on macOS is CoreBluetooth too and likewise never
      exposes MAC addresses. `address().toString()` therefore returned the null address
      `00:00:00:00:00:00` for EVERY device, so every scale, DE1 and refractometer paired on a Mac was
      persisted under one colliding identity. Replaced with a runtime `address().isNull()` check —
      the same form already used in difluidr1.cpp, difluidr2.cpp, bletransport.cpp,
      qtscalebletransport.cpp and bookooscale.cpp. Only these two helpers, whose output is written to
      settings, had the platform ifdef
- [x] 11.2 The matching bug was worse than a mismatch: with both sides evaluating to the null
      address, `deviceIdentifiersMatch()` returned true for ANY device. A user with two BLE scales
      paired connected whichever advertised first
- [x] 11.3 `tryDirectConnectToScale` chose direct-MAC-connect vs scan-and-match on `#ifdef Q_OS_IOS`,
      so macOS dialled `QBluetoothAddress(<uuid or null>)` and burned ~4 s per startup on an address
      that cannot resolve. Now chosen by whether the SAVED IDENTIFIER is a MAC, which is correct on
      every platform. Android/Linux (real MACs) and iOS (UUIDs) are unchanged
- [x] 11.4 Same treatment for `tryDirectConnectToDE1`: it already fell back to scanning on a null
      address, but logged it as `qWarning() invalid saved address` — a UUID is not invalid, and that
      would emit a WARN on every macOS launch. Now a debug-level, identifier-driven branch
- [x] 11.5 NO migration or compatibility shim, per the maintainer's explicit decision: a stale
      null-address entry now matches nothing, so the old pairing simply stops connecting and the user
      re-scans and deletes it. Deliberately not special-cased — keeping the legacy value matching
      would preserve the match-everything behaviour this change exists to remove
- [x] 11.6 PARTIALLY verified on hardware (build 18:18:57, session 60). The stale zero-MAC scale
      entry now logs `Direct wake (no MAC) - scanning for "Decent Scale" id: "00:00:00:00:00:00"` and
      takes the scan path; the pre-fix `Direct wake - connecting ... at 00:00:00:00:00:00` followed
      by `Direct connect not established (~4s elapsed) — aborting` is GONE — zero occurrences of the
      abort or `invalid saved address` across the whole session. STILL UNVERIFIED: that a freshly
      paired BLE scale persists as a UUID (needs a real BLE scale paired on the Mac), and the DE1
      `machineAddress` side (needs a real DE1 connect over BLE)
- [ ] 11.7 No regression test. `blemanager.h`'s helpers are free inline functions and could be tested
      without constructing BLEManager, but no test target links this header today (see 10.11) — the
      target would have to be stood up first
