## ADDED Requirements

### Requirement: One-time upgrade offer for existing users

At the first launch after updating to a version containing this feature, existing users SHALL be shown a one-time dialog offering to modernize their idle layout to the recipes-first arrangement. "Existing user" means a stored layout configuration exists (or first-run onboarding has already completed). The offer SHALL be recorded as shown via a persisted flag the moment the user answers, and SHALL never be shown again regardless of the answer. Fresh installs SHALL never see the dialog (they already get the new default). The dialog SHALL follow the app's accessibility rules (focusable, named, dismissible).

#### Scenario: Existing user sees the offer once

- **WHEN** a user with a stored layout launches the app for the first time after the update
- **THEN** the upgrade dialog appears, and after the user answers (either way) it is never shown on subsequent launches

#### Scenario: Fresh install never sees the offer

- **WHEN** the app completes first-run onboarding on a new install
- **THEN** the upgrade-offered flag is set and the upgrade dialog never appears

#### Scenario: Declining changes nothing

- **WHEN** the user declines the offer
- **THEN** the layout, recipes, and active recipe are all unchanged, and only the offered flag is persisted

### Requirement: Accept-path layout transform preserves customizations

Accepting the offer SHALL apply a targeted transform to the user's **current** layout — not a reset — **except** when the current layout is equivalent to the (migrated) old default, in which case the full new recipes-first default SHALL be applied instead, so uncustomized users land exactly on the same layout as fresh installs. Otherwise the transform SHALL: (1) remove the Profiles/espresso item from any center zone, inserting a Recipes item at its exact position if the layout does not already contain one; (2) place the Profiles/espresso item in the bottom bar immediately after the Equipment item — falling back to immediately before the Settings item in `bottomRight`, then to appending to `bottomRight`, when Equipment or Settings are absent; (3) remove every `autofavorites` item. All other items, their order, per-instance options, and zone options SHALL be preserved. The transform SHALL be idempotent and persisted once.

#### Scenario: Pristine old default gets the full new default

- **WHEN** an accepting user's layout is equivalent to the migrated old default (no customizations)
- **THEN** the stored layout is replaced with the full recipes-first default — center Recipes, Beans, Steam, Hot Water; bottom bar Sleep | Flush, History, Equipment, Profiles, Settings

#### Scenario: Customized layout gets the surgical transform

- **WHEN** a user whose center row is Recipes, Profiles, Steam, Hot Water, Flush plus their own added widgets accepts the offer
- **THEN** the center row becomes Recipes, Steam, Hot Water, Flush; the Profiles item appears after Equipment in the bottom bar; the Auto-Favorites item is removed; and every other widget (including customizations) is untouched

#### Scenario: Layout without a recipes item

- **WHEN** an accepting user's layout contains a center Profiles button but no Recipes item anywhere
- **THEN** a Recipes item is inserted at the Profiles button's former position

#### Scenario: Profiles button already in a bar zone

- **WHEN** an accepting user's layout has the Profiles/espresso item only in a bar zone
- **THEN** the item is left where the user placed it (no duplicate is created) and the remaining transform steps still apply

#### Scenario: Customizations survive

- **WHEN** an accepting user has added widgets, reordered zones, or configured readout options
- **THEN** after the transform those customizations are exactly as before; only the espresso/recipes/autofavorites placements described above change

### Requirement: Starter recipe from the last shot

When the user accepts the offer and has **no existing recipes**, the app SHALL create a recipe from the most recent saved shot using the established promotion semantics (profile, bean link, equipment, dose, yield, temperature, steam block from the shot's steam snapshot with current-settings fallback, hot-water snapshot verbatim, grind inherited from the bag when the shot has a bean and pinned otherwise), and SHALL activate it through the single recipe-activation path. The drink type SHALL be chosen **by the user in the upgrade dialog** via an Espresso / Milk drink choice, **pre-selected** by a heuristic on the shot's steam snapshot (milk when `hasMilk` is true or `milkWeightG` > 0, espresso otherwise). The chosen type SHALL set the recipe's `hasMilk` intent and its default (translated) name. The drink-type choice SHALL only be shown when a starter recipe will actually be created. When no saved shots exist, or the user already has recipes, recipe creation SHALL be skipped silently and the layout transform SHALL still apply.

#### Scenario: Heuristic pre-selects the drink type

- **WHEN** the upgrade dialog opens for a user whose last shot's steam snapshot has `hasMilk` true or `milkWeightG` > 0
- **THEN** the drink-type choice defaults to Milk drink (and to Espresso otherwise), and the user can override it before accepting

#### Scenario: User's choice wins

- **WHEN** an accepting user overrides the pre-selected drink type
- **THEN** the created recipe's `hasMilk` and default name follow the user's choice, and the recipe becomes the active recipe

#### Scenario: No shot history

- **WHEN** an accepting user has no saved shots
- **THEN** no recipe is created and the layout transform completes normally

#### Scenario: User already has recipes

- **WHEN** an accepting user already has one or more recipes
- **THEN** no starter recipe is created, the drink-type choice is not shown, and the active recipe is not changed
