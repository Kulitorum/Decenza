# profile-knowledge-base Specification

## Purpose
The single source of truth for Decenza-specific, app-only per-profile knowledge (expert bands + provenance, UGS, aliases, analysisFlags, family, prose) — authored as one validated structured JSON (`resources/ai/profile_knowledge.json`), parsed at load, gated at build by `tools/validate_kb.py`. Resolution from a running profile to its KB entry is exact-match-or-explicitly-unresolved (no fuzzy fallback); the KB never embeds or mirrors the portable profile. Replaces the former markdown-scraped KB + hardcoded C++ `kBands` table.
## Requirements
### Requirement: Profile knowledge SHALL be authored as a single validated structured JSON source
The profile knowledge base SHALL be authored as structured JSON parsed by `QJsonDocument`, replacing the markdown string-scraped grammar. Each entry SHALL carry typed, validated fields plus one prose string: `id`, `displayName`, `alsoMatches`, `defaultForEditorType`, `ugs` (value, inferred flag, note), `analysisFlags`, `skipCatalog`, `family`, `expertBand` and `prose`. The JSON SHALL ship as a qrc resource, resolved fresh at load.

#### Scenario: Structured fields parsed without string scraping

- **WHEN** `loadProfileKnowledge` runs against the authored JSON
- **THEN** every profile entry's typed fields populate `ProfileKnowledge` via `QJsonDocument`, and no `line.startsWith(...)` field scraping path remains in the loader

#### Scenario: One-sided expert band round-trips

- **WHEN** a profile defines an `expertBand` with `lo` set and `hi` absent (a flow floor)
- **THEN** `expertBandForKbId` returns an `ExpertBand` with the floor set and no ceiling, with band values equal to the pre-migration `flowFloor` row (fact-value parity)

### Requirement: Entry identity is the stable id
`id` SHALL be a unique, stable kebab-case string and the SOLE identity, resolution and cross-reference key. `displayName` SHALL be presentational prose, never a key, so renaming it SHALL NOT affect identity or resolution. `family` SHALL be a validated enum over a closed vocabulary. `category`, `roast` and `summary` SHALL NOT be fields.

#### Scenario: Renaming a display name keeps identity
- **WHEN** an entry's `displayName` is changed
- **THEN** its `id`, identity and resolution are unchanged

### Requirement: Expert band provenance and rationale
`expertBand` SHALL carry a required `provenance` (`cited`, `author-stated` or `inferred`) and a required `rationale` stating what is real and why it is believed. `expertBand.lo` and `expertBand.hi` SHALL each be independently optional, so one-sided rails remain first-class.

#### Scenario: Missing source is inferred with a rationale
- **WHEN** an expert band has no source
- **THEN** its provenance is `inferred` and its rationale justifies the band

### Requirement: Expert band source attribution
When `provenance=cited`, `expertBand.src` SHALL be a well-formed `http` or `https` URL, either an external real-doc URL or the project's GitHub URL of the archive copy. When `provenance=author-stated`, `src` SHALL be the intrinsic token `profile-notes`. When `provenance=inferred`, `src` SHALL be absent. URL values SHALL be the actual attribution, never fabricated.

#### Scenario: Cited band carries a real URL
- **WHEN** an expert band has `provenance=cited`
- **THEN** its `src` is a well-formed `http` or `https` URL that names the actual source

### Requirement: Cited sources keep a durable archive copy
For `cited` sources a durable local copy SHALL be kept under a `docs/` subfolder where capturable, referenced by an optional `expertBand.srcArchive` repo-relative path.

#### Scenario: Archive copy is referenced by path
- **WHEN** a cited source is capturable
- **THEN** its local copy is under `docs/` and referenced by `srcArchive`

### Requirement: Per-entry prose is opaque and re-authorable
The per-entry `prose` SHALL be a single opaque, re-authored string that nothing parses. It SHALL NOT be decomposed into named fields and SHALL NOT be required to byte-match today's content. Only facts are fidelity-gated.

#### Scenario: Prose may be rewritten for clarity
- **WHEN** an entry's prose is re-authored
- **THEN** no parser or byte comparison applies to it

