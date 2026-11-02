# PORTAL conductivity and refractometer estimates

Evidence reviewed 2026-11-02. EC is an electrical measurement; refractometric TDS
is a different, empirically calibrated measurement. A useful correlation in one
experiment is not a universal conversion law or proof of PORTAL accuracy.

## Research and its limits

| Primary source | Relevant finding | Applicability limit |
| --- | --- | --- |
| [von Heynitz et al. (2023), J. Food Process Engineering, 46, e14473](https://doi.org/10.1111/jfpe.14473), sections 2.1–2.3 and 3.1 | In-line EC correlated with gravimetric TDS in a cold-brew reactor. The experiment controlled coffee, water, temperature and extraction technology; a Pt100 supplied temperature correction. Water mineralization contributed to the result. | The campaign used 20 °C feed water and a flooded vessel. It does not validate a hot, intermittently wetted espresso funnel. The authors explicitly distinguish charged substances from all dissolved solids. |
| [Sobolík et al. (2002), J. Food Engineering, 51, 93–98](https://doi.org/10.1016/S0260-8774(01)00042-5), publisher abstract | Specific conductivity of soluble-coffee solutions was studied across concentration and temperature, including 25–72 °C. | Instant-coffee solutions and their experimental temperature range do not establish PORTAL's compensation, calibration factor or a maximum valid espresso temperature. No coefficients from this paper are applied. |
| [Schmieder et al. (2023), Foods, 12, 2871](https://doi.org/10.3390/foods12152871), sections 2.5–2.7 and 3.1 | DE1 espresso fraction collection showed different extraction kinetics for individual compounds and TDS. Flow changed early concentration and decay. TDS was measured separately by calibrated refractometry at 20 °C after sample preparation. | The paper's abbreviation “EC” means espresso coffee, not electrical conductivity. It supplies no electrical EC-to-TDS conversion or sensor cutoff. |
| [USGS (2019), Specific Conductance, NFM A6.3](https://doi.org/10.3133/tm9A6.3), and [USGS field-method troubleshooting](https://pubs.usgs.gov/of/2000/of00-213/manual_eng/prepare.html) | Conductivity depends on dissolved species and temperature; calibration, thermal equilibration, cleanliness and trapped air affect measurements. | These are natural-water methods. Minute-long stabilization rules or percent tolerances from water protocols are not espresso-flow validity limits. |
| [Belka maintenance instructions](https://flicker-suit-bff.notion.site/Cautions-and-Maintenance-28132032eed6807dbef0c41c3ac57ceb) | Gentle cleaning is permitted. Between brews, shaking off remaining water or a quick hot rinse followed by removing excess water near the drain is described. | The inspected instructions do not require prewetting before every shot. They do not identify the auxiliary BLE field or document the BLE EC temperature-compensation transfer function. |

A flooded, stable laboratory cell and a transient espresso sensor are different
measurement systems. Bubbles, partial wetting, deposits and retained liquid can
change the conductive path or the association between sensor concentration and
scale mass. A dip is therefore not proof of channeling. Pressure and pump flow
describe brewing, not an independent electrical calibration. Same total EY also
does not establish identical ionic composition in each fraction.

## Software policy

- Keep EC and its mass integral in raw device units. Never write an EC-derived
  estimate into measured `drinkTds` or `drinkEy`, and never apply an assumed water
  coefficient or double-correct an undocumented device output.
- Resolve scale snapshots sharing a timestamp to the latest monotonic snapshot
  before integration; retain rejection of reversed time, nonfinite data and scale
  resets. An unusable recorded weight curve is distinct from a missing scale.
- A fitted coefficient alone does not authorize a prediction. At least two
  independent pairs must permit leave-one-out evaluation. The selected model
  must have a positive EC/TDS association and lower held-out RMSE than predicting
  each reference from the mean TDS of the other references. Otherwise withhold
  estimated TDS/EY from review, history and structured payloads. This comparison
  is a conservative model-selection policy, not an absolute accuracy guarantee.
- Report missing cross-validation as unavailable, not zero error. Display the
  actual prediction error when available. Fewer than five references remains
  the existing *preliminary* label; five is a product policy, not a scientific
  certificate of validity. Independent prospective measurements remain needed.
- Withhold estimates outside the measured EC/ratio envelope or when measurement
  quality is uncertain. Calibration remains specific to coffee, water, sensor,
  mounting and procedure. Changes in these conditions require fresh validation;
  matching coffee names alone cannot establish matching water or sensor state.
- Retain the existing 68 °C / (ratio >2.55 and temperature >=64 °C) caution as a
  conservative legacy screening heuristic. These values, 85% mass coverage,
  0.5 s gap/boundary allowances and the dip thresholds are engineering policies,
  not published universal coffee-conductivity validity boundaries. No claim of
  physical validity is inferred from falling below them.

## Temperature diagnostics

Store the additional BLE float as optional `auxiliaryTemperatureRaw` throughout
capture, compressed storage, import/export and shared shot projections. Its
physical meaning and units are unverified, so it is not treated as internal or
fluid temperature and never drives a correction. Old recordings omit the field;
absence must not become zero or reject otherwise usable legacy EC samples.

Compute `massWeightedTempC` and, only with complete auxiliary coverage,
`massWeightedAuxiliaryTemperatureRaw` on the same observed, gap-free positive
mass increments as the EC integral. Dry pre-shot periods and scale plateaus have
zero weight. Preserve `meanTempC` and the existing stored summary column as the
legacy sample mean, rather than silently changing old calibration inputs.

These diagnostics can help investigate warm-up and timing. They cannot determine
whether the hardware has already compensated EC or reconstruct missing legacy
measurements. Confirm field semantics and sensor response with Belka before
implementing temperature normalization. Native/browser interaction and actual
sensor accuracy require device acceptance, separate from builds and tests.
