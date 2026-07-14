## MODIFIED Requirements

### Requirement: The live Shot Plan treats an active recipe's yield/temp as the baseline

The idle-screen **Shot Plan** widget SHALL, when a recipe is active, treat that recipe's own `yieldG` / offset-derived temperature as the baseline rather than the profile's default — mirroring Brew Settings — so a recipe's designed values do not render as overrides. Specifically: the yield SHALL render as a plain effective target (e.g. "40.0g") with no "profile-default → target" arrow, and neither the yield nor the temperature segment SHALL be tinted with the override-highlight color, when the live values equal the active recipe's own. The override arrow (yield) and the amber highlight (yield and temperature) SHALL return only for a per-brew value dialed **beyond** the recipe's saved value.

The temperature STRING SHALL show the recipe's OWN temperatures — the profile's frame temperatures shifted uniformly by the recipe's stored `tempOffsetC`, e.g. a recipe with offset −3 on an `84 · 94°C` profile reads **"81 · 91°C"** — with NO profile-relative offset tag. A signed delta tag SHALL appear only for a per-brew value dialed beyond the recipe (measured from the recipe temp). *A baseline is a baseline*: the Shot Plan temperature and the Brew Settings Temp Delta (which reads `0°` at the recipe) SHALL agree. This is provided by a `baselineShiftC` parameter on the shared temperature formatter; with no recipe active the shift is 0 and the profile temps + offset tag render as before.

This recipe-as-baseline rendering applies to the live widget and its layout-editor preview using the currently-loaded profile's frames (via the shared temperature formatter's live state), and to recipe cards (the management list and the wizard's summary preview) using the same baseline decomposition resolved against **their own** recipe's profile instead — never the currently loaded one — since a card must render correctly even while a different profile is loaded (see `recipe-quick-switch`). The Shot Review / Shot Detail plan lines keep their own explicit shot-relative highlighting, showing what was overridden **at shot time** relative to the shot's own profile.

#### Scenario: Active recipe's yield shows as a plain target, un-highlighted

- **WHEN** a recipe with `yieldG` = 40 is active on a profile whose target weight is 36, and no per-brew tweak has been dialed
- **THEN** the Shot Plan yield reads "40.0g" (no "36.0 → 40.0g" arrow) and is not tinted

#### Scenario: A per-brew tweak beyond the recipe re-arms the arrow and highlight

- **WHEN** that recipe is active and the user dials the next-brew stop-at to 44
- **THEN** the Shot Plan yield reads "40.0 → 44.0g" and is tinted with the override-highlight color

#### Scenario: Recipe cards resolve the same baseline against their own profile

- **WHEN** a recipe with `tempOffsetC` = −3 on a profile whose frames are 84 · 94°C is listed on its management-page card while the machine currently has a *different* profile loaded
- **THEN** the card's temperature reads "81 · 91°C" — resolved from that recipe's own profile, not the loaded one — matching what the live Shot Plan would show if that recipe were active
