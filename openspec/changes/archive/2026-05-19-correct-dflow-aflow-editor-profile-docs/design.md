## Context

The editor-vs-profile model (D-Flow/A-Flow are Recipe Editor types; the profile is the name past the `/`) is authoritative — `docs/CLAUDE_MD/RECIPE_PROFILES.md` ("Editor Types" table; title-prefix detection) and decision D11 in `capture-dialin-coaching-guidance`. It was applied to `docs/PROFILE_KNOWLEDGE_BASE.md` (the human synthesis twin) but not to the shipped resource the LLM ingests (`resources/ai/profile_knowledge.md`) or the other D-Flow/A-Flow docs. The shipped KB still says "base D-Flow", "D-Flow / Damian **family**", "## D-Flow Q **variant**", "standard D-Flow **variants**", and lists A-Flow profiles that do not exist (`A-Flow / medium`, `-dark`, `-very dark`, `-like D-Flow`) vs the real built-ins (`A-Flow / default-light/-medium/-dark/-very-dark/-like-dflow`). The shipped KB content is injected verbatim into both AI surfaces, so this is wrong information reaching the model on every request.

## Goals / Non-Goals

**Goals:**
- Make the shipped KB D-Flow/A-Flow prose teach the editor-vs-profile model.
- Fix the factually-wrong A-Flow profile names in the shipped KB.
- Bring the other AI/user-facing D-Flow/A-Flow docs into terminological consistency.
- Keep it doc-only, reversible, and non-colliding with `capture-dialin-coaching-guidance`.

**Non-Goals:**
- Renaming section headers or `Also matches:` aliases (alias/resolution-regression risk; #1160 split mechanics already work).
- Adding `Expected*:`/arm directives — owned by `capture-dialin-coaching-guidance`.
- Any code/struct/schema/migration/behavioral change.
- Editing frozen historical artifacts (`docs/plans/*`, test-plan docs).

## Decisions

**D1 — Prose + names only; headers/aliases are load-bearing and frozen.** Section titles (`## D-Flow`, `## D-Flow Q variant`, `## Damian's LRv2 / LRv3`, `## A-Flow`, `## Londinium`) and `Also matches:` lines drive `profile_kb_id` resolution; the #1160 grind-equivalence fix depends on them. This change edits only the *body prose* and the *profile-name references inside sections*. A drift-check (section/alias count + header text unchanged) gates the edit. *Alternative:* rename sections to "D-Flow editor / D-Flow / Q profile" — rejected: high alias-match regression risk for zero functional gain; D11 already documents the model, the AI just needs the prose to stop contradicting it.

**D2 — Fix the A-Flow names as a factual correctness item, not cosmetic.** `A-Flow / medium` etc. are not the shipped built-ins; the AI is told non-existent profiles exist and may recommend them by name. This is the most concrete, separable, urgent item and justifies the change standing alone (not waiting on the corpus). Names come from the actual `resources/profiles/a_flow_*.json` `title` fields, not invented.

**D3 — Standalone, decoupled from `capture-dialin-coaching-guidance`.** Same rationale used for the #1160 lean split: a correctness/consistency fix (containing a live factual bug in what the LLM reads) must not be hostage to the speculative arms/teaching corpus. Both changes edit `resources/ai/profile_knowledge.md` but in disjoint ways — this one rewrites prose/names, `capture` appends `Expected*:`/arm directive lines — and **both are bound by the same don't-rename-headers/aliases rule**, so they are mergeable in either order without conflict. `capture` task 1.1 is annotated to record that prose/name correctness is owned here.

**D4 — Editor-vs-profile language rule (applied consistently).** "D-Flow"/"A-Flow" unqualified = the *editor*. A profile is always written as the full title (`D-Flow / Q`, `A-Flow / default-medium`). "variant"/"family" is removed where it implies D-Flow is a profile; where a shared-behavior grouping is genuinely meant (e.g. "all profiles built with the D-Flow editor share the pressurized-soak structure"), it is rephrased to "profiles built with the D-Flow editor". "base D-Flow" → "the `D-Flow / default` profile" (it is the starter/example, not a canonical base). The lever-decline shape and per-profile pressure-limit clamp are described as *editor-level* behavior, not profile traits.

## Risks / Trade-offs

- **[Header/alias accidentally changed → profile resolution regression]** → D1: explicit drift-check (heading list + `Also matches:` lines byte-identical before/after) is a task gate and a test guard.
- **[Merge collision with `capture-dialin-coaching-guidance`]** → D3: disjoint edit regions (prose/names vs appended directive lines) + shared don't-rename invariant; whichever lands first, the other rebases cleanly. Cross-reference recorded in `capture` task 1.1.
- **[Re-introducing wrong framing later]** → optional lint/test asserting the D-Flow/A-Flow sections contain none of `A-Flow / medium|dark|very dark|like D-Flow` and no profile-implying "D-Flow variant/family/base D-Flow" phrasings.
- **[Third active change to manage]** → accepted; the payoff is the factual fix landing fast and the model-propagation being auditable rather than implicit. Smallest-possible scope (doc-only) keeps it cheap.

## Migration Plan

Pure text edits. `resources/ai/profile_knowledge.md` D-Flow/A-Flow prose + A-Flow names; consistency edits to the AI/user-facing docs; verification-only on the already-correct ones. No DB/schema/code/migration. Rollback = single revert. Verification: the heading/alias drift-check; a test guard that the shipped KB contains the real A-Flow titles and none of the stale ones; spot-read the rendered `dialing_get_context` KB section and the in-app advisor prompt to confirm the model reads correctly; existing test suite must stay green (no functional change expected).

## Open Questions

- Whether to add a permanent regression test for the "no stale A-Flow names / no profile-implying D-Flow framing" guard (recommended — cheap, prevents recurrence) or a one-time verification. Leaning permanent guard.
- Depth of the sweep for lower-traffic docs (`AUTO_FLOW_CALIBRATION.md`, `BLE_PROTOCOL.md`, `SAW_LEARNING.md`, `MCP_SERVER.md`, `MCP_TEST_PLAN.md`, `SHOT_REVIEW.md`): fix only where the wording actively implies D-Flow/A-Flow is a profile; do not churn incidental mentions. Resolved during apply per file.
