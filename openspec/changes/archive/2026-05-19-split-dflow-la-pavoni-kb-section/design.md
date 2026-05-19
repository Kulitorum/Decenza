## Context

`ShotSummarizer::loadProfileKnowledge()` parses `resources/ai/profile_knowledge.md` `## ` sections into `s_profileKnowledge`, registering the header key plus every `Also matches:` alias (normalized) as map keys pointing at one `ProfileKnowledge`. `matchProfileKey()` returns the matched normalized key; `ugsForKbId`/`getAnalysisFlags`/`canonicalNameForKbId` look up by it. #1160 (archived `correct-dflow-variant-ugs`) split the monolithic `## D-Flow` into `## D-Flow`, `## D-Flow Q variant`, `## Damian's LRv2 / LRv3` but, by its own "lean focused fix" scope statement, left `D-Flow / La Pavoni` as an alias of `## D-Flow`. This change finishes that split for La Pavoni; it introduces no parser, grammar, or code change — pure KB data plus the existing resolution picking up a new section.

## Goals / Non-Goals

**Goals:**
- `D-Flow / La Pavoni` resolves to its own canonical section with correct per-profile UGS / 84°C temperature / 6–9 bar dial-in framing, not the base default section's.
- Mirror the #1160 method exactly (title without `" / "`, `Also matches:` alias, inferred-UGS-with-rationale, shared DO-NOT-flag behavior referenced not duplicated, human-twin mirrored, regression test).

**Non-Goals:**
- No parser/grammar change; no new directive line type; no `ProfileExpectation`/`Expected*:` content (that is the band change's territory).
- No change to `D-Flow / default`, the Q variant, or LRv2/LRv3 resolution beyond removing the La Pavoni alias from `## D-Flow`.
- Not the band feature; this only corrects KB resolution.

## Decisions

**D1 — Title `## D-Flow La Pavoni variant`, not `## D-Flow / La Pavoni`.** The parser splits titles on `" / "`; a `## D-Flow / La Pavoni` header would register the bare key `d-flow` and collide with the base section. This is the exact constraint and exact resolution #1160 used for `## D-Flow Q variant`. Alias via `Also matches: "D-Flow / La Pavoni"`.

**D2 — Inferred UGS `~1.0`, same methodology #1160 shipped for the Q variant.** La Pavoni's de1app stock params (fill 1.2 / 84°C / pour 2.4 mL/s / limit 9.0 / 46 g) and shipped `notes` ("peak 6–9 bar") put it in the same lower-pressure-target + 84°C-fill regime as the Q variant, which #1160 placed at inferred `~1.0` with documented rationale (lower pressure target + low fill temp → coarser than default's ~9 bar/88°C; not on the UGS chart). Applying the *same already-reviewed-and-shipped inference* to a profile with the same characteristics is consistency, not fabrication. *Mitigation against overclaim:* the regression test asserts only relational facts (own canonical name, strictly coarser than default), never an absolute chart number. *Alternative (rejected): omit a numeric UGS* — rejected: it would still inherit nothing better and the #1160 precedent for this exact family is an inferred value with rationale; omitting would be less consistent and less useful than the established method.

**D3 — Shared lever-decline behavior is referenced, not duplicated.** Like the Q variant, the new section points at `## D-Flow` for the shared soak/declining-pressure DO-NOT-flag list rather than copying it, so the behavioral guidance has one source of truth and the split is data-correctness only.

**D4 — Mirror `docs/PROFILE_KNOWLEDGE_BASE.md` and add regression coverage.** Per the #1160 precedent the human-facing twin stays in sync, and `tst_dialing_blocks.cpp` gets relational assertions so the resolution can't silently regress.

## Risks / Trade-offs

- **[Title/alias collision with the base `d-flow` key]** → D1: title omits `" / "`, identical to the proven `## D-Flow Q variant` approach; regression test asserts every built-in title resolves to exactly one section and the heading count grew by exactly one.
- **[Inferred UGS read as fabricated]** → D2: identical methodology to the already-shipped, reviewed #1160 Q-variant inference, fully rationale-documented and sourced (de1app params + profile-notes); test asserts only the relational claim.
- **[Behavioral guidance lost when La Pavoni leaves `## D-Flow`]** → D3: the shared DO-NOT-flag list stays in `## D-Flow`; the new section references it exactly as the Q variant does — nothing duplicated, nothing dropped.
- **[Human twin drifts]** → D4: `docs/PROFILE_KNOWLEDGE_BASE.md` mirrored in the same change, per #1160.

## Migration Plan

Pure KB data + an existing-parser resolution change. Add one `## ` section, edit the base `## D-Flow` section's `Also matches:`/`UGS:` parenthetical/"Profiles in this section" prose, mirror the human twin, add `tst_dialing_blocks.cpp` assertions. No DB migration, no code/parser/schema change. Build + the dialing test suite via Qt Creator MCP; rollback is a single revert (La Pavoni reverts to aliasing `## D-Flow`).

## Open Questions

- None blocking. The exact inferred-UGS phrasing follows the Q-variant line verbatim in structure; the relational regression assertions are the contract, not the absolute value.
