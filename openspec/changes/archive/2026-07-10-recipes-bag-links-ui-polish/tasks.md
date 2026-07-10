# Tasks: recipes-bag-links-ui-polish

## 1. Schema + bag link (foundation)

- [x] 1.1 Add `bag_id` to `recipes`: kCols row in recipestorage.cpp, CREATE TABLE column, new migration step; data pass in the migration resolving each recipe's bean identity to its open bag (resolver logic run once; NULL when none)
- [x] 1.2 Thread `bagId` through `Recipe` struct, `toVariantMap`/`fromVariantMap`, and recipe JSON builders; keep bean identity fields as fallback/matching key
- [x] 1.3 Transfer/backup import: remap `bag_id` through the bag id-map in `importRecipesStatic` (alongside the `equipment_id` remap); dedup identity unchanged
- [x] 1.4 Activation: `requestRecipeForActivation` loads the linked bag directly; retire `resolveOpenBagStatic` from the activation path (keep as relink matching helper); stale recipes activate fully with the finished bag's data (grind incl.), no error
- [x] 1.5 Unit tests: migration (resolves open bag / NULL when none), two-open-bags determinism, stale activation, import remap

## 2. Relink lifecycle

- [x] 2.1 Roll-on-finish: on the bag-finish signal, relink the finished bag's recipes to the newest open bag of the same bean identity, skipping dup-guard collisions (same profile title + drink type on target bag); recipes keep the finished link when no successor
- [x] 2.2 Wake-on-restock: on the bag-add signal, relink stale recipes matching the new bag's identity, MRU-first, same dup-guard
- [x] 2.3 Toast on every automatic move (count + target bag), via the existing toast mechanism; no dialogs, no settings
- [x] 2.4 Manual re-point: stale card affordance ("Bag finished — tap to choose beans") opens a bag picker scoped to the recipe; relink preserves grind pin/inherit untouched
- [x] 2.5 Unit tests: roll happy path, dup-guard skip (comparison pair), different-drink-type non-dup, no-successor, restock wake, MRU-first twin wake, pin preservation

## 3. Shared recipe card component

- [x] 3.1 New QML card component (CMakeLists registration): name anchor + Active badge / icon + short label + profile + milk weight line / bag + shot count line / plan line; WordWrap, profile never elided; profile-less hot-water tea variant ("Tea · Hot water" + vessel amount/temp line)
- [x] 3.2 `drinkTypeShortLabel()` (translation-keyed) beside the long labels; used by cards, summary hero, and auto-naming
- [x] 3.3 RecipesPage adopts the component; stale display state on cards; empty state → two starter tiles (shot history / wizard)
- [x] 3.4 RecipesItem pills: dim or badge stale recipes (activation unaffected)

## 4. Wizard polish

- [x] 4.1 Bag step: tile grid (bag photo via beanbase image cache, roaster caption, coffee name, roast date/age), ghost tiles for Add-new and No-bean; selection links the specific bag; no dedup
- [x] 4.2 Profile step: tiers ①/② as tiles with temp + target yield and on-tile reason chips; tier ③ compact list; search unchanged
- [x] 4.3 Details step: content-sized controls (temp stepper, dose/yield), multi-column SectionCard flow on landscape (single column below width threshold), grind KB hint as callout (icon + tinted background)
- [x] 4.4 Summary step: shared card component as WYSIWYG hero; component rows with single edit glyph (drink, bag, profile, details, steam/milk when present, hot water when present, equipment); steam row shows pitcher + milk weight; add/remove block buttons preserved
- [x] 4.5 `suggestName()`: short labels, skip type word when the bean name already ends with it (case-insensitive)
- [x] 4.6 Sub-pickers (pitcher/vessel/equipment): preset metadata on rows
- [x] 4.7 Promote-from-shot carries the shot's `bag_id` into the prefill

## 5. MCP + web surfaces

- [x] 5.1 MCP recipe tools: expose `bagId` + bag display identity + human-readable stale indication on reads; accept `bagId` on create/update; inherited grind resolves from the linked bag; update register stubs in tst_mcpserver_session/protocol and build --target all
- [x] 5.2 ShotServer: `bagId` on list/detail (+ stale flag), accepted on create/update; promotion carries the shot's bag; web form gains the field (data only, no visual redesign)

## 6. Docs + verification

- [x] 6.1 Update `docs/CLAUDE_MD/RECIPES.md`: bean-level → bag-level rule, relink lifecycle, dup-guard, stale semantics, migration note
- [x] 6.2 Run full test suite (BUILD_TESTS=ON, --target all); fix MCP stub fallout
- [x] 6.3 User verification pass on the tablet: wizard walk (latte), two-bags-same-bean scenario, bag finish roll, restock wake, stale card re-point