### Requirement: A build-time validator SHALL fail the build on any malformed or integrity-violating knowledge data
A build-time validator SHALL reject, as a build failure, any unknown or misspelled field key, value out of range or wrong type, non-kebab-case or duplicate `id`, and any `expertBand` violating `lo < hi` (when both present), a non-positive bound, or an axis outside the allowed enum. A passing validator SHALL be a precondition for the build.

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

### Requirement: Validator rejects source and archive violations
The validator SHALL also reject a `provenance`/`src` mismatch (`src` required for `cited`, absent for `inferred`), a `src` that is neither a well-formed `http` or `https` URL nor the intrinsic token, and a set `srcArchive` whose file is absent.

#### Scenario: Missing archive file fails the build
- **WHEN** `srcArchive` names a file that does not exist
- **THEN** the build fails

### Requirement: Validator rejects alias and parity violations
An alias (an `alsoMatches` entry, a `displayName`, or a `defaultForEditorType`) that resolves to zero or more than one `id`, or is orphaned, SHALL fail the build. The validator SHALL enforce fact-value parity (no silent band, UGS or flag change) and SHALL carry the best-effort prose-restates-band lint. Malformed data SHALL never reach a shipped binary.

#### Scenario: Duplicate alias fails the build
- **WHEN** an alias resolves to two `id`s
- **THEN** the build fails

### Requirement: The profile→KB resolver SHALL be exact-match-or-explicitly-unresolved
Resolution SHALL be ordered: (1) normalize the title as today, then take an **exact** lookup in an explicit alias-to-`id` map built from each entry's `displayName`, `alsoMatches` and `defaultForEditorType` entries; (2) on a miss, the profile-alias longest-boundary-prefix step; (3) on a miss, the `defaultForEditorType` fallback. The resolver SHALL yield an `id`, never prose, and a total miss SHALL return an explicit unresolved outcome.

#### Scenario: Custom-suffixed title resolves to the parent profile variant

- **WHEN** a profile titled `"D-Flow / Q - Jeff"` is resolved
- **THEN** it resolves to the D-Flow-Q variant entry's `id` via the profile-alias longest-boundary-prefix rule (the registered profile alias `D-Flow / Q`, longer than the editor-name alias which is excluded from anchoring), never to the band-less D-Flow-default `id`, and never via an order-dependent substring scan

#### Scenario: The rule applies to any documented profile, not only D-Flow/A-Flow

- **WHEN** `"Adaptive v2 - Jeff"`, `"Londinium - Jeff"`, or `"Allongé - decaf"` is resolved
- **THEN** each resolves via the profile-alias longest-boundary-prefix rule to its parent profile's `id` (`adaptive-v2`, `londinium`, `allonge` respectively) — the step anchors on every documented profile's aliases, only the `defaultForEditorType` editor entries are excluded

#### Scenario: Numbered and bean-suffixed variants resolve to the parent profile

- **WHEN** `"D-Flow / Q2"`, `"D-Flow / Q3"`, `"D-Flow / Q-Jeff"`, `"D-Flow / Q - Ethiopia"`, or `"Damian's Q - decaf"` is resolved
- **THEN** each resolves to the D-Flow-Q variant entry's `id` (digit, `-`, and space are boundary separators), and the A-Flow analogue resolves to its corresponding variant `id` by the same rule

#### Scenario: Longest profile prefix wins; relational facts inherited

- **WHEN** `"D-Flow / La Pavoni 80s"` is resolved
- **THEN** it resolves to the D-Flow-La-Pavoni variant `id` (the longest matching profile alias `D-Flow / La Pavoni`), and `ugsForKbId` for it is strictly greater (coarser) than for `"D-Flow / default"`

#### Scenario: Editor name never anchors a prefix

- **WHEN** `"D-Flow / Bradbury"` (no profile alias is a boundary-prefix) is resolved with the `dflow` editor hint
- **THEN** it resolves to the generic `d-flow` `id` via the step-3 `defaultForEditorType` fallback, NOT via a prefix on the bare `D-Flow` editor-name alias
- **AND** when the same title is resolved with no editor hint, the outcome is explicitly unresolved (the editor-name alias is not a prefix anchor and no profile alias matched)

