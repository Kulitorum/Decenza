## MODIFIED Requirements

### Requirement: The profile→KB resolver SHALL be exact-match-or-explicitly-unresolved

Resolution SHALL be ordered: (1) normalize the title (existing accent/punctuation normalization retained as logic) → **exact** lookup in an explicit **alias→`id`** map built from each entry's `displayName`, every `alsoMatches` entry, and the `defaultForEditorType` entries; (2) on a miss, a **deterministic recipe-alias longest-boundary-prefix** step; (3) on a miss, the editor-type `defaultForEditorType` fallback. The resolver SHALL yield an `id` (never a prose string); all consumers SHALL key on `id`. A total miss SHALL return an explicit unresolved outcome.

The recipe-alias prefix step SHALL be defined as: among registered aliases that do **not** belong to a `defaultForEditorType` entry (*recipe aliases* — the editor namespace is not a recipe identity and SHALL NOT anchor a prefix; the editor's "which editor" role is served solely by step 3), select every recipe alias that is a *boundary-prefix* of the normalized title — the alias immediately followed by a separator character that SHALL be one of `/`, `-`, space, or an ASCII digit (a following letter, or end-of-string, SHALL NOT be a boundary; end-of-string is the exact-match case handled by step 1). The resolver SHALL resolve to the `id` of the **longest** matching recipe alias. This rule is total and deterministic: a string has exactly one prefix of each length, so two equal-length boundary-prefix recipe aliases would be the identical string and therefore reduce to the pre-existing duplicate/colliding-alias rejection; no new ambiguity class is introduced and no per-call "reject if multiple" heuristic is used. Matching SHALL be prefix-only — `contains`/substring matching SHALL NOT be used.

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
