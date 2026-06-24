## Why

The brew temperature override is a single value that, at upload, shifts **every** frame's temperature by a uniform delta (preserving the curve shape). But the UI presents it as one number, so a multi-temperature profile (D-Flow fill/pour, a declining-temperature profile, a Visualizer per-frame import) is misrepresented: the shot plan shows e.g. `Default (90 → 91°C)`, which reads as a single temperature moving 90→91 and hides that **all** steps moved +1°. The Brew Settings "Update Profile" button is likewise unclear about which temperatures it rewrites.

There is also a latent inconsistency: the live-brew path computes the delta anchored on `espressoTemperature` ([profilemanager.cpp:1594](../../../src/controllers/profilemanager.cpp)) while the "Update Profile" button anchors on the first frame ([BrewDialog.qml:406](../../../qml/components/BrewDialog.qml)). On a profile where those differ, the same box value produces different results, so any preview we add could lie unless the anchors are unified.

## What Changes

- **Adaptive temperature display** keyed on the count of *distinct* step temperatures (N):
  - N=1: single value; with override, the existing arrow `90 → 91°C` (unchanged).
  - N=2: list both, mid-dot — `88·93°C`; with override, post-shift values + delta tag — `89·94°C +1°`.
  - N≥3: ellipsis between the **first-step and last-step** temperature (trajectory, not sorted) — `84…52°C`; with override `85…53°C +1°`.
- **Multiplicity shown even with no override active** — a multi-temp profile renders as a list/ellipsis instead of just the scalar, so the user can see it carries more than one temperature.
- **Shot plan widget** ([ShotPlanText.qml](../../../qml/components/ShotPlanText.qml)) uses the adaptive formatter for its temperature segment.
- **Brew Settings dialog** ([BrewDialog.qml](../../../qml/components/BrewDialog.qml)): the stepper still holds the single anchor value the user turns, but the subtext changes from `Profile: X°C` to show the profile's temperature structure (single / list / ellipsis) and, when an override is active, `all steps +Δ → <result>`. The "Update Profile" action gains a preview of the shifted set it will write.
- **Anchor unification (bug fix):** the live-brew path and the "Update Profile" path compute the delta from the **same** anchor (the profile scalar `espressoTemperature`, now frame-grounded after [#1391](https://github.com/Kulitorum/Decenza/pull/1391)), so the preview always matches what is brewed and saved.
- **Shared formatter** consumed by both surfaces so they cannot drift.

Out of scope (later layers): per-temperature editing in Brew Settings, and a dial-in coaching/advice surface.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `brew-overrides`: The **Shot Plan Display** and **Brew Dialog** requirements gain adaptive multi-temperature notation (single / list / ellipsis) with a delta tag, replacing the single-value-only and arrow-only display. The **Persistent Brew Overrides** requirement is clarified so the delta anchor is a single, explicitly-named reference (`espressoTemperature`) used identically by the live-brew and "Update Profile" paths, removing the current first-frame-vs-scalar ambiguity.

## Impact

- **QML:** `qml/components/ShotPlanText.qml`, `qml/components/BrewDialog.qml`, a new shared formatter (component or JS helper, added to the `qt_add_qml_module` file list in `CMakeLists.txt`), all user-visible text via `TranslationManager`/`Tr`.
- **C++:** `src/controllers/profilemanager.cpp` (unify the "Update Profile" delta anchor with the live-brew path; expose distinct-temp data — list or first/last step temps + distinct count — as `Q_PROPERTY` for QML binding).
- **Tests:** unit tests for the formatter (N=1/2/3+, override/no-override, negative delta, ramp-up-then-down edge case) and for anchor parity (live-brew delta == Update-Profile delta for the same input).
- No BLE protocol, DB schema, or settings-storage changes. Brew behavior on the machine is unchanged — only how the existing uniform shift is *displayed* and which anchor the two paths share.
