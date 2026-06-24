## Context

The brew temperature override already does the right thing *mechanically*: at upload it adds a uniform delta to every frame's temperature, preserving the curve's shape (`steps[i].temperature += delta`). What's wrong is the *presentation* — both the shot-plan widget ([ShotPlanText.qml](../../../qml/components/ShotPlanText.qml)) and the Brew Settings dialog ([BrewDialog.qml](../../../qml/components/BrewDialog.qml)) collapse a multi-temperature profile into a single number, so a user can't tell that a profile carries several temperatures or that the override shifted all of them.

Two delta-applying paths exist and currently disagree on their anchor:
- Live brew: `delta = override - espressoTemperature` ([profilemanager.cpp:1594](../../../src/controllers/profilemanager.cpp)).
- "Update Profile" save: `delta = temperatureValue - steps[0].temperature` ([BrewDialog.qml:406](../../../qml/components/BrewDialog.qml)).

After [#1391](https://github.com/Kulitorum/Decenza/pull/1391) the scalar is frame-grounded, so they usually agree — but not on profiles where `espressoTemperature` intentionally differs from frame 0. Any preview we add must be backed by a single anchor or it will occasionally lie.

This is "layer 1" of a larger dial-in thread: display honesty + the anchor bug. Per-temperature editing and dial-in coaching are explicitly later layers.

## Goals / Non-Goals

**Goals:**
- The shot plan and Brew Settings honestly show that the override shifts the whole temperature curve.
- Adapt the rendering to the number of distinct frame temperatures: single / two-value list / three-or-more ellipsis.
- One shared formatter so the two surfaces can't drift.
- Unify the two delta anchors so previews match brewed/saved reality.

**Non-Goals:**
- Per-temperature (per-frame or per-fill/pour) editing in Brew Settings — that's the Recipe Editor's job.
- Any dial-in advice/coaching surface.
- Changes to BLE protocol, DB schema, or override storage semantics. Brew behavior on the machine is unchanged.

## Decisions

### Adaptive notation keyed on distinct-temp count (N)
- **N=1:** single value; with override the existing arrow `90 → 91°C`.
- **N=2:** mid-dot list `88·93°C`; with override post-shift values + delta tag `89·94°C +1°`.
- **N≥3:** ellipsis between **first-step and last-step** temperature `84…52°C`; with override `85…53°C +1°`.

Rationale: enumerating is honest and precise for few values; an ellipsis caps width for many. Using **first→last in step order** (not a sorted min–max range) preserves the thermal *trajectory*, which is the whole point of declining-temperature profiles. *Alternative considered:* sorted min–max range for N≥3 — rejected because it discards direction. *Alternative considered:* range for all N≥2 (one idiom) — rejected because the common 2-temp fill/pour case is clearer enumerated.

The override case shows **post-shift** values plus a signed delta tag (`+1°`/`−1°`). The presence of multiple values next to a single delta unambiguously communicates "all shifted by Δ" without spelling out "all," keeping the shot-plan segment compact (it elides).

### Single shared formatter
A pure function — given the profile's frame temperatures and the active override value — returns the display string (and/or its parts) for both surfaces. Implemented as a small JS helper (or a non-visual QML singleton) so `ShotPlanText.qml` and `BrewDialog.qml` call the identical logic. *Alternative considered:* format in C++ on `ProfileManager` — rejected so the string/i18n stays in QML where `TranslationManager`/`Tr` live; C++ only supplies the raw numeric inputs.

### Distinct-temp data exposed from ProfileManager
Add `Q_PROPERTY` accessors so QML binds reactively without rebuilding a `QVariantMap` (as `getCurrentProfile()` does) on every change. Minimal surface: distinct-temperature count, plus first-step and last-step temperatures (enough to drive N=1/2/≥3). If the N=2 list needs the actual second value, expose the distinct-temp list instead. *Alternative considered:* compute distinct temps in QML from `getCurrentProfile().steps` — workable but heavier per binding and duplicates logic; prefer a typed property.

### Anchor unification
Both the live-brew path and the "Update Profile" path compute the delta from `espressoTemperature`. The "Update Profile" handler in `BrewDialog.qml` switches from `steps[0].temperature` to the same anchor (ideally by calling a single `ProfileManager` method so the math lives in one place). Rationale: `espressoTemperature` is the profile's canonical scalar (frame-grounded post-#1391) and is already the live-brew anchor; making save match brew is the smaller, safer move and makes the new preview truthful.

## Risks / Trade-offs

- **Ramp-up-then-down profiles** (e.g. 88→92→90) render as `88…90°C` for N≥3, hiding the 92 peak. → Accepted simplification; the delta tag still conveys the shift and the full curve lives in the Recipe Editor. Documented in the spec.
- **String width on the shot plan** for N=2 with override (`89·94°C +1°`) plus bean/dose. → The segment already elides; the formatter keeps the delta tag compact (`+1°`, not "all +1°").
- **Anchor change alters "Update Profile" results** on profiles where `espressoTemperature ≠ steps[0]`. → This is the intended bug fix; covered by an anchor-parity unit test, and brew behavior is unchanged because live-brew already used this anchor.
- **i18n of composed strings** (lists, ellipsis, delta tag). → Compose from translated fragments; keep separators (`·`, `…`) as non-translated literals, numbers locale-formatted as elsewhere.

## Migration Plan

Pure display + a delta-anchor change; no data migration. Ships in one PR. Rollback is reverting the PR — no persisted state changes, so no forward/backward data concerns.

## Open Questions

- Exact ProfileManager property shape: first/last+count vs. a distinct-temp list. Resolve during implementation based on whether the N=2 list needs both raw values from C++ or can derive them. (Leaning: expose the distinct-temp list; it covers every case.)
- Whether "Update Profile" should be relabeled (e.g. "Save as default") as part of this change or left as-is with only the added preview. (Leaning: keep the label, add the preview; relabel is a separate copy decision.)
