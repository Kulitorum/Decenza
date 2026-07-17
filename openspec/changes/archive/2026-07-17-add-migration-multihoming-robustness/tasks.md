## 1. Interface selection helper

- [x] 1.1 Add a helper in `DataMigrationClient` that, given a target host string and port, returns an ordered list of candidate local IPv4 source `QHostAddress`es whose interface subnet contains the target (reusing the `QNetworkInterface::allInterfaces()` iteration already in this file), default-route interface first; return a single "unbound/default" candidate when the target is not an on-subnet IPv4 literal. — `gatherSubnetCandidates()` (live) + inline pure `orderedSubnetCandidates()`; empty result = connect unbound.
- [x] 1.2 Unit-test the helper against fixtures: single-homed, dual-homed same subnet, target off-subnet/routed, and hostname input (falls back to default candidate). Follow `docs/CLAUDE_MD/TESTING.md` (no WARN lines). — `tests/tst_migrationinterfaceselect.cpp` (6 cases).

## 2. Bound-socket reachability preflight

- [x] 2.1 Implement a preflight that probes each candidate with a `QTcpSocket` (`bind(sourceAddr)` + `connectToHost(host, port)`) using a short per-candidate timeout; on first success record the winning source and tear down the probe; if all candidates fail, produce an "unreachable on all interfaces" outcome. — async state machine (`tryNextProbeCandidate`/`onProbeSucceeded`/`onProbeFailed`/`finishProbe`), 900ms per candidate.
- [x] 2.2 Call the preflight at the start of `connectToServer()` before the manifest `GET`, for both the discovered-device and manual `IP:port` paths; skip the HTTP fetch entirely when the preflight reports unreachable. — probe runs only when ≥2 same-subnet candidates exist (single-homed keeps the direct path, no added delay); `cancel()` tears the probe down.

## 3. Manifest fetch: bounded retry + failure classification

- [x] 3.1 Add bounded retry (start: 2) to the manifest fetch on transient `QNetworkReply::NetworkError` values (`HostNotFoundError`, `UnknownNetworkError`, `TimeoutError`, `TemporaryNetworkFailureError`, connection refused/closed); event-driven, no timer-as-guard. — `isTransientNetworkError()`; excludes ConnectionRefused/RemoteHostClosed (those mean the host answered → reachable). EHOSTUNREACH arrives as `UnknownNetworkError`.
- [x] 3.2 Rework the `onManifestReply` error branch so 401 → existing auth path (unchanged), other reply errors / parse failures → response error, and reserve the "unreachable" message for the preflight all-candidates-failed outcome (`datamigrationclient.cpp:160` area). — transient errors → "device is not reachable on your local network"; non-transient → `Connection failed: <errorString>`; 401 → auth prompt.

## 4. UI messaging

- [x] 4.1 In `SettingsDataTab.qml`, ensure the surfaced error reflects the classification (unreachable vs auth vs bad response); reuse existing translation keys where possible and add `Tr`/`TranslationManager` entries for any new strings. — the dialog is `DeviceMigrationDialog.qml`, which already renders `dataMigration.errorMessage` and hides it when `needsAuthentication` (auth prompt shown instead); no QML change needed. Classification lives in the C++ `tr()` strings, consistent with the class's existing messages.

## 5. Verification

- [x] 5.1 Reproduce on the multi-homed Mac (Wi-Fi + USB-Ethernet, both on `192.168.10.0/24`): confirm import connects to the Android peer at `192.168.10.163:8888` where it previously showed "Host unreachable". — Confirmed working on hardware by Jeff.
- [ ] 5.2 Bring one link down and confirm the surviving link is selected; confirm single-homed hosts connect with no added user-perceptible delay.
- [ ] 5.3 Confirm reachable-but-unauthenticated still prompts for a code, and a bad HTTP/manifest response shows a response error rather than "unreachable".
- [x] 5.4 Run the migration/unit tests via Qt Creator MCP (`run_tests`), fix any failures, and confirm a clean build. — 77 passed, 0 failed, 0 warnings, clean build (fixed the `failOnWarning` lint miss).

## 6. Docs

- [x] 6.1 If any user-visible error wording changed, update the device-migration section of the wiki Manual; otherwise note in the PR that no manual change is needed. — Only an error-state string changed (not documented in the manual); happy path unchanged, so no manual edit needed.
