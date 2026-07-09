# Design: add-recipe-wizard-tea

## Context

Recipes (add-recipes / finish-recipes-first-class) are the whole drink: profile + bean link + equipment + dose/yield/temp + grind routing + steam block + hot-water block. Creation/editing happens in `RecipeComposerPage.qml` — one page, four section cards, flat pickers. Drink identity is implicit today: `hasMilk` → milk drink, `hasWater` + `order` → americano/long black, the profile's `beverage_type` → espresso/filter/tea.

Facts the design leans on (verified during exploration):

- Profile JSON carries `beverage_type` (`"espresso"` default for empty, `"filter"`, `"pourover"`, `"tea_portafilter"`, maintenance types) — `ProfileManager::currentProfileBeverageType()` already normalizes.
- Shots record `profile_name`, `bean_brand`, `bean_type`, `roast_level` — the ranking query needs no new bookkeeping.
- 15 stock tea profiles ship in `resources/profiles/tea_portafilter_*.json`; their titles encode tea type (black tea, Sencha, Japanese/Chinese green, oolong ×4, white, tisane…) and their temps encode the style (60–100 °C).
- `recipes.profile_title` is already nullable in the schema; "profile required" is app-layer validation only.
- Bag photo resolution is og:image-only; page-text extraction is local-fetch-only with a 20 k char cap and a <100 char failure gate. Verified vendor behaviors: Harney (Shopify SSR — everything works today), Yunnan Sourcing (SSR but 21.7 k text with product content at 14.8–17.5 k — cap-endangered), Fortnum & Mason (JS SPA — 48 chars locally, no og/JSON-LD; provider-side web fetch returns full content including product image URLs, which download directly).
- Equipment packages own typed `EquipmentItem` rows (grinder / basket / puckprep); create paths currently require exactly one grinder.

## Goals / Non-Goals

**Goals:**
- Recipe creation that is faster than the composer: each step narrows the next, selections auto-advance, and the details step is mostly confirmation because values are prefilled from shot history / bag data.
- Editing that is *not* a wizard walk: the summary step is the edit surface; steps are directly addressable.
- Tea as a first-class drink end to end: tea bags, tea extraction, tea profile recommendation, portafilter-tea recipes, and hot-water-only tea recipes.
- URL extraction that works on the three verified vendor archetypes without a scraping proxy.

**Non-Goals:**
- No changes to recipe activation semantics for profile-carrying recipes (single path, write-through/deactivation rules stay as specced).
- No Visualizer behavior change for tea beyond suppressing the canonical search lane (upload/sync stay as-is).
- No per-recipe hot-water amount (stays vessel-carried).
- No tea support in the AI dialing advisor, Bean Base, or shot quality badges (out of scope).
- No new user-facing settings.

## Decisions

### D1. Wizard replaces the composer; summary page is the editor

One new page flow (working names: `RecipeWizardPage` hosting step views + `RecipeSummaryPage` semantics as its final state). Create = linear: drink type → bean → profile → details → summary → Save. Edit / clone / promote-from-shot enter at the summary with all state loaded; tapping a summary row opens just that step and returns. Breadcrumb chips (drink · bean · profile) provide back-navigation during creation and double as the name-suggestion source (`"<Bean> <DrinkType>"`).

*Why not keep the composer for edit?* Two surfaces for the same object drift apart; the summary-as-editor gives one component both jobs and matches how users actually edit (one field at a time).

*Alternative rejected:* a step indicator with a Next button per step — auto-advance on tap is strictly fewer interactions for pickers; Next survives only on the details step (a form, not a picker).

### D2. `drink_type` is stored, not derived

New TEXT column on `recipes` (kCols row + CREATE TABLE + migration, per the bag/recipe column pattern): `espresso | filter | americano | long_black | latte | tea | tea_hotwater`. Stored because: derivation gets ambiguous exactly when D4 templates are escaped (milk + water both present), pills want an icon without re-deriving, and MCP/web need the field. For rows that predate the migration and for promote-from-shot, a derivation function maps blocks → type (water+after → americano, water+before → long_black, milk → latte, profile beverage_type filter/pourover → filter, tea_portafilter → tea, hot-water block without profile → tea_hotwater, else espresso). Derived value is written once at edit/promote time, never re-derived when the stored value exists.

### D3. Profile-less recipes (hot-water tea)

Validation rule (composer/wizard, MCP, web, `RecipeStorage`): **profile required unless the hot-water block is present with `hasWater: true`**. `applyActivatedRecipe` gains an early branch: when `profile_title` is empty, skip the profile-load, dose-write, and yield/temp-override stages entirely; still apply bag, equipment (usually none), grind routing (n/a for tea), and the hot-water block (vessel re-select + `applyHotWaterSettings()`; no heater hold, per the existing hot-water rule). `recipeActivated` still fires. The machine action is the user starting Hot Water. Consequences accepted: hot-water dispenses aren't shots, so these recipes never accumulate shots → always hard-deletable; promote-from-shot can never produce one.

