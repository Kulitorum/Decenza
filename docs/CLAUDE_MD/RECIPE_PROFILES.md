# Recipe Editor & Profile Types

This document describes the Recipe Editor, supported profile editor types, and how Decenza's implementation syncs with de1app's D-Flow and A-Flow editors.

For how a profile — including a renamed or retuned copy of a documented one — reaches its knowledge-base entry, and which KB facts a multi-candidate shape match may and may not transfer, see [`docs/PROFILE_KNOWLEDGE_BASE.md`](../PROFILE_KNOWLEDGE_BASE.md) § *How a profile reaches an entry*.

## What is the Recipe Editor?

The Recipe Editor provides simplified, coffee-concept-based interfaces for creating espresso profiles. Instead of editing raw machine frames, users adjust intuitive parameters like "infuse pressure" and "pour flow", and the editor automatically generates the underlying DE1 frames.

### Key Insight

**Recipe profiles are NOT a different format** — they're a UI abstraction layer. Recipe profiles are standard `settings_2c` (advanced) profiles with the `advanced_shot` array fully populated. The innovation is in the editor, not the storage format.

## Editor Types

The Recipe Editor supports four editor types, each generating a different frame structure:

| Type | Key | Profile Type | Origin | QML Page | Description |
|------|-----|-------------|--------|----------|-------------|
| D-Flow | `dflow` | `settings_2c` | Damian Brakel | `RecipeEditorPage.qml` | Flow-driven extraction with pressure limit |
| A-Flow | `aflow` | `settings_2c` | Janek | `RecipeEditorPage.qml` | Hybrid pressure-then-flow extraction |
| Pressure | `pressure` | `settings_2a` | de1app | `SimpleProfileEditorPage.qml` (via `PressureEditorPage.qml`) | Simple pressure profile |
| Flow | `flow` | `settings_2b` | de1app | `SimpleProfileEditorPage.qml` (via `FlowEditorPage.qml`) | Simple flow profile |

Profiles that don't match any of the above open in `ProfileEditorPage.qml`, the advanced frame-by-frame editor.

### The plugins are the reference — Decenza is verified against them

D-Flow and A-Flow are **de1app plugins**, each in its own repository, and each is the source of
truth for its editor's behaviour. Decenza re-implements them; where the two disagree the plugin is
right by definition.

| Editor | Upstream | Local checkout | Pinned |
|---|---|---|---|
| D-Flow | `github.com/Damian-AU/D_Flow_Espresso_Profile` | `de1app/de1plus/plugins/D_Flow_Espresso_Profile` | `7f3c9726` (v3.1) |
| A-Flow | `github.com/Jan3kJ/A_Flow` | `de1app/de1plus/plugins/A_Flow` | `e1a4d871` (v2.0-beta.2-2) |

Both are git submodules of the de1app clone. **A-Flow's submodule pointer in de1app lags the
plugin's own HEAD** — "latest de1app" is not "latest A-Flow" (see de1app issue #350 below).

**Three facts about these plugins that are not obvious and that shape everything else:**

1. **They store no high-level state.** Each has a `proc prep`, run on profile load, that rebuilds
   its entire editor from the frames. The frames *are* the storage — which is why no `.tcl` profile
   carries a recipe block. A-Flow's three structural toggles are derived from frame structure
   (`ramp_down(seconds) > 0`; `pouring(flow) > pouring_start(flow)`; a 9-frame layout with a
   non-zero pause), not stored anywhere.
2. **They mutate frames in place.** `update_D-Flow` / `update_A-Flow` read the current
   `advanced_shot`, overwrite a named list of fields, and write it back — so every field outside
   that list survives untouched. This is the mechanism that lets one editor drive several profiles
   with different machine personalities. **An editor that rebuilds frames from constants breaks
   that contract**, and is the root cause of most findings recorded against Decenza.
3. **Roles are positional, never pattern-matched.** `prep` indexes. A-Flow's `set_profile_index`
   picks a 9-frame or legacy 6-frame mapping; D-Flow always uses 0/1/2.

