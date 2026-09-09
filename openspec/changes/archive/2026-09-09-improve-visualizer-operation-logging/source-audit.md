# Real-device log audit and previous-PR review

## Scope and completeness

Retrieved with **DE1 MCP `debug_get_log`**, unfiltered and without deduplication,
on 2026-09-09 at 18:50 UTC. Five complete sessions (5–9) were retrieved in seven
pages: **10,376 physical lines**. The rolling-week selection is September 2
12:50:06 through September 9 12:50:06 in the device session clock. Session markers
do not specify a timezone; their local clock plus elapsed seconds defines this
window. The latest recorded entry is September 9 12:48:00.437.

The selection contains **9,342 consecutive physical lines, IDs 8070–17411**:
8,683 DEBUG, 571 INFO, 80 WARN and 8 banners/continuations. No ERROR/FATAL entries.
All lines were classified; all 847 number-normalized message patterns were
reviewed, followed by original values and neighboring events for candidate issues
and noise decisions. Numeric normalization was for ranking only: different
numbers can be different failures and must not become a global suppression rule.
The complete private capture and classification working files are in `/private/tmp`;
private device logs and account content are not committed here.

| Session | Lines in week | First–last device-clock time |
| --- | ---: | --- |
| 5 | 587 | Sep 2 12:54:38 – 14:56:40 |
| 6 | 2,357 | Sep 2 14:56:44 – Sep 4 16:17:10 |
| 7 | 3,099 | Sep 4 16:17:16 – Sep 7 23:05:59 |
| 8 | 1,848 | Sep 7 23:06:04 – Sep 8 15:54:54 |
| 9 | 1,451 | Sep 8 15:55:00 – Sep 9 12:48:00 |

These binaries predate PR #1930. Its new format cannot be judged by whether older
rows now have tags. Compare the observed messages with current source, and test
new emission behavior separately. Historical rows must remain intact.

## What was useful

| Evidence | Diagnostic value / conclusion |
| --- | --- |
| Product-page HTTP 404s, including five attempts Sep 4 09:56–09:57 (9996–10001) | Identifies a missing product URL. Five lines alone do not identify whether these were separate user attempts or automatic retries. Keep endpoint/status and request identity where the existing AI pipeline already owns it. |
| Archive HTTP 429s Sep 4 and Sep 8; no archived copy Sep 7 (11262, 13253, 16235, 16239) | Distinguishes archive rate limiting from the original page failure. The log must distinguish local/page/archive failure from an AI provider being invoked and failing. This justifies the previous PR's terminal context, not start/dispatch/response chatter. |
| Two update-check HTTP 504s at DEBUG (12151, 14420) | Real failures invisible to WARN filtering. Previous PR's failure/recovery severity correction is useful. |
| Scale oscillation and re-arming (10055–10059) | Warn identifies why stop-at-weight was blocked; following recovery establishes that it resumed. Preserve both and their numerical evidence. |
| Eight Bluetooth queue delay warnings, 565–1131 ms | Every one follows an Idle phase in this capture. Useful contention evidence, but not evidence that a shot's stop was delayed. Preserve operation/wait/count; the repeated generic explanation can be shorter. |
| Two DelegateModel index warnings; three closed QSslSocket reads | Potential app/framework defects, not grounds to declare everything an outside-service issue. This audit does not establish their root causes. Preserve warnings and supplied source context. |
| 47 public-Funnel unauthorized-request warnings, four burst-limit warnings | Security rejection evidence. Already bounded per minute. Low total volume; preserve, do not weaken authorization or hide a new failure. |
| One sunrise/sunset timeout; three stale HTTP connection cleanups | Distinguish optional weather failure and connection cleanup from core machine faults. Preserve useful failure/peer context. |
| Missing historical shot during an MCP query (17300) | One requested record was unavailable. Does not establish database corruption. Preserve Storage context. |
| Benign R2 status at WARN (17172); unavailable calibration range printed as nan/inf (16505) | Misleading issue signals. Previous PR already corrected these classifications/wording; keep those fixes. |
| Charging command versus OS observation | Adjacent pre-command samples can disagree transiently. Previous PR's truthful requested-versus-observed wording is useful; retain mismatch warnings and real transitions. |
| Shot stop, final settled weight, calibration decision and storage/upload result | Establishes what actually happened. Preserve these even when successful: they close a diagnosis and prevent a transient failure from appearing unresolved. |

## Where the volume came from

Counts are observed physical rows, not claims that every row in a family is noise.
Candidate reductions must preserve transitions and failures and be tested.

