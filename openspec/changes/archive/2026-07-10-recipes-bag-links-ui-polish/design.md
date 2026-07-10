# Design: recipes-bag-links-ui-polish

## Context

Recipes (add-recipes, add-recipe-wizard-tea, finish-recipes-first-class — all archived) ship in the next release. Two problems surfaced in real use:

1. **Bean-level linking mis-resolves.** `RecipeStorage::resolveOpenBagStatic()` resolves a recipe's bean identity to `ORDER BY last_used DESC LIMIT 1` at every activation. Users who freeze beans run multiple bags of the same bean at different ages; activation and grind inheritance silently land on whichever bag was touched last. The wizard bean picker also lists such bags as indistinguishable duplicate rows.
2. **UI is prototype-grade beyond wizard step 1.** The drink-type step has a designed tile language; the bean/profile steps are bare `ItemDelegate` text lists, the details step stretches controls full-width (a 700px `− 0° +` stepper) and requires scrolling for ~8 fields, the summary hides a latte's milk and reuses one arrow glyph with two meanings, cards can't distinguish same-bean recipes (users stuff profile names into recipe names to compensate), americano and long black share an icon with no disambiguating text, and `suggestName()` produces "Milk Blend Espresso Espresso" and "Gran Bar Latte / Cappuccino".

Constraints: no new user-facing settings (explicit user direction: "minimize options and make this feature really simple to use"); no timers as guards; DB I/O off the main thread; recipe columns follow the kCols + CREATE TABLE + migration rule; the existing archived-spec rule "bean link is bean-level" is deliberately reversed by this change.

## Goals / Non-Goals

**Goals:**
- Deterministic recipe→bag links with automatic, zero-option lifecycle maintenance (roll-on-finish, wake-on-restock, dup-guard, stale display state).
- Production-grade recipe surfaces: card redesign, wizard tile language on every picker step, one-screen details step, WYSIWYG summary.
- Fix the naming bugs and the americano/long-black icon ambiguity via short labels.

**Non-Goals:**
- Web `/recipes` page visual polish (data-field parity only).
- The shared "Add a new coffee…" Bean Base dialog.
- Any new setting, prompt, or dialog for the relink lifecycle.
- Changing ranking logic in the profile step (presentation only).

## Decisions

### D1: Hard `bag_id` link; bean identity retained as fallback + matching key
Recipes gain a `bag_id` column (kCols row, CREATE TABLE, migration step, transfer id-remap like `equipment_id`). `beanBaseId`/`roasterName`/`coffeeName` stay: they are the *matching key* for relinking and the display fallback when the linked bag row is gone. Alternative considered: bag pin as an optional override on top of bean-level links (inherit/pin pattern) — rejected as two link modes to explain; the user chose "use bags, not beans" outright.

### D2: Relink lifecycle is event-driven, silent, dup-guarded
- Hooks: the bag **finish** path and the bag **add** path (CoffeeBagStorage mutations already emit; MainController orchestrates the relink query + toast on the existing signals — no polling, no timers).
- Roll-on-finish: relink the finished bag's recipes to the *newest open* bag of the same bean identity. Wake-on-restock: relink *stale* recipes matching the added bag's identity, MRU-first.
- **Dup-guard**: skip a relink that would create a second recipe with the same (profile title, drink type) on the target bag. This single rule protects deliberate two-bag comparison pairs without asking the user anything. Alternatives considered: per-recipe confirmation sheet at finish time (rejected: dialog + checkboxes contradict "no options"); fully silent always-roll (rejected: collapses deliberate pairs); a "parked vs stale" user-intent flag (rejected: second state to explain).
- Dose/yield/temp differences do NOT make recipes distinct for the guard — comparison pairs usually share numbers; profile+drink type is the tightest reliable identity.
- Toast on every automatic move (existing toast mechanism), naming the count.

