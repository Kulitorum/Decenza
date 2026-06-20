# profile-knowledge-base Specification

## Purpose
The single source of truth for Decenza-specific, app-only per-profile knowledge (expert bands + provenance, UGS, aliases, analysisFlags, family, prose) — authored as one validated structured JSON (`resources/ai/profile_knowledge.json`), parsed at load, gated at build by `tools/validate_kb.py`. Resolution from a running profile to its KB entry is exact-match-or-explicitly-unresolved (no fuzzy fallback); the KB never embeds or mirrors the portable profile. Replaces the former markdown-scraped KB + hardcoded C++ `kBands` table.
## Requirements
### Requirement: Profile knowledge SHALL be authored as a single validated structured JSON source

The profile knowledge base SHALL be authored as structured JSON parsed by `QJsonDocument`, replacing the markdown string-scraped grammar. Each entry SHALL carry typed, validated fields plus one prose string: `id` (stable kebab-case string — the SOLE identity/resolution/cross-reference key), `displayName` (human label; presentational prose, never a key), `alsoMatches`, `defaultForEditorType`, `ugs` (value, inferred flag, note), `analysisFlags`, `skipCatalog`, `family`, `expertBand` (axis, lo?, hi?, src?, provenance, confidence, rationale, srcArchive?, proseRestatesBand?), and `prose`. `id` SHALL be unique; `displayName` SHALL NOT be used as a key — renaming it SHALL NOT affect identity or resolution. `family` SHALL be a validated enum over a closed vocabulary. `category`, `roast`, and `summary` SHALL NOT be fields. `expertBand` SHALL carry a required `provenance` ∈ {`cited`, `author-stated`, `inferred`} and a required `rationale` stating what is real and why it is believed. `expertBand.src` is optional: when `provenance=cited` it SHALL be a well-formed `http`/`https` real-doc URL (an external site OR the project's own GitHub URL of the relocated archive copy); when `provenance=author-stated` it SHALL be the intrinsic token `profile-notes`; when `provenance=inferred` it SHALL be absent. URL values SHALL be the actual source attribution, never fabricated; a missing source is acceptable only as `inferred` with a justifying `rationale`. For `cited` sources a durable local copy SHALL be kept under a `docs/` subfolder where capturable, referenced by an optional `expertBand.srcArchive` repo-relative path; when `srcArchive` is set the validator SHALL fail the build if the referenced file does not exist. The per-entry LLM prose SHALL be a single opaque, re-authored string that nothing parses; it SHALL NOT be decomposed into named fields and SHALL NOT be required to byte-match today's content (only facts are fidelity-gated; prose may be rewritten for clarity). `expertBand.lo` and `expertBand.hi` SHALL each be independently optional so one-sided rails remain first-class. The JSON SHALL ship as a qrc resource so it is resolved fresh at load (recompute-on-load preserved).

#### Scenario: Structured fields parsed without string scraping

- **WHEN** `loadProfileKnowledge` runs against the authored JSON
- **THEN** every profile entry's typed fields populate `ProfileKnowledge` via `QJsonDocument`, and no `line.startsWith(...)` field scraping path remains in the loader

#### Scenario: One-sided expert band round-trips

- **WHEN** a profile defines an `expertBand` with `lo` set and `hi` absent (a flow floor)
- **THEN** `expertBandForKbId` returns an `ExpertBand` with the floor set and no ceiling, with band values equal to the pre-migration `flowFloor` row (fact-value parity)

### Requirement: A build-time validator SHALL fail the build on any malformed or integrity-violating knowledge data

A build-time validator SHALL reject, as a build failure, any of: an unknown or misspelled field key; a value out of range or wrong type; a non-kebab-case or duplicate `id`; an `expertBand` violating `lo < hi` (when both present), a non-positive bound, an axis outside the allowed enum, the `provenance`↔`src` cross-field rule (`src` required for `cited`, absent for `inferred`), or a `src` that is set but is neither a well-formed `http`/`https` URL nor the intrinsic token; a set `srcArchive` whose file is absent; an alias (`alsoMatches` entry, `displayName`, or `defaultForEditorType`) that resolves to zero or more than one `id`, or is orphaned. The validator SHALL also enforce fact-value parity (no silent band/UGS/flag change) and carry the D9 best-effort prose-restates-band lint. A passing validator is a precondition for the build; malformed data SHALL never reach a shipped binary.

#### Scenario: Typo'd field key fails the build

- **WHEN** an authored entry contains `"usg"` instead of `"ugs"` (or any key not in the schema)
- **THEN** the build-time validator exits non-zero and the build fails, naming the offending entry and key

#### Scenario: Out-of-range expert band fails the build

- **WHEN** an entry declares `expertBand` with `lo` ≥ `hi`, a non-positive bound, or an unknown `axis`
- **THEN** the validator fails the build identifying the entry and the violated invariant

#### Scenario: Provenance/src cross-field violation fails the build

- **WHEN** an `expertBand` has `provenance=cited` with no `src`, or `provenance=inferred` with a `src` set
- **THEN** the validator fails the build naming the entry and the provenance/src mismatch

#### Scenario: Unknown family enum value fails the build

- **WHEN** an entry's `family` is not one of the closed family-vocabulary tokens
- **THEN** the validator fails the build naming the entry and the invalid `family` value

#### Scenario: Malformed citation source fails the build

- **WHEN** an `expertBand.src` is set but is neither a well-formed `http`/`https` URL nor the intrinsic `profile-notes` token
- **THEN** the validator fails the build naming the entry and the invalid `src`

#### Scenario: Missing referenced source archive fails the build

- **WHEN** an `expertBand.srcArchive` path is set but no file exists at that repo-relative path
- **THEN** the validator fails the build naming the entry and the missing archive path

#### Scenario: Duplicate or orphaned alias fails the build

- **WHEN** two entries share an `id`, or an `alsoMatches`/`displayName`/`defaultForEditorType` key maps to zero or to multiple `id`s
- **THEN** the validator fails the build identifying the conflicting key and entries

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

### Requirement: A corpus resolution test SHALL be a hard gate

A test (`tst_kb_resolution`) SHALL assert that every profile title appearing in `tests/data/shots/`, every shipped starter profile, and representative D-Flow/A-Flow editor outputs resolves to exactly one `id`. The shot-819 and `"D-Flow / Q - Jeff"` cases SHALL be explicit fixtures. The test SHALL be part of the suite that gates merge.

#### Scenario: Every corpus profile resolves to exactly one entry

- **WHEN** `tst_kb_resolution` enumerates every profile title in the corpus, starter profiles, and editor outputs
- **THEN** each resolves to exactly one `id`, and the test fails loudly if any expected-resolvable title resolves to zero or multiple `id`s

#### Scenario: Historical mis-resolution fixture is pinned

- **WHEN** the shot-819 profile title is resolved in the test
- **THEN** it resolves to its correct canonical entry, and a regression to the band-less default fails the test

### Requirement: The KB SHALL be the single source of truth for per-profile facts; C++ SHALL hold zero facts

All per-profile facts SHALL be sourced from the KB at runtime. The hardcoded `kBands` table in `src/ai/shotsummarizer_kb.cpp` SHALL be removed and the band read from the KB. The `shotsummarizer.cpp:1290/1302` prompt text SHALL be retained verbatim — it is instructional/guardrail text (`:1290` a teaching example, `:1302` an anti-hallucination guardrail), not stranded facts; the underlying profile facts it references SHALL be KB-canonical. C++ SHALL retain only profile-agnostic logic (detector algorithms, analysisFlag→suppression semantics, resolver normalization). No per-profile value SHALL remain as a C++ table or literal fact.

#### Scenario: Expert bands sourced from the KB

- **WHEN** `expertBandForKbId` is called for any profile that had a `kBands` row before this change
- **THEN** it returns the band, axis, provenance, confidence, rationale (and `src` when `cited`) read from the KB, and no `kBands` static table exists in the codebase

#### Scenario: Prompt instructional/guardrail text is preserved, not deleted

- **WHEN** the change is complete and `shotsummarizer.cpp:1290/1302` are inspected
- **THEN** the `:1290` family-switch teaching example and the `:1302` anti-hallucination guardrail remain verbatim, and the underlying profile facts they reference (e.g. 80's-Espresso temperature) are present canonically in the KB `prose`

### Requirement: Loud-vs-silent SHALL split by expectation

A profile that is expected to resolve (present in the corpus, a shipped starter profile, or an editor output) but does not SHALL cause a build/test failure. A genuinely unknown profile at runtime (a new community or custom title with no KB entry) SHALL be a silent no-op: the resolver returns unresolved, `expertBandForKbId` returns `std::nullopt`, and analysis output is byte-identical to the pre-existing absence-intentional behavior.

#### Scenario: Unknown runtime profile is a silent no-op

- **WHEN** a user loads a profile whose title has no KB entry and is not in the corpus
- **THEN** no error or warning is surfaced, `expertBandForKbId` returns `std::nullopt`, and the shot summary is byte-identical to today's no-band behavior

#### Scenario: Expected profile that fails to resolve breaks the build

- **WHEN** a shipped starter profile or corpus title fails to resolve to exactly one entry
- **THEN** the corpus resolution test fails, blocking merge

### Requirement: The KB SHALL stay separate from the portable profile and SHALL NOT mirror portable parameters

The KB SHALL NOT be embedded in or copied into portable profile JSON, which round-trips between apps. The only KB↔profile join SHALL be the resolver. Portable profile parameters (frames, limits, setpoints, including the limiter value used by the expert-band corroboration clause) SHALL continue to be read from the profile at analyze time and SHALL NOT be stored in the KB. Citation rationale prose MAY quote a profile's parameter as evidence (content), which does not constitute the app reading a parameter from the KB.

#### Scenario: Limiter value still read from the profile

- **WHEN** the expert-band corroboration clause needs the profile's pressure limiter value
- **THEN** that value is read from the profile JSON at analyze time, and no frame/limit/setpoint value is stored as a KB field

#### Scenario: KB is not written into exported profiles

- **WHEN** a profile is exported or uploaded to visualizer.coffee
- **THEN** the exported profile JSON contains no KB fields (expertBand, ugs, analysisFlags, prose, etc.)

### Requirement: There SHALL be exactly one authored knowledge source

The authored JSON SHALL be the single source of profile *facts*. The runtime `resources/ai/profile_knowledge.md` SHALL be removed (it is fully superseded by the JSON). `docs/PROFILE_KNOWLEDGE_BASE.md` SHALL NOT be removed until a duplication differential has surfaced its residue (content not represented in the JSON) and every residue item is either folded into the JSON or preserved in a slimmed doc holding only the residue. Any human-readable rollup, if retained, SHALL be generated from the JSON and SHALL NOT be hand-edited.

#### Scenario: Runtime md removed, design doc removed only after residue accounted for

- **WHEN** the change is complete
- **THEN** `resources/ai/profile_knowledge.md` no longer exists as a hand-authored source; and `docs/PROFILE_KNOWLEDGE_BASE.md` is either deleted (only if the differential proved zero residue or the residue was folded into the JSON) or retained slimmed to only its non-duplicated residue — never deleted with un-captured content

### Requirement: Migration SHALL preserve facts except for enumerated reviewed corrections

The migration SHALL be fact-value-preserving, not content-preserving. A parity test SHALL assert that, for every fact already derived today (UGS, inferred flag, analysisFlags, aliases, skipCatalog, family) and every shipped `kBands` row (axis, lo, hi, confidence), the post-migration resolved value equals the pre-migration value. Prose is re-authored and SHALL NOT be byte-compared; the `buildProfileCatalog` output line MAY change (it is now `displayName [family]`). Any deliberate fact change (a nonsensical number fixed, a mis-resolution corrected) SHALL appear on an enumerated, reviewed corrections list; no fact SHALL change silently. `ExpertBand`, every C++ consumer signature, and the recompute-on-load contract SHALL be unchanged (the `kbId` token consumers pass/return is now the stable `id`).

#### Scenario: Fact-value parity holds

- **WHEN** the parity test compares pre- and post-migration resolved fact values for every fact and every former `kBands` row
- **THEN** all values are equal, with differences permitted only for entries on the enumerated reviewed-corrections list (mis-resolution fixes or deliberately corrected facts), and never silently

#### Scenario: KB coverage preserved — no profile loses its entry

- **WHEN** every profile/title that resolved to a KB section under the old markdown (every section title plus every `Also matches:` alias) is resolved post-migration
- **THEN** each still resolves to a valid entry (a non-`skipCatalog` entry with non-empty `prose`); no section was dropped and no alias lost coverage, with divergence permitted only on the enumerated reviewed-corrections list

#### Scenario: Consumer API and recompute contract unchanged

- **WHEN** any existing caller invokes `expertBandForKbId`, `getAnalysisFlags`, `ugsForKbId`, `ugsInferredForKbId`, `canonicalNameForKbId`, `computeProfileKbId`, `allKbUgsEntries`, or `crossProfileReferenceContent`
- **THEN** the signature is unchanged and the value is recomputed fresh at load from the qrc JSON, identical to the recompute-on-load behavior before the change

### Requirement: Prose numbers SHALL be commentary; the expert band SHALL exist exactly once

Numbers inside `prose` SHALL be treated as descriptive commentary and SHALL NOT be substituted or injected at runtime. The expert band SHALL exist in exactly one authoritative place — the `expertBand` struct. The band SHALL be rendered into the assembled LLM blob from `expertBand` (struct → one cited sentence). `prose` SHALL NOT state the expert band as the band (it may discuss dial-in targets and profile behavior, which are distinct from the cited band envelope). The validator SHALL carry a best-effort lint that flags a `prose` line which restates the entry's `expertBand` bounds verbatim or near-verbatim. An entry whose prose legitimately narrates the profile's own setpoints or cited dial-in (which may coincide with a bound — the Adaptive-v2 case) MAY carry a reviewed `expertBand.proseRestatesBand` rationale; when present and a non-empty string the lint SHALL be silenced for that entry. A `proseRestatesBand` that is present but not a non-empty string SHALL be a hard build failure and SHALL NOT suppress the lint. An ack present on an entry whose prose no longer restates any bound (a stale ack) SHALL itself be surfaced as a non-fatal warning so the suppression set cannot rot and silently mask a future regression.

#### Scenario: Band rendered from the struct, not hand-authored in prose

- **WHEN** the assembled LLM blob is built for a profile with an `expertBand`
- **THEN** the cited band sentence is generated from the `expertBand` fields, and the band number appears in no other authored location for that entry

#### Scenario: Prose numbers are not injected

- **WHEN** `prose` contains profile-parameter or dial-in numbers (e.g. "peak 8-9 bar", "9.5 bar limiter")
- **THEN** they are emitted verbatim as commentary with no runtime substitution, even when they differ from the entry's `expertBand` bounds

#### Scenario: Lint catches a prose copy of the band

- **WHEN** an authored `prose` line restates the entry's `expertBand` bounds verbatim or near-verbatim
- **THEN** the validator's best-effort lint flags that entry so the duplicate copy is removed before it can drift

#### Scenario: Reviewed ack silences the lint for an intentional restatement

- **WHEN** an entry's prose restates a bound and its `expertBand` carries a non-empty `proseRestatesBand` reviewer rationale
- **THEN** the D9 lint is silenced for that entry and no warning is emitted

#### Scenario: A stale ack is surfaced so the suppression set cannot rot

- **WHEN** `expertBand.proseRestatesBand` is present but the entry's prose no longer restates any bound
- **THEN** a non-fatal stale-ack warning is emitted prompting removal of the now-unneeded ack

#### Scenario: A malformed ack hard-fails and does not suppress

- **WHEN** `expertBand.proseRestatesBand` is present but is not a non-empty string
- **THEN** it is a hard build failure and the D9 lint still fires for that entry (a broken silencer never silences)

### Requirement: The assembled LLM prompt SHALL be equivalent or improved, never degraded

As a result of this change the shot-analysis prompt the model receives SHALL be equivalent or improved, never worse. The `shotsummarizer.cpp` instructional examples and the `:1302` anti-hallucination guardrail SHALL be retained verbatim. A final prompt-equivalence check over a fixed representative profile set SHALL diff the assembled prompt old-vs-new; every difference SHALL be a deliberate, reviewed improvement, and an unintended or degrading difference SHALL fail the gate.

#### Scenario: Prompt-equivalence gate at the end

- **WHEN** the end-of-change prompt-equivalence check assembles the shot-analysis prompt for the fixed profile set, old-vs-new
- **THEN** the only differences are deliberate reviewed improvements (e.g. the struct-rendered band sentence), the `:1290/:1302` text is byte-identical, and any unintended or degrading difference fails the gate

