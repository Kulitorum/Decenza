# SUPERSEDED — never implemented (0/34), archived as obsolete

**Status:** proposed only, never applied. Archived 2026-05-16 as superseded, **not** completed. Its spec delta was deliberately **not** merged into the canonical `openspec/specs/` — the model it encoded was falsified (see below).

## Why it was superseded

A long design conversation falsified this change's conceptual foundation:

1. **Monolithic "D-Flow is a profile" model is wrong.** D-Flow and A-Flow are Recipe Editor *types*; the profile is the name past the `/` (e.g. `D-Flow / Q`). This change's core deliverable — "split the D-Flow *profile* into pressure-distinct *variant* sections (`## D-Flow Q variant`)" — is the exact monolithic error. The correct model (editor-vs-profile, per-`profile_kb_id` keying) is captured as decision **D11** in `capture-dialin-coaching-guidance`.
2. **Envelope-first / archetype-5 strategy was inverted.** A source review (first-party Decent video transcripts, the Decent guide, the D-Flow-author livestream, profile `notes`, maintainer first-hand knowledge) showed the centred `Expected*:` envelope barely beats prose the LLM already has, while the deferred two-sided diagnostic arms are the behavior-changing signal. The successor is diagnostic-first / coverage-first / advisor-first with an evidence-graded, per-arm-cited set — not envelope-first on 5 archetypes.

## Where its non-obsolete parts went

- **The #1160 grind-equivalence correctness fix** (per-profile KB sections so a grinder anchor isn't transferred 1:1 across pressure-distinct D-Flow-editor profiles) **already shipped separately** via archived `correct-dflow-variant-ugs` — `## D-Flow`, `## D-Flow Q variant`, `## Damian's LRv2 / LRv3`, `## A-Flow`, `## Londinium` exist in `resources/ai/profile_knowledge.md` with `Also matches:` keying. Closing this change drops **no** bug fix.
- **The `ProfileExpectation` parser seam** (struct + `Expected*:` parser + teaching clause) is now owned directly by **`capture-dialin-coaching-guidance`** (no longer a prerequisite/dependency) — see that change's D6 and Context.
- **The D9 source-review audit, the editor-vs-profile model (D11), the per-profile suppression/clamp model, and the evidence-graded arm set** all live in `capture-dialin-coaching-guidance`.
- **The deterministic detector** that would consume `ProfileExpectation` remains a separate deferred change: `profile-expectation-deviation-detector`.

## Do not

- Do not `openspec archive` this (it would merge the obsolete-model spec delta into the canonical spec). It is filed here by hand for history only.
- Do not implement from this directory. Use `capture-dialin-coaching-guidance`.
