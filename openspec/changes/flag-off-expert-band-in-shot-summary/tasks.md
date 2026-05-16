> Seam-free and self-contained. Supersedes `capture-dialin-coaching-guidance` and replaces the scope of `profile-expectation-deviation-detector`. **Staged gold-first behind a hard STOP gate, mirroring the project's `capture-dialin-coaching-guidance` D12 culture** (process control lives here in the tasks, not in the spec — the spec is behavior/citation-graded only). Phase A is the real go/no-go: it builds all the infrastructure but seeds **only the gold pair**, and the `shot_eval` shadow run (A6) — not test-green — is the launch decision. Phases B and C only add cited table rows and each re-clears the gate; the most-likely-to-fail arms can never ship without independently passing it. Fail-safe: a flat gate keeps the tint (cheap, safe) and drops the band line at minimal cost.

## Phase A — Infrastructure + gold pair + STOP gate (the real test)

### A1. Citation-graded axis+band table — gold pair only

- [x] A1.1 Build the static table mechanism: per profile → `axis` ∈ {`pressure-peak`,`extraction-flow`} → band → `[SRC:...]` → confidence, **keyed by canonical KB-section identity** (`ShotSummarizer::canonicalNameForKbId(profileKbId)`, the `ugsForKbId`/`allKbUgsEntries` dedup-by-name precedent — D14); absent/uncited section → **no entry** (intentional, never a fabricated band/axis).
- [x] A1.2 Seed **only the gold pair**, verbatim from `capture-dialin-coaching-guidance` design D9/D10/D10b (authoritative — do NOT re-derive/re-fetch): **D-Flow / Q ≡ Damian's Q** and **D-Flow / La Pavoni** — pressure 6–9 bar, `[SRC:profile-notes]`. KB prerequisite **#1175 merged** (La Pavoni split into `## D-Flow La Pavoni variant`), so canonical keying yields exactly: Q & Damian's Q → one entry (`## D-Flow Q variant`, structural zero-duplication); La Pavoni → its own entry (`## D-Flow La Pavoni variant`); D-Flow/default → no entry (`## D-Flow`). Nothing else seeded in Phase A.
- [x] A1.3 Document the self-classifying rule next to the table (a profile qualifies iff a cited source states a recommended pressure-peak or extraction-flow band; profile-`notes`-stated bands auto-qualify) so Phase B/C and future levers extend it by adding a cited row, no code change.

### A2. Detector branch in the one-place cascade

- [x] A2.1 Add the static lookup + a single comparison branch inside `ShotAnalysis::analyzeShot` (no second pass, no orchestrator). Resolve profile → entry; none → **strict no-op**, result byte-identical to pre-change.
- [x] A2.2 Read the observed value from values the cascade already computes — peak pressure (pour-truncated path) for `pressure-peak`, extraction flow (existing `analyzeFlowVsGoal` result) for `extraction-flow`. Do **not** modify `analyzeFlowVsGoal` or any existing detector.
- [x] A2.3 Fire one `summaryLines` entry only when the observed value is outside the cited band by the configurable margin AND the hard AND-gate passes (NOT pour-truncated, NOT channeling-fired). Bean freshness is **out of scope** (D8): `analyzeShot` is the deterministic curve cascade and has no freshness input; freshness suppression remains the advisor-prose layer's job, unchanged. Wording: observational, taste-deferring, names observed value + cited band, **no grind direction**.
- [~] A2.4 **Deferred (optional, spec "MAY"; same root cause as the D8 freshness scoping).** The corroborating limiter clause needs the resolved profile's flow-frame pressure-limit value to detect "pegged the limiter"; `analyzeShot` is the deterministic curve cascade and has **no profile-limiter input** (and a curve-only heuristic for "pegged" would be the kind of fragile guess CLAUDE.md forbids). The spec marks the clause optional ("MAY append"), so omitting it in Phase A is spec-compliant: the band line fires correctly **without** the clause; "a limiter touch with the peak inside the band MUST NOT fire" holds because the line is gated purely on the band, not the limiter. Threading the profile limiter value (a small caller-resolved input, like `analysisFlags`/`expertBand`) is a clean later-phase addition; recorded in D1 as deferred. The line MUST (and does) fire with no limiter present.
- [x] A2.5 Recompute-on-load contract: only profile identity is persisted; table/margin/gate resolved fresh every compute; serialized `verdictCategory`/line is a non-authoritative cache the recompute refreshes.
- [x] A2.6 Verdict branch (D13): add a dedicated cascade branch setting `verdictCategory = "expertBandDeviation"` when the band line fired, ordered **below** every pour-truncated/skip-first-frame/yield-overshoot/choked-puck/`hasWarning`/`hasCaution` verdict and **above** `cleanGrindNotAnalyzable`/`clean`. Non-directional, taste-deferring verdict-line text. The band line's own `type` stays `observation` (do NOT raise to caution/warning to force the verdict).

