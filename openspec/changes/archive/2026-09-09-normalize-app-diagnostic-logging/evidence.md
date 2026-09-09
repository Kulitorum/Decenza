# Logging migration evidence

## Historical baseline (not a current-build conformance result)

Read through the DE1 Android MCP debug_get_log tool, unfiltered session pages 7–9. The elapsed-time window is 2026-09-06 09:05:00 through 2026-09-09 09:05:40.715 in the tablet's session clock (the session markers do not specify a timezone). Builds 3585–3587. All captured source line IDs 12030–17317 are consecutive; 5,240 entries in the window have timestamps. The extra physical lines include continuations/session banners. This census classifies every timestamped entry's formatting; it does not certify that every possible functional defect was investigated.

- Timestamped entries: 5240
- Registered owner: 2,201
- Other: 3,039 (58.0%)
- WARN: 40; ERROR/FATAL: 0. A quiet severity query cannot exclude failures logged at DEBUG or in separate files.
- Bare FD rows: 1,046 of 1,367 bare entries. Framework warnings: one DelegateModel and two QSslSocket warnings.

### Legacy class families

| Prefix | Entries |
| --- | ---: |
| BatteryManager | 683 |
| ShotHistoryStorage::createBackupStatic | 9 |
| DatabaseBackupManager | 31 |
| AutoWakeManager | 13 |
| sendMachineSettings | 48 |
| ShotDataModel | 34 |
| ExtractionTrack | 98 |
| MainController | 13 |
| ShotReporter | 22 |
| ShotHistoryStorage | 24 |
| Visualizer | 30 |
| SteamDataModel | 5 |
| SteamPage | 65 |
| MachineState | 20 |
| FlushPage | 9 |
| PostShotReview | 3 |
| AI | 18 |
| SettingsPage | 11 |
| DelegateModel::cancel | 1 |
| QIODevice | 2 |
| ProfileManager | 7 |
| loadProfile | 7 |
| MqttClient | 34 |
| ShotServer | 51 |
| Sanitizers | 2 |
| Platform | 2 |
| Display | 2 |
| Settings | 4 |
| ShotSummarizer | 5 |
| ProfileShapeIndex | 2 |
| LocationProvider | 16 |
| AIManager | 5 |
| AIConversation::repairStaleTurnShotIds | 4 |
| AIConversation | 8 |
| WidgetLibrary | 4 |
| buildGrinderCalibrationBlock | 1 |
| ShotHistoryStorage::loadShotRecordStatic | 1 |

### Unregistered bracket families

| Prefix | Entries |
| --- | ---: |
| Memory | 316 |
| Yield | 6 |
| metadata | 36 |
| recipe | 5 |
| fd dump: UpdateChecker pre-teardown  | 2 |
| fd dump: UpdateChecker post-teardown  | 2 |
| ProfileStorage | 6 |
| firmware | 4 |
| recipe pill | 1 |

### Representative sanitized evidence

- L13253, Sep 7 14:54:32: BeanBase extraction fetched getprodigal.com/products/buenos-aires-caturra-colombia-washed and received HTTP 404. The message said “no archived copy”; an empty archive-availability lookup cannot prove permanent absence.
- L16235/L16239, Sep 8 15:56:35/38: archive replay from web.archive.org returned HTTP 429 before provider extraction. This was the archive service's response, not evidence of an AI provider quota error.
- L13294: DelegateModel::cancel index out of range; source context absent from persistence.
- L17300: ShotHistoryStorage::loadShotRecordStatic: shot 1080 not found. Current code combined query failure with row absence.
- BatteryManager reported “charger ON but port not delivering power” using an OS sample taken before the charging command, followed by “Transient power interruption cleared after 1 min.” These statements overstate the observation.
- AI prompt/response file receipts do not distinguish successful interpretation from failure. The separate AI files were not exposed by the read tools; the capture cannot rule out additional provider or parse failures.

## Current-source coverage and validation

- Source inventory: all first-party runtime C++, headers, QML/JS and native bridges
  now checked, including mixed-owner files. Exceptions are enumerated in
  `source-inventory.md`; the Runtime capture fallback is not counted as source conformance.
- Marker gate: 456 runtime files, 20 helper headers, 241 QML/JS files; 28 registry
  markers; 18 positive/negative gate fixtures pass. No Qt/compiler/network needed.
- Qt Creator build 1788809091556 passed in 128,720 ms; build 1788809091557 passed in
  217,250 ms after caller context, duplicate-backup cleanup and privacy tests.
  The QML diagnostic baseline gate passed as part of each successful build.
  Both report the same linker warning: compact-unwind offsets exceed 16 MB.
