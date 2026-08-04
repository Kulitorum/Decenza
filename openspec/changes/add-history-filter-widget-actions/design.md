## Context

A Custom layout widget stores an action string per gesture (`action`, `longPressAction`,
`doubleclickAction`) and `CustomItem.executeActionString()` dispatches it as
`category:target`. The `navigate` category maps a target to a named `AppShell` signal;
`navigate:history` today calls `AppShell.shotHistoryRequested({})`.

That signal already carries a filter payload. `main.qml`'s handler forwards it to
`goToShotHistory(filter)`, which pushes `ShotHistoryPage` with those properties, and the
page's `initialFilter` property drives both the query (`buildFilter()` merges
`profileName`, `beanBrand`, `beanType`, `grinderBrand`, `grinderModel`, `grinderSetting`,
plus numeric fields and `recipeId`) and the filter banner with its Clear control. Two
callers already use it: the Auto-Favorites "Show" button and the in-page recipe
tap-through (`filterByRecipe()`).

Three things are missing rather than one:

1. **No way to reach the filters from a home-screen button.** That is the feature asked
   for.
2. **Bag is not a filter dimension at all.** `shots.bag_id` is written on every shot
   (`shothistorystorage.cpp:2426`, read back at `:3016`, exposed as `bagId`) and
   `CoffeeBagStorage` already queries `SELECT COUNT(*) FROM shots WHERE bag_id = :id`
   before allowing a bag delete — but `ShotFilter` has no bag field, so nothing can ask
   for one bag's shots. **This needs no migration**, contrary to an earlier reading of
   this design: the column exists and is populated.
3. **A latent bug on the History navigation path.** `goToShotHistory()` calls
   `pushUnlessCurrent()`, which returns without applying props when Shot History is
   already the current page. Custom widgets live in the persistent status bar, so tapping
   one while looking at History navigates nowhere AND applies no filter — the tap is a
   silent no-op today, before this change adds any filtered variants.

The action catalog is separately awkward. It is written by hand in two places —
`CustomEditorPopup.getFilteredActions()` (QML, translated) and the web editor's `ACTIONS`
array (`shotserver_layout.cpp`, English) — with no shared declaration, and it has already
drifted: the web copy is missing `navigate:beaninfo`, `navigate:espresso`,
`navigate:community`, `navigate:flowCalibration`, `navigate:profileImport` and
`navigate:shotReview`, plus several commands. Two neighbouring catalogs
(`widgetCatalogTable()`, `readoutOptionSchema()`) already solved this shape: declared once
in `settings_network.cpp`, consumed by QML directly and injected into the web page as JSON.

## Goals / Non-Goals

**Goals:**

- Four new `navigate:` targets opening Shot History pre-filtered to the active recipe, the
  current bean, the active bag, or the loaded profile.
- Bag as a first-class Shot History filter, in both shapes recipe already has: an exact id
  and a typed `bag:` keyword.
- Filters resolved at activation time from live state, so a widget never holds a stale
  identity.
- Both editors offer the actions, from one declaration rather than two drifted lists.
- The already-on-History navigation bug fixed.
- No schema change, no new settings, no new Shot History UI beyond the existing banner.

**Non-Goals:**

- No filter combinations (recipe AND bag) and no user-configurable filter per widget
  instance; four fixed actions is the whole surface.
- No new Custom-widget option keys — the actions are ordinary action strings, so the
  Custom widget's existing bespoke editor covers them with no capability-schema change.
- No gestures added to the dedicated History widget (decided: Custom only).
- Not centralizing the community-library `commActionFilter` `<select>` in
  `shotserver_layout.cpp` — a deliberately short search-filter subset, not an editor
  picker.

## Decisions

### Four distinct action ids, not one parameterized action

`navigate:historyRecipe`, `navigate:historyBean`, `navigate:historyBag`,
`navigate:historyProfile`.

The alternative — one `navigate:history` target with a parameter
(`navigate:history:bean`) — reuses the existing dispatch arm but makes the action picker
either lie (one entry that behaves four ways) or need a second selection step. Four ids
stay flat: each is one picker row, each round-trips through the web editor as a plain
string, and the existing `parts.slice(1).join(":")` target parsing is untouched. Legacy
`navigate:history` keeps its current meaning, so no stored layout changes behaviour.

### Bean and bag are both kept, and they answer different questions

Bean filters `beanBrand` + `beanType` (text on the shot row); bag filters `bag_id`
(exact). Same coffee, two bags: the bean action shows both, the bag action shows one.
Collapsing them either way loses a real question — "is this roast behaving like the last
one" needs the cross-bag view, "is this bag going stale" needs the single-bag view — and
the split costs one extra picker row.

This mirrors a split the codebase already made deliberately for recipes (`recipeId` exact
vs `recipeName` substring, `shothistory_types.h:274-280`), so the shape is familiar rather
than novel.

### `ShotFilter` gains `bagId` and `bagLabel`, mirroring `recipeId` / `recipeName`