### D4. Drink-type templates configure the wizard, blocks stay source of truth

A static template table keyed by drink type drives: profile filter set, bag kind filter, block pre-seed (milk on for latte; water on + order fixed for americano/long_black), and the details-step field list (grind hidden for tea; vessel row for water drinks; milk row for latte). The summary offers "Add milk" / "Add hot water" rows for types that didn't pre-seed them, so any historical combination remains expressible. Machine behavior continues to read only the blocks — `drink_type` is presentation and intent.

### D5. Profile ranking: three tiers, one new query

New `ShotHistoryStorage` read (background thread, `withTempDb`, request/ready signal pair like `requestShot`): given bean identity (brand+type) and a beverage-type filter set, return `(profile_name, MAX(timestamp))` grouped, plus the same for the similar-bean tier. Wizard-side assembly:

- **Tier ① used with this bean** — exact `bean_brand`+`bean_type` match, recency order.
- **Tier ② similar beans** — coffee: same `roast_level`; tea: same `teaType` (from the bag blob of bags whose shots matched). Recency order.
- **Tier ③ everything else** in the filter set — coffee: alphabetical; tea: ordered by |profile temp − bag `brewTempC`| when the bag states one, else alphabetical.

Tea tier ② adds a *type-match* signal that needs no history: stock tea profile titles are keyword-matched against the bag's `teaType` (black → "black tea", green+japan/sencha → "Japanese green"/"Sencha", oolong → oolong variants, herbal → "tisane", white → "white tea"). Type-matched profiles rank at the top of tier ② with a reason chip ("matches black tea"). Filter sets per the proposal: espresso-family → `espresso` (+empty), filter → `filter`+`pourover`, tea → `tea_portafilter`. Maintenance types always excluded. Search filters within the drink-type set.

*Why recency, not frequency:* a re-dial makes the old pairing stale; frequency is sticky in the wrong direction.

### D6. Details-step prefill priority

For the chosen (bean, profile): most recent shot with that exact pair seeds dose / yield / temp / grind (one `ShotHistoryStorage` lookup, reusing the shot-projection read). Fallbacks in order: bag's structured brewing data (tea: `brewTempC`, dose = target volume × `leafGramsPer100Ml`/100), then profile recommended dose / target weight / temperature (the composer's current behavior). Temperature correction rule for portafilter tea: if the chosen profile is type-matched to the bag, trust the profile's temp (it encodes the portafilter adaptation); only seed a temp override from `brewTempC` when the profile is generic (e.g. "no pressure") or the user picked across types. Hot-water tea uses vendor numbers verbatim (it *is* steeping).

### D7. Bag `kind` at birth; two add buttons

`coffee_bags.kind` TEXT (`"coffee"` default, migration + kCols row). Set by the entry point, no editor toggle (identity, not a setting; zero-shot bags are hard-deletable, so a mis-creation is recreated). `BeanInfoPage` header: "Add Coffee" keeps the primary button and current flow; "Add Tea" is a secondary button opening `ChangeBeansDialog` in a tea mode: Visualizer canonical lane suppressed (verified junk for tea), past-tea-bags lane retained ("buy again"), straight to form when no tea bags exist. Tea form = same fields minus roast level, grind/rpm, canonical-link affordances; labels "Brand"/"Tea". The `roast_level` column stays empty for tea (`teaType` lives in the blob — see D8 — rather than overloading the column). Unified bean search model, BagCard, and MCP bag tools read/expose `kind`.

### D8. Tea extraction: second prompt, structured brewing fields in the blob

`AIManager` selects the system prompt by bag kind. Tea prompt keys (blob vocabulary, schemaless — no migration): `teaType` (black/green/oolong/white/herbal/pu-erh), `origin`, `region`, `garden` (estate), `cultivar`, `flush` (harvest), `tastingNotes`, plus `brewTempC` (number, Celsius — prompt mandates conversion: 212 °F → 100, "boiling"/"freshly-boiled" → 100), `leafGramsPer100Ml` (number — prompt normalizes "2 g per cup (237 ml)" → 0.84), `steepTime` (display string, shown in bag details / notes; no machine mapping). Never-guess rule unchanged: absent statements leave keys out; the wizard then uses tea defaults (black/herbal 98–100 °C, oolong 90, green/white 80) from the template table, not from guessing.

### D9. Two-stage URL extraction with provider web-fetch fallback