- Focused test run 1788809091549: logger suite passed. New operation identity,
  interpretation, overlap, settings snapshot, cancellation and local fetch privacy
  cases passed. Older AI/provider/BeanBase expectations failed on deliberately
  changed severity/text; those assertions have been updated, with rerun pending.
- Text gates passed: translation key conflicts, rich-text escaping, font literals,
  test-source duplication and MCP tool budget. Glyph scan reports only the existing
  AddLanguagePage language-name exceptions (exit 0).
- The initial Qt Creator startup-project block was resolved with the user's
  permission: the active project and all 117 discovered CTest entries now point to
  this Decenza checkout. No source changes were made in Decenza-Desktop.
- Local Qt Creator has only the macOS Qt kit. Per the user’s clarified workflow, all testing runs on Mac; Android, iOS
  and other platform compilation is verified by beta builds. The connected DE1 MCP still reports the
  physical Android tablet running build 3587, so its logs cannot validate these
  unshipped native changes. No physical machine commands or paid AI requests were made.
- A short wiki edit is prepared in `wiki-manual.patch`, against the cloned wiki's
  Manual.md. It has not been published.

## Remaining validation

Native platform builds and a representative updated-device charge/mismatch capture
remain pending. Current Mac MCP runtime/volume evidence was subsequently completed
and is recorded below. The historical 5,240-line census above is not reused as proof
of any new-build result.

### Archive disposition (2026-09-09)

The user authorized merging PR #1930 while explicitly holding beta builds, and
requested use of `openspec archive` with its result committed last in the PR.
Implementation and Mac validation are complete; the remaining portions of tasks
3.6 and 5.2 stay unchecked and are carried into the next logging change. Archival
records this disposition and synchronizes the implemented requirements; it does
not certify Android, iOS or other beta builds or the updated-device capture.
The prepared wiki patch remains queued for publication with the shipped feature.

### Focused and full-suite follow-up

- Focused run 1788809091550: all 4 CTest suites passed (19,260 ms), with no suite
  warnings: WebDebugLogger, AIManager, AIProviders and BeanBaseClient. Covers shared
  C++/QML prefix validation, contextual multiline/session behavior, distinct fetch
  identities, parsed success/empty/invalid output, rejection, settings snapshots,
  cancellation/late completion, and provider error-content omission. Stub servers
  assert unchanged request counts. Existing logger reentrancy/history/shutdown tests pass.
- First full run 1788809091551: 106/117 suites passed (53,680 ms, after a successful
  pre-test build). Eleven failed on old prefix/quoting/severity expectations; matching
  assertions were revised without suppressing unrelated warnings. The review also
  removed AIConversation's duplicate raw provider-error message and added conversation
  privacy coverage. A new full run is pending.
- Memory/FD coverage: the actual Android dump emits `dump=<counter> reason=<tag>`
  through Memory/FDs on its header and every fd/target row, at DEBUG. A page beginning
  at any row retains both owner and dump identity. Native execution/build remains a
  separate platform-validation requirement.
- Documentation now describes App/Storage/Network's expanded ownership and the
  registered-marker catalog, with historical coverage limitations. MCP surface version
  is 1.8.0; the tool-budget fingerprint is unchanged because the registration schema
  did not change (the runtime catalog/guidance did).

### Final local suite

- Full run 1788809091552 passed 116/117 suites. The remaining settings assertion
  had been edited after its compilation started; the next rebuild picked it up.
- Full run 1788809091553 passed all 117 suites, 0 failed/fatal/skipped/blacklisted,
  86,710 ms test duration, no suite warnings. Includes provider 503 → retry → 200
  with two requests sharing one operation and one terminal success, conversation
  preflight rejection/privacy, one INFO recovery after an update-check failure,
  and sparse calibration diagnostics reporting unavailable key/range without
  changing the numerical output. No paid requests.
- The migration changes battery logging expressions and comments, not thresholds,
  command arguments, polling cadence, mismatch counters or charging branches.
  Repeated samples remain DEBUG and existing collapse calls remain intact. Actual
  mobile charge/mismatch observations still need the native runtime pass.
- History/recipe/profile/storage and machine suites pass with the new owner text;
  missing-row messages now carry operation, shot ID and requesting function, while
  query failures retain the database error. Source review removed duplicate backup
  signal-forwarding receipts and found no new per-sample INFO traffic.
- The wiki edit exists in the separate local wiki clone and in the reviewable patch;
  publication should accompany shipping the feature. The main repository guide and
  CLAUDE.md now agree on coverage and no longer exempt mixed-owner files.

### Platform workflow clarification