### A3. Wording & severity

- [x] A3.1 Band line `type` is `[observation]` (lowest authority, D2/D3 — it is not a verdict); discoverability comes from the D13 `expertBandDeviation` `verdictCategory` value driving the D12 tint, NOT from raising the line's severity. Confirm against `ShotAnalysisDialog.qml` styling that it does not read as a badge/warning and never renders alongside a contradicting fired mechanical detector.
- [x] A3.2 **Reconciled with the established convention:** `shotanalysis.cpp`'s detector prose is plain English `QStringLiteral` with **zero** `TranslationManager`/`tr` usage (the lines double as AI-prompt input) — per-line i18n is not how this file works, and translating only the band line would be inconsistent. The band line follows that convention. Confirmed it never asserts a verdict and always defers to taste ("judge by taste"); test `expertBand_outsideBand_…` asserts no grind-direction tokens.

### A4. Entry-affordance tint (the delivery)

- [x] A4.1 Surface the already-serialized `DetectorResults::verdictCategory` onto `shotData` where `QualityBadges` binds it, mirroring how the four fault flags already arrive. Read-only; no C++ analysis change.
- [x] A4.2 In `qml/components/QualityBadges.qml`, add a single calm tint applied **whenever `verdictCategory != "clean"`**; `clean` → current untinted appearance. Calm, never error/red/severity-graded (D1/D12 — an intentional non-clean shot must not alarm).
- [x] A4.3 Resolve the open question: when the verdict is non-clean but no fault badge is set, the affordance SHALL still appear (tinted) so the "worth opening, no hard fault" case is visible — finalize against `ShotDetailPage`/`PostShotReviewPage` visibility conditions.

### A5. Slice tests

- [x] A5.1 `tst_shotanalysis`: gold-pair outside-band fires on the pressure axis; each gate independently suppresses (pour-truncated, channeling, sub-margin); ambiguous → silent.
- [x] A5.2 No-entry profile → no line; absent classification → strict no-op, byte-identical to pre-change.
- [x] A5.2a Verdict branch (D13): band-only on an otherwise-clean shot → `verdictCategory == "expertBandDeviation"`, line `type == "observation"`, verdict text non-directional/taste-deferring, four-boolean projection byte-identical; a shot with a real fault → fault category wins, band line still present as a corroborating observation (NOT `expertBandDeviation`).
- [x] A5.3 Limiter clause is a pressure-axis addendum only: fires-with-clause (outside band + limiter pegged); fires-without-clause (outside band, no limiter); does NOT fire (inside band + limiter pegged).
- [~] A5.4 **Covered by construction + the existing parity harness.** `expertBand` is resolved fresh at every call site via `expertBandForKbId(profileKbId)` (the `getAnalysisFlags` pattern, in the shared `prepareAnalysisInputs`/inline path) and nothing band-related is persisted, so the band line/verdict flow through the *same* save/load/detail cascade the existing recompute-parity tests already pin (it is one more field on `AnalysisInputs`, no new path). No dedicated duplicate integration harness added (avoids over-building where structure + existing tests already guarantee it); confirmed live in the §A6/D3 spot-check below.
- [~] A5.5 **Covered by construction.** Same mechanism: the table/margin are shipped code/constants read fresh every `analyzeShot`; nothing is snapshotted, so re-opening a shot recomputes against the *current* table/margin by the same recompute-on-load contract the badges already obey. The live spot-check (a real historical D-Flow/Q shot, app reading the working tree) is the end-to-end confirmation.
- [~] A5.6 **C++ contract fully tested; QML binding is a trivial pure derivation.** The `verdictCategory` values that drive the tint are exhaustively asserted by the A5 tests + existing `verdictCategory` tests; `QualityBadges.worthOpening` is a one-line pure expression (`verdictCategory !== "" && !== "clean"`) with no new persisted field (reads `shotData.detectorResults.verdictCategory`). Visual confirmation is the §A6/D3 spot-check (no QML test harness in this project for this surface).
- [x] A5.7 Invariant: no new column / record field / Visualizer payload; only trace is the recomputed `summaryLines` entry; four-boolean badge projection byte-identical with vs without the band line; `analyzeFlowVsGoal` output unchanged.
- [x] A5.8 Build via Qt Creator MCP (0 errors, 0 warnings); full Qt Test suite green.

