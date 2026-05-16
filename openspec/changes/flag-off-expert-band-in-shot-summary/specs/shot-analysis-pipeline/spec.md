## ADDED Requirements

### Requirement: A profile-aware expert-band check SHALL run within the single analyzeShot cascade

`ShotAnalysis::analyzeShot` SHALL run, as part of the single cascade (no second pass, no separate orchestrator), a profile-aware check that compares the shot against a citation-bound per-profile expert-recommended operating band. Each band entry SHALL carry an **axis** (`pressure-peak` or `extraction-flow`), a **band**, a `[SRC:...]` provenance tag, and a confidence marker, all taken verbatim from a cited source (the grading in `capture-dialin-coaching-guidance` design D9/D10/D10b is authoritative; no value is invented). Entries SHALL be keyed by profile title using the existing `Also matches:` resolution so aliased twins resolve to one entry. The observed value SHALL be read from values the cascade already computes — peak pressure for `pressure-peak`, extraction flow for `extraction-flow` — and `analyzeFlowVsGoal` (the profile's *commanded*-flow check) SHALL be left unmodified.

#### Scenario: A profile with a cited pressure band exposes the check on the pressure axis

- **WHEN** `analyzeShot` resolves a profile whose cited entry is `pressure-peak` 6–9 bar (e.g. D-Flow / Q, `[SRC:profile-notes]`)
- **THEN** the check SHALL compare the shot's peak pressure to 6–9 bar
- **AND** the entry SHALL also resolve for that profile's aliased twin (Damian's Q) via `Also matches:` without a duplicate entry

#### Scenario: A profile with a cited flow band exposes the check on the flow axis

- **WHEN** `analyzeShot` resolves a profile whose cited entry is `extraction-flow` (e.g. Rao Allongé "reach ~4.5 ml/s", `[SRC:light-video]`)
- **THEN** the check SHALL compare the shot's extraction flow to that band
- **AND** the existing `analyzeFlowVsGoal` commanded-flow result SHALL be unchanged and SHALL be able to emit independently of this check

#### Scenario: A profile with no cited band is a strict no-op

- **WHEN** `analyzeShot` resolves a profile with no cited band entry
- **THEN** no expert-band line SHALL be produced, no band or axis SHALL be fabricated, and the entire `AnalysisResult` (lines, detectors, badges) SHALL be byte-identical to the pre-change behavior for that shot

### Requirement: An out-of-band shot SHALL emit one soft, observational, taste-deferring summary line

When the observed value on the entry's axis is outside the cited band by the configured margin AND the hard AND-gate passes, `analyzeShot` SHALL append exactly one low-authority `summaryLines` entry that names the observed value and the cited band and defers to taste. The line SHALL NOT state or imply a grind direction (band-vs-actual is a confounded signal; a directional verdict here is the #1155 failure). The hard AND-gate SHALL suppress the line when the cascade already fired pour-truncated or channeling, or bean freshness is unknown/very-fresh; anything ambiguous SHALL be silent. This guidance SHALL reach the in-app Shot Summary and (incidentally, identically) the AI advisor via the existing `summaryLines` path; no separate copy is authored and the advisor is not required.

#### Scenario: Outside the cited band, gate clear → one taste-deferring line

- **WHEN** the observed value on the cited axis is outside the band by the margin AND pour-truncated/channeling did not fire AND bean freshness is known
- **THEN** exactly one `summaryLines` entry SHALL be appended, naming the observed value and the cited band and deferring to taste
- **AND** it SHALL NOT assert or imply "grind coarser/finer"

#### Scenario: Gate blocks the line

- **WHEN** the observed value is outside the band BUT the cascade already fired pour-truncated or channeling, or bean freshness is unknown/very-fresh
- **THEN** no expert-band line SHALL be produced

#### Scenario: Inside the band → silent

- **WHEN** the observed value on the cited axis is within the band
- **THEN** no expert-band line SHALL be produced

### Requirement: The firmware limiter SHALL be a corroborating clause only, never the band

On the `pressure-peak` axis, when an out-of-band shot also pegged the machine's pressure limiter, the line MAY append a corroborating clause noting the limiter hit. The limiter value SHALL NOT be used as the band. The line SHALL be able to fire with no limiter hit, and a limiter touch with the peak still inside the cited band SHALL NOT fire the line.

#### Scenario: Out of band and limiter pegged → line with corroborating clause

- **WHEN** the peak is outside the cited pressure band AND the shot pegged the machine pressure limiter
- **THEN** the line SHALL fire AND MAY append the corroborating limiter clause

#### Scenario: Out of band, no limiter → line without clause

- **WHEN** the peak is outside the cited pressure band AND no limiter hit occurred
- **THEN** the line SHALL fire without any limiter clause

#### Scenario: Limiter pegged but inside the band → no line

- **WHEN** the shot pegged the machine pressure limiter BUT the peak is within the cited band
- **THEN** no expert-band line SHALL be produced

### Requirement: The expert-band signal SHALL NOT influence any badge