### D3: Stale = linked bag not in inventory; display state, never a gate
No stored stale flag — computed at read time by joining the linked bag's `in_inventory`. Activation applies the full bundle including the finished bag's grind (better starting point than nothing). Card shows "Bag finished — tap to choose beans" (opens a bag picker for that recipe); idle pill dims/badges.

### D4: One shared recipe-card component
The management-page card and the wizard summary hero render the same QML component (new file, CMakeLists registration). This enforces WYSIWYG (what you build is what you'll see) and halves the maintenance surface. Card layout: name anchor / icon + short label + profile + milk weight / bag + shot count / plan line; WordWrap everywhere, no eliding the profile. Profile-less hot-water tea: "Tea · Hot water" + vessel snapshot line in place of ShotPlanText.

### D5: Short labels as a second label function
`drinkTypeShortLabel()` beside the existing `drinkTypeLabel()` (wizard picker keeps the long forms). Used by: cards, summary hero, pills where text appears, and `suggestName()`. `suggestName()` additionally skips appending the type when the bean name already ends with that word (case-insensitive). Both label functions stay translation-keyed.

### D6: Wizard pickers adopt the step-1 tile language
- Bag step: tile per open bag (photo via existing `beanbase.bagImagePath`/`ensureBagImage` cache, roaster caption, coffee name, roast date/age). Ghost tiles (dashed border) for "Add a new coffee…" and "No bean". No dedup — distinct bags are the point.
- Profile step: tiers ①/② as tiles with temperature + target yield (one profile read per tile — same pattern the cards already use) and the reason as an on-tile chip; tier ③ stays a compact list under the always-present search.
- Details step: controls sized to content; SectionCards flow into ≥2 columns on landscape so the step fits without scrolling; grind KB hint becomes a callout (icon + tinted background).
- Sub-pickers (pitcher/vessel/equipment): metadata on rows, no layout change otherwise.

### D7: Migration resolves once, then the resolver retires from activation
Forward migration populates `bag_id` using the current resolver logic (canonical id first, else roaster+coffee, MRU first). Unresolvable recipes migrate with `bag_id` NULL → stale. `resolveOpenBagStatic` survives only as the relink matching helper, not in the activation path. Rollback: the column is additive; pre-migration builds ignore it.

## Risks / Trade-offs

- [Relink fires mid-activation or during a transfer import] → relink runs on the storage worker like every other mutation, serialized by `SerialDbWorker`; import remaps `bag_id` through the bag id-map before any relink logic can see the rows.
- [Dup-guard identity too coarse (profile+drink type)] → the manual swap is one tap from the stale card; a wrongly-skipped roll costs one tap, a wrongly-executed roll silently destroys a comparison pair — asymmetric cost favors the guard.
- [Toast unseen → user surprised recipes moved] → cards and pills always show the current bag; the stale/moved state is inspectable after the fact, the toast is only a courtesy.
- [Shared card component regresses one of its two hosts] → the component takes plain properties (no page-specific lookups); both hosts covered in QML smoke tests.
- [Two-column details grid on narrow/portrait] → columns collapse to one below a width threshold; portrait keeps today's scroll behavior.
- [MCP tool signature changes break test register stubs] → known gotcha; update `tst_mcpserver_session/protocol` stubs and build `--target all` before pushing.

## Migration Plan

1. Schema migration N: add `bag_id` (nullable) to `recipes` + kCols row.
2. Data pass in the same migration: resolve each recipe's bean identity to its open bag; NULL when none.
3. Transfer/backup import: remap `bag_id` through the bag id-map (alongside the existing `equipment_id` remap).
4. Web/MCP: additive `bagId` field — older clients simply omit it (create/update falls back to bean-identity fields, resolved to a bag at save time).
5. Rollback: additive column; no destructive change.

## Open Questions

None — behavioral decisions were settled in the explore session with the user (silent roll + dup-guard, no options; stale never gates; short labels; card layout; wizard tile language).
