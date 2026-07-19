## Context

The idle-screen Recipes and Beans pill rows are rendered by the shared `PresetPillRow.qml`, a width-balanced flow that wraps overflow onto extra centered rows (`calculateRows()`). Today both rows are truncated in QML to the five most-recently-used items:

- **Recipes:** `IdlePage.qml` `recipePresetLoader` (~L1079) and the compact `RecipesItem.qml` popup both do `recipes.slice(0, 5)`. Selection is `MainController.selectedRecipeId`; a pill tap runs the two-tap `tryStartRecipe()`.
- **Beans:** `IdlePage.qml` `beanPresetLoader` (~L1028) and the compact `BeansItem.qml` popup do `bags.slice(0, 5)`. Selection is `Settings.dye.activeBagId`; a pill tap sets `activeBagId`.

`RecipeStorage`/`BagStorage` already emit the **entire** MRU-ordered inventory via `inventoryReady` (SQL `ORDER BY last_used DESC`, no `LIMIT`) — the only cap is the QML `slice(0, 5)`. So the data to paginate is already in hand.

`PresetPillRow` is also used by the steam/espresso/hotwater/flush/equipment rows, which must remain unpaginated.

Only `resources/icons/ArrowLeft.svg` exists (no right variant); it is mirrored horizontally for the next arrow.

## Goals / Non-Goals