**A-Flow profile provenance (de1app issue #350).** The plugin's `profiles/` directory ships all
five stock profiles at 9 frames. de1app's `de1plus/profiles/` holds a stale snapshot: four profiles
at 6 frames, `default-light` missing entirely, added in de1app commit `80eb34cc` (2025-09-03) and
never refreshed — `check_profiles_exist` only copies a file when it is absent, so the stale copy
wins forever. **Always take A-Flow fixtures from the plugin, never from `de1plus/profiles/`.**

**D-Flow ships no `.tcl` files at all.** Its three stock profiles are embedded in `plugin.tcl` and
written out at plugin start. `tools/extract_dflow_profiles.py` extracts them for testing.

The transcribed rules — every parameter, every write, every derived value, with line citations —
live in `openspec/changes/verify-recipe-editor-parity/reference.md`, and the parity suite is
`tests/tst_recipeeditorparity.cpp`. Read the reference before changing either generator.

### How Decenza honours those three facts

Each of the three has a counterpart in the code. Changing one without the other reopens a whole
class of bug, so they are named here together.

1. **Frames are the source of truth. There is no longer a stored `recipe` block at all.**
   `RecipeAnalyzer::prepDFlow` / `prepAFlow` are direct transcriptions of the plugins' `prep`, and
   they are what `getOrConvertRecipeParams` uses — on every read, for every D-Flow/A-Flow profile.

   The block used to be a cache of exactly those derived values, written when parameters had been
   "established". That rule was not enough: the five A-Flow built-ins complied with it and still
   shipped byte-identical 88 °C / 20 s / 9 bar blocks against frames saying 93–95 / 6–60 / 9–10,
   because their parameters had been established at some point in the past and then drifted. A
   cache no reader trusts still goes stale, so **nothing writes one now**, and a stored block is
   removed on sight (`ProfileManager::stripStoredRecipeBlocks` at startup for what is already
   saved, plus a write-back in `loadProfile` for anything that arrives later).

   Three consequences worth knowing:
   - `recipe` **stays listed in `kKnownProfileKeys`** even though nothing reads or writes it. That
     set is the unknown-key passthrough's *exclusion* list, so membership is what makes a stored
     block get dropped — delist it and every one is captured and re-emitted forever.
   - `Profile::jsonParityErrors` **excuses** `recipe` via `deliberatelyDroppedKeys()`. A structured
     value is otherwise never inert, so without this a dropped block reads as `KEY LOST` and the
     four consumers of that check — stored-encoding upgrade, the `espresso_temperature` repair,
     legacy format migration, and `profile_sync --rewrite-format` — all refuse the profile,
     including the write-back that removes the block.
   - `dose` was the block's only field not re-derived from frames or duplicated by a top-level key.
     It is promoted to `recommended_dose` on read, but **only** when it differs from the 18 g
     default and the profile carries no explicit recommendation of its own.

   This is transitional. Decenza was the only producer of the block — de1app has no such key in any
   of its 88 profiles, Decaid models ten fields and drops the rest, Visualizer normalises it away
   in both renderings — so the population carrying one is closed and draining. The strip pass, the
   load write-back, the dose promotion and the parity excusal can all go once it has drained. The
   `kKnownProfileKeys` entry cannot: it is what keeps a straggler harmless.

2. **`Profile::restoreFieldsThePluginNeverWrites()` reinstates in-place mutation.** After
   generating, it restores by frame ROLE every field the corresponding `update_*` proc does not
   assign. **If you add a field to a generator, check the plugin actually writes it** — if not, add
   it to the restore's write-set instead, or the first save silently overwrites what the profile's
   author chose.

3. **Roles resolve positionally**, through the same rule as `set_profile_index`, for both the
   9-frame and legacy 6-frame layouts. No name matching, no sequence pattern matching.

**Do not reintroduce a fill-pressure, fill-flow, fill-duration or infuse-enable parameter.**
`update_A-Flow` writes the fill frame's temperature and nothing else; D-Flow additionally derives
its pressure and pressure-over exit from the soak pressure. Decenza carried all four, wrote them
into the frames, and overwrote fields the plugins preserve. "No soak" is `infuseTime` 0 — which is
how the plugins express a disabled step everywhere else.

### The gates

Three, and they answer different questions. All are in `tests/tst_recipeeditorparity.cpp` and
`tests/tst_recipeeditorapppath.cpp`; see `docs/CLAUDE_MD/TESTING.md` for how to regenerate the
fixtures.

| Gate | Question | Standing |
|---|---|---|
| **Edit matrix** (`editMatrixMatchesDe1app`) | every plugin parameter × every stock profile, one edit, through `ProfileManager` | 0 divergences / 99 |
| **Compound edit** (`compoundEditMatchesDe1app`) | two successive saves, so the second `prep` re-derives from what the first wrote | 8 / 8 |
| **Byte parity** (`everyDe1appProfilePacksIdentically`, `everyDe1appProfileSurvivesASaveCycle`) | do all 89 de1app stock profiles reach the machine as identical bytes, on load and after a save | 89 / 89, nothing excluded |

The last one is the regression guard for everything **outside** the two recipe editors: ~80 of
those profiles are advanced, pressure or flow profiles that no recipe-editor test touches, but they
pass through the same load and save code.

A golden is never hand-adjusted to match Decenza. If one looks wrong, re-read the oracle; if the
oracle is right, Decenza changes.

### `insert_preinfusion_pause` — carried, compared, deliberately not implemented

This is the one known case where a `.tcl` and Decenza disagree about what the machine receives, and
it is **closed as won't-implement** ([#1635](https://github.com/Kulitorum/Decenza/issues/1635)).
Written down so nobody re-derives the divergence and "fixes" it.

**What de1app does.** When `::settings(insert_preinfusion_pause)` is 1, `binary.tcl:880-891`
prepends a 2-second, zero-flow, zero-pressure frame to the profile before packing it — a real
extraction frame, not UI state, and not gated on profile type.

**Why it exists.** It is not a brewing technique. The origin commit (`ec4c0dcc`, John Buckman,
2021-07-24) introduces it to work around a DE1 whose inlet valve is scaling up and opening
sluggishly; the pause gives the valve time to seat before water is commanded through it. That is
why the toggle lives on de1app's **calibrate** tab labelled "Slow start", next to "Two tap steam
stop" and "Eco steam", and why no de1app profile editor exposes it.

**It is a global setting, not a profile property.** It reaches profile files only because
`save_settings_vars` dumps the whole `profile_vars` list (`vars.tcl:3305`) on every save. In de1app
the value is one global cell that a profile load clobbers, so a profile carrying `1` turns the pause
on for **every profile loaded after it** that does not carry the key — which is all 88 stock ones.
A `1` in a file is therefore a snapshot of that author's machine-maintenance setting, not intent
about the coffee. Implementing it per-profile would apply one user's valve workaround to everyone
who downloads their profile.

**Population today: zero.** No de1app stock profile carries the key. The only files that do are the
three A-Flow built-ins (`default-dark`, `default-like-dflow`, `default-light`), all at `0`.

**What Decenza does, and why it is right:**

- **Carried** — `Profile::loadFromTclString()` passes it through (`profile.cpp:1452`), so a
  Decenza-written file still means the right thing to de1app and we do not silently clear someone's
  setting.
- **Compared** — `De1AppTcl::scalarFields()` includes it (`de1apptclfields.cpp:152`), so a built-in
  that gains or loses it fails `tst_tclimport::builtinScalarParity` instead of drifting quietly.
- **Not materialised** — no pause frame is ever generated.

**If you ever do implement it, two traps.** `materializedSteps()` is the wrong place despite being
the obvious one: it feeds `toJsonObject()`, not the wire, so a pause added there is stored, and
re-exporting to `.tcl` hands de1app a profile with the frame *and* the flag — it prepends a second
one. And the DE1 reports `sample.frameNumber` straight into `m_currentProfile->steps()[...]`
(`shottimingcontroller.cpp:163`, `maincontroller.cpp:4095`), so a frame the machine has and
`m_steps` does not puts every frame label, timeline, chart overlay, skip-frame and history record
off by one for the whole shot.

**Upstream note.** `binary.tcl:995` guards the `NumberOfPreinfuseFrames` increment on
`::setting` — singular, a typo for `::settings` — and `ifexists` does `upvar` + `info exists`, which
never brings that array into being. The branch is dead: de1app prepends the frame but does *not*
increment the count, so the machine is told preinfusion ends a frame early, which also mis-arms the
"no stop-at-weight during preinfusion" guard added in `855ca267`. Matching de1app's *behaviour*
means leaving the count alone; matching its apparent *intent* would diverge. If upstream fixes line
995, the header byte changes for every profile using the pause — treat that as a scheduled input to
the parity audit, not a surprise.

## Editor Selection

`MainController::currentEditorType()` determines which editor page opens. Selection is **title-first**:

1. Title starts with `D-Flow/` → D-Flow editor
2. Title starts with `A-Flow/` → A-Flow editor
3. `profile_type` is `settings_2a` → Pressure editor
4. `profile_type` is `settings_2b` → Flow editor
5. Everything else → Advanced frame editor

This matches de1app's convention where D-Flow and A-Flow profiles are identified by their title prefix.

## Profile Types in de1app

The de1app uses `settings_profile_type` to distinguish profile complexity:

| Type | Name | Has advanced_shot? | Description |
|------|------|-------------------|-------------|
| `settings_2a` | Simple Pressure | Empty `{}` | Basic pressure profile, converted at runtime |
| `settings_2b` | Simple Flow | Empty `{}` | Basic flow profile, converted at runtime |
| `settings_2c` | Advanced | Populated | Full frame control (D-Flow/A-Flow output this) |
| `settings_2c2` | Advanced + Limiter | Populated | Advanced with limiter UI |

## Design Philosophy

1. **Simplicity First** — Intuitive parameters vs raw frame fields
2. **Live Preview** — Graph updates as you adjust
3. **Backward Compatible** — Saves both recipe params AND generated frames
4. **Escape Hatch** — Can convert to advanced frames for fine-tuning

---

## Recipe Parameters

### Core Parameters

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Stop at Weight | `targetWeight` | 36 | 0–100 | g |
| Stop at Volume | `targetVolume` | 0 | 0–200 | mL |
| Dose | `dose` | 18 | 3–40 | g |

### Fill Phase

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Fill Temperature | `fillTemperature` | 88 | 80–100 | °C |
| Fill Pressure | `fillPressure` | 3.0 | 0–12 | bar |
| Fill Flow | `fillFlow` | 8.0 | 0–10 | mL/s |
| Fill Timeout | `fillTimeout` | 25 | 0+ | s |

### Infuse Phase

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Infuse Enabled | `infuseEnabled` | true | — | bool |
| Infuse Pressure | `infusePressure` | 3.0 | 0–6 | bar |
| Infuse Time | `infuseTime` | 20 | 0–60 | s |
| Infuse Weight | `infuseWeight` | 4.0 | 0–20 | g |
| Infuse Volume | `infuseVolume` | 100 | 10–200 | mL |
### Pour Phase (D-Flow / A-Flow)

Pour is always flow-driven with a pressure limit (matching de1app D-Flow/A-Flow model).
`pourFlow` = flow setpoint, `pourPressure` = pressure cap (max_flow_or_pressure).

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Pour Temperature | `pourTemperature` | 93 | 80–100 | °C |
| Pour Pressure | `pourPressure` | 9.0 | 1–12 | bar |
| Pour Flow | `pourFlow` | 2.0 | 0.1–8 | mL/s |
| Ramp Time | `rampTime` | 5.0 | 0–30 | s |

### A-Flow Specific

| Parameter | Key | Default | Description |
|-----------|-----|---------|-------------|
| Ramp Down | `rampDownEnabled` | false | Split pressure ramp into Up + Decline phases (doubles/halves `rampTime`) |
| Flow Up | `flowExtractionUp` | true | Smooth flow ramp during extraction (vs flat) |
| 2nd Fill | `secondFillEnabled` | false | Add 2nd Fill (15s) + Pause (15s) frames before pressure ramp |

### Simple Profile Parameters (Pressure / Flow editors)

| Parameter | Key | Default | Range | Unit |
|-----------|-----|---------|-------|------|
| Preinfusion Time | `preinfusionTime` | 20 | 0+ | s |
| Preinfusion Flow Rate | `preinfusionFlowRate` | 8.0 | 0–10 | mL/s |
| Preinfusion Stop Pressure | `preinfusionStopPressure` | 4.0 | 0–12 | bar |
| Hold Time | `holdTime` | 10 | 0+ | s |
| Espresso Pressure | `espressoPressure` | 8.4 | 0–12 | bar |
| Hold Flow | `holdFlow` | 2.2 | 0–10 | mL/s |
| Decline Time | `simpleDeclineTime` | 30 | 0+ | s |
| Pressure End | `pressureEnd` | 6.0 | 0–12 | bar |
| Flow End | `flowEnd` | 1.8 | 0–10 | mL/s |
| Limiter Value | `limiterValue` | 3.5 | 0–12 | — |
| Limiter Range | `limiterRange` | 1.0 | 0–10 | — |

### Per-Step Temperatures (Pressure / Flow editors)

| Parameter | Key | Default | Unit |
|-----------|-----|---------|------|
| Start Temperature | `tempStart` | 90 | °C |
| Preinfuse Temperature | `tempPreinfuse` | 90 | °C |
| Hold Temperature | `tempHold` | 90 | °C |
| Decline Temperature | `tempDecline` | 90 | °C |

---

## Frame Generation

### Architecture: Regeneration vs Patching

**de1app uses a patch model**: `update_D-Flow` and `update_A-Flow` read existing frames, modify only the fields exposed in the UI, and write them back. Fields not exposed in the UI (like Fill `volume`, Pouring `seconds`, dead exit values on `exit_if=0` frames) are preserved from the saved profile.

**Decenza uses a regenerative model**: `RecipeGenerator` builds all frames from scratch using recipe parameters. A passthrough mechanism in `Profile::regenerateFromRecipe()` preserves `volume` and `exitWeight` from old frames by matching on frame name, preventing lossy round-trips for fields that RecipeParams doesn't control.

**Metadata-only optimization**: When only non-frame-affecting params change (`targetWeight`, `targetVolume`, `dose`), frame regeneration is skipped entirely. This matches de1app where changing `final_desired_shot_weight` doesn't call `update_D-Flow` / `update_A-Flow`. Implemented in `MainController::uploadRecipeProfile()` via `RecipeParams::frameAffectingFieldsEqual()`.

**Preinfuse frame count**: The BLE header's `NumberOfPreinfuseFrames` byte tells the DE1 firmware where preinfusion ends and extraction begins, affecting PID tuning. De1app stores this as `final_desired_shot_volume_advanced_count_start` in the profile TCL.

**Whether de1app recomputes it depends on the profile type** — the same split as the `_advanced` fields below:

- **Advanced (`settings_2c`/`2c2`, which is what D-Flow and A-Flow emit)**: preserved as stored, never recomputed. Templates set it to 2. **D-Flow/A-Flow preinfuse frame count must NOT be recalculated** — it is always preserved from the loaded profile or from the editor defaults for new profiles. This is intentional: de1app's `update_D-Flow` and `update_A-Flow` never touch this value, and Decenza must match that behavior.
- **Simple (`settings_2a`/`2b`)**: **recomputed from the generated frames.** `pressure_to_advanced_list` and `flow_to_advanced_list` reset it to 0 (`profile.tcl:17`, `:212`) and `incr` it once per preinfusion frame they append (`:56`, `:79`, `:251`, `:274`). The value stored in the file is discarded, exactly like the stored `advanced_shot`. Decenza's `countPreinfuseFrames()` (count consecutive leading `exitIf=true` frames) is the counterpart, and is the correct behaviour here rather than a fallback.

A comparison that reads the file's `count_start` for a simple profile is reading a value de1app throws away, and will report drift on every such profile.

### D-Flow Frames

```
Filling → Infusing → Pouring
```

**Always 3 core frames** matching de1app (Filling, Infusing, Pouring). When `infuseEnabled=false`, Infusing is emitted with `seconds=0` (machine skips it), NOT omitted — this preserves the 3-frame structure de1app expects.

#### D-Flow Frame Details (matches `update_D-Flow` in de1app `D_Flow/code.tcl`)

**Frame 0: Filling** — pressure pump to saturate puck

| Field | Value | Source |
|-------|-------|--------|
| name | "Filling" | — |
| pump | "pressure" | — |
| pressure | recipe.infusePressure | de1app: `Dflow_soaking_pressure` |
| flow | recipe.fillFlow | de1app: preserved from profile (default 8.0) |
| temperature | recipe.fillTemperature | de1app: `Dflow_filling_temperature` |
| seconds | recipe.fillTimeout | de1app: preserved from profile (default 25.0) |
| transition | "fast" | — |
| sensor | "coffee" | — |
| volume | 100.0 | de1app: preserved (passthrough handles) |
| weight | 5.0 | de1app: preserved (default 5.0 = app-side fill exit) |
| exit_if | true | — |
| exit_type | "pressure_over" | — |
| exit_pressure_over | formula (see below) | de1app: same formula |
| exit_flow_over | 6.0 | — |
| max_flow_or_pressure | 0.0 | — |
| max_flow_or_pressure_range | 0.2 | — |

**Fill exit_pressure_over formula** (matches `update_D-Flow` line 341-346):
```
if pressure >= 2.8:
    exit = round_to_one_digit(pressure / 2 + 0.6)
else:
    exit = pressure
if exit < 1.2:
    exit = 1.2
```

**Frame 1: Infusing** — hold at soak pressure

| Field | Value | Source |
|-------|-------|--------|
| name | "Infusing" | — |
| pump | "pressure" | — |
| pressure | recipe.infusePressure | de1app: `Dflow_soaking_pressure` |
| flow | 8.0 | — |
| temperature | recipe.pourTemperature | de1app: `Dflow_pouring_temperature` |
| seconds | recipe.infuseTime (0 when disabled) | de1app: `Dflow_soaking_seconds` |
| volume | recipe.infuseVolume (100 when disabled) | de1app: `Dflow_soaking_volume` |
| weight | recipe.infuseWeight | de1app: `Dflow_soaking_weight` (app-side SkipToNext) |
| exit_if | false | — |
| exit_type | "pressure_over" | — |
| exit_pressure_over | recipe.infusePressure | de1app: preserved (default 3.0) |
| max_flow_or_pressure | 0.0 | — |
| max_flow_or_pressure_range | 0.2 | — |

**Frame 2: Pouring** — flow-driven extraction with pressure limiter

| Field | Value | Source |
|-------|-------|--------|
| name | "Pouring" | — |
| pump | "flow" | — |
| flow | recipe.pourFlow | de1app: `Dflow_pouring_flow` |
| pressure | 4.8 | de1app: preserved (vestigial) |
| temperature | recipe.pourTemperature | de1app: `Dflow_pouring_temperature` |
| seconds | 127.0 | de1app: preserved (max duration) |
| transition | "fast" | — |
| volume | 0.0 | de1app: preserved (passthrough handles) |
| exit_if | false | — |
| exit_type | "flow_over" | — |
| exit_flow_over | 2.80 | — |
| exit_pressure_over | 11.0 | — |
| max_flow_or_pressure | recipe.pourPressure | de1app: `Dflow_pouring_pressure` |
| max_flow_or_pressure_range | 0.2 | — |

### D-Flow Stock Profiles (from de1app `D_Flow/code.tcl`)

| Profile | Fill pressure | Fill exit | Infuse seconds | Pour flow | Pour pressure | Weight |
|---------|--------------|-----------|----------------|-----------|---------------|--------|
| D-Flow / default | 3.0 bar | 1.5 bar | 60s | 1.7 mL/s | 8.5 bar | 50g |
| D-Flow / Q | 6.0 bar | 3.0 bar | 1s | 1.8 mL/s | 10.0 bar | 36g |
| D-Flow / La Pavoni | 1.2 bar | 1.2 bar | 60s | 2.4 mL/s | 9.0 bar | 46g |

Note: Stock profiles have hand-tuned `exit_pressure_over` values that differ from the formula. The formula produces 2.1 for pressure=3.0 (not the stock 1.5). Both de1app's editor and Decenza apply the formula when the user edits any parameter, so hand-tuned values are overwritten on first edit. This is de1app's intended behavior.

### A-Flow Frames

```
Pre Fill → Fill → Infuse → 2nd Fill → Pause → Pressure Up → Pressure Decline → Flow Start → Flow Extraction
```

**Always 9 frames** (matching de1app). When `infuseEnabled=false`, the Infuse frame is emitted with `seconds=0`, NOT omitted. When `secondFillEnabled=false`, 2nd Fill and Pause have `seconds=0`. De1app's `set_profile_index` uses `> 8` frames to detect new-format profiles; omitting frames would cause it to fall back to 6-frame (old format) indexing.

#### A-Flow Frame Details (matches `update_A-Flow` in de1app `A_Flow/code.tcl`)

**Frame 0: Pre Fill** — 1s workaround for DE1 "skip first step" bug

| Field | Value |
|-------|-------|
| pump | "flow" |
| flow | 8.0, pressure=3.0 |
| temperature | recipe.fillTemperature |
| seconds | 1.0 |
| exit_if | false |
| max_flow_or_pressure | 8.0 (range 0.6) |

**Frame 1: Fill** — flow pump with pressure limiter

| Field | Value | Source |
|-------|-------|--------|
| pump | "flow" | — |
| flow | recipe.fillFlow | de1app: preserved (not modified by `update_A-Flow`) |
| pressure | recipe.fillPressure | de1app: preserved |
| temperature | recipe.fillTemperature | de1app: `Aflow_filling_temperature` |
| seconds | recipe.fillTimeout | de1app: preserved |
| exit_if | true, exit_type "pressure_over" | — |
| exit_pressure_over | recipe.fillPressure | — |
| max_flow_or_pressure | 8.0 (range 0.6) | — |

**Frame 2: Infuse** — pressure hold with zero flow

| Field | Value | Source |
|-------|-------|--------|
| pump | "pressure" | — |
| flow | 0.0 | — |
| pressure | recipe.infusePressure | de1app: `Aflow_soaking_pressure` |
| temperature | recipe.fillTemperature | de1app: `Aflow_filling_temperature` (NOT pour temp) |
| seconds | recipe.infuseTime (0 when disabled) | de1app: `Aflow_soaking_seconds` |
| volume | recipe.infuseVolume (100 when disabled) | de1app: `Aflow_soaking_volume` |
| weight | recipe.infuseWeight | de1app: `Aflow_soaking_weight` (app-side SkipToNext) |
| exit_if | false | — |
| max_flow_or_pressure | 1.0 (range 0.6) | — |

**Frames 3-4: 2nd Fill + Pause** — optional second saturation cycle

| | 2nd Fill | Pause |
|-|----------|-------|
| pump | flow | pressure |
| flow/pressure | flow=8.0 | pressure=1.0, flow=6.0 |
| temperature | pourTemperature (95 when disabled) | pourTemperature (95 when disabled) |
| seconds | 15 (0 when disabled) | 15 (0 when disabled) |
| exit | pressure_over 2.5 | flow_under 1.0 |
| limiter | 3.0 (range 0.6) | 1.0 (range 0.6) |

**Frame 5: Pressure Up** — smooth ramp to pour pressure

| Field | Value | Source |
|-------|-------|--------|
| pump | "pressure" | — |
| pressure | recipe.pourPressure | de1app: `Aflow_pouring_pressure` |
| flow | 8.0 | — |
| temperature | recipe.pourTemperature | de1app: `Aflow_pouring_temperature` |
| transition | "smooth" | — |
| seconds | floor(rampTime/2) when rampDown, else rampTime | de1app: `round_to_integer(rampTime/2)` |
| exit_if | true, exit_type "flow_over" | — |
| exit_flow_over | round(pourFlow*2, 1) when rampDown, else round(pourFlow, 1) | de1app: `round_to_one_digits` |
| exit_pressure_over | 8.5 | — |

**Frame 6: Pressure Decline** — decline to 1 bar

| Field | Value | Source |
|-------|-------|--------|
| pump | "pressure" | — |
| pressure | 1.0, flow=8.0 | — |
| temperature | recipe.pourTemperature | de1app: `Aflow_pouring_temperature` |
| transition | "smooth" | — |
| seconds | rampTime - floor(rampTime/2) when rampDown, else 0 | de1app: `round_to_integer(rampTime/2 + rampTime%2)` |
| exit_if | true, exit_type "flow_under" | — |
| exit_flow_under | round(pourFlow + 0.1, 1) | de1app: `round_to_one_digits` |
| exit_flow_over | 3.0 | — |
| exit_pressure_over | 11.0, exit_pressure_under=1.0 | — |

Note: Integer rounding gives the remainder second to Decline (e.g., rampTime=11 → Up=5, Decline=6).

**Frame 7: Flow Start** — conditionally activated

| State | Condition | seconds | exit |
|-------|-----------|---------|------|
| Passthrough | pressureUpSeconds >= 1 | 0 | exit_if=false |
| Activated | pressureUpSeconds < 1 | 10 | exit_if=true, flow_over round(pourFlow-0.1, 1) |

When activated (ramp disabled or very short), this frame waits for flow to stabilize before extraction. When passthrough, the machine skips it immediately (seconds=0).

**Frame 8: Flow Extraction** — main extraction with pressure limiter

| Field | Value | Source |
|-------|-------|--------|
| pump | "flow" | — |
| flow | round(pourFlow*2, 1) when flowExtractionUp, else 0 | de1app: `round_to_one_digits` |
| pressure | 3.0 (vestigial) | — |
| temperature | recipe.pourTemperature | de1app: `Aflow_pouring_temperature` |
| seconds | 60.0 | — |
| transition | "smooth" | — |
| max_flow_or_pressure | recipe.pourPressure | de1app: `Aflow_pouring_pressure` |
| max_flow_or_pressure_range | 0.6 | — |
| exit_if | false | — |

#### A-Flow Toggle Effects

**Ramp Down** (`rampDownEnabled`):
- OFF: Pressure Up gets full `rampTime`, Pressure Decline gets 0s (exit condition only)
- ON: `rampTime` is doubled by the UI; Pressure Up and Decline each get `rampTime/2`
- Integer rounding: `floor(rampTime/2)` for Up, remainder for Decline

**Flow Up** (`flowExtractionUp`):
- ON (default): Flow Extraction flow = `pourFlow * 2` with smooth transition (ramps up)
- OFF: Flow Extraction flow = 0 (flat, pressure-limited only)

**2nd Fill** (`secondFillEnabled`):
- OFF: 2nd Fill and Pause frames have 0s duration and temperature=95 (skipped immediately)
- ON: 2nd Fill gets 15s, Pause gets 15s, both use pourTemperature

#### Value Rounding

All computed flow exit values use `round_to_one_digits` matching de1app:
- Pressure Up `exit_flow_over`: `round(value * 10) / 10`
- Pressure Decline `exit_flow_under`: `round((pourFlow + 0.1) * 10) / 10`
- Flow Start `exit_flow_over`: `round((pourFlow - 0.1) * 10) / 10`
- Flow Extraction `flow`: `round(pourFlow * 2 * 10) / 10`

### Pressure Profile Frames (settings_2a)

```
[Preinfusion Boost] → Preinfusion → [Forced Rise] → Hold → [Forced Rise] → Decline
```

Matches de1app's `pressure_to_advanced_list()`:

| Frame | Pump | Key Values | Notes |
|-------|------|-----------|-------|
| Preinfusion Boost | flow | tempStart, 2s | Only when tempStart != tempPreinfuse |
| Preinfusion | flow | tempPreinfuse, exit pressure_over | |
| Forced Rise | pressure | espressoPressure, 3s, limiter when `maximum_flow > 0` | When holdTime > 3 |
| Hold | pressure | espressoPressure, with limiter | |
| Forced Rise | pressure | espressoPressure, 3s, limiter when `maximum_flow > 0` | When holdTime was short and declineTime > 3 |
| Decline | pressure | pressureEnd, smooth, with limiter | When simpleDeclineTime > 0 |

### Every pressure step carries a flow limit

`Profile::applyDefaultPressureFlowLimit()` gives any pressure step with no limiter
`Profile::kDefaultPressureFlowLimit` (8 mL/s), and defaults a `settings_2a` profile's scalar
`maximum_flow` the same way. Decent's second machine reaches ~20 mL/s where the DE1 manages 7-8,
so an unlimited pressure step pours differently on the two. de1app ships the same default
(`skialpine/de1app@fdd091f3`), where it is user-overridable and its own comment notes 7-7.5 may
track the DE1 better — so move the number only with upstream.

`ProfileManager::setCurrentProfile()` is the only caller, and the only place `m_currentProfile` is
assigned — `currentProfileAssignedOnlyBySetCurrentProfile` fails the build if a new path assigns
it directly. That gate exists because the cap was first wired by mirroring de1app's two call
sites, and Decenza turned out to have six ways a profile becomes current; four brewed uncapped.

Not called from a parse: a stored profile (a shot's record of what it was pulled with, an import
being compared for de-duplication) must read back exactly what it says. Not called after an editor
save either — de1app caps at load only, so its plugins write uncapped frames on save;
`tst_recipeeditorapppath` compares against those plugins and enforces it.

In `loadProfile()` the hand-over sits ABOVE the two repair write-backs (recipe-block strip,
`espresso_temperature`), so both of them deliberately serialize the local `candidate` rather than
`m_currentProfile`: the cap must reach the editor and the machine and must never be persisted by
loading. Writing the capped copy is silent corruption rather than untidiness — `collectParityErrors()`
only walks keys present in the BEFORE object, so a limiter ADDED to a step that had none is invisible
to the gate and the file is rewritten with a limit the author never chose. Two tests hold this:
`loadDoesNotPersistTheDefaultFlowLimitIntoTheUsersFile` covers the encoding upgrade, which runs
before the hand-over, and `loadDoesNotPersistTheCapThroughTheRepairWriteBacks` covers the two that
run after it.

de1app dropped "without limit" from the forced-rise frame name once it started limiting it. Both
spellings are live — see `ProfileFrame::isForcedRiseName()`.

### Flow Profile Frames (settings_2b)

```
[Preinfusion Boost] → Preinfusion → Hold → Decline
```

Matches de1app's `flow_to_advanced_list()`:

| Frame | Pump | Key Values | Notes |
|-------|------|-----------|-------|
| Preinfusion Boost | flow | tempStart, 2s | Only when tempStart != tempPreinfuse |
| Preinfusion | flow | tempPreinfuse, exit pressure_over | |
| Hold | flow | holdFlow, with limiter | When holdTime > 0 |
| Decline | flow | flowEnd, smooth, with limiter | When holdTime > 0 |

---

## Exit Conditions

There are two independent exit condition systems:

### Machine-side exits (pressure/flow)

Controlled by `exit_if` flag and `exit_type` in the BLE frame:
- Encoded in BLE frame flags (DoCompare, DC_GT, DC_CompF)
- Machine autonomously checks and advances frames
- Types: `pressure_over`, `pressure_under`, `flow_over`, `flow_under`

### App-side exits (weight)

Controlled by `weight` field on the frame, INDEPENDENTLY of `exit_if`. The app monitors scale weight and sends `SkipToNext` (0x0E) command.
- **CRITICAL**: Weight exit is independent of `exit_if` flag!
- A frame can have no `exit` object (no machine exit) with `"weight": 3.6` (app exit)
- Both can coexist: machine checks pressure/flow, app checks weight

D-Flow uses weight exits on:
- Filling frame: `weight=5.0` (exit fill early if scale reads 5g)
- Infusing frame: `weight=infuseWeight` (exit infuse at target weight)
- Profile-level `targetWeight`: stops the shot via the app

### Weight Exit Implementation

```cpp
// CORRECT - weight is independent of exitIf
if (frame.exitWeight > 0) {
    if (weight >= frame.exitWeight) {
        m_device->skipToNextFrame();
    }
}

// WRONG - don't require exitIf for weight!
if (frame.exitIf && frame.exitType == "weight" ...) // BUG!
```

### Mixed-frame race & the Step-Exit Arbiter

When a frame carries **both** a weight exit and a firmware exit (e.g. the D-Flow
`Filling` frame, or imported "soup" profiles with `pressure_over 2.0` + `weight 1.0`),
the two can fire within the same ~100 ms window. `SkipToNext` is **blind/relative** —
it advances whatever frame the DE1 is currently in, with no frame index. So if the
firmware's own exit fires while the app's `SkipToNext` is in flight, the firmware
advances the frame *and* the late tablet skip advances it again — a double
frame-advance that truncates short 2–3 frame profiles.

`StepExitArbiter` (`src/machine/stepexitarbiter.{h,cpp}`, owned per-shot by
`WeightProcessor`) guards this. On a mixed frame, when the weight threshold is
reached the arbiter checks the live firmware sensor against its own threshold:
- **far from threshold** → fire the tablet skip now (weight is the intended exit);
- **near and trending toward it** → defer (≤ 3 samples, ~200–400 ms worst case) so firmware owns the transition;
- **near but not trending** → fire (firmware unlikely to fire on its own).

If the firmware advances the frame mid-deferral, `onFrameAdvanced()` drops the
deferral state and the tablet never sends a stale skip for the frame the firmware
already left. Weight-only and firmware-only frames bypass the arbiter entirely
(behavior unchanged). It is always on — no user-facing setting. See the
`step-exit-arbitration` capability spec and `tst_weightprocessor.cpp`.

---

## Decenza vs de1app Default Values

These defaults only apply when creating brand-new profiles, not when editing existing ones:

| Parameter | de1app D-Flow/default | de1app A-Flow stock | Decenza default |
|-----------|----------------------|---------------------|-----------------|
| fillTemperature | 88 | 93 | 88 |
| pourTemperature | 88 | 93 | 93 |
| infuseTime | 60s | 60s | 20s |
| pourFlow | 1.7 mL/s | 2.0 mL/s | 2.0 mL/s |
| pourPressure | 8.5 bar | 10.0 bar | 9.0 bar |
| targetWeight | 50g | 36g | 36g |
| fillTimeout | 25s | 15s | 25s |
| infuseWeight | 4.0g | 3.6g | 4.0g |

---

## JSON Format

> **Historical.** The `recipe` block shown below is **no longer written** — see "How Decenza
> honours those three facts" above. It is kept here because files in the wild still contain one and
> the reader still promotes its `dose`. Everything outside the block is current.

Recipe profiles used to store both the recipe parameters and generated frames:

```json
{
  "title": "A-Flow / default-medium",
  "author": "Recipe Editor",
  "beverage_type": "espresso",
  "profile_type": "settings_2c",
  "target_weight": 36.0,
  "espresso_temperature": 93.0,
  "mode": "frame_based",

  "is_recipe_mode": true,
  "recipe": {
    "editorType": "aflow",
    "targetWeight": 36.0,
    "targetVolume": 0.0,
    "dose": 18.0,
    "fillTemperature": 88.0,
    "fillPressure": 3.0,
    "fillFlow": 8.0,
    "fillTimeout": 25.0,
    "infuseEnabled": true,
    "infusePressure": 3.0,
    "infuseTime": 20.0,
    "infuseWeight": 4.0,
    "infuseVolume": 100.0,
    "pourTemperature": 93.0,
    "pourPressure": 9.0,
    "pourFlow": 2.0,
    "rampTime": 5.0,
    "rampDownEnabled": false,
    "flowExtractionUp": true,
    "secondFillEnabled": false,
  },

  "steps": [
    { "name": "Pre Fill", "pump": "flow", "flow": 8.0, "..." : "..." },
    { "name": "Fill", "pump": "flow", "..." : "..." },
    { "name": "Infuse", "pump": "pressure", "..." : "..." },
    { "name": "2nd Fill", "pump": "flow", "seconds": 0, "..." : "..." },
    { "name": "Pause", "pump": "pressure", "seconds": 0, "..." : "..." },
    { "name": "Pressure Up", "pump": "pressure", "..." : "..." },
    { "name": "Pressure Decline", "pump": "pressure", "..." : "..." },
    { "name": "Flow Start", "pump": "flow", "..." : "..." },
    { "name": "Flow Extraction", "pump": "flow", "..." : "..." }
  ]
}
```

This dual storage ensures:
- Recipe profiles work on older versions (they just see the frames)
- Recipe parameters are preserved for re-editing
- Advanced users can convert to pure frame mode

---

## File Structure

```
src/profile/
├── recipeparams.h          # RecipeParams struct + EditorType enum
├── recipeparams.cpp        # JSON/QVariantMap serialization + validation + frameAffectingFieldsEqual()
├── recipegenerator.h       # Frame generation interface
├── recipegenerator.cpp     # Frame generation for all 4 editor types
├── profile.h               # Extended with recipe support
└── profile.cpp             # regenerateFromRecipe() with passthrough preservation

src/controllers/
└── maincontroller.cpp      # uploadRecipeProfile() with metadata-only optimization
                            # currentEditorType() with title-first detection

qml/pages/
├── RecipeEditorPage.qml        # D-Flow + A-Flow recipe editor
├── SimpleProfileEditorPage.qml # Pressure + Flow recipe editor (shared base)
├── PressureEditorPage.qml      # Thin wrapper: SimpleProfileEditorPage { profileType: "pressure" }
├── FlowEditorPage.qml          # Thin wrapper: SimpleProfileEditorPage { profileType: "flow" }
└── ProfileEditorPage.qml       # Advanced frame-by-frame editor (fallback for non-recipe profiles)

qml/components/
├── RecipeSection.qml       # Section with title header
├── RecipeRow.qml           # Label + input row
├── ValueInput.qml          # Slider/stepper input control
└── PresetButton.qml        # Preset selector
```

## Profile Modes & Stop Limits

- **FrameBased mode**: Upload to machine, executes autonomously
- **DirectControl mode**: App sends setpoints frame-by-frame
- Formats: JSON (unified with de1app v2), TCL (de1app import)
- Tare happens when frame 0 starts (after machine preheat)
- **The resolve-to-grams boundary (add-yield-ratio-anchor)**: a recipe, bag, or the brew session can define its yield as a **ratio of the dose** (`{yieldValue, yieldMode}` with mode `none|absolute|ratio` — see `src/core/yieldspec.h` and `RECIPES.md`). A **profile never stores a ratio**: `target_weight` stays absolute-only — profiles are shared, exported, and authored by third parties, and the de1app JSON format has no ratio field. The ratio resolves to plain grams in `ProfileManager::targetWeight()` (the ladder's single evaluation point: session anchor → profile `target_weight`, with the dose latched for the duration of a shot) **before** `MachineState::setTargetWeight`, so everything downstream — `WeightProcessor`, SAW/SAV stop logic below, the quality detectors, `shots.yield_override`, the MQTT `target_weight` entity, and DYE/Visualizer export — only ever sees grams.
- **Stop limits**: `target_weight` (SAW) and `target_volume` (SAV) are checked independently — whichever triggers first stops the shot. A value of 0 means disabled. Volume bucketing uses **DE1 substate** splitting (matching de1app): flow during Preinfusion substate → preinfusion volume, flow during Pouring substate → pour volume. Other substates (heating, stabilising) are excluded. SAV uses a raw `pourVolume >= target` comparison with no lag compensation (matching de1app). SAW ignores the first 5 seconds of extraction and only fires after the current frame reaches `number_of_preinfuse_frames` (matching de1app). For **basic profiles** (`settings_2a`/`settings_2b`) with a BLE scale *configured* (not just connected), SAV is skipped (matching de1app's `skip_sav_check` / `expecting_present`). The DE1 firmware also has a `TargetEspressoVol` safety limit (200 ml, matching de1app's `espresso_typical_volume`) sent via `setShotSettings`.

## JSON Format (canonical — one format for Decenza, de1app, Decaid, Visualizer)

**The community goal is that a profile makes the same coffee in every DE1 app**, so Decenza emits exactly one profile format everywhere: on-disk, exported, share-code, and the Visualizer upload.

**`Profile::toJsonObject()` is the single canonical serializer** (`toJson()` wraps it in a document). `VisualizerUploader::buildVisualizerProfileJson()` **delegates** to it and must never re-serialize fields itself — that duplication is exactly what let the two paths silently drift. The history re-upload path deliberately does **not** re-serialize — it uploads the stored shot snapshot verbatim, because `fromJson` fills defaults and would make a historical shot claim values it never ran.

The canonical format is:

- **Numeric values are string-encoded** (`"pressure": "9.00"`, not `9.0`), matching de1app / the tablet / Visualizer / Decaid. The reader stays dual-tolerant: `jsonToDouble()` parses both strings and numbers, so older number-encoded profiles still load.
- **Ecosystem-required keys are always present**: `tank_temperature` (alias of `tank_desired_water_temperature`) and `target_volume_count_start` (alias of `number_of_preinfuse_frames`). **Decaid's `Profile.fromJson` hard-rejects a profile missing either**, so omitting them makes the file unreadable in Decaid — not merely lossy.
- **Standard DE1 v2 metadata**: `type` (derived: `settings_2a`→`pressure`, `settings_2b`→`flow`, else `advanced`), `lang`, `hidden`, `reference_file`, `changes_since_last_espresso`.
- **`steps` is never empty.** Simple `settings_2a`/`settings_2b` profiles carry frames implicitly; `materializedSteps()` generates them before emit so every file Decenza writes is a runnable profile in any app (Decaid rejects an empty `steps` array).
- **`weight` is omitted when zero** — Decaid reads an absent weight as "no weight exit" (`parseOptionalDouble` → null), which is the correct semantic.

`Profile::decaidReadabilityErrors(obj)` validates an object against Decaid's contract (required keys, non-empty steps, enum vocabulary for `pump`/`sensor`/`transition` and exit `type`/`condition`). It's the shared checker used by `tests/tst_builtinprofileformat.cpp`, which gates every shipped built-in.

- **Writer keys**: `notes` (not `profile_notes`), `legacy_profile_type` (not `profile_type`), `number_of_preinfuse_frames` (not `preinfuse_frame_count`), nested `exit`/`limiter`/`weight` (no flat exit fields)
- **Reader fallbacks**: Accepts old flat fields (`exit_if`, `exit_type`, `exit_pressure_over`, `max_flow_or_pressure`, `profile_notes`, `profile_type`, `preinfuse_frame_count`) for backward compat with shot history snapshots
- **Decenza extensions**: `mode`, `recommended_dose`, `has_recommended_dose`, `temperature_presets` — de1app ignores these *spellings*. Note `recommended_dose` is not a concept de1app lacks: it writes the same value as `profile_grinder_dose_weight` (see "de1app's per-profile dose" above), which Decenza now reads on import. The mapping is **one-way** — Decenza has no `.tcl` writer, so a dose set here never travels back. (`recipe` was one and is no longer written; the key stays in `kKnownProfileKeys` so stored blocks are dropped rather than round-tripped.) A key added here must hold state **independent of the frames** — never a cache of something re-derived on read, which is exactly what made `recipe` drift. (The simple-profile params are **not** an extension: de1app writes them unconditionally and so do we; gating them on `settings_2a/2b` is what destroyed those keys on 58+ advanced built-ins.) (`is_recipe_mode` was removed; editor type is now derived at runtime from title + `legacy_profile_type`)
- **No separate reader**: There is no `loadFromDE1AppJson()` — `fromJson()` handles all variants

### Bare (unbraced) values: prose keys take the whole line

A `.tcl` assignment whose value is neither braced nor quoted normally takes only the **first
whitespace-delimited token**, which is what Tcl itself reads — a profile file is parsed with
`array set`, so `profile_title D-Flow / Q` yields `profile_title` → `D-Flow` plus a stray `/` → `Q`
(verified with `tclsh`).

**Three prose keys are exceptions and take the rest of the line**: `profile_title`, `author`,
`profile_notes` (`De1AppTcl::isFreeTextKey`). A trailing `;#` or ` #` Tcl comment is stripped first.

This is a deliberate divergence from de1app, and it is confined to fields that cannot reach a
frame, a machine value or a classification. It exists because **Visualizer's `.tcl` export does not
brace multi-word values**, so every multi-word title from that export truncates — `D-Flow / Q` →
`D-Flow`, `Damian's Q` → `Damian's`. The cost of matching de1app there is one-sided: the profile
loses its slash-prefix category and drops out of its editor group, its filename collides with the
next download in the same family, and **in de1app itself it loses the editor entirely**, because
the dispatch matches `[string range $title 0 7]` against the literal `"D-Flow /"`
(`plugins/D_Flow_Espresso_Profile/plugin.tcl:1143`) and six characters cannot satisfy it.
Decenza's own writer braces properly, so re-saving repairs the file for every app downstream.
Visualizer's JSON rendering of the same profile gets the title right; only the `.tcl` one is
affected, and the in-app importer reaches it because Visualizer returns TCL for some shots even
when `?format=json` is requested.

**Do not add an enum or a code to that list.** `beverage_type` is written bare across the de1app
corpus in eight values (`espresso` ×44, `tea_portafilter` ×11, `calibrate` ×5, `cleaning` ×3,
`pourover` ×3, `filter` ×2, `manual`, `tea`); reading a malformed line whole would produce an
unmatchable string and silently drop a classification that drives tea/pourover handling and travels
on to Visualizer and Decaid. For those, first-token truncation is the *correct* recovery.
`original_profile_title` is excluded for a different reason — Decenza models it nowhere, so listing
it would put an unhandled key into `uncoveredTclKeys()`.

The fixture is real, not synthesised: `tests/data/malformed_tcl/visualizer_unbraced_title.tcl` is a
verbatim Visualizer API response, with provenance in the README beside it.

### de1app's per-profile dose

`profile_grinder_dose_weight` is in de1app's `profile_vars` (`vars.tcl:3305`), so de1app writes it
into every profile it saves — but only the **Streamline** skin populates it
(`skins/Streamline/skin.tcl:2550-2556`), which is why none of the 88 shipped profiles carries one.
It maps to Decenza's `recommended_dose`, with `has_recommended_dose` enabled **only for a value
greater than zero** (Streamline writes the key unconditionally, so `0` means "not set" — the eight
shipped profiles carrying `grinder_dose_weight 0` are the evidence). The flag is set in
`Profile::loadFromTclString`, not in the field table, because `readScalar` hands back a bare double.

That row's `whenAbsent` **must stay unset**. `compareScalars()` walks the same table and is the
built-in drift gate; a `0` fallback would compare 0 against each built-in's `recommended_dose` of
18.0 and fail the gate on eight files.

`profile_grinder_setting` — the per-profile grind setting Streamline writes alongside it — is
listed as deliberately ignored rather than mapped. Decenza models grind settings on equipment and
recipes, and pinning one to a profile would invent an association de1app does not make either.

### Reading a de1app `.tcl`: which spelling wins depends on `settings_profile_type`

Four de1app fields exist in two spellings, and **which one is authoritative is not fixed —
it depends on the profile type.** Getting this wrong does not fail loudly; it silently
produces a profile that brews differently, and it makes any parity tool report drift that
is not there. A comparison written against the wrong spelling once inflated the built-in
drift list from 4 rows to 60 and sent a whole day of analysis down the wrong path.

de1app's converter `legacy_profile_to_v2` (`de1plus/profile.tcl:450`) always reads the
`_advanced` spelling. But it is never called on the raw file — the dispatch at
`profile.tcl:467-472` first runs one of three builders, and **two of them overwrite the
`_advanced` fields from their plain counterparts** before the converter sees them:

| Builder | `settings_profile_type` | Overwrites `_advanced`? |
|---------|------------------------|-------------------------|
| `pressure_to_advanced_list` (`:11`) | `settings_2a` | **Yes** — `profile.tcl:194-201` |
| `flow_to_advanced_list` (`:206`) | `settings_2b` | **Yes** — `profile.tcl:345-352` |
| `settings_to_advanced_list` (`:357`) | `settings_2c`, `settings_2c2` | No |

So the authoritative source is:

| Canonical JSON key | `settings_2a` / `2b` reads | `settings_2c` / `2c2` reads |
|--------------------|---------------------------|----------------------------|
| `target_weight` | `final_desired_shot_weight` | `final_desired_shot_weight_advanced` |
| `target_volume` | `final_desired_shot_volume` | `final_desired_shot_volume_advanced` |
| `maximum_pressure_range_advanced` | `maximum_pressure_range_default` | `maximum_pressure_range_advanced` |
| `maximum_flow_range_advanced` | `maximum_flow_range_default` | `maximum_flow_range_advanced` |

The same dispatch appears on de1app's save path (`profile.tcl:692-695`), so the rule is not
an import-only quirk — it is how de1app defines these fields.

**The rule lives in `src/profile/de1apptclfields.h` (`De1AppTcl`), not in the reader.**
`Profile::loadFromTclString()` and `profile_sync` both resolve keys through
`De1AppTcl::valueFor()` / `tclKeyFor()`, so the importer and the drift gate cannot disagree
about which spelling wins. Restating the rule in either one is how the 338-row drift below
went unnoticed. The same table also carries de1app's **absent-key values**: a profile that
omits `maximum_flow` / `maximum_pressure` runs with *no* limiter in de1app
(`profile.tcl:513-519`, "Disable limits by default", inside `convert_all_legacy_to_v2` — the
same `.tcl` → JSON conversion Decenza performs), where `Profile`'s own defaults are 6 mL/s
and 12 bar. 28 of 89 de1app profiles omit them, and taking the member default switched on a
limiter de1app never applies.

**Absent ≠ zero, and absent ≠ our default.** When adding a field to the table, check what
de1app does when the key is missing before assuming `Profile`'s constructor default matches.

**Corollary: for `settings_2a`/`2b`, the `advanced_shot` stored in the `.tcl` is dead data —
and Decenza now discards it too.**

Not "possibly stale" — provably not the profile's own data. de1app's legacy save writes a
fixed key list out of the **global** `::settings` array (`vars.tcl:3305`), and `advanced_shot`
is on that list, so a simple profile saved after another profile was loaded is written
carrying that other profile's frames. **10 of the 12 stock simple profiles that ship an
`advanced_shot` hold frames that contradict their own `espresso_temperature`**, and five of
them — `Traditional lever machine`, `Trendy 6 bar low pressure shot`, `Two spring lever
machine to 9 bar`, `Preinfuse then 45ml of water`, `Test/temperature calibration` — share one
byte-identical `advanced_shot`: a pour-over frame list (Prewet / Pause / Main water, on
`sensor water`) belonging to none of them and to no profile in the corpus. All 12 are
`read_only 1`, so no user edit is involved; this is authoring-time global-state bleed in
de1app itself. Before this was fixed, Decenza brewed those frames: two unrelated lever
profiles poured the same curve, and neither matched its own settings. `loadFromTclString()` regenerates the frames from the
scalars for every simple profile, matching `pressure_to_advanced_list` /
`flow_to_advanced_list`, which open with `set temp_advanced(advanced_shot) {}`
(`profile.tcl:16`, `:212`). `NumberOfPreinfuseFrames` is likewise **derived** for simple
profiles — de1app resets it to 0 and `incr`s it per generated preinfusion frame
(`:17/56/79`, `:212/251/274`) — and only read from the file for advanced profiles, where
de1app does not recompute it and D-Flow/A-Flow depend on the authored number.

A simple profile's `.tcl` can therefore carry frames that contradict its own
scalars, and de1app will run the scalars. `Steam_only.tcl` is exactly this: it stores
frames at 82/80/72 °C while `espresso_temperature` is `0`, and de1app brews the `0`.
Anything that reads those stored frames — a comparison tool, or a sync that copies them
into a built-in — adopts values de1app discards.

**This is also the default explanation for a difference against Decaid, not a reason
to suspect Decenza.** Decaid's bundled set was harvested from de1app copy-exports with a
converter that read `advanced_shot` verbatim, so it inherited exactly the bleed described
above. A 2026-07-25 audit of the 63 profiles common to both apps found 11 brew-affecting
divergences and **all 11 resolved in Decenza's favour** — five from this stale-`advanced_shot`
mechanism, four from de1app issue #350 shadowing the A-Flow profiles, one profile occupying
another's name, one stale harvest. The tell is physical implausibility: Decaid's `Default`
ran frames at 75 °C and 54 °C against a declared `espresso_temperature` of 90.0.

So when a simple profile differs from Decaid, check *their* provenance before auditing ours.
The same mechanism extends to the stop targets — de1app picks between `final_desired_shot_weight`
and `..._weight_advanced` by profile type (`device_scale.tcl:1322`, `de1_de1.tcl:862`, both
`settings_2c { advanced } default { plain }`), which is the rule encoded in
`src/profile/de1apptclfields.h`; reading the `_advanced` spelling unconditionally made one
profile stop at 60 g instead of 36 g.

Both sides are now reconciled — Decaid fixed their converter, and the comparison returns
64 of 64 equivalent. The audit, and the script that reproduces it, are in
`openspec/changes/sync-builtin-profiles/`. **Two caveats that outlive it:** de1app's own users
still brew 6-frame A-Flow until #350 is resolved, and encoding differences (omitted zero
`weight`, zero-value `limiter`, inactive-axis `""` vs `0.00`) persist by design in the
hundreds of rows — a structural diff of the two corpora is not a useful signal.

## Profile Comparison / Sync Tools

- **Profile comparison/sync**: Use the `profile_sync` C++ tool (built with the main project, no extra flags). `profile_sync <de1app_profiles_dir> <builtin_profiles_dir>` compares TCL sources against built-in JSONs. Pass `de1plus/profiles/` as the first arg — the tool also scans `de1plus/plugins/*/profiles/` and a plugin copy overrides a base copy with the same output filename (canonical source wins, e.g. the 9-frame `A_Flow` plugin profiles beat the stale 6-frame copies in `de1plus/profiles/`). **The base copies really are stale, and this is settled**: de1app added them on 2025-09-03 in commit `80eb34cc`, "Added A-Flow default profiles to distribution, so they can be translated" — a snapshot taken so the string extractor could see them — and has never refreshed them, while the source repo `Jan3kJ/A_Flow` updated its profiles twice since (`9ca39813` 2025-09-25, `7784922b` 2025-11-07). The plugin submodule is the source; `de1plus/profiles/` is a translation artefact. Don't "fix" the override by preferring the base copy. Simple profiles (`settings_2a`/`settings_2b`) ship with `"steps": []` and have their frames regenerated in-memory before comparison so the equality check is like-for-like. Add `--sync` to overwrite stale JSONs and create missing ones (**modifies `resources/profiles/` in-place** — review changes before committing).
- **Format-only rewrite**: `profile_sync <de1app_dir> resources/profiles --rewrite-format` re-saves every built-in through the canonical serializer, leaving **content untouched**. It serializes in memory and audits with `Profile::jsonParityErrors` + `Profile::decaidReadabilityErrors` **before** writing, so a file that would lose data is left untouched rather than clobbered-then-reported; failures go to stderr and the tool exits non-zero. (`functionallyEqual` is explicitly NOT sufficient for this — it compares frames only, and once reported "content-identical" while recipe blocks were being stripped from 8 built-ins.) **This is the sanctioned way to strip recipe blocks from the shipped built-ins**, which mirror de1app sources and must never be hand-edited: the parity audit passes because `recipe` is excused in `deliberatelyDroppedKeys()`, and the result is pure deletions. Note that plain compare mode (`--sync`) cannot see a block at all — it asks "does this built-in still match its de1app source?", and `recipe` was never a de1app key. The guard for that is `tst_builtinprofileformat::noShippedProfileCarriesARecipeBlock`. Use this to adopt a serialization change. It deliberately ignores de1app — reconciling *content* against de1app/Decaid is a separate concern (OpenSpec `sync-builtin-profiles`), and conflating the two would hide content changes inside a format diff.
- **`profile_sync` exit status and refusals**: compare mode exits **1** when the run could not do the job it claims — an unreadable/unparseable/invalid source, a refused write, or a de1app key absent from the field map. Drift itself does **not** gate (reporting it is what compare mode is *for*; `tst_tclimport` is the gate for that). `--sync` **refuses** rather than warns: it will not write a profile whose rewrite would drop a key, will not write over a built-in it cannot read or parse (that audit used to pass vacuously on exactly those files), and skips any `.tcl` the app's own `isValid()` rejects — otherwise the tool that *populates* the corpus would be the one path bypassing the import gate. All failures go to **stderr**.
- **Scalar drift gate**: `profile_sync` compares **profile-level scalars** as well as frames, through `De1AppTcl::compareScalars()`, and `tst_tclimport::builtinScalarParity` fails the build on any drift. Before this existed the tool compared frames only, and 338 scalar mismatches across 82 of 89 built-ins went unseen. The comparison reads the raw `.tcl` rather than a parsed `Profile` **on purpose**: routing both sides through the reader would make the gate structurally unable to see a reader bug, which is the class of bug that caused this. Keys present in the corpus but absent from the field map are reported as `UNCOMPARED` rather than skipped — silently narrowing the comparison is how a 338-row drift was once measured as 4. **Frames are still not compared field-by-field against de1app** beyond `Profile::frameDiffReport()`; full frame parity remains open.
- **de1app oracle (`tools/de1app_oracle/`)**: the only check that answers *"does this profile make the same coffee in both apps?"*. It `source`s de1app's real `profile.tcl` and runs its own frame builders, then diffs the result against `resources/profiles/`. **The `.tcl` is de1app's input, not its output** — for a simple profile de1app discards the stored `advanced_shot` and rebuilds from the scalars, so comparing against the file cannot tell you what it brews. Run `python3 tools/de1app_oracle/compare_builtins.py <de1app-checkout>`; exit status 1 means a real portability break. Requires `tclsh`, and is deliberately NOT wired into CMake/ctest so a machine without Tcl can still build and test. On its first run it found three divergences the entire C++ suite had missed — two profiles Decenza brewed 4–6 °C colder than de1app, and two where we omitted a whole preinfusion frame. See `tools/de1app_oracle/README.md`.
- **Profile import test**: Run `ctest -R tst_tclimport` (requires `-DBUILD_TESTS=ON`). The `compareWithBuiltin` test loads all TCL files from `tests/data/de1app_profiles/` through the C++ parser and verifies they match their built-in JSON counterparts field-by-field.

## Auto-Load

Decenza can pin a single profile to be reloaded automatically on three triggers:

1. **App startup** — fires once after `ProfileManager` is initialised.
2. **DE1 wake from sleep** — `DE1Device.state` transitions `Sleep → Idle`.
3. **N-minute idle on the Idle page** — controlled by `Settings.app.autoLoadRevertMinutes` (`0` disables this trigger only; startup + wake-from-sleep still fire). Touch input, phase changes, and navigating off the Idle page all reset the countdown.

State lives on two `SettingsApp` properties:
- `autoLoadProfileFilename` (default `""`) — empty = feature entirely off.
- `autoLoadRevertMinutes` (default `5`, clamped 0..60) — `0` disables the idle trigger only. Preserved across enable/disable cycles.

**Eligibility**: only profiles currently in the Selected list (`selectedBuiltInProfiles` ∪ user profiles not in `hiddenProfiles`) can be the auto-load. If the pinned profile is deleted, hidden, or de-selected, the setting is cleared eagerly (in `SettingsApp::addHiddenProfile` / `removeSelectedBuiltInProfile` / `ProfileManager::deleteProfile`) and at trigger time (`ProfileManager::loadAutoLoadProfileIfNeeded()`), with `autoLoadStaleCleared` emitted so the UI can toast.

**UI** lives entirely on `ProfileSelectorPage`:
- A pin icon on the auto-load row (next to the title, beside the AI-knowledge sparkle).
- A contextual overflow MenuItem labelled `Set Auto-Load` / `Disable Auto-Load`, visible only when the row is in the Selected list.
- A compact status strip above the view filter showing the pinned title, the revert-minutes `ValueInput` (0..60, where `0` renders as "off"), and a clear button. The strip is the only place `autoLoadRevertMinutes` can be tuned.

**MCP**: three tools — `profiles_get_auto_load` (read), `profiles_set_auto_load` (settings), `profiles_clear_auto_load` (settings). See `docs/CLAUDE_MD/MCP_SERVER.md`.

## References

- [D-Flow GitHub Repository](https://github.com/Damian-AU/D_Flow_Espresso_Profile)
- [de1app Profile System](https://github.com/decentespresso/de1app/blob/main/de1plus/profile.tcl)
- de1app D-Flow source: `de1plus/profile_editors/D_Flow/code.tcl`
- de1app A-Flow source: `de1plus/profile_editors/A_Flow/code.tcl`