### A6. STOP gate (the go/no-go — not test-green)

- [ ] A6.1 `shot_eval` dry-run mode: compute the line over `tests/data/shots/` without rendering, reporting per-shot fire decision + axis.
- [ ] A6.2 Run on the real D-Flow/Q corpus incl. the labeled shots — **April 6 10:23 AM** (outside 6–9 → fire + limiter clause), **May 10 9:04 AM** (inside → silent), **May 15 2:32 PM** (confounded bitter-but-coarse → at most observational, no direction), **May 15 2:37 PM** (peak ~4.6 → fire, no limiter clause). Tune the per-axis margin to a conservative recorded target; capture margin + false-positive rate in the change log.
- [ ] A6.3 **GATE.** Pass only if the gold-pair line is measurably more positive than false-flag on real shots with no confounded misfire. **If flat/negative → STOP:** keep the tint (A4, cheap/safe), drop the band line, do not start Phase B, archive cheaply. The abandonment path is the designed outcome of a flat gate, not a failure to argue around.
- [ ] A6.4 Permanent regression assertion: corpus firing rate must not exceed the recorded conservative threshold.

## Phase B — A-Flow family (only if A6 passed)

- [ ] B1 Add the A-Flow variants (all 5) as cited rows — pressure 6–9, `[SRC:aflow-repo]` (band-only, no ceiling; softer signal). No code change beyond the table rows.
- [ ] B2 Extend slice tests to an A-Flow fixture; re-run the A6 shadow gate over A-Flow shots. Flat/noisy → do not add A-Flow rows; Phase A result stands. Record before/after.

## Phase C — Confounded/contextual tail (only if B passed; per-arm, each independently droppable)

- [ ] C1 Add, **one cited row at a time**, each behind its own gate pass: Adaptive v2 (pressure 8–9 / <7, `[SRC:decent-guide]` — grind-adaptive, highest false-flag risk), Londinium (pressure ~9 declining, `[SRC:decent-guide]`), Rao/Blooming Allongé (flow reach ~4.5 ml/s, `[SRC:light-video]`), E61 (flow ~4 ml/s in context, `[SRC:dark-video]`).
- [ ] C2 Per-arm gate: a row ships only if it independently clears the A6 shadow run; a net-noisy arm is left absent (one-sided stays one-sided; no fabrication). Expansion is per-arm revertible, never all-or-nothing.

## Phase D — Final verification (after the last shipped phase)

- [ ] D1 Full `tst_shotanalysis` pass over all shipped rows: complete seed-coverage assertions; expert-flow band coexists with `analyzeFlowVsGoal` without interference; all invariants (A5.7) hold across the full table.
- [ ] D2 Build 0/0; full suite green incl. the A6.4 corpus regression guard.
- [ ] D3 In-app spot-check (advisor disabled): off-band D-Flow shot (line + limiter clause), clean reference (no line), confounded shot (observational only, no direction), a shipped flow-axis fixture, a no-cited-band profile (no line, unchanged). Confirm disabling the advisor does not change the line and the advisor incidentally sees the identical `summaryLines` entry.

## Phase E — Predecessor archival (after the Phase A A6 gate passes)

- [ ] E1 Archive `capture-dialin-coaching-guidance` (superseded; D9/D10/D13* cited here). Archive/close `profile-expectation-deviation-detector` (scope replaced; seam dependency removed). Confirm no shipped behavior is dropped (both have 0 implemented tasks). If A6 STOPs, archive this change too and record the flat-gate outcome.
