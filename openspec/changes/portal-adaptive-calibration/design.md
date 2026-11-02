# Design

## Context

See proposal.md and the belka-portal delta. The shared QtRO services carry review and settings data to both native and Wasm QML.

## Goals / Non-Goals

Use recorded measurements as the calibration contract. General shot verdicts and machine control keep their existing behavior.

## Decisions

- Each accepted weight packet records its actual serving source through ShotTimingController into ShotDataModel, before UI/control callbacks. Any nonphysical packet makes the shot ineligible, including a manual stop before the next graph sample. Mounting is captured only while PORTAL is active.
- Save provenance in typed projections, compressed samples and SQLite columns. Keep schema migration separate from the one-time background presence scan; normal reads never repair rows. Pre-change shots without provenance remain ineligible rather than guessing from log wording.
- Resolve persisted coffee names using one Unicode case-folded key before SQL selects matching identities from the partial PORTAL index. No global 200-shot bound can evict an older coffee. Non-PORTAL rows never load a fit; hidden settings panels never request one.
- Recompute eligibility on dose/yield edits inside both the metadata and Visualizer-pull transactions. Shared Options assembly carries corrected cup mass through save/import/review and advisor fallback.
- Keep observed baselines, tolerate at most 0.1 g of baseline noise and count equal-time EC packets once. Measurement quality controls ratio advice; calibration count/extrapolation controls TDS prediction confidence.
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
- The first owned/active PORTAL schedules a one-time sample-blob scan on the existing database worker; users without PORTAL never schedule it. Normal reads use the small PORTAL index. The presence-only scan model measured 185.3 ms median / 186.9 ms worst on 1,268 shots (9.74 MB), and 1858.1 / 1869.5 ms at 10x (96.10 MB), five runs with Python SQLite/zlib/JSON on this Mac; these are not Qt startup measurements.
- Live sensor acceptance and native/browser screen parity require a running paired installation; CI does not prove physical sensor behavior.

## Migration Plan

Migration 51 consolidates the unmerged PORTAL schema and adds a database-local completion marker, including upgrades from previews 48-50. It does not scan shots or sample blobs. The shared Settings source requests the presence scan only for an owned/active PORTAL and queues calibration behind it. A session flag coalesces requests; the persisted marker prevents a new scan after restart. Presence updates and completion commit together, so a failed pass remains retryable. The optional index is created after completion and cannot invalidate the pass. Recorded physical-scale references keep their quality flag; legacy missing provenance is excluded even before the scan completes.
