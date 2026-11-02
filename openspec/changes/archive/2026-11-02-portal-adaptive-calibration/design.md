# Design

## Context

See proposal.md and the belka-portal delta. The shared QtRO services carry review and settings data to both native and Wasm QML.

## Goals / Non-Goals

Use recorded measurements as the calibration contract. General shot verdicts and machine control keep their existing behavior.

## Decisions

- Each accepted weight packet records its actual serving source through ShotTimingController into ShotDataModel, before UI/control callbacks. Any nonphysical packet makes the shot ineligible, including a manual stop before the next graph sample. Mounting is captured only while PORTAL is active.
- Save provenance in typed projections, compressed samples and SQLite columns. The migration adds columns only; normal reads never repair rows. Pre-change shots without provenance remain ineligible rather than guessing from log wording.
- Resolve persisted coffee names using one Unicode case-folded key before SQL selects matching identities among `portal_present=1` shots. No global 200-shot bound can evict an older coffee. Non-PORTAL rows never load a fit; hidden settings panels never request one.
- Recompute eligibility on dose/yield edits inside both the metadata and Visualizer-pull transactions. Shared Options assembly carries corrected cup mass through save/import/review and advisor fallback.
- Keep observed baselines, tolerate at most 0.1 g of baseline noise and count equal-time EC packets once. Measurement quality controls ratio advice; calibration count/extrapolation controls TDS prediction confidence.
- Coalesce ordered scale snapshots sharing a timestamp to their latest running-maximum value before interpolation. Preserve reset/time-reversal rejection and distinguish unusable recorded weight data from missing scale provenance.
- Translate structured reasons/advice in the shared QML client so language switches and Wasm clients use their own TranslationManager.
- Assemble one JSON PORTAL block for AI/MCP. Keep user notes untouched.

## Evidence-based extension

See `docs/PORTAL_EVIDENCE.md` for primary sources and applicability limits.
Withhold predictions without complete leave-one-out evaluation, positive EC/TDS
association and improvement over the leave-one-out reference-mean baseline.
Keep fitted coefficients available for inspection; absence of validation is not
zero error. Preliminary count and existing thermal/coverage limits remain
engineering policies, not universal scientific validity boundaries.

Persist the optional auxiliary BLE float without assigning physical units or
using it for temperature correction. Compute diagnostic temperature means only
on covered positive mass increments; retain the legacy sample-temperature mean
and its stored projection unchanged. No database migration is needed: the
optional field travels in the existing sample JSON/blob and shared projection.

## Risks / Trade-offs

- Legacy references become ineligible until new physical-scale measurements are saved.
- No historical sample blob is ever scanned: pre-change shots lack weight provenance, so a scan could mark them present but never make them references. Calibration reads run only for shots with PORTAL data and the owner's Settings panel, on the database worker, without an index (0.52 ms median / 2.81 ms worst on 1,339 shots; 8.71 / 9.65 ms at 10x, Python sqlite3 on an M-series Mac).
- Live sensor acceptance and native/browser screen parity require a running paired installation; CI does not prove physical sensor behavior.

## Migration Plan

Migration 51 consolidates the unmerged PORTAL schema. From main's 47 it only adds empty columns, an O(1) change in SQLite, so the users who never own a PORTAL pay nothing. From previews 48-50 one statement in the same transaction marks shots that already stored a PORTAL summary as present. Database import copies a v51 source's flags; an older source has no provenance to copy.
