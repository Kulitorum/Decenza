# Design

## Context

See proposal.md and the belka-portal delta. The shared QtRO services carry review and settings data to both native and Wasm QML.

## Goals / Non-Goals

Use recorded measurements as the calibration contract. General shot verdicts and machine control keep their existing behavior.

## Decisions

- Weight packets carry their actual serving source through ShotTimingController into ShotDataModel; cached values retain their packet source. Any nonphysical sample makes the curve ineligible. The controller snapshots mounting at extraction start.
- Save provenance in typed projections, compressed samples and SQLite columns. Replace repeated log scans/read repairs with one migration. Pre-change shots without provenance remain ineligible rather than guessing from log wording.
- Select only the relevant coffee from the partial PORTAL index. No global 200-shot bound can evict an older coffee. Non-PORTAL rows never load a fit; hidden settings panels never request one.
- Recompute thermal eligibility on dose edits. Translate structured reasons/advice in the shared QML client so language switches and Wasm clients use their own TranslationManager.
- Assemble one JSON PORTAL block for AI/MCP. Keep user notes untouched.

## Risks / Trade-offs

- Legacy references become ineligible until new physical-scale measurements are saved.
- One-time migration reads historical sample blobs; normal reads use the small PORTAL index. Measure realistic history and a multiple before completion.
- Live sensor acceptance and native/browser screen parity require a running paired installation; CI does not prove physical sensor behavior.

## Migration Plan

Migration 50 adds provenance and a partial sensor-presence index; sample-derived backfill and schema stamping share a transaction. Restore a pre-update database backup when rolling back to code predating the new columns. No reset of user notes or general shot verdicts is needed.
