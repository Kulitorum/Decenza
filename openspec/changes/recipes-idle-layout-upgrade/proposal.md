## Why

Drink recipes are now the primary way to start a shot, but the idle page still leads with the profile-centric layout designed before recipes existed: the Profiles (ex-espresso) button holds the prime center spot, Recipes sits beside it as an add-on, and the bottom bar carries an Auto-Favorites button that recipes largely supersede. Fresh installs and "Reset to default" should get a recipes-first home screen, and existing users should be offered — not forced into — the same modernization without losing their customizations.

## What Changes

- **New default idle layout** (fresh installs + "Reset to default" in the layout editor and web editor): center row becomes **Recipes · Beans · Steam · Hot Water**; bottom bar becomes **Sleep** (left) and **Flush · History · Equipment · Profiles · Settings** (right). The Profiles button moves out of the center into the bottom bar, Flush moves from center to bottom bar, Beans moves up to the center row, and Auto-Favorites is dropped from the default. Status bar, shot-plan row, and all other zones are unchanged.
- **One-time upgrade offer at first launch after update**: existing users (those with a stored layout) see a dialog once, offering to apply the recipes-first arrangement to their current layout as a targeted transform — replace the center Profiles/espresso button with Recipes (in place), move the Profiles button into the bottom bar beside Equipment, and remove the Auto-Favorites button — **preserving every other customization** (added widgets, zone options, ordering, readout configs). Declining keeps everything as-is; either way the dialog never reappears.
- **Starter recipe from the last shot**: accepting the upgrade also creates a recipe from the user's most recent saved shot (reusing the existing promote-from-shot path) and activates it — so the Recipes button is immediately populated and the machine state matches what the user last brewed. The dialog carries an Espresso / Milk drink choice, pre-selected by a heuristic on the shot's steam snapshot, that sets the recipe's `hasMilk` intent and default name.
- **Pristine layouts upgrade fully**: users still on the untouched (migrated) old default get the complete new default on accept — identical to fresh installs — while customized layouts get the surgical three-edit transform.
- Fresh installs (no stored layout) get the new default directly and are never shown the upgrade dialog.

## Capabilities

### New Capabilities

- `idle-default-layout`: the default idle-page layout composition — which widgets sit in which zones — used for fresh installs and by the whole-layout "Reset to default" actions (in-app layout settings and web editor).
- `layout-recipes-upgrade-offer`: the one-time first-launch upgrade dialog for existing users — eligibility, the accept-path layout transform (replace center espresso with recipes, relocate espresso to the bottom bar, remove autofavorites, preserve everything else), the decline path, and the starter-recipe creation/classification/activation from the last shot.

### Modified Capabilities

<!-- none — recipe creation reuses the existing promotion path (recipe-model / recipe-activation requirements unchanged); layout-zone-configuration's per-zone reset is untouched -->

## Impact

- `src/core/settings_network.cpp` — `defaultLayoutJson()` (new default composition), new one-shot layout-transform method for the accept path, upgrade-offered flag; possibly interaction with the existing `add-recipes` injection migration (which inserts Recipes left of the espresso button).
- `qml/main.qml` — first-launch upgrade dialog (follows the existing `firstRunDialog` one-time pattern), gating flag.
- Recipe promotion path — reuse of the `recipe_create_from_shot` mechanics (`src/mcp/mcptools_recipes.cpp` logic extracted or mirrored for in-app use), `MainController::activateRecipe`, shot record `steam_json` snapshot (`hasMilk`, `milkWeightG`) for espresso-vs-milk classification.
- Web layout editor "Reset to Default" (`src/network/shotserver_layout.cpp`) picks up the new default automatically via `resetLayoutToDefault()`.
- User manual (wiki) idle-screen sections will need a post-release update (already being staged for the Profiles rename).
