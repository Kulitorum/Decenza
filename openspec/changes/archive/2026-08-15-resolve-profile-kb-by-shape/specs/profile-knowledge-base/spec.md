## MODIFIED Requirements

### Requirement: The profile→KB resolver SHALL be exact-match-or-explicitly-unresolved

Resolution SHALL be ordered: (1) normalize the title (existing accent/punctuation normalization retained as logic) → **exact** lookup in an explicit **alias→`id`** map built from each entry's `displayName`, every `alsoMatches` entry, and the `defaultForEditorType` entries; (2) on a miss, a **deterministic recipe-alias longest-boundary-prefix** step; (3) on a miss, the editor-type `defaultForEditorType` fallback. The resolver SHALL yield an `id` (never a prose string); all consumers SHALL key on `id`. A total miss SHALL return an explicit unresolved outcome.

The recipe-alias prefix step SHALL be defined as: among registered aliases that do **not** belong to a `defaultForEditorType` entry (*recipe aliases* — the editor namespace is not a recipe identity and SHALL NOT anchor a prefix; the editor's "which editor" role is served solely by step 3), select every recipe alias that is a *boundary-prefix* of the normalized title — the alias immediately followed by a boundary character, defined as **any character that is not a letter** (a following letter, or end-of-string, SHALL NOT be a boundary; end-of-string is the exact-match case handled by step 1). This subsumes the previously enumerated set of `/`, `-`, space and ASCII digit and additionally admits punctuation such as `_`, `.`, `,` and `(`. The rule is stated as the complement of the one case the requirement actually turns on — a letter must not be a boundary, because that is what separates `D-Flow / Q` from `D-Flow / Quark` — rather than as an enumeration that silently omits whatever punctuation a user happens to type. The resolver SHALL resolve to the `id` of the **longest** matching recipe alias. This rule is total and deterministic: a string has exactly one prefix of each length, so two equal-length boundary-prefix recipe aliases would be the identical string and therefore reduce to the pre-existing duplicate/colliding-alias rejection; no new ambiguity class is introduced and no per-call "reject if multiple" heuristic is used. Matching SHALL be prefix-only — `contains`/substring matching SHALL NOT be used.

Every built-in/shipped/starter profile and editor-canonical output SHALL resolve via the step-1 exact alias lookup and SHALL NOT depend on the recipe-prefix step (they are all registered exact aliases; the prefix step is reached only after an exact miss and therefore cannot alter a built-in's resolution). The recipe-prefix step is exclusively the best-effort path for **user-created profiles derived from a real recipe** that keep the source recipe's name as the title prefix.

The order-dependent greedy `startsWith`/`contains` fallback historically removed from `matchProfileKey` SHALL NOT be reintroduced: an order-dependent, non-anchored, or non-deterministic non-exact best guess is prohibited. The recipe-alias longest-boundary-prefix step defined above is permitted precisely because it is anchored on a registered recipe alias, prefix-only, total, deterministic, and test-gated — it is the defined resolution behavior, not a best guess. `resolveKbInput` SHALL apply the same shared recipe-prefix step (after id-passthrough and exact alias→id) so a legacy persisted normalized-title kbId for a renamed variant resolves under the recompute-on-load contract.

#### Scenario: Custom-suffixed title resolves to the parent recipe variant

- **WHEN** a profile titled `"D-Flow / Q - Jeff"` is resolved
- **THEN** it resolves to the D-Flow-Q variant entry's `id` via the recipe-alias longest-boundary-prefix rule (the registered recipe alias `D-Flow / Q`, longer than the editor-name alias which is excluded from anchoring), never to the band-less D-Flow-default `id`, and never via an order-dependent substring scan

#### Scenario: The rule applies to any documented profile, not only D-Flow/A-Flow

- **WHEN** `"Adaptive v2 - Jeff"`, `"Londinium - Jeff"`, or `"Allongé - decaf"` is resolved
- **THEN** each resolves via the recipe-alias longest-boundary-prefix rule to its parent profile's `id` (`adaptive-v2`, `londinium`, `allonge` respectively) — the step anchors on every documented profile's aliases, only the `defaultForEditorType` editor entries are excluded

#### Scenario: Numbered and bean-suffixed variants resolve to the parent recipe

- **WHEN** `"D-Flow / Q2"`, `"D-Flow / Q3"`, `"D-Flow / Q-Jeff"`, `"D-Flow / Q - Ethiopia"`, or `"Damian's Q - decaf"` is resolved
- **THEN** each resolves to the D-Flow-Q variant entry's `id` (digit, `-`, and space are boundary separators), and the A-Flow analogue resolves to its corresponding variant `id` by the same rule

#### Scenario: Longest recipe prefix wins; relational facts inherited

- **WHEN** `"D-Flow / La Pavoni 80s"` is resolved
- **THEN** it resolves to the D-Flow-La-Pavoni variant `id` (the longest matching recipe alias `D-Flow / La Pavoni`), and `ugsForKbId` for it is strictly greater (coarser) than for `"D-Flow / default"`

#### Scenario: Editor name never anchors a prefix

- **WHEN** `"D-Flow / Bradbury"` (no recipe alias is a boundary-prefix) is resolved with the `dflow` editor hint
- **THEN** it resolves to the generic `d-flow` `id` via the step-3 `defaultForEditorType` fallback, NOT via a prefix on the bare `D-Flow` editor-name alias
- **AND** when the same title is resolved with no editor hint, the outcome is explicitly unresolved (the editor-name alias is not a prefix anchor and no recipe alias matched)

#### Scenario: A following letter blocks the boundary

- **WHEN** `"D-Flow / Quark"` or `"D-FlowX"` is resolved
- **THEN** it does NOT resolve to the D-Flow-Q variant `id` (the character after the candidate recipe alias is a letter, which is not a boundary separator), and resolution falls through to step 3 / explicitly unresolved as applicable

#### Scenario: A non-letter suffix separator resolves to the parent recipe

- **WHEN** `"Best practice (light roast)_cris"`, `"Londinium.v2"`, or `"Londinium, decaf"` is resolved
- **THEN** each resolves to its parent recipe's `id` via the recipe-alias longest-boundary-prefix rule, because the character following the matched alias is not a letter
- **AND** the profile therefore receives that entry's `analysisFlags`, so a curve behaviour the entry declares expected is not reported as a fault

#### Scenario: Exact match still wins first and is unchanged

- **WHEN** `"D-Flow / Q"` or `"Damian's Q"` is resolved
- **THEN** it resolves to the D-Flow-Q variant `id` via the step-1 exact alias lookup, with the recipe-prefix step never consulted

#### Scenario: Built-in profiles resolve exactly and never depend on the prefix step

- **WHEN** every built-in/shipped/starter profile title and editor-canonical output is resolved
- **THEN** each resolves to exactly one `id` via the step-1 exact alias lookup, and resolution is unchanged if the recipe-prefix step is disabled (the prefix step is the user-derived-profile path only and cannot override a built-in)

#### Scenario: No order-dependent greedy scan on a total miss

- **WHEN** the resolver finds no exact match AND no recipe alias is a boundary-prefix of the normalized title
- **THEN** it proceeds to the deterministic editor-type default (if an editor hint is present) or returns the explicit unresolved outcome, and performs no order-dependent `startsWith`/`contains` scan over arbitrary keys

#### Scenario: Legacy persisted variant kbId heals via the shared resolver

- **WHEN** a shot record persisted with the legacy normalized-title kbId `"d-flow / q - jeff"` is resolved through `resolveKbInput`
- **THEN** it resolves to the D-Flow-Q variant `id` via the same shared recipe-prefix step, so band/UGS/analysisFlags recompute correctly on load

## REMOVED Requirements

### Requirement: A corpus resolution test SHALL be a hard gate

**Reason**: renamed and widened. The requirement named a specific test FILE (`tst_kb_resolution`) as the gate, and that file has never existed in this repository — so the stated hard gate was not enforced by anything. It is replaced by an obligation on the corpus OUTCOME plus direct identity assertions, which an existing test file already carries, so the requirement can be satisfied without inventing a file whose absence nobody noticed.

**Migration**: superseded by "A corpus resolution gate SHALL be a hard merge gate, asserting outcomes as well as identity" in the ADDED section below.

## ADDED Requirements

### Requirement: A corpus resolution gate SHALL be a hard merge gate, asserting outcomes as well as identity

Resolution SHALL be gated by the shot corpus, which runs the real analysis pipeline over stored shots and
compares emitted findings against per-shot expectations. The corpus SHALL carry at least one fixture whose
profile title resolves ONLY through the recipe-alias boundary-prefix step and whose expectations depend on a
KB `analysisFlags` entry, so that a regression in resolution surfaces as a failed expectation about a
*finding the user would have seen*, not merely as a changed identifier.

Gating on outcome rather than identity is deliberate. An identity-only assertion fails a refactor that
resolves differently but suppresses correctly, and passes a change that resolves correctly but emits the
finding anyway — both are the wrong way round for a capability whose purpose is to avoid telling a user a
by-design behaviour is a fault.

Resolution identity SHALL additionally be asserted directly, so that a profile which stops resolving is
reported as such rather than only through a downstream symptom. Every shipped/starter profile title, the
editor-canonical outputs, and the boundary-prefix fixtures (including the shot-819 and `"D-Flow / Q - Jeff"`
cases) SHALL resolve to exactly one `id`. These assertions SHALL live beside existing KB tests rather than
in a test binary created for them; a new test binary SHALL NOT be added for this purpose.

Both gates SHALL be part of the suite that gates merge.

#### Scenario: A renamed profile that loses its KB entry fails the corpus

- **GIVEN** a corpus fixture whose profile title resolves only through the boundary-prefix step, and whose
  expectations depend on a suppression flag from the resolved entry
- **WHEN** resolution regresses such that the title no longer resolves
- **THEN** the corpus run SHALL fail on the changed finding, naming the shot and the expectation

#### Scenario: Every corpus profile resolves to exactly one entry

- **WHEN** the resolution assertions enumerate every profile title in the corpus, the starter profiles, and
  the editor outputs
- **THEN** each SHALL resolve to exactly one `id`, and the assertion SHALL fail loudly if any
  expected-resolvable title resolves to zero or multiple `id`s

#### Scenario: Historical mis-resolution fixture is pinned

- **WHEN** the shot-819 profile title is resolved
- **THEN** it SHALL resolve to its correct canonical entry, and a regression to the band-less default SHALL
  fail


### Requirement: A shape-based resolution step SHALL follow the title steps and SHALL resolve to a candidate set

When all title-based resolution steps miss and the caller supplies the profile's frame data, the resolver
SHALL attempt a fourth step keyed on the profile's **shape** (as defined by the `profile-shape-equivalence`
capability) rather than on its name. The step SHALL compare the candidate profile's shape against the shape
of every shipped profile whose own title resolves to a KB entry through the title steps, and SHALL yield the
**set** of KB ids whose shipped profile has the same shape. An empty set SHALL be the explicit unresolved
outcome, identical to today's total miss.

The step SHALL be total, deterministic and order-independent: it SHALL NOT rank, score, or measure distance
between profiles, SHALL NOT depend on the enumeration order of the shipped profile set, and SHALL introduce
no threshold or tunable constant. It is admissible under this capability's standing prohibition on a
"non-exact best guess" on exactly those grounds — it is anchored on structural equality, not on a name
heuristic. That prohibition on order-dependent, non-anchored or non-deterministic name matching remains in
force and is not weakened by this step.

The shape step SHALL NOT be consulted when any title step resolved. A shipped profile's own resolution SHALL
be unchanged by this step.

#### Scenario: A renamed dial-in derivative resolves by shape

- **GIVEN** a user profile whose title matches no alias and is no recipe boundary-prefix, and whose shape
  equals that of a shipped profile that resolves to a KB entry
- **WHEN** the profile is resolved
- **THEN** the resolver SHALL yield a candidate set containing that KB entry's id

#### Scenario: Title resolution still wins and is never overridden

- **GIVEN** a profile whose title resolves through the exact alias, recipe-prefix, or editor-default step
- **WHEN** the profile is resolved
- **THEN** the shape step SHALL NOT be consulted and the resolved id SHALL be exactly the title step's result

#### Scenario: An unrecognised shape stays explicitly unresolved

- **GIVEN** a user profile whose title resolves through no title step and whose shape equals no shipped
  profile's shape
- **WHEN** the profile is resolved
- **THEN** the candidate set SHALL be empty and the outcome SHALL be the explicit unresolved outcome

#### Scenario: Resolution does not depend on enumeration order

- **GIVEN** any profile resolved by shape
- **WHEN** the shipped profile set is enumerated in any order
- **THEN** the resulting candidate set SHALL be identical

#### Scenario: Shipped profiles are unaffected

- **WHEN** every shipped profile is resolved
- **THEN** each SHALL resolve exactly as it does through the title steps alone, and disabling the shape step
  SHALL leave every shipped profile's resolution unchanged

### Requirement: Facts SHALL transfer from a candidate set under per-fact rules chosen by which error they risk

A candidate set with more than one member SHALL NOT be collapsed by picking a member. Each KB fact SHALL
transfer under the rule below, chosen so that the residual error is a missing statement rather than a wrong
one, except where a missing statement would itself hide a real fault.

- **Presence of KB context** (the signal that gates profile-shape-dependent analysis) SHALL be true for any
  non-empty candidate set. Every member answers the same structural question, so no member disagrees.
- **Suppression flags that silence a shape-derived diagnosis** SHALL transfer as the **union** across the
  candidate set. Such a flag asserts that a curve behaviour is by design for this shape; the shape is shared
  by construction, and failing to apply it produces a *wrong* finding, whereas over-applying it produces at
  most a missing one.
- **A suppression flag that disables a detector reading physics rather than profile shape** SHALL require
  **unanimity** across the candidate set, and SHALL be withheld otherwise. Applying such a flag on the
  strength of one member would silence findings that hold on any profile regardless of shape — hiding a
  genuinely faulty shot, which this capability treats as worse than an unexplained one.
- **Assertive per-profile facts** — a cited expert operating band, a grind-scale position — SHALL require
  **unanimity** across the candidate set and SHALL be withheld otherwise. These are claims about specific
  numbers, which a derivative may have moved. Withholding them SHALL be a strict no-op, indistinguishable
  from the fact's absence.

A single-member candidate set SHALL transfer every fact, making a unique shape match equivalent to a title
match for every consumer.

Any KB fact not explicitly classified above SHALL default to the **unanimity-or-withhold** rule. A fact
added to the knowledge source later SHALL NOT silently acquire the union rule by omission; the union is the
narrow exception for flags that only silence a shape-derived diagnosis, not the default.

Facts that identify *which* profile the knowledge came from — its canonical display name, the derivation
label, its roast affinity — SHALL be treated as a single indivisible identity claim and SHALL require a
**single-member** candidate set, not merely unanimity. Where the set has more than one member there is no
one profile to name, and naming an arbitrary member would assert something the resolution did not
establish. Analysis-affecting facts SHALL still transfer under their own rules in that case: a false
positive can be suppressed without claiming to know which profile the shot's was derived from.

The knowledge **indicator** and the **prose body** are not identity claims and SHALL NOT require a single
member. Where the candidate set is non-empty the indicator SHALL be shown, and opening it SHALL present
every member's prose, each labelled with that member's canonical display name, together with an explicit
statement that the profile's frame structure matched more than one documented profile. A multi-member set
still shapes the badges and the summary — suppression flags transfer by union, agreed facts by unanimity —
so withholding the indicator would leave a user told nothing about knowledge that demonstrably moved what
they were shown. What ambiguity withholds is the claim to know which profile; it does not withhold the
disclosure that knowledge was used.

#### Scenario: A shape-silencing flag applies when only one candidate carries it

- **GIVEN** a candidate set whose members disagree on a suppression flag that silences a shape-derived
  diagnosis
- **WHEN** the flags are read for that profile
- **THEN** the flag SHALL be applied

#### Scenario: A physics-detector flag is withheld unless every candidate carries it

- **GIVEN** a candidate set whose members disagree on a suppression flag that disables a detector reading
  physics rather than profile shape
- **WHEN** the flags are read for that profile
- **THEN** the flag SHALL NOT be applied, and the detector SHALL run

#### Scenario: A disputed expert band is withheld rather than guessed

- **GIVEN** a candidate set in which one member carries an expert operating band and another does not, or
  the members carry different bands
- **WHEN** the band is read for that profile
- **THEN** no band SHALL be returned, and the outcome SHALL be identical to that profile having no band

#### Scenario: A disputed grind-scale position is withheld

- **GIVEN** a candidate set whose members carry different grind-scale positions
- **WHEN** the grind-scale position is read for that profile
- **THEN** none SHALL be returned

#### Scenario: An unclassified fact defaults to unanimity

- **GIVEN** a KB fact that the rules above do not explicitly classify
- **WHEN** it is read for a multi-member candidate set whose members disagree on it
- **THEN** it SHALL be withheld

#### Scenario: Identity is withheld for a multi-member set while its knowledge is still disclosed

- **GIVEN** a profile whose candidate set has more than one member, at least one of which carries a
  suppression flag that silences a shape-derived diagnosis
- **WHEN** the profile and a shot taken with it are presented
- **THEN** no derivation label or roast affinity SHALL be shown for it
- **AND** the knowledge indicator SHALL be shown
- **AND** opening it SHALL present every member's prose, each labelled with that member's canonical
  display name, and SHALL state that more than one documented profile shares this frame structure
- **AND** the suppression flag SHALL still be applied to the shot's analysis

#### Scenario: A unique shape match shows its identity

- **GIVEN** a profile whose candidate set has exactly one member
- **WHEN** the profile is presented
- **THEN** the knowledge indicator, prose body and derivation label SHALL be shown for that member

#### Scenario: A unique shape match behaves exactly like a title match

- **GIVEN** a profile whose candidate set has exactly one member
- **WHEN** any KB fact is read for that profile
- **THEN** the value SHALL be identical to the value a profile resolving to that same id by title receives

### Requirement: The knowledge source SHALL NOT describe one profile with more than one entry

Two shipped profiles that are identical in extraction — same frame count, and per frame the same pump,
sensor, transition, temperature, target and exit — are one profile, whatever their titles say, and SHALL
resolve to one knowledge entry. Splitting them across entries does not produce two opinions; it produces
one opinion recorded twice with different completeness, and the candidate-set rules then read that
duplication as disagreement and withhold facts that are true of the profile.

This is the failure the split between `Damian's LRv2` and the standalone Londinium entry actually caused:
the two shipped files differ only in title, notes, target weight, a hidden flag and one frame popup — the
Londonium profile's own notes read "This is identical to the LRv2 profile, but renamed to be easier to
understand" — yet only one entry carried the cited pressure-peak band, so unanimity withheld that band
from every shape match on the pair. The split rested on a KB claim ("different fill/infuse behavior and
higher frame temperatures") that the profile files disprove.

Shape equivalence is therefore an integrity check on the knowledge source, not only a resolution step: a
shape bucket whose members resolve to different entries SHALL be treated as a claim requiring evidence
from the profile data, and SHALL be reconciled in the source rather than absorbed by the transfer rules.

#### Scenario: Extraction-identical profiles resolve to one entry

- **GIVEN** two shipped profiles that are identical in every extraction-affecting field and differ only in
  presentation metadata such as title, notes or target weight
- **WHEN** each is resolved
- **THEN** both SHALL resolve to the same knowledge entry
- **AND** that entry's facts, including any expert operating band, SHALL apply to both

### Requirement: An indicator that KB knowledge exists SHALL resolve identically to the content behind it

Wherever the product indicates that knowledge exists for a profile, that indicator and the content shown
when the user acts on it SHALL be produced by the same resolution. An indicator SHALL NOT be shown for a
profile whose content path would return nothing.

#### Scenario: Indicator and content agree for a shape-resolved profile

- **GIVEN** a profile that resolves only by shape
- **WHEN** the knowledge indicator is shown for it and the user opens the knowledge content
- **THEN** the content SHALL be non-empty

#### Scenario: No indicator without content

- **GIVEN** a profile for which the knowledge content path would return nothing
- **WHEN** the profile is presented
- **THEN** no knowledge indicator SHALL be shown

### Requirement: A shape-derived match SHALL be presented as a derivation, not as an identity

Where a profile resolves by shape rather than by title, any surface that identifies the profile's knowledge
SHALL name the matched entry using the KB's canonical display name and SHALL word it as a derivation, so the
user can tell the relationship was inferred from the profile's structure rather than read from its name. A
profile that resolved by title SHALL be presented exactly as it is today, with no added label.

#### Scenario: Shape-resolved profile names its base

- **GIVEN** a user profile that resolved by shape to a KB entry
- **WHEN** the profile is shown in the profile list, or a shot taken with it is shown on the shot review or
  shot detail page
- **THEN** the surface SHALL name the matched entry's canonical display name, worded as a derivation

#### Scenario: Title-resolved profile gains no label

- **GIVEN** a profile that resolved through any title step
- **WHEN** it is shown on any of those surfaces
- **THEN** no derivation label SHALL be added
