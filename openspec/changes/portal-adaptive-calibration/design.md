# Design

## Context

See proposal.md and the belka-portal delta. The shared QtRO services carry review and settings data to both native and Wasm QML.

## Goals / Non-Goals

Use recorded measurements as the calibration contract. General shot verdicts and machine control keep their existing behavior.

## Decisions

- Each accepted weight packet records its actual serving source through ShotTimingController into ShotDataModel, before UI/control callbacks. Any nonphysical packet makes the shot ineligible, including a manual stop before the next graph sample. Mounting is captured only while PORTAL is active.
- Save provenance in typed projections, compressed samples and SQLite columns. Replace repeated log scans/read repairs with one migration. Pre-change shots without provenance remain ineligible rather than guessing from log wording.
- Select only the relevant coffee from the partial PORTAL index. No global 200-shot bound can evict an older coffee. Non-PORTAL rows never load a fit; hidden settings panels never request one.
- Recompute eligibility on dose/yield edits in the metadata transaction. Shared Options assembly carries corrected cup mass through save/import/review and advisor fallback.
- Keep observed baselines, tolerate at most 0.1 g of baseline noise and count equal-time EC packets once. Measurement quality controls ratio advice; calibration count/extrapolation controls TDS prediction confidence.
- Translate structured reasons/advice in the shared QML client so language switches and Wasm clients use their own TranslationManager.
- Assemble one JSON PORTAL block for AI/MCP. Keep user notes untouched.

## Risks / Trade-offs

- Legacy references become ineligible until new physical-scale measurements are saved.
- One-time migration reads historical sample blobs; normal reads use the small PORTAL index. Measure realistic history and a multiple before completion.
- Live sensor acceptance and native/browser screen parity require a running paired installation; CI does not prove physical sensor behavior.

## Migration Plan

Migration 50 adds all PORTAL columns and backfills legacy sensor presence in the schema-stamp transaction. Legacy references remain ineligible; backfill does no curve reconstruction or EC analysis. The optional index is created after the stamp. Moving backfill to first attachment conflicts with the original migration requirement and awaits the user's preference.