The expert-band check SHALL contribute only to `summaryLines`. It SHALL NOT set, clear, or otherwise influence the grind badge or any other badge, and SHALL NOT alter `decenza::deriveBadgesFromAnalysis`. The four-boolean badge projection SHALL remain driven solely by the existing intrinsic detectors. No confidence/quality/trust score SHALL be synthesized, and nothing SHALL be written to the shot record, any database column, or visualizer.coffee.

#### Scenario: Four-badge projection is byte-identical with and without the band line

- **WHEN** the same shot is analyzed and the expert-band line is appended
- **THEN** `decenza::deriveBadgesFromAnalysis` SHALL produce the same four booleans it would produce if the band line were absent
- **AND** no new column, record field, synthesized score, or Visualizer payload field SHALL exist; the only trace SHALL be the recomputed `summaryLines` entry

#### Scenario: A real intrinsic fault still drives the grind badge independently

- **WHEN** a shot is a true gusher/choke (an intrinsic `detectGrindIssue`/`GrindCheck` fault) AND is also outside the cited band
- **THEN** the grind badge SHALL be set by the intrinsic detector exactly as today
- **AND** the expert-band line SHALL render as additional corroborating prose without being the cause of the badge state

### Requirement: The band line and tint SHALL be recomputed every open from current code and table, never frozen at save

The expert-band check and the resulting `verdictCategory`/tint SHALL be produced by `analyzeShot` on every open (save / recompute-on-load / detail-load), inheriting the existing badge recompute-on-load contract. The **only** band-relevant value persisted on the shot SHALL be profile identity (already persisted); the citation-graded band table, the firing margin, and the gate logic SHALL be resolved fresh from shipped code/data at every compute and SHALL NOT be snapshotted onto the shot record. Any serialized `verdictCategory`/line SHALL be a non-authoritative cache that the recompute refreshes; display SHALL bind to the recomputed value. `analyzeShot` SHALL still be invoked exactly once on the canonical detail-load path.

#### Scenario: Same shot, same table → same line on save / load / detail

- **WHEN** the same shot is analyzed on the save path, the recompute-on-load path, and the detail-load path with the band table unchanged
- **THEN** the same `summaryLines` entry SHALL appear in the same position on all three
- **AND** `analyzeShot` SHALL be invoked exactly once on the canonical detail-load path

#### Scenario: Improving the table retroactively improves historical shots

- **WHEN** a shot is saved under one band table, the table is later corrected/expanded or the firing margin retuned, and the same historical shot is then re-opened
- **THEN** the re-opened shot SHALL reflect the **new** table/margin (a previously-absent line may now appear, a previously-present line may now be absent or changed, and the tint SHALL track the recomputed `verdictCategory`)
- **AND** no value from the original save SHALL shadow or override the recomputed result

#### Scenario: Tint binds to the recomputed verdict, not a stale cache

- **WHEN** a historical shot whose serialized `verdictCategory` differs from what current code would compute is opened
- **THEN** the tint SHALL reflect the freshly recomputed `verdictCategory`, not the stale serialized value

### Requirement: The Shot Summary entry affordance SHALL signal whether the analysis has anything worth reading

The `QualityBadges` affordance that emits `summaryRequested()` SHALL present a single calm tint derived purely from the already-computed `DetectorResults::verdictCategory`: when `verdictCategory` is exactly `clean` the affordance SHALL retain its current untinted appearance; for **any** other `verdictCategory` value it SHALL show the tint, indicating the summary is worth opening. The tint SHALL NOT be severity-graded into an error/alarm state, SHALL introduce no new threshold/score/classification, and SHALL NOT cause anything to be persisted or exported beyond the already-serialized `verdictCategory`. The tint SHALL NOT alter `analyzeShot`, the four-boolean badge projection, or the dialog contents.

#### Scenario: Clean verdict → untinted affordance

- **WHEN** the resolved shot's `verdictCategory` is exactly `clean`
- **THEN** the affordance SHALL retain its current untinted appearance and SHALL NOT show the tint

#### Scenario: Any non-clean verdict → calm "worth opening" tint, not an alarm

- **WHEN** `verdictCategory` is any value other than `clean` (including a deliberately-pulled experimental shot that produced `minorIssues*`)
- **THEN** the affordance SHALL show the single calm tint
- **AND** the tint SHALL NOT use error/alarm styling or a severity-graded scale

#### Scenario: The cue adds no judgment and no persistence

- **WHEN** the cue is rendered for a shot
- **THEN** it SHALL be a pure function of the existing `verdictCategory` with no new threshold/score
- **AND** no new column, record field, synthesized score, or Visualizer payload SHALL be introduced by the cue
- **AND** the four-boolean badge projection and the dialog contents SHALL be unchanged by the cue

#### Scenario: The expert-band line reaches the cue through the existing verdict

- **WHEN** the expert-band check appends its `summaryLines` entry and the resulting `verdictCategory` is non-clean
- **THEN** the affordance SHALL show the accent via the same `verdictCategory` path, with no separate coupling between the band check and the cue
