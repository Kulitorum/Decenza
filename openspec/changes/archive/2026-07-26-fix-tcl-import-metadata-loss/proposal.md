## Why

Two values a `.tcl` profile genuinely carries are dropped on import, and both were found while
verifying a Visualizer-downloaded `D-Flow / Q` against the shipped built-in. The frames were
byte-identical; the metadata was not.

**Multi-word bare values truncate at the first space.** `topLevelAssignments` takes only the
first whitespace-delimited token for an unbraced value. Visualizer's `.tcl` export writes
`profile_title D-Flow / Q` unbraced, so the profile imports as `D-Flow`. Confirmed against
`tclsh`: Tcl's own `array set` reads the same thing and invents a junk key `/` → `Q`, so Decenza
is being faithful — but the result is a profile that loses its category (parsed from the slash
prefix, so it drops out of the D-Flow group), collides with the next Visualizer D-Flow download
on `d_flow.json`, and in **de1app would lose its editor entirely**, since the dispatch matches
`[string range $title 0 7]` against the literal `"D-Flow /"`. Every multi-word title from a
Visualizer TCL export is affected — `Damian's Q` → `Damian's`, `Tea portafilter/Sencha` → `Tea`.
The JSON export is unaffected; this is TCL-only. It reaches the in-app importer too, which
requests `?format=json` but still branches on TCL because Visualizer returns it for some shots.

**de1app's per-profile dose is unknown to us.** `profile_grinder_dose_weight` and
`profile_grinder_setting` are in de1app's canonical `profile_vars` list (`vars.tcl:3305`), so
de1app writes them into every profile it saves. Decenza models neither and lists neither as
ignored — so a Streamline-saved profile's real dose is silently discarded, and the keys would
surface in the drift gate the moment one appeared in the corpus. None of the 88 shipped de1app
profiles carries them, which is the only reason the gate passes today.

Separately, the `preserve-recipe-visualizer-roundtrip` change is still open on a premise that has
since been disproved and should be withdrawn rather than left as standing intent.

## What Changes

- Bare (unbraced, unquoted) values of free-text keys — `profile_title`, `author`,
  `profile_notes` — take the rest of the line rather than the first token. Numeric, enum and code
  keys keep the first-token rule. `beverage_type` and `profile_language` are deliberately NOT in
  the list: `beverage_type` is a bare-written enum across the corpus (`espresso`, `cleaning`,
  `tea_portafilter`, …) where reading a malformed line whole would drop the classification —
  strictly worse than truncation. `original_profile_title` is excluded because Decenza models it
  nowhere and including it would put an unhandled key into the drift gate.
- `profile_grinder_dose_weight` maps to `recommended_dose`, setting `has_recommended_dose` when
  the value is greater than zero. The companion flag cannot come from the scalar table, which
  yields a bare double, so the conditional lives in `Profile::loadFromTclString`. The table row
  MUST leave the absent-value substitute unset: the same table drives the built-in drift
  comparison, and supplying a `0` would fail every built-in against its de1app source.
- `profile_grinder_setting` is listed as known-and-ignored with its evidence, alongside the
  existing `grinder_model` / `grinder_setting` / `grinder_dose_weight` entries.
- `preserve-recipe-visualizer-roundtrip` is withdrawn, with the reason recorded.
- An issue is opened against `miharekar/decent-visualizer` for the unbraced title, since the
  export is the root cause and de1app users are affected identically.

## Capabilities

### New Capabilities

<!-- none: both behaviours belong to the existing Tcl-import capability -->

### Modified Capabilities

- `de1app-profile-parity`: the scalar set that survives a Tcl import gains
  `profile_grinder_dose_weight`; a new requirement governs how a bare multi-word value is read.

## Impact

- `src/profile/de1apptclfields.cpp` — `topLevelAssignments` bare-value rule, the scalar field
  table, the known-ignored list
- `src/profile/profile.cpp` — `loadFromTclString`, for the dose flag
- `tests/tst_tclimport.cpp` and the de1app profile corpus round-trip assertions
- `docs/CLAUDE_MD/RECIPE_PROFILES.md`. No wiki manual entry: this restores behaviour a user
  already expects rather than changing the documented feature set
- `openspec/changes/preserve-recipe-visualizer-roundtrip/` — withdrawn
- No change to frames, BLE output, or any numeric profile value. The bare-value rule is verified
  to alter nothing across all 88 de1app profiles, none of which carries a bare multi-word value.
- Deliberately excluded: any new dose UI. Recipes own dose going forward, and profiles keep
  assuming the existing 18 g default.
- Coordinates with `replace-recipe-block-with-recommended-dose`, which also writes
  `recommended_dose` (promoting a stored `recipe.dose`). Two sources, one field: the block
  promotion fires only on a non-default value with no explicit recommendation already set, and
  this import fires only on a value greater than zero. Neither overwrites an explicit
  recommendation.
