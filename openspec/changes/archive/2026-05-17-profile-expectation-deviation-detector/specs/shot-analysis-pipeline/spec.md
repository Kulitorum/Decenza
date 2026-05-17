## ADDED Requirements

### Requirement: A profile-aware deterministic detector SHALL run within the single analyzeShot cascade

`ShotAnalysis::analyzeShot` SHALL accept an optional `ProfileExpectation` parameter (defaulting to absent, the same backwards-compatible pattern as `expectedFrameCount`) and SHALL run a profile-aware deviation detector as part of the single cascade. The detector SHALL compare the resolved shot's observed pressure / flow / time / preinfusion-dripping against the supplied expectation's centred envelope, two-sided `GrindTooCoarse:` / `GrindTooFine:` signatures, and `PressureShape:` qualifier. No second analysis pass and no separate post-cascade orchestration SHALL be introduced; the cascade SHALL remain in exactly one place.

#### Scenario: Detector participates in the one-place cascade

- **WHEN** `analyzeShot` is invoked with a `ProfileExpectation` whose `GrindTooCoarse:` signature the shot's observed values match with margin and no gate suppresses it
- **THEN** the returned analysis SHALL include exactly one profile-deviation observation
- **AND** it SHALL be produced by `analyzeShot` itself, not by a second pass or a separate post-processing step

#### Scenario: Absent expectation is a strict no-op

- **WHEN** `analyzeShot` is invoked without a `ProfileExpectation` (parameter unset), or with one whose relevant fields are absent
- **THEN** the detector SHALL contribute nothing
- **AND** the full analysis result (summary lines, four badge columns, detector results) SHALL be byte-identical to the result produced before this change for the same shot

### Requirement: The detector SHALL emit only a soft taste-deferring summary line and SHALL NOT add a badge

When it fires, the detector SHALL contribute at most one `summaryLines` entry with a low-authority severity type (`[observation]` or `[caution]`, never `[warning]`) and taste-deferring wording (it SHALL direct the user to judge by taste and SHALL NOT assert a definitive verdict). It SHALL NOT add, remove, or alter any of the four boolean badge columns, and the `decenza::deriveBadgesFromAnalysis` projection SHALL be unchanged. The line SHALL be rendered by the existing Shot Summary `summaryLines` path with no AI involvement.

#### Scenario: Firing contributes a prose line, not a badge

- **WHEN** the detector fires for a shot
- **THEN** the analysis SHALL contain one additional `summaryLines` entry of type `[observation]` or `[caution]` whose text defers to taste
- **AND** all four boolean badge columns SHALL be exactly what they would be without this detector
- **AND** the Shot Summary dialog SHALL render the line through its existing `summaryLines` path without invoking any AI surface

### Requirement: The detector SHALL hard AND-gate and bias to false negatives

The detector SHALL stay silent unless ALL of the following hold: a `ProfileExpectation` exists for the resolved profile; the observed values match a `GrindTooCoarse:` / `GrindTooFine:` signature, or clearly depart from the `PressureShape:` qualifier, with a configured margin; the cascade did NOT already fire pour-truncated or channeling for this shot; the profile's parsed `Suppress:` catalogue does not cover the observed behavior; the profile is not marked grind-tolerant-by-design; and bean freshness is not unknown or very-fresh. Any ambiguous or borderline case SHALL resolve to silence. Taste guidance SHALL always outrank this line; it SHALL never contradict or override a fired mechanical detector.

#### Scenario: Suppressed when the profile declares the behavior intentional

- **WHEN** the observed values match a too-coarse-like pattern but the profile's `Suppress:` catalogue lists that behavior as intentional (e.g. declining pressure on a lever-decline profile)
- **THEN** the detector SHALL NOT fire

#### Scenario: Suppressed when a mechanical detector already fired

- **WHEN** the cascade has already flagged pour-truncated or channeling for the shot
- **THEN** the profile-deviation detector SHALL NOT fire (the mechanical fault explains the curve; channeling/truncation reads take precedence)

#### Scenario: Suppressed for grind-tolerant or unknown-freshness cases

- **WHEN** the resolved profile is grind-tolerant-by-design, OR the shot's bean freshness is unknown or very-fresh
- **THEN** the detector SHALL NOT fire even if the observed values nominally match a signature

### Requirement: The new line SHALL recompute identically on save, load, and detail

Because the detector runs inside the single cascade, the profile-deviation line SHALL be produced consistently by the save-time path, the recompute-on-load path, and the detail-load path for the same shot and the same resolved `ProfileExpectation`. The existing "analyzeShot exactly once per detail load" and recompute-on-load contracts SHALL continue to hold with the detector present.

#### Scenario: Save / load / detail produce the same line

- **WHEN** the same shot with the same resolved profile expectation is analyzed via the save path, the recompute-on-load path, and the detail-load path
- **THEN** each SHALL produce the same profile-deviation `summaryLines` entry (same text, same type, same position relative to other lines)
- **AND** `analyzeShot` SHALL still be invoked exactly once on the canonical detail-load path

### Requirement: The detector SHALL NOT synthesize, persist, or export any signal

The detector SHALL compute no confidence, quality, or trust score that is fed anywhere as a hint. Its only output SHALL be the single soft `summaryLines` entry delivered through the existing line contract. It SHALL write nothing new to the shot record, any database column, or visualizer.coffee. It SHALL add no deterministic value that escapes the analysis result.

#### Scenario: No new persisted or exported state

- **WHEN** a shot is analyzed, saved, loaded, and (if configured) uploaded to visualizer.coffee
- **THEN** no new column, record field, or Visualizer payload field SHALL carry a profile-deviation score or verdict
- **AND** the only trace of the detector SHALL be the soft `summaryLines` entry within the in-memory/recomputed analysis result

### Requirement: A shadow-validation path SHALL measure the false-positive rate before the line is enabled

The implementation SHALL provide a dry-run mode that computes the profile-deviation line over the `tests/data/shots/` regression corpus via the `shot_eval` harness WITHOUT rendering it to users, reporting the firing rate on known-good corpus shots. The user-visible firing margin SHALL be tuned against that measured false-positive rate to a conservative target recorded in the change log, and the line SHALL NOT be enabled for users until that tuning is recorded.

#### Scenario: Shadow run reports a known-good false-positive rate

- **WHEN** the `shot_eval` harness runs the detector in dry-run mode over the known-good `tests/data/shots/` corpus
- **THEN** it SHALL report the number of corpus shots on which the line would have fired
- **AND** that figure SHALL be used to set the firing margin conservative before the line is enabled, with the chosen margin and resulting rate recorded

#### Scenario: Regression guard on the corpus false-positive rate

- **WHEN** the test suite runs after the line is enabled
- **THEN** a regression assertion SHALL fail if the detector's firing rate on the known-good corpus exceeds the recorded conservative threshold