#### Scenario: A following letter blocks the boundary

- **WHEN** `"D-Flow / Quark"` or `"D-FlowX"` is resolved
- **THEN** it does NOT resolve to the D-Flow-Q variant `id` (the character after the candidate profile alias is a letter, which is not a boundary separator), and resolution falls through to step 3 / explicitly unresolved as applicable

#### Scenario: A non-letter suffix separator resolves to the parent profile

- **WHEN** `"Best practice (light roast)_cris"`, `"Londinium.v2"`, or `"Londinium, decaf"` is resolved
- **THEN** each resolves to its parent profile's `id` via the profile-alias longest-boundary-prefix rule, because the character following the matched alias is not a letter
- **AND** the profile therefore receives that entry's `analysisFlags`, so a curve behaviour the entry declares expected is not reported as a fault

#### Scenario: Exact match still wins first and is unchanged

- **WHEN** `"D-Flow / Q"` or `"Damian's Q"` is resolved
- **THEN** it resolves to the D-Flow-Q variant `id` via the step-1 exact alias lookup, with the profile-prefix step never consulted

#### Scenario: Built-in profiles resolve exactly and never depend on the prefix step

- **WHEN** every built-in/shipped/starter profile title and editor-canonical output is resolved
- **THEN** each resolves to exactly one `id` via the step-1 exact alias lookup, and resolution is unchanged if the profile-prefix step is disabled (the prefix step is the user-derived-profile path only and cannot override a built-in)

#### Scenario: No order-dependent greedy scan on a total miss

- **WHEN** the resolver finds no exact match AND no profile alias is a boundary-prefix of the normalized title
- **THEN** it proceeds to the deterministic editor-type default (if an editor hint is present) or returns the explicit unresolved outcome, and performs no order-dependent `startsWith`/`contains` scan over arbitrary keys

#### Scenario: Legacy persisted variant kbId heals via the shared resolver

- **WHEN** a shot record persisted with the legacy normalized-title kbId `"d-flow / q - jeff"` is resolved through `resolveKbInput`
- **THEN** it resolves to the D-Flow-Q variant `id` via the same shared profile-prefix step, so band/UGS/analysisFlags recompute correctly on load

### Requirement: The profile-alias prefix step is boundary-anchored
The profile-alias step SHALL consider only registered aliases that do NOT belong to a `defaultForEditorType` entry, since the editor namespace SHALL NOT anchor a prefix. It SHALL select every such alias that is a boundary-prefix of the normalized title, and SHALL resolve to the `id` of the **longest** match. A boundary is any character that is not a letter; a following letter, or end-of-string, SHALL NOT be a boundary.

#### Scenario: Longest profile prefix wins
- **WHEN** two profile aliases are boundary-prefixes of the title
- **THEN** the longer alias's `id` is resolved

### Requirement: The prefix step is total and deterministic
The prefix step SHALL be total and deterministic. A string has exactly one prefix of each length, so equal-length boundary-prefix profile aliases reduce to the existing duplicate-alias rejection. No per-call reject-if-multiple heuristic SHALL be used. Matching SHALL be prefix-only; `contains` or substring matching SHALL NOT be used.

#### Scenario: No order-dependent greedy scan on a total miss
- **WHEN** the title misses every step
- **THEN** no order-dependent greedy scan is applied and the result is unresolved

### Requirement: Shipped profiles never depend on the prefix step
Every built-in, shipped or starter profile and editor-canonical output SHALL resolve via the step-1 exact lookup and SHALL NOT depend on the prefix step. The prefix step is the best-effort path only for user-created profiles derived from a documented profile that keep the source profile's name as title prefix.

#### Scenario: Built-in profiles resolve exactly
- **WHEN** a built-in profile title is resolved
- **THEN** it resolves through the exact lookup and the prefix step is never reached

