# dialing_get_context

The primary read for a dial-in conversation: one call returns everything needed to analyse a
shot and suggest a change.

## What comes back

| Block | What it holds |
|---|---|
| `dialInSessions` | Dial-in history grouped into sessions — runs of shots on the same profile within ~60 minutes of each other, each shot carrying a `changeFromPrev` diff against the one before it |
| `profileKnowledge` | The curated knowledge-base entry for the current shot's profile (~1 KB) |
| bean / grinder metadata | What the shot was pulled with |
| `grinderContext` | The settings range observed in the user's OWN shot history, the typical dial `stepSize` (noise-filtered, so one mistyped setting cannot skew it), and whether the burrs are swappable |
| `tastingFeedback` | Whether the shot has enjoyment, notes or refractometer data. When any is missing the block carries a recommendation to ASK THE USER before suggesting changes |
| `bestRecentShot` | The highest-rated past shot on the same profile within the last 90 days, with a `changeFromBest` diff, so advice can reference what success looked like rather than only what changed since the last pull. Omitted when no rated shot exists in that window |
| `sawPrediction` | Predicted post-cut drip in grams from the stop-at-weight learner. Espresso only; `sourceTier` says which model is active so confidence can be weighted. Omitted with no scale configured or no usable flow data |

`changeFromPrev` and `changeFromBest` share the shape `shots_compare` returns per comparison: `inputs` (what the user dialled — profile, brew temperature, dose, target yield, grind, RPM; bean and equipment changes are in each shot's own fields, and `shots_compare` lists them here too) and `outcomes` (what the shot did — duration, yield, ratio, first drop, peak pressure, mean flow, rating), each field as `{from, to, delta}`. A `from` or `to` of null means that shot did not record the value. Inputs within measurement noise (dose ±0.3 g, target ±0.5 g, grind ±0.25 step, RPM ±25) are not listed.

The 90-day window on `bestRecentShot` keeps the anchor inside the user's current setup era.

## includeFullKnowledge

Default returns only the current profile's KB entry. `includeFullKnowledge: true` adds the
dial-in system prompt, the reference tables and the cross-profile catalog — about 18 KB in
total. Useful once at session start, redundant on later turns.

## Grinder settings are user notation

Settings appear exactly as the user entered them: numbers, letters, click counts, or
grinder-specific notation such as Eureka multi-turn (`1+4` = one rotation plus position 4).
Do not normalise them.

## What is deliberately NOT here

Cross-profile grind translation — the recommended setting for a profile OTHER than the current
shot's. That lives in `dialing_get_grinder_calibration`, because it is a stable property of the
grinder: fetching it once on demand keeps a multi-turn dial-in lean.