The user confirmed that all testing runs on Mac and Android, iOS and other builds
are verified through beta builds. No separate platform test builds are being
dispatched. Beta compilation evidence remains pending the normal beta build cycle;
this is recorded separately from the completed Mac suite.

### Current-build MCP capture and final review

- Final Mac app build 1788809091558 passed in 83,560 ms, with the same compact-unwind
  linker warning and a passing QML diagnostic gate. The last changes removed empty
  string operands from logging expressions. No transport or machine behavior changed.
- Qt Creator launched this checkout under its debugger. Local MCP initialize and
  app_get_info confirmed macOS arm64, Qt 6.11.2, app 2.0.5 build 3586 and MCP 1.8.0.
  This identifies the new source independently of the unchanged local build number.
- The complete saved session at 2026-09-09T10:57:03 contains 186 physical lines
  (19,446 UTF-8 bytes including newlines), with consecutive persisted line IDs.
  All 185 diagnostic entries have registered owners; the only unprefixed line is
  the session banner. No Runtime fallback entries, unregistered bracket prefixes,
  or legacy class prefixes occur. Full raw responses are saved locally under
  `/private/tmp/decenza-current-log-final/`; no private log payload is added here.
- Network (19 lines), Battery (3), and WARN+ (10) MCP queries exactly match selection
  from that unfiltered snapshot. The preceding 232-line session also remains readable
  through MCP. Logger tests separately cover older unformatted sessions, pagination,
  multiline warning continuations, session-like message text, reentrancy and shutdown.
- Exercised startup paths include profile/history loading, an existing valid backup,
  app/settings/location initialization, network connection/retry, battery telemetry,
  memory samples, and simulator attachment. Paid AI outcomes are covered by the fake
  providers/local servers in the passing suite, not by live provider requests.
- The 132 identical event payloads shared by the two successive migrated Mac starts
  consume 13,271 bytes in each capture. Supplied source context adds 1,204 bytes across
  the 10 current warnings. Whole-session sizes differ with elapsed time and network
  retries and are not a controlled measurement of legacy-versus-new runtime volume.
  Historical source-format coverage remains the separate 5,240-entry baseline above.
- Three warnings were produced by the validation client's initial obsolete protocol
  request; the client now requests 2025-11-25 and the later initialize negotiates it
  without a warning. Other warnings concern the saved Wi-Fi scale address and MQTT.
  The user explicitly says MQTT is intentionally not enabled; it is not a follow-up
  issue. None of these messages is evidence of a logging regression.
- After the successful captures, the native UI tool resolved the ambiguous name
  Decenza to Decenza-Desktop and opened an additional copy. That was an execution
  mistake and could contend for ports. Its Settings navigation is NOT counted as
  validation of this checkout. Subsequent MCP refusal is not attributed to logging.
  A final process/listener check found no Decenza app processes, no port-8888 listener,
  and no Qt Creator debug session. No further app was launched.
- Remaining verification: mobile charge/mismatch wording in a representative device
  capture, and Android/iOS/other platform compilation in the normal beta builds.
  Those task portions remain unchecked. At the end of implementation, no commit, push, release, paid provider call,
  or physical machine control action had been made. PR preparation follows below.

### PR review and final branch verification

- Rebased onto main 367f4655 (including the scale retry fix from #1929 and build
  number 3587). The rebase was clean; upstream connection behavior was retained.
- Review fixed two diagnostic defects: extraction's existing REST timeout now
  finishes as failed/consumerTimeout before cleanup, and a repeated/completed
  operation ID receives a separate request context instead of prematurely ending
  an in-flight operation or suppressing the next result. Request/response routing,
  network call counts and existing timeout duration remain unchanged.
- The AIManager regression drives same-ID concurrent entry and completed-ID reuse,
  checks distinct terminal identities and preserved bag context, and verifies that
  a late provider callback adds no second result after consumer timeout.
- Full Qt Creator MCP run 1788809091554 passed all 117 suites in 45,690 ms after
  rebuilding the rebased source: 0 failed/fatal/skipped/blacklisted; no suite warnings.
  The active project and discovered test paths were checked against this checkout.
- Marker gate/18 fixtures, translation keys (3,505 after the upstream update),
  rich-text/font literals, MCP budget, test-source duplication, strict OpenSpec
  validation and diff whitespace checks pass. No app instance was launched during
  PR preparation; the saved runtime capture predates these two review fixes and
  is evidence for the unchanged logger/reader path.
- Final review covers registry/helpers, logger persistence/lifetime, native entry,
  source enforcement, operation outcomes/privacy, migrated owners, and the rebased
  scale changes. No additional blocking finding remains from this review. Beta
  compilation and mobile charge/mismatch evidence remain pending as documented.