### Requirement: No order-dependent greedy fallback is reintroduced
The order-dependent greedy `startsWith` and `contains` fallback historically removed from `matchProfileKey` SHALL NOT be reintroduced. Any order-dependent, non-anchored or non-deterministic non-exact best guess is prohibited. The profile-alias prefix step is permitted because it is anchored on a registered profile alias, prefix-only, total, deterministic and test-gated.

#### Scenario: Greedy fallback stays removed
- **WHEN** a non-exact title is resolved
- **THEN** no order-dependent `startsWith` or `contains` scan is used

### Requirement: resolveKbInput applies the shared prefix step
`resolveKbInput` SHALL apply the same shared profile-prefix step, after id-passthrough and exact alias-to-id, so a legacy persisted normalized-title kbId for a renamed variant resolves under the recompute-on-load contract.

#### Scenario: Legacy persisted variant kbId heals
- **WHEN** a persisted normalized-title kbId for a renamed variant is loaded
- **THEN** it resolves through the shared prefix step

### Requirement: The KB SHALL be the single source of truth for per-profile facts; C++ SHALL hold zero facts
All per-profile facts SHALL be sourced from the KB at runtime. The hardcoded `kBands` table in `src/ai/shotsummarizer_kb.cpp` SHALL be removed and the band read from the KB. The prompt text at `shotsummarizer.cpp:1290` and `:1302` SHALL be retained verbatim, as instructional and guardrail text whose referenced profile facts are KB-canonical.

#### Scenario: Expert bands sourced from the KB

- **WHEN** `expertBandForKbId` is called for any profile that had a `kBands` row before this change
- **THEN** it returns the band, axis, provenance, confidence, rationale (and `src` when `cited`) read from the KB, and no `kBands` static table exists in the codebase

#### Scenario: Prompt instructional/guardrail text is preserved, not deleted

