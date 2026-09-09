# Visualizer outcome and consumer audit

Baseline: `5c2f1d585bde0fd5675eae91988fa89de7208b29` (PR #1930).
Reviewed the complete uploader/importer sources and all result consumers on
2026-09-09. This is a source audit plus controlled Mac validation, not a new device
incident report. Test names below refer to `TstVisualizerShotParse` unless noted.

Every initiated operation starts at INFO and owns one terminal record. `U` means
`[Visualizer][VisualizerUploader]`, `I` means `[Visualizer][VisualizerImporter]`.
Success/empty/skip/cancel are INFO; failed/rejected/partial are WARN. Intermediate
retries and per-item successes stay DEBUG. WARN item problems and a WARN batch
summary describe different events. Source-only branches are explicit below; they
are not claimed as injected runtime failures.

## Entry, branch and exit inventory

| Entry / branch | Existing behavior retained | Diagnostic owner/result | Validation |
| --- | --- | --- | --- |
| Live/history upload; override wrapper cannot coerce a shot | `uploadFailed`, no POST | U rejected / missingShotData | Source guards; payload regressions in full suite |
| Upload maintenance / short shot | `uploadSkipped`, no POST | U skipped / maintenanceProfile or shotTooShort | `preflightAndConnection`; maintenance source audit |
| Upload credentials absent | `uploadFailed`, no POST | U rejected / missingCredentials | `preflightAndConnection` |
| Upload transport/5xx retry | Same multipart JSON, at most two retries, same signals/state transitions | U DEBUG retry, same op | `uploadRetriesAndChildOutcome`, `uploadFailures` |
| Upload accepted with nonempty ID | Existing success, local writeback and subsequent coffee sync | U success / uploaded; shot ID captured at entry | `uploadRetriesAndChildOutcome` |
| Upload 2xx missing ID, malformed response | `uploadFailed`, no success/writeback | U failed / missingReturnedShotId at interpret | `uploadFailures` |
| Upload exhausted, 401/422/429/other failure | Existing error wording, no extra retry | U failed / requestFailed; status/error code only | `uploadFailures`, preserves 422 UI text |
| Metadata update wrappers / missing remote ID / credentials | Existing failure signals, no PATCH | U rejected / missingShotData, missingRemoteId, missingCredentials | Source audit |
| Metadata update corrupt bean snapshot / canonical identity conflict | Existing omitted canonical link, same other fields | U WARN prepare problem / DEBUG decision, no raw bean names | Source audit; canonical payload suite |
| Metadata PATCH success / failure / 404 | Existing updateSuccess/uploadFailed/updateFailed, permanent only on 404; overlap still allowed | U success or failed, callback-owned shot/remote IDs | `overlappingUpdateOutcomes` |
| Connection test missing credentials / response | Same capability reset and result signal | U rejected / success / failed | `preflightAndConnection`, persisted filter test |
| Shot list missing credentials / network error | Same shotListFailed | U rejected / failed | Guard runtime; network source audit |
| Shot list malformed / missing paging / page ceiling | Same error text, no partial success signal | U failed / invalidResponse, missingPaging, pageLimit | `listOutcomes`, `overlappingListsAndPageCeiling` |
| Shot list next page / older page / empty / success | Same page recursion, accumulation and cutoff | U one op, page/count context, empty or success | Two-list overlap + 50-page runtime; cutoff suite `tst_visualizershotlist` |
| Import ID / renamed / share-code empty input or busy | Same importFailed or no-op guard | I rejected / missingInput or busy; no share code logged | Runtime busy; remaining guards source audit |
| Shared discovery credentials / busy | Same importFailed or no-op guard | I rejected / missingCredentials or busy | Source audit; refresh covered by duplicate test |
| Single import network / malformed JSON / invalid profile or TCL | Same UI error and state changes | I failed / requestFailed, invalidResponse, invalidProfile | `importFailureAndShareCodePrivacy`; TCL branch source audit |
| Shared list non-array / empty | Same importFailed or sharedShotsChanged | I failed / expectedArray or empty summary | Runtime automatic refresh; non-array exercised during test development |
| Share-code empty array / error object / missing ID | Same UI rejection, no profile request | I failed / noSharedShots, serverRejected, missingReturnedShotId | Source audit |
| Share-code resolved ID / profile URL follow-up | Same URL selection/query and profile request | I same op; bounded remote ID; URL excludes query | `importFailureAndShareCodePrivacy`, URL privacy test |
| Profile direct/renamed/TCL save succeeds | Same helper save and signals; JSON paths still refresh shared list | I success / profileSaved after save | Duplicate seed saves; renamed/TCL source audit |
| Profile save fails / second profile while duplicate pending | Same helper refusal and error signals | I failed / saveFailed; original pending op retained | Source audit |
| Duplicate found | Same duplicateFound signal; helper remains pending | I DEBUG awaitingUser, no terminal yet | `importDuplicateDecision` |
| Duplicate overwrite / save-as-new / rename succeeds | Same helper signals and files | I original op success / profileSaved | `importDuplicateDecision` four rows |
| Duplicate cancel | Same helper clear, no added UI signal | I original op cancelled / userCancelled | `importDuplicateDecision` |
| Duplicate invalid name or retryable save failure | Existing helper may remain pending for correction | I WARN save problem; terminal only once resolved | Source audit |
| Duplicate decision with no pending / helper discards failed save | Same existing helper errors | I rejected / noPendingImport or failed / saveFailed | Source audit |
| Batch empty / busy | Same counters/signal or no-op | I empty summary or rejected / busy | Source audit |
| Batch item network / invalid profile | Existing skipped counter, continue next item | I item problem; diagnosticFailures makes final partial despite UI skipped classification | `batchPartialPreservesUiCounters` |
| Batch existing profile skip / save / save failure | Same file check/overwrite policy/counters | I DEBUG skip/save or WARN save problem | Source audit; profile file operations tested in duplicate cases |
| Batch complete through either callback exit | Same batchImportComplete counts and refresh | I total/imported/skipped/failed + diagnosticFailures; summary before reentrant consumer signals | `batchPartialPreservesUiCounters` |
| Shared detail parallel replies / invalid JSON, profile or network | Same comparison, invalid flags and shared list signal | I per-reply remote ID captured at dispatch; one complete/partial summary | `sharedDetailsOutOfOrder` |
| Recovery busy / missing credentials / history unavailable | Same no-op or recoveryFailed | I rejected; safe reason | Source audit |
| Recovery list error / malformed / paging missing / page cap | Same recoveryFailed and stop | I failed; page/status/error context | Source audit; shared page helper and uploader production loop tested |
| Recovery next page / empty / downloads | Same normalized range and queue | I same op, item remote ID | `recoveryRetryParseAndDatabase`; empty source audit |
| Recovery download retry / exhausted or permanent failure | Same maximum three attempts, failed counter and continue | I DEBUG retry / WARN item problem | Runtime transient then success; exhaustion source audit |
| Recovery profile fetch failure | Same best-effort import without profile | I WARN distinct profile problem; partial summary even if row import succeeds | `recoveryRetryParseAndDatabase` |
| Recovery JSON/shot parse failure | Same failed counter and continue | I WARN interpret problem | Runtime JSON failure; parser suite covers invalid shot |
| Recovery DB insert / duplicate / DB error | Same positive/zero/negative return interpretation | I saved/skipped detail or WARN save problem; Storage retains DB diagnostics | Real isolated DB insert and duplicate; DB error source audit |
| Recovery completion | Same refresh/counters/progress/completion signals | I one summary with all four counts and partial state | `recoveryRetryParseAndDatabase` |
| Post-upload coffee sync preflight missing context / known unavailable capability | Same early return | U child skipped, parentOp references successful upload | `uploadRetriesAndChildOutcome`; capability source audit |
| Coffee local shot/bag read missing bag / failed query/row/open | Same worker query, no new read/write | U child skipped noLocalBag, otherwise failed with safe stage reason | Source audit |
| Coffee shot readback failure / invalid JSON | Same return, later upload may retry | U failed / pendingRetry or invalidResponse | Source audit |
| No server bag; canonical absent/conflicting / valid | Same no-op or canonical PATCH | U skipped / noServerBag or canonicalIdentityConflict, or canonical-link result | `coffeeReadBackLinkAndCapability`; conflict payload tests |
| Server bag linked | Same persist IDs, enrichment, pending drain, optional roaster badge | U bag/shot context, independent related children | `coffeeReadBackLinkAndCapability`; worker branches source audit |
| Canonical PATCH accepted / failure | Same request, no UI mutation | U success / canonicalLinked or failed / pendingRetry | Runtime success; failure source audit |
| Bag read gone / request/parse failure / no missing fields | Same ID clear, return or no PATCH | U skipped / remoteBagGone or alreadyComplete, failed / pendingRetry or invalidResponse | Source audit; body builder tests in `tst_coffeebags` |
| Bag enrichment PATCH 200/403/404/other | Same capability/ID state and request policy | U success / skipped limitation or gone / failed pendingRetry | Runtime 403 after full readback chain; remaining branches source audit |
| Roaster badge read network/parse/already linked; PATCH result | Same optional read/link, no changed routing | U separate roasterEnrich child, failed/skipped/success | `roasterResolutionFailureRemainsPartial` PATCH failure; other branches source audit |
| Bag update local context / inactive capability / unknown capability | Same rejection or pending flag; no request | U rejected context, skipped limitation or pendingCapabilityCheck | Source audit |
| Bag worker missing/unsynced/valid | Same pending flag and roaster resolution/PATCH chain | U failed bagUnavailable / skipped notSyncedYet / continuing op | Source audit |
| Roaster list fails / matches / no match | Same return, callback or create | U failed pendingRetry, or DEBUG stages; WARN malformed shape/missing ID | `roasterResolutionFailureRemainsPartial`; other branches source audit |
| Roaster create 201 / 403 / failure | Same callback even if returned ID empty, same capability update | U problem on missing ID, skipped limitation or failed pendingRetry | Runtime missing-ID fallback retains following PATCH |
| Bag PATCH no UUID / 200 / 403 / 404 / 422 / other | Same pending flag, ID clear, rejection toast and retry behavior | U skipped / success (partial after resolution problem) / limitation / gone / rejected / failed pendingRetry | `coffeeOutcomes` five rows; missing UUID source audit |
| Pending bag scan ineligible / query failure / empty / nonempty | Same early return or worker query and updates | No op for ineligible background scan; otherwise one scan terminal and one related op per bag | Source audit |
| Bean repair empty / busy snapshot | Same silent return or missed-work flag; no extra finish signal | No new operation; DEBUG dropped snapshot when busy | `beanRepairKeepsPendingOnAccountFailure`; empty source audit |
| Bean repair invalid queue entry / GET 404 / 401,429,403 / other network / unreadable body or bag ID | Same skip, settle, abandon or later retry; same 4-second pacing | U item decision/problem; final partial when incomplete | Runtime account failure and reentry; policy tests in `tst_coffeebags`; remaining exits source audit |
| Bean repair no-op / canonical clear / restore names | Same decision table, request body, settle rules | U DEBUG decision; full pass owns terminal | Existing policy/payload regressions; callback branches source audit |
| Repair PATCH 2xx invalid/readback disagreement/match, permanent or transient failure | Same declined/repaired/settled/pending counters | U WARN distinct invalid/readback/HTTP problems, partial even when legacy pass flag says settled | Source audit and pure decision tests |
| Canonical clear 2xx / account/per-shot/transient failure | Same clear/settle/abandon/pending behavior | U distinct stage + final complete/partial | Source audit |
| Owner destroyed while reply/duplicate/worker pending; late diagnostic callback | Existing object lifetimes/callback routing | Context destructor cancels unfinished op; terminal latch suppresses later diagnostic events | `cancellationAndLateDiagnostics` |

## Signal consumers and ownership

| Consumer | Behavior kept / logging decision |
| --- | --- |
| MainController uploadSucceededForShot | Queues local link writeback. Missing local ID stays a distinct WARN handoff failure; uploader owns remote upload success. |
| MainController updateSuccess/updateFailed migration-16 drain | Keeps in-flight ID filter, queue removal/retry and next dispatch. Backend terminal is not forwarded again: DEBUG pending-on-next-boot, INFO removal of a missing remote record. Remote error prose removed. |
| MainController pendingBeanRepairsReady / beanRepairSettled / beanRepairFinished | Same worker queue, settle and missed-snapshot re-drain. Queue-read failure belongs to Storage; uploader owns pass outcome. |
| MainController shotListFailed/shotListFetched / visualizerLinksReconciled | Same single-shot connections and backfill flag. Request failure is backend-owned; DEBUG records queue policy. DB reconciliation failure belongs to Storage. |
| PostShotReviewPage uploadingChanged/uploadSucceededForShot/updateSuccess/uploadFailed/uploadSkipped | Same pending flags, shot ID checks, URL/metadata refresh, status text. No duplicate terminal logger. |
| ShotDetailPage uploadSuccess/updateSuccess | Same history reload. No duplicate terminal logger. |
| VisualizerBrowserPage importSuccess/importFailed/duplicateFound | Same result display, refresh and duplicate decisions. No duplicate terminal logger. |
| VisualizerMultiImportPage sharedShotsChanged/importSuccess/importFailed | Same list selection/import tracking and result display. No duplicate terminal logger. |
| SettingsVisualizerTab connectionTestResult/recoveryProgress/recoveryComplete/recoveryFailed | Same status, date-range recovery counters and error styling. No duplicate terminal logger. |
| main.qml bagPushRejected | Same named bag toast and accessibility announcement. UI error text retained; no main-log server echo. |
| ProfileSaveHelper/Profile/ProfileStorage | Existing file/duplicate/validation events remain Profiles. Importer reports operation-level save outcome without copying profile payload or helper prose. |
| ShotHistoryStorage/CoffeeBagStorage | Existing database events remain Storage; operation logs record only the downstream outcome. |
| Other similarly named import handlers | ProfileImportPage and SettingsHistoryDataTab target other importers; they do not consume Visualizer results. |

## Boundaries retained

No request routing, payload, retry budget, pacing, business counters or signal
arguments changed. Tests allow existing metadata-update overlap and account for
JSON imports' automatic shared-list refresh. Diagnostic context is callback-owned;
existing shared application state is not refactored in this change. Recovery's
history accessor substitutes an isolated storage only under `DECENZA_TESTING`;
upload debug files likewise use a PID-scoped test path. Production paths are
unchanged. No extra Decenza process, machine command or live service mutation was
used.

Known source-only limitations for later behavioral review: the importer uses a
shared request-type field across independently guarded fetch/import entry points;
some coffee endpoints retain their permissive JSON-object fallback. These are
existing source patterns, not failures demonstrated in these tests. This change
does not claim to repair them.

## Next app-wide candidates

- `locationprovider.cpp`: explicit lookup failure versus optional/denied lookup.
- `shotreporter.cpp`: API completion versus optional location failure.
- `translationmanager.cpp`: update-check outcome versus batch bookkeeping.
- `shotserver_auth.cpp`: safe, non-secret TOTP refusal diagnostics.
- `librarysharing.cpp` / `datamigrationclient.cpp`: optional counting/settings
  failures versus terminal download/transfer failures.

MQTT is intentionally disabled and excluded. Beta/native-device checks and wiki
publication remain explicitly held in `validation-holds.md`.
