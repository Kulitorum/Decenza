## MODIFIED Requirements

### Requirement: Dose and grind keep their existing dial write-through

Dose and grind/RPM are dial-in values, not overrides: they have no per-brew "override vs. baseline" split and no "Update" button. This change SHALL leave their existing write-through untouched — dose continues to write through to the active bag and stamp the active recipe's `doseG`; grind/RPM continue to write through to the active bag and stamp the recipe's `grindPinned`/`rpmPinned` (per `fix-recipe-grind-integrity`, with the non-tea guard). This change SHALL NOT add or remove any write-back for dose or grind.

Those two write-throughs are already the top two rungs of the `dose-source-precedence` ladder, so an edit lands on whichever source the ladder names — with one deliberate exception: **the profile is NOT a write target here.** Writing it means marking the profile modified, and this dialog commits on every OK, so a dose nudge would dirty the loaded profile. A dose dialed with neither a recipe nor a bag active therefore stays in `Settings.dye` (which persists), and the profile's recommendation is edited where the rest of the profile is.

This split is the measurement/intent line of `yield-anchor`: dose, grind, and RPM are things the user physically did, so they are remembered automatically; the yield anchor is design intent, so it is button-protected. A dose capture therefore always updates the dose and never changes the yield mode.

The re-seed performed on a recipe switch SHALL write only the dialog's local QML values (`root.*`), never `Settings`, so that re-seeding never triggers a dose/grind stamp into the newly activated recipe.

Cup tare is NOT recipe-stored (it lives in DYE only). **Ratio is now recipe- and bag-stored** as the mode of the yield spec (`yield-anchor`) — superseding the previous rule that ratio lived only in `Settings.brew`; `Settings.brew.lastUsedRatio` survives only as preset memory. Steam and hot-water blocks are recipe-stored but are not edited by this dialog.

#### Scenario: Editing dose in recipe mode still writes through
- **WHEN** a recipe is active and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye`, writes through to the active bag, and stamps the active recipe's `doseG` — exactly as before this change
- **AND** the profile's recommended dose is NOT touched, because the recipe owns the dose

#### Scenario: Editing dose with only a bag active does not reach the profile
- **WHEN** no recipe is active, a bag is active, and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye` and written through to the active bag
- **AND** the profile's recommended dose is unchanged

#### Scenario: Editing dose with no recipe and no bag does not dirty the profile
- **WHEN** neither a recipe nor a bag is active and the user changes the dose and taps OK
- **THEN** the change is applied to `Settings.dye`
- **AND** the loaded profile is NOT marked modified and its recommended dose is unchanged

#### Scenario: Grind edit still mirrors to the bag and stamps the recipe
- **WHEN** a non-tea recipe is active and the user changes the grind setting (or RPM) and taps OK
- **THEN** the active recipe's `grindPinned` (`rpmPinned`) is stamped and the setter's unconditional bag write-through mirrors the value onto the linked bag — unchanged from `fix-recipe-grind-integrity`

#### Scenario: A dose capture while the dialog is open does not flip the anchor
- **WHEN** Brew Settings is open with an `{36.0, absolute}` anchor and a scale dose capture lands
- **THEN** the dialog's dose updates to the captured value
- **AND** the anchor stays `{36.0, absolute}`, the Stop-at value stays 36 g, and the persist button does not move

#### Scenario: Re-seed after a switch does not stamp the new recipe
- **WHEN** the user switches recipes in the dialog and the dial-in fields are re-seeded
- **THEN** only local `root.*` values are written
- **AND** no `Settings` mutation occurs from the re-seed, so the newly activated recipe is not stamped with re-seeded dose/grind values