`bagId` (`qint64`, `-1` unset) matches `shots.bag_id` exactly. The `bag:` keyword's term
is a substring, but unlike `recipeName` it cannot match a single column: bag identity is
spread over `coffee_name`, `roaster_name` and `roast_date`. So the keyword filters through
a subquery — `s.bag_id IN (SELECT id FROM coffee_bags WHERE coffee_name LIKE ? OR
roaster_name LIKE ? OR roast_date LIKE ?)` — rather than a JOIN, so it composes with the
other WHERE clauses without changing the shot query's row shape or its column indices
(which are read positionally at `shothistorystorage.cpp:3016`, and are exactly the kind of
thing a stray JOIN breaks silently). `queries.cpp:825` already joins `coffee_bags` in the
ranked-profiles query, so the table is a known quantity here.

`bagLabel` travels with an exact filter for the banner only — coffee name, falling back to
roaster name, the same rule `BeansItem.bagLabel()` uses — and is never a query term.

### Empty values are omitted, not sent as empty strings

`buildFilter()` already skips filter fields that are `""`, so a bean with a brand but no
type filters on the brand alone with no extra code. An action whose whole context is empty
sends `{}` — Shot History opens unfiltered with no banner. The alternative (disable or hide
the widget) means a home-screen button that silently does nothing, which reads as a bug;
opening unfiltered History is a defensible outcome for a button labelled "History".

Note the bag filter must distinguish "no bag" from "any bag": a pre-bag shot has a NULL
`bag_id`, and `bagId <= 0` means *unset filter*, never *match the null rows*. That is why
the guard is `filter.bagId > 0` before emitting the clause, matching `recipeId`'s
treatment at `queries.cpp:245`.

### Already-on-History is handled in the shell, once

`goToShotHistory()` applies the filter in place when the current page is Shot History —
assigning `initialFilter` and reloading, the same path `filterByRecipe()` takes — instead
of falling out of `pushUnlessCurrent()`. Fixing it in the handler rather than in each new
action means the existing unfiltered `navigate:history` is fixed too: it becomes a "clear
the filter" tap rather than a no-op.

Alternative considered: have `CustomItem` detect the current page and call the page's
methods directly. Rejected — a page's methods are not a widget's business
(`QML_NAVIGATION.md`: a page never reaches for `pageStack`; it emits a shell signal and the
shell decides), and it would put the same branch in four call sites.

### Action catalog moves to C++, alongside the two catalogs that already live there

A `layoutActionCatalog()` table in `settings_network.cpp` holding `{id, labelKey, label,
contexts}`, with `SettingsNetwork::layoutActionCatalog()` (QVariantList, for QML) and
`layoutActionCatalogJson()` (for injection into the web page) — mirroring
`widgetCatalog()` / `readoutCapabilitiesJson()` exactly, including the translation-key +
English-fallback pairing so in-app labels stay translatable and the web stays English.

Alternative considered: add the four entries to both hand-written lists and leave the
duplication. Rejected — the lists have already drifted by six navigate actions, the
divergence is invisible until a user notices an action missing from one surface, and this
change would be the second edit-both-in-lockstep event. Centralizing fixes the drift that
already happened, in the pass that would otherwise deepen it.

The dynamic `command:loadProfile` entry stays dynamic: its catalog row is one ordinary
entry, and the profile-list expansion on selection remains in `CustomEditorPopup.qml`.

## Risks / Trade-offs

- **Centralizing closes the in-app/web gap, so the web picker gains ~10 actions it never
  offered** → That is the intended fix, but it lands in the same PR as the feature.
  Contexts are carried in the catalog, so nothing becomes offerable where it shouldn't be;
  verify the web picker's context filtering against the in-app picker page by page.
- **The `bag:` subquery runs a `LIKE` per term over `coffee_bags` per search** → Not a
  hazard, and the "measure it at the call site" this section originally called for would
  have been measuring the wrong thing. `requestShotsFiltered()` builds the SQL on the main
  thread (pure string work) and **executes on a background thread**, so the query's cost is
  not on any path a user or the machine waits on. It also sits beside an existing subquery
  of the same shape (grinder identity over `equipment_items`), against a table that is one
  user's bag inventory. No index, no measurement.
- **A translation key typo in the moved catalog shows an English fallback silently** →
  Keys are copied verbatim from the QML list, not retyped; diff the key strings before and
  after.
- **The in-place re-filter changes behaviour of an existing, shipped path** → Today's
  behaviour is a silent no-op, so there is no user expectation to break, but it does mean a
  tap that used to do nothing now navigates the list. Called out in the manual entry.
- **`Settings.dye.dyeBeanBrand` is the DYE metadata field, so editing bean metadata
  mid-session changes what the bean action filters on** → Correct by design (the widget
  follows current state), but it means the filter tracks what the *next* shot will be
  recorded as, not necessarily what the last one was. The bag action does not have this
  property, which is another reason to keep both.
- **Web editor injection point** → The `ACTIONS` array sits inside a large JS string
  literal; the injection follows the established `WIDGET_CATALOG` /
  `WIDGET_CAPABILITIES` pattern in the same file, so the mechanism is proven, but the
  `.arg()` ordering is easy to get wrong and fails only at runtime in the browser.

## Migration Plan

None, in either sense. New action ids are additive; existing layouts, library items and
stored `navigate:history` widgets are untouched. Bag filtering uses the existing populated
`shots.bag_id`, so there is no schema migration and no `schema_version` bump. The catalog
centralization is a refactor behind identical output — same ids, same labels, same contexts
— so a layout saved before the change loads identically after it.

## Open Questions

None outstanding. Two prior questions are settled: bag is added as both a widget action and
a `bag:` search keyword, and the dedicated History widget gains no gestures.
