## Why

The Custom layout widget can already send the user to Shot History, but only to the
unfiltered list. The question a user actually has standing at the machine is narrower —
"how have my last shots with *this* recipe / *this* bean / *this* bag / *this* profile
gone?" — and answering it today means opening History and then typing or tapping a filter
in. Shot History already supports most of these filters through `initialFilter` (the
Auto-Favorites "Show" button and the recipe tap-through use it), so mostly what is missing
is a way to reach them from a home-screen button.

Bag is the exception: `shots.bag_id` is recorded on every shot but nothing can filter on
it. So "how did *this bag* pour" — the question a user has when a bag is behaving
differently from the last one of the same coffee — is currently unanswerable, from any
surface.

## What Changes

- Four new Custom-widget navigate actions, selectable for tap, long-press and
  double-click like every existing action:
  - **Go to History (this recipe)** — Shot History filtered to the active recipe.
  - **Go to History (this bean)** — filtered to the current bean (brand + type, so the
    same coffee across bags).
  - **Go to History (this bag)** — filtered to the active bag exactly.
  - **Go to History (this profile)** — filtered to the currently loaded profile's name.
- Shot History gains bag as a filter dimension, in the two shapes recipe already has:
  an exact `bagId` filter (what the widget action uses) and a `bag:` search keyword
  matching a bag's coffee name, roaster or roast date.
- Each action opens Shot History with the filter already applied, showing the existing
  filter banner and its Clear control — no new History UI.
- When nothing is active to filter on (no active recipe, no active bag, no loaded
  profile), the action opens Shot History unfiltered rather than doing nothing, so the
  button is never dead.
- **Bug fix**: `goToShotHistory()` applies its filter in place when Shot History is
  already the current page. Today `pushUnlessCurrent()` drops the filter silently, so a
  status-bar History widget tapped while History is showing does nothing at all.
- The action catalog, currently hand-copied between `CustomEditorPopup.qml` and the web
  editor's `ACTIONS` array — and already drifted, the web copy missing six navigate
  actions the in-app picker offers — is centralized into one C++ table injected into both,
  in the same shape as the existing widget catalog and readout capability schema.

## Capabilities

### New Capabilities

- `custom-widget-history-actions`: The Custom layout widget's four context-filtered
  Shot History actions — what each one filters on, where the filter values come from,
  the empty-context behaviour, and their presence in both widget editors.
- `history-bag-filter`: Filtering Shot History by coffee bag — the exact `bagId` filter,
  the `bag:` search keyword, how a bag is labelled in the filter banner, and how bag
  filtering differs from bean filtering.
- `layout-action-catalog`: The single C++ declaration of the Custom widget's action
  catalog (id, label key + English fallback, page contexts), consumed by the in-app
  Custom widget editor and the web layout editor, replacing the hand-synchronized copies.

### Modified Capabilities

<!-- None. history-recipe-search and history-recipe-identity are reused as-is; the bag
     filter is a new dimension alongside them, not a change to how recipe filtering works.
     The already-on-History fix restores intended behaviour of an existing path without
     changing a stated requirement. -->

## Impact

- `qml/components/layout/items/CustomItem.qml` — four new `navigate:` targets in
  `executeActionString()`, each assembling an `initialFilter` and emitting
  `AppShell.shotHistoryRequested()`.
- `qml/main.qml` — `goToShotHistory()` applies the filter in place when already on
  Shot History.
- `qml/pages/ShotHistoryPage.qml` — `bagId` in the `initialFilter` passthrough, `bag:`
  keyword parsing beside `recipe:`, bag label in the filter banner.
- `src/history/shothistory_types.h`, `shothistorystorage_queries.cpp` — `bagId` /
  `bagLabel` on `ShotFilter` plus their WHERE clauses. `shots.bag_id` already exists and
  is populated, so **no schema migration**.
- `src/core/settings_network.{h,cpp}` — new action catalog table + JSON accessor, beside
  `widgetCatalogTable()` / `readoutOptionSchema()`.
- `qml/components/layout/CustomEditorPopup.qml`, `src/network/shotserver_layout.cpp` —
  both action lists replaced by the injected catalog.
- Reads only existing state: `Settings.dye.activeRecipeId`, `MainController.activeRecipe`,
  `Settings.dye.activeBagId`, `dyeBeanBrand` / `dyeBeanType`,
  `ProfileManager.currentProfileName`. No new settings, no BLE impact.
- Wiki manual (`Manual`) — the Custom widget's action list gains four entries; the Shot
  History search-keyword list gains `bag:`.
