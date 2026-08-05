# dialing_get_grinder_calibration

Cross-profile grind guidance for the user's grinder and burrs. Espresso only.

Call it ONLY when the user asks about switching profiles, or wants a grind setting for a profile
other than the current shot's.

## The model

`grind(profile, coffeeBatch) ≈ batchBaseline + UGS·conversionKey`, derived from paired
within-roast-batch history. It is anchored on the CURRENT roast batch — re-fetch when the coffee
changes.

## Reading the response

- `confidence` — `"approximate"` means a gated numeric conversion is available; `"directional"`
  means there is not enough same-batch data and only relative guidance is possible.
- `usageConstraint` — a directive to follow verbatim.
- `currentProfileUgsPlaced` — `false` means the current profile is not on the UGS chart, so
  finer/coarser cannot be ordered against it.
- When approximate, also `conversionKey`, `calibratedUgsRange`, and `coffeeAnchor` (the recent
  dialed-in shot of the current coffee the numbers are anchored on).

Each `profiles[]` entry carries `profileName`, `ugs`, `source`, and conditionally `rgs` /
`direction`:

| `source` | Meaning | Carries |
|---|---|---|
| `history` | Median from the user's own current-batch shots | `rgs` |
| `derived` | Interpolated inside the calibrated range and cap | `rgs` |
| `directional` | Outside the cap, or no numeric calibration at all | no `rgs`; `direction` `"finer"`/`"coarser"` relative to the current profile |

**Never quote or compute a number for a `directional` profile.** Give finer/coarser and tell the
user to pull a reference shot. When a directional entry carries no `direction` either, the two
profiles cannot be ordered at all.