| Existing family | Rows | Decision and evidence it must retain |
| --- | ---: | --- |
| Full FD inventories / headers | 2,044 / 8 | Remove automatic dumps entirely, with no replacement summary. Counts across four updates fell 248→243, 256→251, 274→269 and 256→247; the capture does not establish an FD/socket leak. On-demand MCP FD inspection remains unchanged. |
| Battery poll snapshots | 1,267 | Use existing LogCollapse with a state signature and a last-emitted 5-percentage-point progress band. Every mode, desired-charge, cycle, OS-status and power-source transition must remain immediate. Replay of the recorded samples retains 364, suppresses 903; this is a candidate replay, not a new-device measurement. |
| Battery mismatch/clear / charge switching | 131 / 112 | Keep truthful mismatch, recovery and command transitions. They explain behavior; not all battery traffic is expendable. |
| Memory samples / class deltas | 590 / 124 | Only sustained memory growth warrants a log. Remove routine snapshots, isolated steps and class churn; retain exact sampled history and on-demand object snapshots. Sparse historical emitted samples cannot validate a trend gate that uses every minute of sampling. |
| Healthy constant-weight reports | 243 | Proving healthy samples once per shot is enough; changing a held weight must not defeat collapse and restart narration. Keep real stall/resume evidence and the existing end-of-shot feed statistics. |
| Scale interval/rate checks | 117 | Keep anomalies that explain timing; inspect normal periodic reports separately from disagreements. |
| Extraction color tracking / raw scale weights | 127 / 121 | Repeats numbers already in recorded shot data and UI calculations. Remove main-log traces; retain actual stop, tare, scale fault and frame-exit decisions. |
| Settling samples | 141 | Routine half-second progress is redundant with final settling results. Keep interrupted stability where it explains a delay, final weight and timeout/refusal outcomes. |
| Weather success receipts | 169 | Temperature changes are normal, not diagnostic novelty. Collapse unchanged success state, preserving first result, failure and recovery. |
| Auto-load invocation | 76 | 60 idle-countdown and 16 Sleep-to-Idle receipts. Source must distinguish a request that did useful work from an unchanged no-op; avoid repeating an invocation receipt when nothing changes. |
| SSE connection lifecycle | 103 | Retain connection identity/errors when diagnosing loss; do not promote every normal poll/session bookkeeping event to an issue. |
| Visualizer metadata bodies | 4 | Existing result and record ID carry the useful fact. Remove full metadata JSON from main log; do not add a separate body-size receipt merely to replace it. |
| AI diagnostic-file receipts | 24 | Three receipts per exchange add no result evidence. Keep a useful artifact location only where needed for a failure or explicit diagnostics; terminal result is what answers the user's question. |
| Duplicate Steam UI state / timer traces | 77 / 76 | Remove duplicate SteamPage state handlers; retain actual scale-timer commands and timer recovery/refusal events, with decorative banners removed. Keep steam-scaling decisions that explain applied settings. The 77 count matches only the five deleted state/visibility emitters. |

Within a session, 4,056 rows repeat exact message text (timestamps and existing
collapse annotations removed for this count). This is an upper-bound candidate
set, not 4,056 proven useless events: a later shot or a new connection is a new
episode even if its wording repeats.

## Parseability

Only 3,011 rows have an owner and source pair; 1,301 have one bracket prefix,
2,663 a bare named prefix, 2,359 a bare body, and eight are banners/continuations.
The older mixed formats are why a subsystem-only read missed useful context.

Useful existing shapes are `[SAW][Worker] Stop triggered: weight=… target=…` and
`[DE1][Phase] Idle → Espresso`: owner, event, actual values and chronology are
clear. Harder shapes include orphan FD rows, repeated URLs inside error prose,
unnamed positional numbers, mixed quoted values, embedded JSON, all-caps decorative
timer banners, the R2 line saying both “error” and “not a fault”, and historical
calibration sentinels that look like computation failures.

Keep the previous PR's registered prefix and multiline/source handling. Do not
rewrite old logs, infer owners from arbitrary message text at capture time, or
use minLevel as a substitute for removing DEBUG noise at its source.

## PR #1930: retain and redo

- **Retain:** registry/helpers/source gate, first-party owner migration, framework
  fallback/context, multiline attribution, safe AI/page/archival terminal outcome,
  response privacy, and the demonstrated severity/wording corrections.
- **Redo:** FD row-by-row identity made a large dump longer rather than useful.
  Remove the automatic inventories altogether; preserve independent MCP inspection.
- **Trim:** AI operation starts and generic dispatch/response/ready stage records;
  keep diagnostic state in memory for the single useful final result. Omit absent
  fields and redundant source function signatures where file/line already locates
  the emitter, if tests confirm that diagnosis remains unambiguous.
- **Remove from this PR:** the new Visualizer operation lifecycle and its verbose
  test scaffolding. Existing Visualizer messages can be improved directly without
  adding a start/stage/end protocol to every action.

MQTT is intentionally excluded. Native beta validation and wiki publication remain
held. UI/password accessibility and functional defects identified above are not
silently mixed into this logging cleanup.

## What the previous audit established

The archived PR #1930 evidence records a 5,240-timestamped-line formatting census.
That was not an adequate assessment of which records helped diagnose an issue.
The present audit reviews message value, repetition and fault context across the
complete rolling week; a formatting count or a clean WARN query is not a health verdict.

## Additional Mac observation

The earlier live Mac capture showed repeated reconnect failures being demoted to
DEBUG rather than suppressed, plus hostname-resolution warnings bypassing the
shared failure sink. These sources now use LogCollapse, with one first failure and
one pending-count record at the end of an episode. Distinct failures and fresh
user attempts remain visible. The rebuilt Mac capture then exposed four completed discovery cycles in 84
seconds, with 16 discovery-start, resolver-start/end and discovery-result receipts, plus repeated direct-wake intent.
Those redundant starts and resolver receipts are removed; one changes-only
discovery result retains backend, resolved/unresolved/withdrawn counts and error
state, with elapsed time excluded from the suppression key. Found-device records
remain distinct. Unchanging retry tails omit their per-attempt receipts.
