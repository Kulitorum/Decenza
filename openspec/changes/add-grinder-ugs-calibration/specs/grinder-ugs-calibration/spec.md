## ADDED Requirements

### Requirement: The system SHALL support an opt-in deliberate UGS calibration per grinder + burrs

The system SHALL let a user calibrate a grinder by recording two anchor shots on profiles far apart on the UGS scale, and SHALL compute and persist a Conversion Key for that grinder + burrs:

```
conversionKey = (coarseAnchorSetting − fineAnchorSetting) / (coarseAnchorUGS − fineAnchorUGS)
```

It SHALL be stored per `(grinderModel, grinderBurrs)` in a `SettingsGrinder` sub-object with both anchors' profiles, UGS values and settings, the Conversion Key and a timestamp. It SHALL be opt-in; without one, Phase 1 behaviour SHALL NOT degrade.

#### Scenario: User completes a two-anchor calibration

- **WHEN** the user records a fine-anchor shot and a coarse-anchor shot on profiles ≥ K UGS apart for a given grinder + burrs
- **THEN** the system SHALL compute the Conversion Key from the two settings and their UGS values
- **AND** persist it keyed by `(grinderModel, grinderBurrs)` with both anchors and a timestamp

#### Scenario: Anchors too close together are rejected

- **WHEN** the two chosen anchor profiles are closer than the minimum UGS span
- **THEN** the system SHALL refuse to store the calibration and SHALL explain that the anchors must be far apart for a reliable Conversion Key

#### Scenario: No stored calibration leaves Phase 1 untouched

- **GIVEN** no deliberate calibration is stored for the current grinder + burrs
- **WHEN** `buildGrinderCalibrationBlock` runs
- **THEN** it SHALL behave exactly as the Phase 1 within-coffee path with no error or degradation

### Requirement: A stored Conversion Key SHALL take precedence over the mined within-coffee key

When a deliberate calibration is stored for the resolved shot's grinder + burrs, `buildGrinderCalibrationBlock` SHALL use the stored Conversion Key instead of the mined within-coffee key, SHALL set `confidence: "calibrated"`, and SHALL set the validated range to the deliberate calibration's anchor span. The per-coffee intercept rule and the extrapolation cap SHALL still apply (numbers remain anchored on a recent dialed-in shot of the current coffee and bounded to the calibrated range + cap).

#### Scenario: Stored calibration drives a calibrated-confidence block

- **GIVEN** a stored deliberate calibration spanning UGS 0–8 for the current grinder + burrs and a recent dialed-in shot on the current coffee
- **WHEN** `buildGrinderCalibrationBlock` is called
- **THEN** `confidence` SHALL be `"calibrated"`
- **AND** `conversionKey` SHALL equal the stored value
- **AND** profile numbers SHALL still be anchored on the current coffee's recent shot and bounded by the calibrated range plus the extrapolation cap

### Requirement: Long-hop numeric output from deliberate calibration SHALL be validation-gated

Numeric recommendations far outside the Phase 1 mined window (long-hop, e.g. lever → turbo) enabled by a deliberate calibration SHALL sit behind a default-off gate until validated against at least one independent real shot database in the `shot_eval` corpus. With the gate off, a calibration MAY report `confidence: "calibrated"` within the Phase 1 window but SHALL fall back to directional output beyond it.

#### Scenario: Gate off — long-hop stays directional even with a stored calibration

- **GIVEN** a stored full-range deliberate calibration but the validation gate is off (default)
- **WHEN** a long-hop profile (UGS far beyond the Phase 1 window) is requested
- **THEN** that profile SHALL be reported `source: "directional"` with no number

#### Scenario: Gate on after independent validation — long-hop numbers permitted

- **GIVEN** the deliberate-calibration mechanism has been validated on ≥1 independent database fixture and the gate is enabled
- **WHEN** a long-hop profile within the deliberate calibration's span is requested with a current-coffee anchor present
- **THEN** that profile MAY receive a numeric `rgs` bounded by the calibrated range plus the extrapolation cap
