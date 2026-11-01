# Proposal

## Why

PORTAL conductivity needs mass-weighted interpretation and independent refractometer references before it can estimate TDS/EY. The implementation must preserve behavior for users without PORTAL and keep diagnostic logs out of the measurement contract.

## What Changes

- Analyze EC against recorded cup mass and expose relative integral/tail metrics.
- Fit the same coffee and mounting geometry from independent physical-scale references.
- Persist actual serving weight source, mounting and sensor presence; backfill legacy data once during migration and exclude unknown provenance.
- Show translated review/history/settings results on shared native/Wasm surfaces, without changing user notes or general shot verdicts.
- Send one structured PORTAL block to the advisor and document calibration in the separate wiki.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `belka-portal`: mass-domain analysis, provenance, calibration and advisory presentation.

## Impact

Shot capture, SQLite schema/migration/imports, shot projections, advisor payload, shared QML and PORTAL tests. No machine-control change.