**Goals:**
- Reach every recipe and every inventory bag from the idle screen, five per page, via prev/next arrows ([#1548](https://github.com/Kulitorum/Decenza/issues/1548)).
- Preserve today's exact layout and behavior for ≤5 items and for all non-paginated pill rows.
- One implementation of the paging affordance, reused by the four call sites (inline + compact, recipes + beans).

**Non-Goals:**
- Swipe gestures. The issue floated swipe; arrows are the committed affordance. Swipe can be layered on later.
- Paginating the equipment pill row (small inventory) — out of scope, but trivial to add via the same mechanism.
- Changing the storage layer, the MRU ordering, or the two-tap activation semantics.
- A search/filter box on the idle screen (an earlier idea, dropped in favor of arrows).

## Decisions

### D1 — Add opt-in pagination to the shared `PresetPillRow`, keep windowing in the callers
`PresetPillRow` gains three optional inputs and one signal:
- `property int pageCount: 1` — total number of pages (caller computes `Math.ceil(total / 5)`).
- `property int pageIndex: 0` — zero-based current page.
- `signal pageChangeRequested(int delta)` — emitted with `-1`/`+1` when an arrow is tapped.

The component renders left/right arrows flanking its pill content **only when `pageCount > 1`**, with the left arrow visible when `pageIndex > 0` and the right when `pageIndex < pageCount - 1`. It does **not** own paging state or slice any list — the caller still owns the inventory, computes the visible window, and reacts to `pageChangeRequested`. `presets`, `selectedIndex`, and `presetSelected(index)` continue to operate on whatever windowed list the caller passes, so their existing index math is unchanged.

*Why:* four call sites reuse one arrow implementation; the component stays presentation-only and backward-compatible (defaults → no arrows, so steam/espresso/etc. are untouched); windowing lives where the inventory and selection already live.

*Alternative rejected:* duplicating arrow rendering in each of the four callers (more code, drift risk), or moving inventory/paging state into `PresetPillRow` (couples the generic component to recipe/bean specifics).

### D2 — Reserve arrow gutters only when paginating, so pills never reflow across pages
When `pageCount > 1`, the component reserves a fixed-width gutter on **both** sides for the arrows (arrow shown or not) and reduces the pills' `effectiveMaxWidth` accordingly. When `pageCount <= 1`, no gutter is reserved and the pills use the full width exactly as today.

*Why:* reserving both gutters keeps the pill block width stable as the user pages (an arrow appearing/disappearing at an end does not shift the pills), and guarantees arrows never overlap a full five-wide row on a narrow tablet. The common ≤5 case keeps its current pixel-for-pixel layout because no gutter is reserved.

*Alternative rejected:* overlaying arrows on top of the centered pill row (simpler, but overlaps pills when a five-wide row fills the width) or reserving a gutter only for the currently-visible arrow (pills shift horizontally when paging to/from an end).

### D3 — Caller state: full list + page index, derived visible window
Each caller keeps the **full** MRU list (drop the `slice(0, 5)`) and adds a `pageIndex` int and a derived `readonly property var visible<Recipes|Bags>` = `full.slice(pageIndex*5, pageIndex*5 + 5)`. The `PresetPillRow` is pointed at the *visible* window everywhere it currently references the full-then-sliced list (`presets`, `selectedIndex`, and the `onPresetSelected` index lookup). `pageCount` = `Math.ceil(full.length / 5)`. `onPageChangeRequested` clamps `pageIndex + delta` to `[0, pageCount-1]`.

Page reset rules:
- **Reset to page 0** when the row opens (inline: `activePresetFunction` becomes this function; compact: popup `onOpened`), so the user always lands on the most-recent five.
- **Clamp** `pageIndex` into range on every `inventoryReady` (list add/remove/reorder) so it never points past the end; do not force-reset mid-view.

*Why:* MRU reorder-on-activation could otherwise strand the page index past the end; clamping is the minimal safe response, and reset-on-open matches the "quick access to recent" intent.

### D4 — Right arrow by mirroring the single existing SVG
Render `ArrowLeft.svg` for the previous arrow and the same asset with a horizontal mirror (`Image.mirror: true` or a `Scale { xScale: -1 }`) for the next arrow. Both are colorized to `Theme.iconColor` via the existing `ThemedIcon`/`MultiEffect` pattern used elsewhere in the file.

*Why:* no new asset, guaranteed visual symmetry, follows the "icons are SVG, never font glyphs" rule (`→`/`←` are banned per CLAUDE.md).

### D5 — Accessibility
Each arrow is an `AccessibleButton` (or `AccessibleTapHandler`) with role button, `Accessible.name` "Previous recipes"/"Next recipes" (and the beans equivalents, internationalized), focusable, and a press action. On a page change, announce the new page's pills, reusing the existing announcement helpers the rows already have for TalkBack/VoiceOver.

## Risks / Trade-offs

- **[MRU reorder shifts the page under the user]** Activating a recipe/bag bumps it to MRU #1; if `last_used` updates immediately, a user paged to page 2 who taps a pill could see the list reorder. → First tap only *selects*; the reorder that matters (`last_used`) is driven by shot completion, by which point the pill row is gone (machine brewing). Clamp-on-change plus reset-on-open bound any visible effect; no force-reset mid-view avoids a jarring jump.
- **[Reserved gutter narrows pills when paginating]** With `pageCount > 1`, ~2 arrow-widths are taken from the pill area, so a five-wide page may wrap one pill sooner than an unpaginated five would. → Acceptable; wrapping to a second row is the row's existing overflow behavior, and it only applies once there are >5 items.
- **[Four call sites to keep in sync]** Recipes and beans, inline and compact, must all adopt the same pattern. → Mitigated by D1 (shared rendering); the per-caller change is small and identical in shape. A regression in one popup is cosmetic, not data-affecting.
- **[Compact popups re-request inventory on open]** They already `slice(0,5)`; dropping the slice means holding a longer array in QML. → Negligible; lists are small (tens of items).

## Migration Plan

Pure additive UI change; no data migration, no persisted state, no schema change. Rollback is reverting the QML. Ships behind no flag — the ≤5-item path is unchanged, so users with few recipes/bags see no difference.

## Open Questions

- Should the page change animate (the issue calls animation "sexy")? Default plan: a light opacity fade on window change if it composes cleanly with `calculateRows()`; skip if it fights the deferred row-flow timer. Not a blocker.
- Should equipment adopt the same pagination for consistency? Deferred; out of scope unless requested.
