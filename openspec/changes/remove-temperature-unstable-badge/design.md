## Context

The `temperatureUnstable` quality badge has existed since PR #649 (Tier 1 diagnostics), refined repeatedly: PR #898 added the `reachedExtractionPhase` gate, PR #1078 added the cold-startup warmup skip, and the `hasIntentionalTempStepping` check has carried the weight of suppressing it on the dozens of profiles whose actual-vs-goal temp gap is by design.

Today the detector reads stored shot curves, computes `avgTempDeviation(temperature, temperatureGoal, pourStart, pourEnd)`, and fires the badge when deviation exceeds `TEMP_UNSTABLE_THRESHOLD = 2.0°C` — unless `hasIntentionalTempStepping(temperatureGoal) == true` (goal range > `TEMP_STEPPING_RANGE = 5.0°C`) or the gates above (`pourStart > 0`, `reachedExtractionPhase`) reject the shot.

The detector touches a wider surface than its 30 lines suggest:
- `src/ai/shotanalysis.{h,cpp}`: detector logic, four constants, three static helpers, four `DetectorResults` fields.
- `src/history/shothistorystorage.cpp`: stored column `temperature_unstable`, write at save, recompute at load, lazy-persist in `requestReanalyzeBadges`, history-list filter in `buildFilterQuery`, MCP serialization in `convertShotRecord`.
- `src/history/shotbadgeprojection.h`: one row in the projection table.
- `src/ai/shotsummarizer.{h,cpp}`: per-phase `PhaseSummary::temperatureUnstable` markers, the `markPerPhaseTempInstability` helper, AI prompt observation lines.
- `src/mcp/mcptools_shots.cpp`: nested `detectorResults.temperature` block.
- QML: `qml/components/QualityBadges.qml` chip rendering, host-page property bindings (`ShotDetailPage.qml`, `PostShotReviewPage.qml`), filter chip on `ShotHistoryPage.qml`, `shotBadgesUpdated` signal arity (6 → 5 args).
- DB schema: column added in migration 11; will need a removal migration.
- Tests: ~10 cases in `tst_shotanalysis.cpp` and N entries across `tests/data/shots/manifest.json`.
- Docs: `docs/SHOT_REVIEW.md` §1, §2.3, §3, §4, §5, §6, §7; `docs/CLAUDE_MD/MCP_SERVER.md` "Shot Detector Outputs"; the `shot-analysis-pipeline` capability spec.

The change is mechanical deletion across this surface. The architectural decisions are: (a) what to do with the existing DB column, (b) how to handle the QML signal arity change, (c) whether to keep `reachedExtractionPhase` and `avgTempDeviation` in case they're useful later, (d) what happens to historical shots whose stored `temperature_unstable = 1` survives the schema change.

## Goals / Non-Goals

**Goals:**
- Eliminate the false-positive badge end-to-end on every code path that reads or writes it (badge UI, dialog observation, MCP, AI prompt, history filter, DB).
- Remove the dead constants, helpers, and `DetectorResults` fields so the codebase doesn't carry obviously unused machinery.
- Update the `shot-analysis-pipeline` capability spec to reflect the smaller `DetectorResults` shape and the four-badge projection.
- Drop or rewrite the spec's existing temperature-related requirements (the "Intentional temperature stepping does NOT fire the temp badge" scenario, etc.) so the spec doesn't lie.
- Keep historical shots loadable. Old DB rows with `temperature_unstable = 1` should not crash the load path or surface stale state.
- One coherent migration step — not a multi-version dance.

**Non-Goals:**
- Designing a replacement detector. We're removing because the signal isn't worth fixing; if a future change wants to add a real instability detector (variance, mid-shot crash, heater-failure heuristic), that's a separate proposal.
- Profile-tagging via `temp_drift_expected` analysisFlag (the rejected alternative).
- The grind-detector false positives surfaced in the same #1128 investigation — separate change.
- Graceful API deprecation for MCP consumers. This is defect removal; the field disappears.
- Backporting badge data on shot import (Visualizer JSON imports never carried the field anyway).

## Decisions

### Decision 1: Drop the `temperature_unstable` SQLite column outright

**Choice:** Migration 15 issues `ALTER TABLE shots DROP COLUMN temperature_unstable;` matching the existing migration style in `shothistorystorage.cpp`. (Migration 14 was taken by `enjoyment_source` between when this design doc was first drafted and when the implementation landed.)

**Alternatives considered:**
- *Leave the column dormant.* Pros: zero migration risk, no DB change. Cons: column rot accumulates; future readers wonder why the column exists; load path either ignores it (silently confusing) or has dead read code. Rejected — we have the migration framework in place and the column is now dead weight.
- *Set the column to NULL on load and keep the schema.* Worst of both — extra write traffic on every load, no schema cleanup.

**Rationale:** SQLite has supported `ALTER TABLE … DROP COLUMN` since 3.35 (March 2021); we ship newer than that on every platform. The existing migrations 10–13 are pure column additions, so this will be the first removal — no precedent to follow but no precedent to break either. Use a single-statement transaction matching nearby code style.

### Decision 2: Drop `reachedExtractionPhase`, `avgTempDeviation`, `hasIntentionalTempStepping` rather than keep them

