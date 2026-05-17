## 1. Schema & authored JSON

- [x] 1.1 Define the JSON schema (`resources/ai/profile_knowledge.schema.json`): `id` (kebab key) + `displayName` + `alsoMatches` + `defaultForEditorType` + `ugs{value,inferred,note}` + `analysisFlags` + `skipCatalog` + `family` [closed enum] + `expertBand{axis,lo?,hi?,src?,provenance,confidence,rationale,srcArchive?}` + one opaque `prose` string; `lo`/`hi` independently optional; no `category`/`roast`/`summary` fields
- [x] 1.2 Re-author `resources/ai/profile_knowledge.md` + `docs/PROFILE_KNOWLEDGE_BASE.md` into `resources/ai/profile_knowledge.json`: assign each entry a stable `id` + `displayName`; transcribe facts faithfully (UGS/flags/aliases/family); re-author the section body into one clear `prose` string (rewrite unhelpful prose, not byte-mirrored); preserve the cross-profile `skipCatalog` reference sections
- [x] 1.3 Transcribe every shipped `kBands` row into the matching entry's `expertBand` (axis, lo, hi, confidence) verbatim, its C++ comment audit into `expertBand.rationale`; set `provenance` (`cited`/`author-stated`/`inferred`); for `cited`, resolve `src` to a real-doc URL from the existing attributions (external or the project GitHub copy) — never fabricate; `author-stated` → `src:"profile-notes"`; `inferred` → no `src`, `rationale` justifies why we believe it
- [x] 1.4 Verified `shotsummarizer.cpp:1290/1302` are prompt instructional/guardrail text (`:1290` family-switch teaching example, `:1302` anti-hallucination guardrail), NOT stranded facts; the underlying facts (e.g. 80's-Espresso temps) are KB-canonical; the prompt text is retained verbatim — no relocation (artifacts corrected accordingly)

- [x] 1.5 Create `docs/kb_sources/` and **relocate the existing saved sources into it via `git mv`** (not re-captured): `light_roast_profiles_transcript.txt`, `medium_roast_profiles_transcript.txt`, `dark_roast_profiles_transcript.txt`, `dflow_damian_profile_transcript.txt`, `UNIVERSAL_GRIND_SETTING.md`; update **live** inbound references (e.g. `docs/CLAUDE_MD/AI_ADVISOR.md`); do NOT rewrite references inside `openspec/changes/archive/*` (frozen history)
- [x] 1.6 For any external `src` with no existing local copy, capture a durable one where possible (saved page/PDF, transcript, or at minimum the cited excerpt as text); set every `expertBand.srcArchive` to its repo-relative path under `docs/kb_sources/`; intrinsic `profile-notes` sources need none

## 2. Build-time validator (hard gate)

- [x] 2.1 Implement the schema/structure validator: unknown-key rejection, type/range checks, `id` kebab+unique, `family` ∈ closed enum, `expertBand` invariants (`lo<hi` when both present, positive bound, axis ∈ enum, `provenance` enum + cross-field `src` required-if-`cited`/absent-if-`inferred`, `src` if set = well-formed `http`/`https` URL or `profile-notes` token, set `srcArchive` points to an existing repo file)
- [x] 2.2 Implement identity/alias integrity as an enumerate-and-assert: unique `id`; every `alsoMatches`/`displayName`/`defaultForEditorType` key resolves to exactly one `id`; no orphaned/duplicate alias
- [x] 2.3 Decision checkpoint (design D1/D5): integrity is a clean ~15-line dict enumerate-and-assert in tools/validate_kb.py — no relational machinery → **Option B confirmed, SQLite fallback NOT triggered** (recorded in design Open Questions)
- [ ] 2.4 Wire the validator into CMake as a build step that fails the build on non-zero exit (no generated artifact in the build graph — pass/fail check only); verify it fails on a deliberately corrupted entry then passes on the real JSON

- [x] 2.5 Add the best-effort lint: flag any `prose` line that restates the entry's `expertBand` bounds verbatim/near-verbatim (D9 regression guard)

## 3. Resolver rewrite

- [ ] 3.1 Build the explicit alias→`id` map from every entry's `displayName` + `alsoMatches` + `defaultForEditorType` at load
- [ ] 3.2 Rewrite `matchProfileKey`/`computeProfileKbId` to: normalize (retain existing accent/punctuation normalization) → exact map lookup → yield an `id`, or explicit unresolved on miss
- [ ] 3.3 Delete the order-dependent greedy `startsWith`/`contains` fallback entirely
- [ ] 3.4 Confirm unresolved propagates as a silent no-op (`expertBandForKbId` → `std::nullopt`, byte-identical to shipped absence-intentional behavior)

## 4. Loader & consumers switch to JSON

- [ ] 4.1 Replace `loadProfileKnowledge`'s line-scraper with `QJsonDocument` parsing into `ProfileKnowledge`; remove every `line.startsWith(...)` field path
- [ ] 4.2 Repoint `expertBandForKbId` to read the band from the parsed KB; delete the static `kBands` table
- [ ] 4.3 Assemble the LLM blob from typed fields + the re-authored `prose` (not byte-mirrored); `buildProfileCatalog` line becomes `displayName [family]`; leave the `shotsummarizer.cpp:1290/1302` instructional/guardrail text untouched (not stranded facts)
- [ ] 4.4 Verify `getAnalysisFlags`, `ugsForKbId`, `ugsInferredForKbId`, `canonicalNameForKbId` (now returns `displayName`), `buildProfileCatalog`, `crossProfileReferenceContent`, `allKbUgsEntries` read the structured KB with unchanged signatures, keying on `id`

- [ ] 4.5 Render the cited band sentence into the assembled LLM blob from `expertBand` (struct → one sentence); confirm prose carries no second copy of the band claim (D9)

## 5. Tests (hard gates)

- [ ] 5.1 `tst_kb_schema`: corrupted JSON (typo'd key, out-of-range band, duplicate/orphaned alias) fails; real JSON passes
- [ ] 5.2 `tst_kb_resolution`: every title in `tests/data/shots/` + shipped starter profiles + representative D-Flow/A-Flow editor outputs resolves to exactly one `id`; shot-819 and `"D-Flow / Q - Jeff"` as explicit fixtures
- [ ] 5.3 Fact-value parity test: post-migration resolved fact equals pre-migration for every fact (UGS/inferred/flags/aliases/skipCatalog/family) and every former `kBands` row (axis/lo/hi/confidence); divergences only for entries on the enumerated reviewed-corrections list, never silent
- [ ] 5.4 Assembled-blob test: the LLM blob per profile is well-formed, carries the D9 struct-rendered band sentence, and contains no second copy of the band claim (NOT a byte-compare against old `pk.content`)

## 6. Remove the old sources & verify

- [x] 6.1a Duplication differential: take `docs/PROFILE_KNOWLEDGE_BASE.md`, strip every part now represented in `profile_knowledge.json` (per-profile facts/prose, UGS table, citation src URLs, cross-profile ordering); surface the **residue** (content captured nowhere else)
- [x] 6.1b DECISION (Jeff): keep `PROFILE_KNOWLEDGE_BASE.md` as a slimmed **residue-only companion** (NOT folded into the JSON), for now. It is NEVER deleted while it holds un-captured knowledge.
- [x] 6.1c Category-4 spot-diff DONE (dense, most-cited sample: D-Flow editor, A-Flow, Adaptive v2, LPI, Blooming, Allongé, Default, Londinium, Damian's Standalone). FINDING: the docs `### Profile Details` is materially richer than the runtime md the JSON prose was authored from (verbatim profile-notes quotes, de1app stock params, per-`[SRC]` tags, granular roast/flavor). Two consequences: (1) **no regression** — JSON prose faithfully condenses the *shipped runtime md*, the non-reg bar (D11); the docs-only richness was never in the shipped prompt. (2) That richness is **genuine residue**: folding it into JSON would be a massive re-author + a per-profile prompt change needing D11 review. CONCLUSION: `## Profile Details` + `## Cross-Profile Grind Ordering` are **RETAINED verbatim in the companion — NOT stripped, NOT folded** (lossless preservation, matching Jeff's residue-companion decision).
- [ ] 6.1d Perform the slim as ONE operation. STRIP only the unambiguously-safe: `## KB File Format` (100%-superseded by `profile_knowledge.schema.json`) + `## Basecamp Research Status` + `## TODO: Additional Data Needed` (Jeff decided drop — "old news"; substance already in JSON `prose` + `## Sources`). RETAIN everything else as the residue companion: `## Profile Details`, `## Cross-Profile Grind Ordering`, `## The 4 Mother Categories`, `## General Roast-Level Advice`, `## Cross-Roast Profile Summary`, the full `## Sources` table; add a header explaining the companion's role (fuller design-doc knowledge; the structured runtime KB is `resources/ai/profile_knowledge.json`).
- [ ] 6.1e Remove `resources/ai/profile_knowledge.md` (runtime md — fully superseded by the JSON); update CMake/`ai.qrc` + the `shot_eval` target list to ship `profile_knowledge.json`
- [ ] 6.2 Grep-confirm no `kBands` table or per-profile value literal remains in C++; confirm `shotsummarizer.cpp:1290/1302` instructional/guardrail text is retained verbatim
- [ ] 6.3 Full Qt Test suite green; `shot_eval` over `tests/data/shots/` shows analysis unchanged except entries on the enumerated reviewed-corrections list (mis-resolution fixes + deliberate fact fixes)
- [ ] 6.4 Confirm out-of-scope items untouched: the `title.startsWith("D-Flow"/"A-Flow")` editor-membership rule, factory default profile names, MCP A-Flow editor-param schema descriptions
- [ ] 6.5 Prompt-equivalence gate (D11): assemble the shot-analysis prompt old-vs-new over a fixed representative profile set; every diff must be a deliberate reviewed improvement; `:1290/:1302` byte-identical; degrading/unintended diff fails
- [ ] 6.6 KB-coverage gate: enumerate every profile/title that resolved to a KB section under the OLD md (every `## ` section title + every `Also matches:` alias); assert each still resolves post-migration to a valid entry (a non-skipCatalog entry with `prose`); no section dropped, no alias lost — divergence only on the enumerated reviewed-corrections list
