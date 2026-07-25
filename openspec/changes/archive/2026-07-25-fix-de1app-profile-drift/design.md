## Context

Decenza ships 93 built-in profiles imported from de1app. The community contract is that a
profile makes the same coffee in every DE1 app, so these files should be equivalent to their
de1app sources. A type-aware comparison of profile-level scalars found **338 mismatches
across 82 of 89 comparable profiles**.

The current state is worse than "some files are stale". `Profile::loadFromTclString` never
reads most de1app scalars, and `profile_sync` compares frames only — so nothing in the
codebase could have caught this. The two failures are complementary: the reader loses the
data, and the tool that exists to detect exactly this looks somewhere else.

Three prior attempts to measure this drift during investigation produced 60, then 4, then 338
rows. The first two were wrong because the measuring script was hand-written against guessed
field names. That history is the main design constraint here: the fix cannot be validated by
another ad-hoc script.

Relevant de1app source (`de1plus/profile.tcl` in the reference checkout):
`legacy_profile_to_v2` (`:450`), the type dispatch (`:467-472`), and the `_advanced`
overwrites in `pressure_to_advanced_list` (`:194-201`) and `flow_to_advanced_list`
(`:345-352`).

## Goals / Non-Goals

**Goals:**

- Make `loadFromTclString` preserve every de1app profile-level scalar.
- Encode de1app's type-dependent field-selection rule once, in a place both the reader and
  the comparison tool use.
- Put a checked-in, failing-on-drift test in place *before* rewriting any profile data.
- Bring `resources/profiles/` into agreement with the de1app corpus.

**Non-Goals:**

- Frame-level content parity. All 338 rows are profile-level scalars; frames are a separate
  and larger comparison, deliberately deferred so this change stays reviewable.
- reaprime parity. This change is de1app-only. A-Flow and D-Flow differences against reaprime
  remain out of scope.
- Rewriting user-saved profiles. Only app-authored data moves.
- Changing how Decenza's profile list decides visibility.

## Decisions

**1. Fix the reader, not the data.**

The alternative — hand-correcting 82 JSON files — was rejected: it leaves the reader broken,
so the next de1app import re-introduces the drift, and it treats a 3-line logic bug as an
82-file data problem. The constant-per-field signature (`espresso_pressure` is 9.2 in all 23
affected profiles) is what makes the reader diagnosis certain rather than plausible.

**2. Read the scalars unconditionally; keep frame generation conditional.**

The gate at `profile.cpp:1348` conflates two questions: "do I need to synthesize frames?" and
"should I read the scalars?". Only the first depends on whether `advanced_shot` was populated.
Splitting them is the minimal correct change — scalars are read for every profile of every
type, and frame synthesis keeps its own condition.

This is the same separation already applied to the **writer**, which writes the scalar block
unconditionally after gating it on `settings_2a/2b` "destroyed those keys on 58+ advanced
built-ins". Reusing a shape that has already been validated in this codebase beats inventing
one.

**3. Put the type-dependent rule in one helper, used by both reader and tool.**

The rule spans four fields and three profile types. Duplicating it in
`loadFromTclString` and again in `profile_sync` is how the two would drift apart, which is
precisely the failure mode this change exists to fix. Alternative considered: encode it only
in the reader and have the tool compare post-import `Profile` objects instead of raw Tcl.
That is attractive — it makes the tool structurally unable to disagree with the reader — but
it also makes the tool blind to reader bugs, which is the class of bug we are fixing. Chosen:
one shared helper, with the tool comparing raw Tcl through it.

**4. The gate lands before the data.**

Sequence is: reader fix → comparison + test → re-sync → golden re-baseline. Re-syncing first
would mean the only evidence the data is right is the same script that has already been wrong
twice. Landing the test first means the re-sync commit's diff is *verified* rather than
asserted, and the commit is independently revertable.

**5. `advanced_shot` regeneration ships as its own commit.**

Requirement "Simple profiles derive frames from their scalars" changes what the machine
actually does for 26 profiles. That is the objective, not a risk to be weighed: the standing
requirement is that our profiles behave identically to de1app's, and a simple profile whose
frames come from a stored array de1app discards cannot do that. `Steam_only` is the clearest
case — de1app brews it at 0 °C, we brew 82.

The split into its own commit is purely about reviewability. Everything else in this change
is fidelity — no shot behaves differently — so folding a behaviour change into a 93-file data
diff would bury it. It lands after the rest is green, not conditionally.

**6. `hidden` is corrected despite having no local effect.**

Decenza filters its profile list through `SettingsApp::isHiddenProfile()`, so the 72 `hidden`
rows change nothing visible here. Correcting them still matters: de1app and reaprime read the
field, and this change is about what our files mean to other apps. It is also the cheapest 72
rows in the set.

## Risks / Trade-offs

- **The re-sync rewrites 82 shipped files at once.** → It lands as its own commit with the
  parity test already passing, so the diff is machine-verified and revertable without
  touching the reader fix.

- **Reading scalars that were previously ignored could change extraction for profiles that
  synthesize frames.** → Frame synthesis inputs are exactly these scalars, so a profile whose
  scalars were previously defaulted may now generate different frames. This is the intended
  correction, but it means the change is not purely cosmetic even before the `advanced_shot`
  decision. The full suite plus `tst_tclimport`'s `compareWithBuiltin` must be green, and the
  golden corpus diff reviewed field-by-field rather than accepted wholesale.

- **The golden corpus is declared immutable.** → Re-baselining is a deliberate, separately
  reviewed commit with the diff explained, not a silent regeneration. If the diff contains
  anything the parity test does not explain, stop.

- **`profile_hide` on 72 built-ins.** → Confirmed inert locally (`isHiddenProfile` is a
  separate mechanism); risk is limited to other apps, where the change is the fix.

- **The comparison's field map could be incomplete again.** → The spec requires the tool to
  report de1app scalars absent from its field map rather than skip them. That is the
  specific defect that produced the wrong 4-row answer, and it is now a tested behaviour
  instead of a habit.

- **de1app is a moving reference.** → The submodules were brought to their upstream tips
  during investigation; the A-Flow duplicate-filename question is settled and recorded in
  `RECIPE_PROFILES.md`. Re-verify the corpus is current before the re-sync commit.

## Migration Plan

1. Reader fix — no data changes, existing tests must stay green.
2. Comparison + parity test — expected to **fail** at this point, proving it detects the
   known 338 rows.
3. Re-sync `resources/profiles/` — parity test goes green.
4. Golden corpus re-baseline — reviewed diff.
5. `advanced_shot` regeneration for `settings_2a`/`2b`, then re-run 3–4 for any built-in
   whose frames change as a result.

Rollback: each step is a separate commit. Reverting the re-sync restores the old data while
keeping the reader correct; reverting the reader fix restores prior behaviour without
touching data.

## Open Questions

- **Do the four A-Flow built-ins get re-synced from the plugin submodule copy?** Provenance
  is settled (the `de1plus/profiles/` copies are a stale 2025-09-03 translation snapshot),
  but the resulting frame change is frame-level, which this change otherwise defers.
- **Should `tests/data/de1app_profiles/` be refreshed** from the updated submodules as part
  of this change, or pinned until the frame comparison lands?
