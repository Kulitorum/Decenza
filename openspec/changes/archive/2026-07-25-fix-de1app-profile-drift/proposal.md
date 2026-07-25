## Why

Decenza's 93 built-in profiles are supposed to be byte-for-byte equivalent to de1app's, so
that a profile makes the same coffee whichever app started the shot. They are not. A
type-aware comparison of every profile-level scalar against the de1app `.tcl` corpus finds
**338 mismatches across 82 of 89 profiles**.

The cause is not 82 bad data files — it is three defects in the Tcl reader, and the drift
signature proves it: for each affected field our side holds a single constant across every
profile (`espresso_pressure` is 9.2 in all 23 cases, `preinfusion_time` is 5 in all 19,
`espresso_decline_time` is 25 in all 19). Those are Decenza's `fromJson` defaults. We are
not misreading these fields; we are not reading them at all, then writing defaults over
them on save.

## What Changes

**Tcl reader fidelity (`Profile::loadFromTclString`)**

- **Ungate the simple-profile scalar block.** It is currently guarded by
  `m_steps.isEmpty() && !isAdvancedProfile` (`src/profile/profile.cpp:1348`). Most de1app
  simple profiles ship a stored `advanced_shot`, so `m_steps` is non-empty and the entire
  block — `espresso_pressure`, `preinfusion_time`, `espresso_hold_time`,
  `espresso_decline_time`, `pressure_end`, `preinfusion_flow_rate`,
  `preinfusion_stop_pressure`, `temp_steps_enabled`, `maximum_*_range_default` — is skipped.
- **Read the scalar set for every profile type, not just the matching branch.** `flow_profile_*`
  is read only inside the `settings_2b` branch, but de1app writes the full scalar set on every
  profile regardless of type. This is the same gating mistake already found and fixed on the
  **writer**, where it "destroyed those keys on 58+ advanced built-ins"
  (`docs/CLAUDE_MD/RECIPE_PROFILES.md`); the reader still has it.
- **Map `profile_hide` → `hidden`.** The key appears nowhere in `src/`, so every Tcl import
  ends with no `hidden` and defaults to `"0"` (72 rows). Note this is cosmetic *inside*
  Decenza — our profile list hides via `SettingsApp::isHiddenProfile()`, a separate per-user
  filename list — but de1app and reaprime read the profile field.
- **Apply the type-dependent `_advanced` rule** to `maximum_pressure_range_advanced` and
  `maximum_flow_range_advanced` (40 rows), which are currently read straight from disk. For
  `settings_2a`/`2b` de1app derives them from the `_default` counterparts. The rule is
  documented in `docs/CLAUDE_MD/RECIPE_PROFILES.md`.

**Drift detection**

- Extend `profile_sync` with a **profile-level scalar comparison** implementing the
  type-aware rule, and add a test that fails on any drift. Today the tool compares frames
  only, which is why 338 scalar mismatches went unseen. The gate must exist and pass
  *before* any built-in data is rewritten — an ad-hoc script has already produced two
  materially wrong drift numbers (60, then 4) during this investigation.

**Built-in data**

- Re-sync `resources/profiles/` from the de1app corpus once the reader and the gate are
  correct, as an isolated, reviewable commit. This rewrites app-authored values only; a
  user's own saved profiles load through `fromJson`, not the Tcl path, and are not touched.
- Re-baseline the immutable golden corpus (`tests/data/profiles_legacy/`), which is designed
  to fail on exactly this and must be updated deliberately rather than as a side effect.

**Extraction behaviour**

- **BREAKING**: for `settings_2a`/`2b`, discard the stored `advanced_shot` and regenerate
  frames from the scalars, matching de1app. de1app never reads the stored array for a simple
  profile; we do. `Steam_only.tcl` stores frames at 82/80/72 °C while `espresso_temperature`
  is `0` — de1app brews the 0, we brew the 82. This changes extraction for the 26 simple
  profiles, which is the point: the goal is that our profiles behave identically to de1app's,
  and keeping frames de1app discards is exactly how they diverge. Marked breaking because
  shots will differ from previous Decenza builds, not because it is optional. Ships as its
  own commit so the behaviour change is reviewable apart from the data diff.

## Capabilities

### New Capabilities

- `de1app-profile-parity`: Fidelity of the de1app `.tcl` import path and the standing
  guarantee that shipped built-ins match their de1app sources — which spelling of a
  dual-spelled field is authoritative per profile type, which scalars must survive a
  round-trip, how a simple profile's frames are derived, and the comparison gate that keeps
  the built-in corpus honest.

### Modified Capabilities

None. `builtin-profile-sync` (pending promotion from the `sync-builtin-profiles` change)
covers frame-level sync; the scalar-level contract is new surface and lives in the new
capability to avoid deltaing against an unpromoted spec.

## Impact

- **Code**: `src/profile/profile.cpp` (`loadFromTclString`), `tools/profile_sync`
- **Data**: all 93 files in `resources/profiles/`; `tests/data/profiles_legacy/` re-baseline
- **Tests**: `tst_tclimport`, `tst_builtinprofileformat`, new scalar-parity test
- **Docs**: `docs/CLAUDE_MD/RECIPE_PROFILES.md` (the `_advanced` rule section already landed
  in commit `f5506f54`)
- **Cross-app**: improves fidelity for de1app and reaprime readers of Decenza-written files
- **No user-data migration**: user-saved profiles are not rewritten
