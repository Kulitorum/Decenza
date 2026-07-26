## Context

D-Flow and A-Flow are de1app **plugins** that present a handful of high-level parameters and write DE1 `advanced_shot` frames. Decenza re-implements both as `RecipeParams` (the parameters) + `RecipeGenerator` (the frame writer), with `Profile::editorType()` selecting which. None of that has ever been checked against the plugins.

Reading the two plugins settles several things the Decenza-side code and the in-flight `preserve-recipe-visualizer-roundtrip` design had wrong.

**The plugins store nothing.** Each has a `proc prep`, run on profile load, that rebuilds every editor variable from the frames:

```tcl
# D_Flow_Espresso_Profile/plugin.tcl — 3 frames, fixed indices
set ::Dflow_filling_temperature $filling(temperature)
set ::Dflow_soaking_{seconds,pressure,volume,weight}  ...from soaking(...)
set ::Dflow_pouring_flow        [round_to_one_digits $pouring(flow)]
set ::Dflow_pouring_pressure    $pouring(max_flow_or_pressure)   ;# NOT pouring(pressure)
set ::Dflow_pouring_temperature $pouring(temperature)

# A_Flow/code.tcl — 9 or 6 frames via set_profile_index
set ::Aflow_ramp_updown_seconds [round_to_integer [expr {$ramp_up(seconds) + $ramp_down(seconds)}]]
set ::Aflow_pouring_flow        [round_to_one_digits $pouring_start(flow)]
set ::Aflow_pouring_pressure    $ramp_up(pressure)
set ::ramp_down_enabled   [expr {$ramp_down(seconds) > 0}]
set ::flow_extraction_up  [expr {$pouring(flow) > $::Aflow_pouring_flow}]
set ::2nd_fill_step       [expr {[llength ...] > 8 && $pause(seconds) > 0}]
```

So the frames are the storage, the three A-Flow toggles are recovered from frame *structure*, and the editor type comes from the title prefix. This is why no `.tcl` in the 89-file corpus carries a recipe key — not an oversight upstream, but the design.

**The plugins mutate frames in place; Decenza rebuilds them from constants.** `update_D-Flow` and `update_A-Flow` read the current `advanced_shot`, overwrite a specific list of fields, and write it back. `RecipeGenerator` constructs all frames from literals. Any field the plugin leaves alone but Decenza writes is silently overwritten on the first Decenza save — the most likely source of drift, and it is structural rather than a typo.

**A-Flow inherits from D-Flow.** Its readme: "Profile Editor based on D-Flow … Infuse parameters are not changed compared to D-Flow. Only the fill step is different with 8 ml/s flow." The inheritance is by lineage, not code — neither plugin sources the other — so it must be verified, not assumed. Note one real divergence that inheritance does *not* cover: A-Flow sets `soaking(temperature)` from `Aflow_filling_temperature`, D-Flow sets it from `Dflow_pouring_temperature`.

## Prior art — what is already settled, and what this change is not

Three archived changes did adjacent work. **All of them verified profile *data*; none verified the *editor*.** That distinction is this change's whole reason to exist.

- **`2026-07-25-sync-builtin-profiles`** compared Decenza's built-ins against de1app and reaprime: 64/64 equivalent, zero divergences. It also settled two things this change must inherit rather than re-litigate:
  - **`Jan3kJ/A_Flow` is the canonical source; `de1plus/profiles/` is a stale snapshot.** The base copy holds four A-Flow profiles at **6 frames**, added in de1app commit `80eb34cc` (2025-09-03) and never refreshed, while the plugin ships all five at **9**. `check_profiles_exist` only copies when a file is absent, so the stale copy wins forever. Upstream de1app issue #350. `profile_sync` already prefers the plugin copy.
  - **Consequence for this change:** fixtures must come from `plugins/A_Flow/profiles/`, never `de1plus/profiles/` and never `tests/data/de1app_profiles` if that mirrors the base copy. Verifying against the stale 6-frame snapshot would produce a suite that passes against the wrong oracle — the precise failure D2 guards. The 6-frame layout still needs coverage, but as the *legacy* case `set_profile_index` handles, not as the reference.
- **`2026-07-25-fix-de1app-profile-drift`** fixed the Tcl *reader*, finding 338 scalar mismatches across 82 profiles caused by three reader defects. It establishes that profile-level scalars now round-trip, so this change can assume the reader and treat frame/parameter behaviour as the open question.
- **`2026-05-16-correct-dflow-variant-ugs`** and **`2026-05-19-correct-dflow-aflow-editor-profile-docs`** corrected the knowledge base's model of D-Flow/A-Flow (editor types, not profiles; the profile is the name past the `/`). Documentation only — no bearing on generator behaviour, but the editor-vs-profile vocabulary they established is used throughout here.

A grep of all 154 archived changes finds exactly one mentioning `RecipeGenerator`/`RecipeParams` (`refactor-extract-profile-manager`, a refactor). **The generator has never been verified against either plugin.**

## Goals / Non-Goals

**Goals:**
- Know, with executable evidence, whether Decenza generates the frames each plugin generates and recovers the parameters each plugin recovers.
- Know whether load→save is lossless for every stock profile of both editors.
- Have an enumerated, justified list of every place Decenza deliberately differs from its reference.
- Leave behind a regression gate, not a one-off audit.

**Non-Goals:**
- Fixing what the verification finds. Findings are reported; remediation is scoped separately once the size is known.
- Editing shipped profile JSONs, or either plugin.
- Executing Tcl. Parity is asserted against the plugin's *transcribed* rules and its stock profile data, not by running de1app.
- Verifying the simple `pressure`/`flow` (settings_2a/2b) editors — different lineage, not A-Flow/D-Flow.