**Choice:** All three static helpers are removed from `ShotAnalysis`.

**Alternatives considered:**
- *Keep them for future reuse.* Pros: easier to revive a temp detector later. Cons: dead code, dead test surface, confused readers, the helpers inevitably bit-rot, and the next reviver should be making fresh design decisions anyway (e.g., `reachedExtractionPhase` as defined gates on `TEMP_MIN_EXTRACTION_SEC` which only made sense for the temp detector).

**Rationale:** YAGNI. If we ever reintroduce a temp detector it'll have different gates and we'll write fresh helpers. Carrying these forward earns nothing.

### Decision 3: Reduce `shotBadgesUpdated` signal arity from 6 args to 5 (drop `tempUnstable`)

**Choice:** Change the signal signature; update both QML connections (`ShotDetailPage.qml`, `PostShotReviewPage.qml`); update the lazy-persist emit site in `requestReanalyzeBadges`.

**Alternatives considered:**
- *Pass `false` permanently in the temp slot.* Avoids QML breakage during the change but leaves a permanently-false bool in the signal — every reader has to know it's vestigial.

**Rationale:** Signal signatures are part of the QML/C++ contract. Carrying a vestigial slot adds no safety since the QML side is in this same repo and changes together. Cleaner to bite the bullet.

### Decision 4: Existing stored `temperature_unstable = 1` rows surface as nothing

**Choice:** Migration drops the column. Lazy-persist no longer touches it. The badge no longer renders. No retroactive UI adjustment for shots that historically had the badge.

**Alternatives considered:**
- *Show a "had-temp-unstable historically" indicator.* No user value — the underlying detector was firing on profile design, so the historical badge was likely wrong anyway.

**Rationale:** The badge was noise; surfacing its history is more noise.

### Decision 5: Update the `shot-analysis-pipeline` capability spec rather than create a sibling spec

**Choice:** The change ships a `specs/shot-analysis-pipeline/spec.md` delta that removes the temperature-related requirements / scenarios and rewrites the projection-mapping requirement to drop the `temperatureUnstable` row.

**Alternatives considered:**
- *Leave the spec mentioning temp and document the removal in proposal/design only.* Diverges spec from code.

**Rationale:** OpenSpec convention: specs describe current behavior. The change deletes behavior, the spec deletion follows.

## Risks / Trade-offs

[**Stored shots that drift on reload**] Today, shots loaded from history get their badges recomputed (PR #893's "always recompute on load"). For five-badge shots, recompute matched the column. Post-change, recompute will run with four badges and `temperature_unstable` won't exist as a column. → Mitigation: the lazy-persist code path needs to be updated *before* the migration runs (the migration runs on first launch after upgrade, the C++ code is already at the new version). Confirmed by reading the load path: the column is read into a local before the recompute compares it; just remove that local and the compare branch.

[**MCP consumer breakage**] External MCP clients that read `detectorResults.temperature` get a missing field. → Mitigation: this is the documented "post-1.7.4" change list; ship in a release with a clear changelog entry. The MCP clients we know about (the in-app advisor) are in this repo and update together. External AI agents querying historical shots will see the field disappear silently — same model as any breaking change to a JSON shape.

[**Test corpus drift**] The `shot_corpus_regression` ctest target uses `tests/data/shots/manifest.json` to lock in expected detector verdicts. Every fixture currently asserts on `temperature_unstable`. → Mitigation: bulk-edit the manifest to drop the field; run the suite locally before push to confirm it's the only assertion that needs to move.

[**Per-phase `PhaseSummary::temperatureUnstable` removal cascades to AI prompt format**] The AI advisor's user prompt currently lists per-phase temp markers. Removing them changes the prompt format slightly, which could affect cached responses (Anthropic prompt cache 1-hour TTL). → Mitigation: cache invalidation is automatic when the prompt template changes; existing cached entries simply expire. No code action needed.

[**SQLite DROP COLUMN compatibility**] We rely on SQLite ≥ 3.35. → Mitigation: every Qt 6.10 platform we ship ships SQLite > 3.35 (Android NDK, iOS, macOS system, Windows bundled). No platform issue.

[**Implicit dependencies on the temp helpers**] Other code may grep-call `reachedExtractionPhase` or `hasIntentionalTempStepping` from beyond what I've listed. → Mitigation: full repo grep for each removed symbol before deleting; tasks.md specifies an explicit grep step.

## Migration Plan

1. **Code changes ship in app version N** (current main → next release). The new code stops writing to `temperature_unstable`, stops reading it on load, and stops emitting it in MCP / signal / QML.
2. **Schema migration 15 runs on first launch** of version N. Drops the column. Idempotent — checks the schema before running, like existing migrations.
3. **Rollback story:** if we have to roll back to N-1 after a user has run the migration, the column is gone but N-1's load path tolerates a missing column? **Verify before merge** — if N-1 reads the column unconditionally, rollback would crash. Likely we have to either (a) accept no-rollback, or (b) make the column drop conditional on a feature-flag / settings key. Default plan: no-rollback (consistent with all prior migrations being one-way).

No data migration is needed (no value being preserved or transformed).
