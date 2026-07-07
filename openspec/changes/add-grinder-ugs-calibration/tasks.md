# Tasks: Deliberate UGS Grinder Calibration (Phase 2)

## 0. Decision gate

- [ ] 0.1 Go/no-go: decide the two-shot calibration ritual is worth the UI
      surface (vs. Phase 1 directional guidance being sufficient). If no-go,
      delete this change — do not archive it.
- [ ] 0.2 Decide the capture UX shape (guided flow vs. post-shot anchor action)
      and the anchor rule (specific profiles vs. any two ≥ K UGS apart)

## 1. Storage & capture

- [ ] 1.1 Add a `SettingsGrinder` domain sub-object (settings-architecture
      rules: not on `Settings` directly; `qmlRegisterUncreatableType`;
      `Settings.grinder.*` QML access) storing per-`(grinderModel, grinderBurrs)`
      calibration records
- [ ] 1.2 Implement the two-anchor capture (record fine + coarse anchor shots,
      reject anchors closer than the minimum UGS span, compute + persist the
      Conversion Key with both anchors + timestamp)

## 2. Consumption & gating

- [ ] 2.1 Make `buildGrinderCalibrationBlock` prefer a stored Conversion Key
      when present → `confidence: "calibrated"`, validated range = deliberate
      anchor span; per-coffee anchor + extrapolation cap still apply
- [ ] 2.2 Add the default-off long-hop validation gate; with the gate off,
      calibrated confidence holds only within the Phase 1 window and falls
      back to directional beyond it
- [ ] 2.3 Validate the mechanism against the markpalmos fixture offline
      (`tools/calib_analysis.py`) before enabling the gate

## 3. Tests

- [ ] 3.1 Stored-key precedence; anchors-too-close rejection; no-calibration
      leaves Phase 1 byte-identical; gate-off keeps long-hop directional;
      gate-on (after fixture validation) permits bounded long-hop numbers
