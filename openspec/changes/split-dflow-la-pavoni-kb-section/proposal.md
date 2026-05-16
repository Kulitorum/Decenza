## Why

The #1160 fix (archived `correct-dflow-variant-ugs`) split the monolithic `## D-Flow` KB section along pressure/temperature fault lines — but explicitly punted on `D-Flow / La Pavoni` as outside its "lean focused fix" scope, leaving it an `Also matches:` alias of `## D-Flow`. That alias is **factually wrong** for La Pavoni and is a residual correctness bug, not a band-feature problem:

- `## D-Flow` carries `UGS: 0.5`, "D-Flow / default uses an 88°C fill", and the ~9-bar/88°C family framing. `D-Flow / La Pavoni` inherits all of it.
- Per de1app `D_Flow/code.tcl` stock params, La Pavoni is fill 1.2 bar / **84°C** / pour flow 2.4 mL/s / **pressure limit 9.0 bar** / 46 g — and its shipped profile `notes` state verbatim *"Damian created this profile to simulate the results from his La Pavoni machine which he profiled side by side with a Slayer V3 for same taste in cup. The goal is to grind for a pressure peak between 6 and 9 bar."* (`[SRC:profile-notes]`, `[SRC:de1app-dflow]`).
- So La Pavoni is materially the **`## D-Flow Q variant` family** (84°C fill, 6–9 bar dial-in target, lever-emulation, coarser-than-default grind) — *not* the default ~9-bar/88°C family it is currently grouped with. Today the AI is told La Pavoni runs 88°C with no dial-in band and the same grind center as default; all three are wrong.

Any per-profile consumer keyed off the resolved KB section (UGS, analysis flags, the in-progress expert-band feature) inherits the wrong data for La Pavoni. This change finishes the #1160 per-profile split for the one profile #1160 deferred. It is **independent of and a prerequisite for** `flag-off-expert-band-in-shot-summary` (which can only key La Pavoni distinctly once it has its own canonical section); that change does not block this one.

## What Changes

- **Add a new `## D-Flow La Pavoni variant` section** to `resources/ai/profile_knowledge.md`, mirroring the `## D-Flow Q variant` precedent exactly:
  - Title deliberately omits `" / "` (the parser splits titles on `" / "`; `## D-Flow / La Pavoni` would register the bare key `d-flow` and collide with the base section — the identical reason `## D-Flow Q variant` is titled that way). Resolution via `Also matches: "D-Flow / La Pavoni"`.
  - `UGS: ~1.0 (inferred — La Pavoni's 6–9 bar target + 84°C fill pull coarser than D-Flow / default's ~9 bar / 88°C, the same mechanism the Q variant documents; not on the UGS chart)` — the **same inference methodology #1160 already established and shipped for the Q variant**, applied to a profile with the same pressure/temperature characteristics; sourced, not fabricated. The regression test asserts only the relational facts (own canonical name, coarser than default), not an absolute chart value.
  - `AnalysisFlags: flow_trend_ok`, `Category: Lever/Flow hybrid (Londinium family)`, `Family: lever-decline` (shares the lever-decline behavior).
  - Prose: D-Flow-editor La Pavoni emulation (Damian ran D-Flow/default + a real La Pavoni side by side vs a Slayer V3 reference; 18 g VST; milk-drink tuned); the verbatim profile-notes dial-in goal (peak 6–9 bar); de1app stock params; "coarser than default, do not transfer a default grinder setting 1:1"; the shared DO-NOT-flag list (declining pressure / early soak / setpoint-vs-actual temp gap).
- **Edit `## D-Flow`**: remove `"D-Flow / La Pavoni"` from `Also matches:`; update the `UGS:` parenthetical and the "Profiles in this section" prose so the section cleanly covers `D-Flow / default` and the standalone-by-alias profiles only (no behavioral guidance lost — the shared DO-NOT-flag lines stay; La Pavoni's new section references them the same way the Q variant does).
- **Mirror in `docs/PROFILE_KNOWLEDGE_BASE.md`** (the human-facing twin) so it stays in sync, per the #1160 precedent.
- **Regression coverage in `tests/tst_dialing_blocks.cpp`**: `D-Flow / La Pavoni` resolves to a canonical name distinct from `D-Flow / default`; `ugsForKbId("d-flow / la pavoni")` strictly coarser than `ugsForKbId("d-flow / default")`; `flow_trend_ok` preserved on La Pavoni; `## ` heading count increases by exactly one and every built-in title still resolves to exactly one section.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `dialing-context-payload`: `D-Flow / La Pavoni` resolves to its own KB section with its own UGS/temperature/dial-in framing instead of inheriting the base `## D-Flow` (default) section's. Same per-profile-correctness mechanism as #1160; no parser/grammar change, no new directive.

## Impact

- **Lineage**: completes #1160 (`correct-dflow-variant-ugs`, archived) for the profile it explicitly deferred. Parent #1147.
- **Prerequisite for** `flag-off-expert-band-in-shot-summary` La Pavoni coverage (that change keys by canonical KB-section identity; La Pavoni needs its own section to be a distinct entry — Phase A there is the D-Flow/Q twin only, La Pavoni enters its Phase B gated on this change). This change has no dependency on the band change.
- **Data**: `resources/ai/profile_knowledge.md` (+1 section, edit `## D-Flow`), `docs/PROFILE_KNOWLEDGE_BASE.md` (mirror). **Code**: none — pure KB data + an existing-parser resolution change. **Tests**: `tests/tst_dialing_blocks.cpp` regression assertions.
- **No** DB migration, schema/storage change, parser/grammar change, Visualizer write, or behavior change beyond La Pavoni resolving to correct per-profile KB. Rollback is a single revert.
- **Related**: #1147 (parent), #1160 (the precedent it completes). Plain references, no auto-close.
