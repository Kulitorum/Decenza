# Adaptive PORTAL calibration

## Implementation

- Merge current upstream main into the feature branch, retaining all prior PORTAL work.
- Derive k from suitable paired refractometer/EC measurements for the current coffee
  and mounting mode. Preserve leave-one-out estimates for the reviewed shot.
- Show k, active fit, references/exclusions, ratio coverage and cross-validation error
  in the shared Settings panel. Refresh after stored TDS changes; never hardcode a
  user's measured k. Keep calibration separate from high-frequency live snapshots.
- Distinguish a shot's training eligibility from preliminary/extrapolated predictions.
- Retain conservative slow-wetting, thermal, gap and mounting gates. Do not infer
  puck overcompression from pressure or grind setting alone.
- Make calibration searchable and explain TDS entry/exclusions in the manual.

## Validation

Extend existing PORTAL storage coverage through a real metadata save and the new
async Settings projection. Extend the existing QtRO disconnect test for calibration
withdrawal. Run full Linux tests, QML lint, Android and Wasm builds on GitHub Actions
under the owner's standing authorization. Do not install or change the live tablet.
Live source data is kept in temporary files and is not a public test fixture.
