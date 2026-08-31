## Why

Decent is releasing a second machine (Bengle) whose pump reaches ~20 mL/s, against the DE1's natural 7-8 mL/s. A profile whose pressure step carries no flow limit therefore pours very differently on the two machines — and profiles are shared freely between owners of both. The Diaspora discussion settled on a single answer, now shipped in de1app nightly (`skialpine/de1app@fdd091f3`): **every pressure step gets an 8 mL/s flow limit by default, and a pressure step's flow limit can no longer be "off"**.

Decenza must brew a given profile the same way de1app does. It currently does not: of the 77 shipped profiles that contain a pressure step, **74 have at least one step with no flow limit** — every one of those now pours differently in the two apps. The divergence is not only about Bengle: 8 mL/s is inside the DE1's own range, so a profile that would have run at 8.5 mL/s on a fast puck is capped in de1app and uncapped here.

The same commit raises de1app's editable flow ceiling from 8 to 20 mL/s so that Bengle profiles can be authored and, importantly, **read back without being clamped**. Decenza's editors cap at 8, so a Bengle-authored profile opened here shows a wrong number and writes that wrong number back if the control is touched.

## What Changes

- **Load-time normalization.** When a profile becomes the current profile, every pressure step with no flow limit (`max_flow_or_pressure <= 0`) is capped at the default 8 mL/s, and a basic Pressure profile's scalar `maximum_flow` is defaulted the same way. In memory only: the value is visible in the editor and is written only if the user saves the profile — shipped profile files on disk are untouched, exactly as in de1app.
- **Generated frames carry the limit.** The basic-pressure→advanced conversion attaches the limiter to its forced-rise frames instead of leaving them unlimited, and the frame is renamed `"forced rise"` (de1app dropped `"without limit"` because it is no longer true). The legacy name stays readable, so existing profiles and imports keep working.
- **A pressure step's flow limit can no longer be off.** In both the advanced step editor and the simple Pressure editor, the flow-limit control floors at 0.1 mL/s and no longer shows "off". Switching a step from Flow to Pressure snaps a 0/off limit to the default.
- **Editable flow ceiling 8 → 20 mL/s** for flow goals and flow limits, matching de1app, so a high-flow profile round-trips through Decenza's editors unchanged. The DE1 itself still runs at its own maximum, and the BLE frame encoding (U8P4) still saturates at 15.9375 mL/s — that is a protocol property shared with de1app, not something this change introduces.

Not in scope: the same commit's Samsung font/resolution fix (no counterpart here — Decenza does not rewrite screen size from the running resolution) and its Streamline steam on/off fix (skin-local; Decenza's `steamDisabled` is a runtime flag driven by the Off pitcher preset, not derived from the steam timeout). de1app's `steam_flow` default moved 700 → 70 to fix a bug where a fresh install read as steam-off in Streamline; Decenza's default is already 150 (1.5 mL/s) and needs no change.

## Capabilities

### Modified Capabilities
- `de1app-profile-parity`: adds the load-time flow-limit normalization as a parity requirement, and updates the forced-rise requirement for the frame's new name and its now-present limiter.

## Impact

- `src/profile/profile.{h,cpp}` — `applyDefaultPressureFlowLimit()`, the default constant, forced-rise frame generation and naming, forced-rise counting.
- `src/profile/profileframe.{h,cpp}` — new frame-name constant plus a name predicate that accepts both spellings.
- `src/profile/recipegenerator.cpp` — forced-rise frames in the recipe generator's pressure path.
- `src/profile/recipeparams.cpp` — flow and limiter clamps raised to 20.
- `src/controllers/profilemanager.{h,cpp}` — call the normalizer on the load and recipe-upload paths; expose the default limit and the flow ceiling to QML so neither number is written twice.
- `qml/components/ValueInput.qml` — opt-in `snapZeroTo`, default 0/disabled. It is what lets a typed 0 land on the default while the − button still floors at 0.1, which is what de1app does; without it the control's own clamp would turn a typed 0 into 0.1.
- `qml/pages/ProfileEditorPage.qml`, `qml/pages/SimpleProfileEditorPage.qml`, `qml/pages/RecipeEditorPage.qml` — control floors, ceilings, and the removal of "off" on a pressure step's flow limit.
- `tools/de1app_edit_oracle.tcl`, `tools/gen_edit_matrix.py` — see Oracles below.
- Wiki manual: a short note in the profile-editing section that a pressure step always has a flow limit.
- No BLE protocol, database, or settings-schema changes.

## Oracles

Three recorded oracles moved with the de1app bump, all limiter-only:

- `tests/data/de1app_packed/` — 4 goldens, regenerated with `tools/gen_de1app_pack_corpus.py`
  against the updated mirror. de1app's own packer now emits the rise frame's extension frame,
  byte-identically to ours.
- `tests/data/edit_matrix/` — 107 goldens. These needed a tool fix first: `de1app_edit_oracle.tcl`
  set `advanced_shot` and ran the plugin's `prep`/`update_*` without ever running de1app's load
  path, so it could not see a cap that `select_profile` applies. It now sources `profile.tcl` and
  calls the real `::profile::apply_default_flow_limit_to_pressure_steps` — the proc itself, not a
  transcription of it, which is what keeps the oracle from going stale on the next bump.
- `resources/profiles/` — 4 built-in JSONs, via `profile_sync --sync`. The forced-rise frame picks
  up its limiter and its new name; nothing else in the 89 differed.