## Decisions

### D1. The plugins are the oracle; Decenza is the subject
Every expected value traces to a line in `plugin.tcl` / `code.tcl` or to a stock profile shipped by the plugin. Where Decenza and a plugin disagree, the plugin is right by definition and the difference is a finding. No expected value is derived from Decenza's own code or from its built-in JSONs — those are what is being tested.

### D2. Transcribe the plugin rules into the test, cite the source line
The suite cannot run Tcl, so each rule is transcribed into C++ with a comment naming the proc and line it came from. This is the weak point of the approach — a transcription error yields a test that passes against the wrong oracle — so transcription is checked twice over: once against the plugin source, and once against the plugin's own stock profiles, which are the plugin's rules already executed. A transcribed rule that disagrees with the shipped profiles is wrong regardless of how it reads.

### D3. Drive the suite from the plugins' stock profiles
A-Flow ships five `.tcl` profiles; D-Flow embeds three in `write_*_profile` procs. These are the plugins' own output and the highest-value fixtures available: for each, `prep`-equivalent extraction then `update_*`-equivalent generation must reproduce the file byte-for-byte in every frame field. Any hand-written fixture is a second-order check by comparison.

### D4. Verify D-Flow first, A-Flow as its delta
Ordering follows the dependency. D-Flow's pass settles the shared infuse/soak semantics; A-Flow's pass then covers only its own additions (fill step, pressure ramp/decline, extraction ramp, three toggles) plus an explicit assertion that the inherited half is genuinely unchanged. This keeps a shared-half bug from being diagnosed twice with two different explanations.

### D5. Round-trip stability is the headline assertion
`frames → parameters → frames` as a fixed point is the property users actually depend on: open a profile, change nothing, save, and it must still be the same profile. It subsumes most extraction and generation bugs into one check per stock profile, and it is the exact operation that corrupts profiles today.

### D6. Decenza-only parameters are enumerated, not silently tolerated
Each of `fillTimeout`, `fillPressure`, `fillFlow`, `infuseEnabled` gets a written verdict: deliberate extension (with what it does to a profile the plugin would round-trip untouched), or accident. `filling(seconds)` and `filling(flow)` are never written by either plugin, and D-Flow derives `filling(pressure)` from the soak pressure — so a Decenza save can change fill behaviour the plugin would have preserved. Whether that is acceptable is a product decision; this change's job is to state it precisely.

### D7. Report, do not repair
The change ends with a findings document and a passing-or-failing gate. Tests for confirmed defects are committed **failing** (`QEXPECT_FAIL` with the finding id) rather than weakened to pass, so the gate records reality. Repair is scoped once the findings exist — several plausible outcomes (adopting `prep`-style extraction wholesale, dropping Decenza-only parameters) are large enough to deserve their own proposal.

## Risks / Trade-offs

- **Transcription error gives false confidence.** → D2's two-source check: the plugin's stock profiles are the rules already executed, so they catch a misread rule. Any expected value that cannot be corroborated by a shipped profile is called out as transcribed-only.
- **The suite pins current behaviour instead of correct behaviour** if written by reading Decenza first. → D1 forbids deriving any expected value from Decenza; write the expectation from the plugin, then run it.
- **Plugins move.** Both are submodules pinned in the de1app clone; A_Flow is at `e1a4d87`, matching its origin at the time of writing. A later plugin release can invalidate a transcribed rule with no test failure. → Record the pinned commit in the suite and re-check on bump; treat the plugins as a vendored dependency.
- **D-Flow's stock profiles are embedded in Tcl procs, not files**, so they are harder to use as fixtures than A-Flow's five `.tcl` files. → Extract them once into test data with their provenance recorded, rather than hand-copying values into assertions.
- **Findings may be large.** The premise error already found suggests more. → D7 keeps this change bounded at "know the truth"; a big finding becomes its own change rather than expanding this one.

## Migration Plan

No runtime behaviour changes and no data migration — this change adds tests and a findings document.

1. Transcribe D-Flow's `prep` / `update_D-Flow` rules and extract its three embedded stock profiles into test data.
2. Land the D-Flow parity suite. Record findings; commit failing tests for confirmed defects.
3. Transcribe A-Flow's `prep` / `update_A-Flow` / `set_profile_index`, using its five stock `.tcl` files directly.
4. Land the A-Flow parity suite, including the inheritance assertions and both frame layouts.
5. Write up findings, and feed the reconstruction verdict back into `preserve-recipe-visualizer-roundtrip`.

Rollback: delete the suite. Nothing else is touched.

## Open Questions

- Are Decenza's `fillTimeout` / `fillPressure` / `fillFlow` / `infuseEnabled` intended extensions, or accidents? D6 produces the evidence; the call is the maintainer's.
- If frame-derived extraction proves faithful, does `preserve-recipe-visualizer-roundtrip` still need a `recipe` block at all — or does implementing `prep` close the round-trip with no schema, no Visualizer PR, and no de1app PR? This change should answer it with evidence rather than assertion.
- Should the parity suite gate on the de1app submodule commit, failing loudly on a plugin bump rather than silently verifying against a stale oracle?
- D-Flow's stock profiles are generated by `write_La_Pavoni_profile` / `write_Q_profile` / the `default` writer. Are those three the complete set Decenza ships as `d_flow_*.json`, and does Decenza's copy match what the plugin writes today?
