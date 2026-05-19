## Why

D-Flow and A-Flow are Recipe Editor *types* (`dflow`/`aflow`), not profiles — the profile is the name past the `/` (established as decision D11 in `capture-dialin-coaching-guidance`, confirmed by `docs/CLAUDE_MD/RECIPE_PROFILES.md`). That model was applied to one doc (`docs/PROFILE_KNOWLEDGE_BASE.md`) but **not propagated**. The shipped knowledge base the LLM actually ingests (`resources/ai/profile_knowledge.md`) still teaches the wrong model ("base D-Flow", "the whole D-Flow / Damian **family**", "## D-Flow Q **variant**", "standard D-Flow **variants**") **and contains a factual error**: it lists A-Flow profiles as `A-Flow / medium`, `A-Flow / dark`, `A-Flow / very dark`, `A-Flow / like D-Flow` while the actual shipped built-ins are `A-Flow / default-light`, `-default-medium`, `-default-dark`, `-default-very-dark`, `-default-like-dflow`. The AI is being told profiles exist that do not. ~17 other docs mention D-Flow/A-Flow with unaudited framing. This is a correctness + consistency fix that should not wait on the speculative arms/teaching corpus (`capture-dialin-coaching-guidance`).

## What Changes

- **Rewrite the D-Flow/A-Flow section *prose* in `resources/ai/profile_knowledge.md`** to the editor-vs-profile model: D-Flow/A-Flow are editor types; the profile is the name past the `/`; per-profile differences are *different profiles built with the editor*, not "variants of a D-Flow profile". Remove "base D-Flow / family / variant" framing where it implies D-Flow is a profile (keep it only where it accurately means "the `D-Flow / default` profile").
- **Fix the stale A-Flow profile names** in the shipped KB to the actual built-ins (`A-Flow / default-light/-medium/-dark/-very-dark/-like-dflow`).
- **Preserve section headers and `Also matches:` alias lines exactly** (`## D-Flow`, `## D-Flow Q variant`, `## Damian's LRv2 / LRv3`, `## A-Flow`, `## Londinium`). Renaming risks alias/profile-resolution regressions and the #1160 split mechanics already work — this change touches *prose and names inside sections*, never headers/aliases.
- **Editor-vs-profile consistency sweep** of the other D-Flow/A-Flow-mentioning docs, prioritized: AI/user-facing first (`docs/CLAUDE_MD/AI_ADVISOR.md`, `docs/ESPRESSO_DIAL_IN_REFERENCE.md`, `docs/SIMPLE_PROFILE_EDITOR.md`, `docs/UNIVERSAL_GRIND_SETTING.md`); verify `docs/PROFILE_KNOWLEDGE_BASE.md` (already cleaned) and `docs/CLAUDE_MD/RECIPE_PROFILES.md` (already the authoritative-correct source) stay consistent; historical `docs/plans/*` and test-plan files are out of scope (frozen artifacts).
- **No code, no struct, no schema, no migration, no behavioral change** beyond the LLM ingesting corrected prose + correct profile names.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `dialing-context-payload`: adds a content-accuracy invariant for the shipped Profile Knowledge Base — it SHALL describe D-Flow/A-Flow as editor types (not profiles) and SHALL reference only profile names that correspond to actual shipped built-in profiles. This is a KB-content correctness requirement; the UGS parser, alias matching, `ProfileExpectation` schema, and RGS algorithm are unchanged.

## Impact

- **Docs/resources**: `resources/ai/profile_knowledge.md` (D-Flow/A-Flow section prose + A-Flow names; headers/aliases untouched); consistency edits to `docs/CLAUDE_MD/AI_ADVISOR.md`, `docs/ESPRESSO_DIAL_IN_REFERENCE.md`, `docs/SIMPLE_PROFILE_EDITOR.md`, `docs/UNIVERSAL_GRIND_SETTING.md`; verification-only on `docs/PROFILE_KNOWLEDGE_BASE.md` and `docs/CLAUDE_MD/RECIPE_PROFILES.md`.
- **AI surfaces**: both the in-app advisor system prompt and `dialing_get_context` ingest the corrected KB section content via the existing injection path — no plumbing change; the only behavioral delta is the LLM no longer being taught a wrong model or non-existent A-Flow profile names.
- **Tests**: a guard asserting the shipped KB contains no stale A-Flow names (`A-Flow / medium` etc.) and does contain the actual built-in A-Flow titles; optionally a lint that the D-Flow/A-Flow sections don't reintroduce "variant/family/base D-Flow" profile-framing language. No functional test changes.
- **No** DB migration, schema/storage/API change, header/alias rename, or Visualizer surface. Rollback is a single revert.
- **Relationship**: independent of and decoupled from `capture-dialin-coaching-guidance` (which only *adds* `Expected*:` directives to the same file and explicitly must not rename headers). This change owns the prose/name correctness; `capture` owns the arm directives. Both preserve headers/aliases, so they do not collide. Conceptual model is decision D11 in `capture-dialin-coaching-guidance`; this change is its propagation. `split-ugs-pressure-variants` is archived/superseded and out of scope.