Stage 1 unchanged: `BeanBaseClient::fetchPageText` (local GET → squish → cap). Cap raised 20 k → 48 k chars (≈12 k tokens; eliminates the Yunnan-Sourcing class where nav cruft crowds the tail-truncated product content). Stage 2 triggers only on stage-1 failure (`emptyPage`, or transport block): the app asks the configured provider to fetch the URL itself — Anthropic `web_fetch` tool / OpenAI web-search tool in the same extraction request — and return the same JSON contract plus one new key, `imageUrl` (main product photo, absolute URL; the model picks it from the rendered page). `BeanBaseClient`'s existing image download/cache consumes `imageUrl` exactly like an og:image hit; og:image remains the fast path and is attempted first. No proxy, no new keys, no new trust surface (the provider was already receiving the page text; now it receives the URL).

*Alternative rejected:* a scraping proxy (Visualizer's approach) — third-party dependency, cost, and a new place user data flows.

Failure shape: if stage 2 also yields nothing (provider lacks web tools, or the page is truly empty), the existing `emptyPage` error surfaces unchanged.

### D10. Per-drink-type equipment defaults; grinder-less packages

The wizard's equipment row prefills from "last package used on a recipe of this drink type" — resolved by query (most recent recipe row of that `drink_type` with a non-zero `equipment_id`), not by a new setting. Fallback: current active package; final fallback: none. `EquipmentStorage` create paths accept a package with no grinder item (basket-only) for tea setups: `requestCreatePackage` relaxes the grinder-required precondition; display name falls back to basket brand+model; grinder-scoped dial memory and rpm surfaces simply stay absent (grind is hidden for tea anyway). No schema change (items are typed rows).

## Risks / Trade-offs

- [Wizard adds steps for power users who liked one page] → auto-advance keeps taps ≈ equal (type→bean→profile is 3 taps); summary-as-editor means edits never pay the walk; prefill makes details confirm-only.
- [Provider web-fetch availability differs (Anthropic web_fetch vs OpenAI web search); either may fail on some sites] → stage 2 is best-effort behind stage 1; failure lands on the existing, already-translated error path. Feature-detect per provider; hide nothing when unsupported (the failure message is the same as today's).
- [20 k→48 k cap raises extraction token cost ~2.5× worst-case] → only pages that large pay it; extraction is a rare, user-initiated action.
- [Type-matching stock tea profile titles is string-keyed and English-only] → it's a ranking signal, not a gate: mismatch degrades to temp-proximity ordering; profile search always available. Keyword table lives beside the template table for easy extension.
- [`drink_type` can contradict blocks after MCP/web edits] → activation and machine behavior never read `drink_type`; the summary re-derives its row list from blocks; MCP `recipe_update` re-derives `drink_type` only when blocks change and the caller didn't set it.
- [Grinder-less packages may surprise code assuming `grinder.isValid()`] → audit `flattenPackage`/QML consumers for the invalid-grinder case; the existing "basket is optional" handling is the pattern to mirror.
- [Migrations 28+ (recipes.drink_type, coffee_bags.kind) on two tables in one change] → both follow the additive-column pattern with defaults; import/transfer id-remapping untouched (columns ride along like `hot_water_json` did).
- [ChangeBeansDialog is already large; a tea mode adds branching] → tea mode is subtraction (hide lanes/fields) driven by one `kind` property, not a parallel form.

## Migration Plan

1. Schema migrations land first (both additive with defaults; safe on downgrade-read since SQLite ignores unknown columns on old SELECT lists is *not* true — but both migrations only ADD columns, and older app versions never run against a newer DB in this project's update model).
2. Wizard ships in the same release as the composer removal — entry points (`RecipesPage` add, promote buttons, clone, `RecipesItem`) re-target in one commit so there is no orphaned path. `RecipeComposerPage.qml` removed from CMakeLists and deleted.
3. Existing recipes get `drink_type` lazily: derivation runs when a legacy row is loaded into the summary (then stored on save) and in `recipe_list`/pill display as a read-time fallback. No backfill pass needed.
4. Docs: `RECIPES.md` (wizard, drink_type, profile-less), `BEAN_BASE.md` (two-stage extraction, tea prompt), `MCP_SERVER.md` (tool field changes), wiki Manual pages (Recipes, Bag Inventory).

## Open Questions

- Exact Anthropic/OpenAI web-tool request shapes per provider SDK version in use — resolve at implementation against the providers added in #1445 (spike task first in tasks.md).
- Whether the tea details step needs a yield field distinct from vessel volume for portafilter tea (profile target weight vs vessel) — default plan: portafilter tea uses profile yield like espresso; hot-water tea uses vessel volume only.