- **WHEN** the change is complete and `shotsummarizer.cpp:1290/1302` are inspected
- **THEN** the `:1290` family-switch teaching example and the `:1302` anti-hallucination guardrail remain verbatim, and the underlying profile facts they reference (e.g. 80's-Espresso temperature) are present canonically in the KB `prose`

### Requirement: C++ holds only profile-agnostic logic
C++ SHALL retain only profile-agnostic logic: detector algorithms, analysisFlag-to-suppression semantics and resolver normalization. No per-profile value SHALL remain as a C++ table or literal fact.

#### Scenario: No per-profile literal remains in C++
- **WHEN** the C++ sources are searched for per-profile values
- **THEN** none is found outside the KB

### Requirement: Loud-vs-silent SHALL split by expectation

A profile that is expected to resolve (present in the corpus, a shipped starter profile, or an editor output) but does not SHALL cause a build/test failure. A genuinely unknown profile at runtime (a new community or custom title with no KB entry) SHALL be a silent no-op: the resolver returns unresolved, `expertBandForKbId` returns `std::nullopt`, and analysis output is byte-identical to the pre-existing absence-intentional behavior.

#### Scenario: Unknown runtime profile is a silent no-op

- **WHEN** a user loads a profile whose title has no KB entry and is not in the corpus
- **THEN** no error or warning is surfaced, `expertBandForKbId` returns `std::nullopt`, and the shot summary is byte-identical to today's no-band behavior

#### Scenario: Expected profile that fails to resolve breaks the build

- **WHEN** a shipped starter profile or corpus title fails to resolve to exactly one entry
- **THEN** the corpus resolution test fails, blocking merge

### Requirement: The KB SHALL stay separate from the portable profile and SHALL NOT mirror portable parameters
The KB SHALL NOT be embedded in or copied into portable profile JSON, which round-trips between apps. The only KB-to-profile join SHALL be the resolver. Portable profile parameters, including the limiter value used by the expert-band corroboration clause, SHALL be read from the profile at analyze time and SHALL NOT be stored in the KB.

#### Scenario: Limiter value still read from the profile

- **WHEN** the expert-band corroboration clause needs the profile's pressure limiter value
- **THEN** that value is read from the profile JSON at analyze time, and no frame/limit/setpoint value is stored as a KB field

#### Scenario: KB is not written into exported profiles

- **WHEN** a profile is exported or uploaded to visualizer.coffee
- **THEN** the exported profile JSON contains no KB fields (expertBand, ugs, analysisFlags, prose, etc.)

### Requirement: Citation prose may quote profile parameters
Citation rationale prose MAY quote a profile's parameter as evidence. Such quoting is content and SHALL NOT be treated as the app reading a parameter from the KB.

#### Scenario: Quoted parameter is content, not a read
- **WHEN** a citation rationale quotes a profile's limiter value
- **THEN** the app does not read that value from the KB

### Requirement: There SHALL be exactly one authored knowledge source
The authored JSON SHALL be the single source of profile *facts*. The runtime `resources/ai/profile_knowledge.md` SHALL be removed. `docs/PROFILE_KNOWLEDGE_BASE.md` SHALL NOT be removed until a duplication differential has surfaced its residue and every residue item is folded into the JSON or preserved in a slimmed doc.

#### Scenario: Runtime md removed, design doc removed only after residue accounted for

- **WHEN** the change is complete
- **THEN** `resources/ai/profile_knowledge.md` no longer exists as a hand-authored source; and `docs/PROFILE_KNOWLEDGE_BASE.md` is either deleted (only if the differential proved zero residue or the residue was folded into the JSON) or retained slimmed to only its non-duplicated residue — never deleted with un-captured content

### Requirement: Any human-readable rollup is generated
Any human-readable rollup of the knowledge base, if retained, SHALL be generated from the JSON and SHALL NOT be hand-edited.

#### Scenario: Rollup regenerated from JSON
- **WHEN** a rollup of the knowledge base is kept
- **THEN** it is generated from the JSON and never edited by hand

### Requirement: Migration SHALL preserve facts except for enumerated reviewed corrections
The migration SHALL be fact-value-preserving, not content-preserving. A parity test SHALL assert that, for every fact already derived today (UGS, inferred flag, analysisFlags, aliases, skipCatalog, family) and every shipped `kBands` row (axis, lo, hi, confidence), the post-migration resolved value equals the pre-migration value.

#### Scenario: Fact-value parity holds

- **WHEN** the parity test compares pre- and post-migration resolved fact values for every fact and every former `kBands` row
- **THEN** all values are equal, with differences permitted only for entries on the enumerated reviewed-corrections list (mis-resolution fixes or deliberately corrected facts), and never silently

#### Scenario: KB coverage preserved — no profile loses its entry

- **WHEN** every profile/title that resolved to a KB section under the old markdown (every section title plus every `Also matches:` alias) is resolved post-migration
- **THEN** each still resolves to a valid entry (a non-`skipCatalog` entry with non-empty `prose`); no section was dropped and no alias lost coverage, with divergence permitted only on the enumerated reviewed-corrections list

#### Scenario: Consumer API and recompute contract unchanged

- **WHEN** any existing caller invokes `expertBandForKbId`, `getAnalysisFlags`, `ugsForKbId`, `ugsInferredForKbId`, `canonicalNameForKbId`, `computeProfileKbId`, `allKbUgsEntries`, or `crossProfileReferenceContent`
- **THEN** the signature is unchanged and the value is recomputed fresh at load from the qrc JSON, identical to the recompute-on-load behavior before the change

### Requirement: Prose is re-authored and not byte-compared
Prose SHALL be re-authored and SHALL NOT be byte-compared. The `buildProfileCatalog` output line MAY change to `displayName [family]`.

#### Scenario: Catalog line may change format
- **WHEN** the catalog is built after migration
- **THEN** the output line may read `displayName [family]`, and no prose comparison is made

### Requirement: Deliberate fact changes are enumerated
Any deliberate fact change SHALL appear on an enumerated, reviewed corrections list. No fact SHALL change silently.

#### Scenario: Corrected mis-resolution is listed
- **WHEN** a mis-resolution is corrected in migration
- **THEN** it appears on the reviewed corrections list

### Requirement: Consumer API and recompute contract are unchanged
`ExpertBand`, every C++ consumer signature and the recompute-on-load contract SHALL be unchanged. The `kbId` token consumers pass and return is now the stable `id`.

#### Scenario: Consumers pass the stable id
- **WHEN** a consumer passes or returns a `kbId`
- **THEN** the value is the stable `id`

### Requirement: Prose numbers SHALL be commentary; the expert band SHALL exist exactly once
Numbers inside `prose` SHALL be descriptive commentary and SHALL NOT be substituted or injected at runtime. The expert band SHALL exist in exactly one authoritative place, the `expertBand` struct, and SHALL be rendered into the assembled LLM blob from it as one cited sentence.

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

### Requirement: Prose SHALL NOT state the expert band
`prose` SHALL NOT state the expert band as the band. It MAY discuss dial-in targets and profile behaviour, which are distinct from the cited band envelope.

#### Scenario: Prose discusses dial-in, not the band
- **WHEN** an entry's prose discusses dial-in targets
- **THEN** it does not restate the cited band envelope as the band

### Requirement: Restatement lint with reviewed acknowledgement
The validator SHALL carry a best-effort lint that flags a `prose` line restating the entry's band bounds verbatim or near-verbatim. An entry whose prose legitimately narrates its own setpoints MAY carry a reviewed `expertBand.proseRestatesBand` rationale, which silences the lint when it is a non-empty string. A `proseRestatesBand` that is present but not a non-empty string SHALL hard-fail the build and SHALL NOT suppress the lint.

#### Scenario: Reviewed ack silences the lint
- **WHEN** an entry carries a non-empty `proseRestatesBand` rationale
- **THEN** the restatement lint is silenced for that entry

#### Scenario: Malformed ack hard-fails
- **WHEN** `proseRestatesBand` is present but not a non-empty string
- **THEN** the build fails and the lint is not suppressed

### Requirement: Stale acknowledgements are surfaced
An acknowledgement on an entry whose prose no longer restates any bound SHALL be surfaced as a non-fatal warning, so the suppression set cannot rot and silently mask a future regression.

#### Scenario: Stale ack warns
- **WHEN** an entry's prose no longer restates a bound but still carries an ack
- **THEN** a non-fatal warning is reported

### Requirement: The assembled LLM prompt SHALL be equivalent or improved, never degraded

As a result of this change the shot-analysis prompt the model receives SHALL be equivalent or improved, never worse. The `shotsummarizer.cpp` instructional examples and the `:1302` anti-hallucination guardrail SHALL be retained verbatim. A final prompt-equivalence check over a fixed representative profile set SHALL diff the assembled prompt old-vs-new; every difference SHALL be a deliberate, reviewed improvement, and an unintended or degrading difference SHALL fail the gate.

#### Scenario: Prompt-equivalence gate at the end

- **WHEN** the end-of-change prompt-equivalence check assembles the shot-analysis prompt for the fixed profile set, old-vs-new
- **THEN** the only differences are deliberate reviewed improvements (e.g. the struct-rendered band sentence), the `:1290/:1302` text is byte-identical, and any unintended or degrading difference fails the gate

### Requirement: A corpus resolution gate SHALL be a hard merge gate, asserting outcomes as well as identity
Resolution SHALL be gated by the shot corpus, which runs the real analysis pipeline over stored shots and compares emitted findings against per-shot expectations. The corpus SHALL carry at least one fixture whose title resolves ONLY through the profile-alias boundary-prefix step and whose expectations depend on a KB `analysisFlags` entry.

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

### Requirement: The corpus gate asserts outcomes, not identity alone
The corpus gate SHALL assert outcomes, meaning the findings a user would have seen, rather than identity alone. An identity-only assertion would fail a refactor that resolves differently but suppresses correctly, and pass a change that resolves correctly but emits the finding anyway.

#### Scenario: Correct resolution emitting a by-design finding fails
- **WHEN** a profile resolves correctly but emits a finding its KB entry suppresses
- **THEN** the corpus gate fails on the emitted finding

### Requirement: Resolution identity is asserted directly
Resolution identity SHALL additionally be asserted directly, so that a profile which stops resolving is reported as such. Every shipped or starter profile title, the editor-canonical outputs and the boundary-prefix fixtures, including the shot-819 and `"D-Flow / Q - Jeff"` cases, SHALL resolve to exactly one `id`.

#### Scenario: Stopped resolution is reported directly
- **WHEN** a shipped profile title stops resolving
- **THEN** the identity assertion fails for that profile

### Requirement: Gate assertions live beside existing KB tests
These assertions SHALL live beside existing KB tests, and a new test binary SHALL NOT be added for this purpose. Both gates SHALL be part of the suite that gates merge.

#### Scenario: Gates run in the merge suite
- **WHEN** the merge suite runs
- **THEN** both the outcome gate and the identity assertions execute

### Requirement: A shape-based resolution step SHALL follow the title steps and SHALL resolve to a candidate set
When all title-based resolution steps miss and the caller supplies the profile's frame data, the resolver SHALL attempt a fourth step keyed on the profile's **shape**, as defined by `profile-shape-equivalence`, rather than its name. The step SHALL yield the **set** of KB ids whose shipped profile has the same shape. An empty set SHALL be the explicit unresolved outcome, identical to a total miss.

#### Scenario: A renamed dial-in derivative resolves by shape

- **GIVEN** a user profile whose title matches no alias and is no profile boundary-prefix, and whose shape
  equals that of a shipped profile that resolves to a KB entry
- **WHEN** the profile is resolved
- **THEN** the resolver SHALL yield a candidate set containing that KB entry's id

#### Scenario: Title resolution still wins and is never overridden

- **GIVEN** a profile whose title resolves through the exact alias, profile-prefix, or editor-default step
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

### Requirement: The shape step is total and order-independent
The shape step SHALL be total, deterministic and order-independent. It SHALL NOT rank, score or measure distance between profiles, SHALL NOT depend on enumeration order, and SHALL introduce no threshold or tunable constant. It is anchored on structural equality, so the prohibition on non-exact best guesses remains in force.

#### Scenario: Resolution does not depend on enumeration order
- **WHEN** the shipped profile set is enumerated in a different order
- **THEN** the shape step yields the same set

### Requirement: The shape step runs only after title misses
The shape step SHALL NOT be consulted when any title step resolved, and a shipped profile's own resolution SHALL be unchanged by it.

#### Scenario: Title resolution still wins
- **WHEN** a title step resolves
- **THEN** the shape step is not consulted

### Requirement: Facts SHALL transfer from a candidate set under per-fact rules chosen by which error they risk
A candidate set with more than one member SHALL NOT be collapsed by picking a member. Each KB fact SHALL transfer under the rule chosen so that the residual error is a missing statement rather than a wrong one, except where a missing statement would itself hide a real fault.

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

### Requirement: Presence and shape-silencing flags transfer by rule
Presence of KB context, which gates profile-shape-dependent analysis, SHALL be true for any non-empty candidate set, since every member answers the same structural question. Suppression flags that silence a shape-derived diagnosis SHALL transfer as the **union** across the candidate set.

#### Scenario: A shape-silencing flag applies when only one candidate carries it
- **WHEN** one candidate in a multi-member set carries a shape-silencing flag
- **THEN** the flag applies to the set

### Requirement: Physics flags and assertive facts require unanimity
A suppression flag that disables a detector reading physics rather than profile shape SHALL require **unanimity** across the candidate set and SHALL be withheld otherwise. Assertive per-profile facts, such as a cited expert operating band or a grind-scale position, SHALL also require **unanimity** and SHALL be withheld otherwise. Withholding SHALL be a strict no-op, indistinguishable from the fact's absence.

#### Scenario: A physics-detector flag is withheld unless every candidate carries it
- **WHEN** only some candidates carry a physics-detector flag
- **THEN** the flag is withheld

#### Scenario: A disputed expert band is withheld rather than guessed
- **WHEN** candidates disagree on an expert band
- **THEN** the band is withheld

### Requirement: Unclassified facts default to unanimity
Any KB fact not explicitly classified SHALL default to the **unanimity-or-withhold** rule. A fact added later SHALL NOT acquire the union rule by omission; the union is the narrow exception for flags that only silence a shape-derived diagnosis.

#### Scenario: An unclassified fact defaults to unanimity
- **WHEN** a fact without an explicit classification is transferred from a multi-member set
- **THEN** it transfers only when every member agrees

### Requirement: A single-member set transfers every fact
A single-member candidate set SHALL transfer every fact, making a unique shape match equivalent to a title match for every consumer.

#### Scenario: A unique shape match behaves exactly like a title match
- **WHEN** a shape resolves to exactly one entry
- **THEN** every consumer behaves as it would for a title match

### Requirement: Identity facts require a single-member set
Facts that identify which profile the knowledge came from, such as its canonical display name, derivation label or roast affinity, SHALL be a single indivisible identity claim and SHALL require a **single-member** candidate set. Analysis-affecting facts SHALL still transfer under their own rules in that case.

#### Scenario: Identity is withheld for a multi-member set
- **WHEN** a multi-member set would otherwise show a display name
- **THEN** no identity is shown
- **AND** analysis-affecting facts still transfer under their own rules

### Requirement: Knowledge indicator and prose are shown for any non-empty set
The knowledge indicator and the prose body are not identity claims and SHALL NOT require a single member. Where the candidate set is non-empty the indicator SHALL be shown. Opening it SHALL present every member's prose, each labelled with that member's canonical display name, with an explicit statement that the frame structure matched more than one documented profile.

#### Scenario: Identity is withheld but knowledge is still disclosed
- **WHEN** the candidate set has more than one member
- **THEN** the indicator is shown, and opening it lists each member's prose with an explicit multi-match statement

### Requirement: The knowledge source SHALL NOT describe one profile with more than one entry
Two shipped profiles that are identical in extraction (same frame count, and per frame the same pump, sensor, transition, temperature, target and exit) are one profile, whatever their titles say, and SHALL resolve to one knowledge entry.

#### Scenario: Extraction-identical profiles resolve to one entry

- **GIVEN** two shipped profiles that are identical in every extraction-affecting field and differ only in
  presentation metadata such as title, notes or target weight
- **WHEN** each is resolved
- **THEN** both SHALL resolve to the same knowledge entry
- **AND** that entry's facts, including any expert operating band, SHALL apply to both

### Requirement: Shape buckets split across entries are reconciled in the source
A shape bucket whose members resolve to different entries SHALL be treated as a claim requiring evidence from the profile data. It SHALL be reconciled in the source, not absorbed by the transfer rules.

#### Scenario: Split bucket is reconciled in the source
- **WHEN** a shape bucket's members resolve to different entries
- **THEN** the source is reconciled against the profile data rather than the transfer rules hiding the split

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
Where a profile resolves by shape rather than by title, any surface that identifies the profile's knowledge SHALL name the matched entry by the KB's canonical display name and SHALL word it as a derivation, so the user can tell the relationship was inferred from structure rather than read from its name. A profile that resolved by title SHALL be presented exactly as today, with no added label.

#### Scenario: Shape-resolved profile names its base

- **GIVEN** a user profile that resolved by shape to a KB entry
- **WHEN** the profile is shown in the profile list, or a shot taken with it is shown on the shot review or
  shot detail page
- **THEN** the surface SHALL name the matched entry's canonical display name, worded as a derivation

#### Scenario: Title-resolved profile gains no label

- **GIVEN** a profile that resolved through any title step
- **WHEN** it is shown on any of those surfaces
- **THEN** no derivation label SHALL be added

#### Scenario: Title-resolved profile may still show its dial-in differences

- **GIVEN** a profile that resolved through a title step and is the same shape as the bundled profile its
  entry was authored against
- **WHEN** the user opens its knowledge entry
- **THEN** the dial-in difference block SHALL be shown
- **AND** no derivation label SHALL have been added on the list or shot surfaces

### Requirement: The derivation label scope
The derivation label SHALL govern only the short attribution beside a profile in the profile list, and beside a shot on the shot review and shot detail pages. It SHALL NOT govern the dial-in difference block, which is shown inside the knowledge entry and is gated on shape equality rather than on resolution origin.

#### Scenario: Title-resolved profile may still show dial-in differences
- **WHEN** a profile resolved by title has dial-in differences
- **THEN** it gains no derivation label but remains eligible for the dial-in difference block

