## Why

The ShotServer web pages for Beans (`/beans`), Recipes (`/recipes`), and Equipment (`/equipment`) look like unstyled demos next to the polished in-app QML screens, **and** they expose only a subset of the app's features — several capabilities were deliberately deferred to a v1 ("plain form fields, no URL/AI import"; puck-prep flags "stay in-app"). Users who manage their inventory from a browser should get the same clean presentation *and* the same feature set as the app. This change brings the three pages to **full app parity — look and features.**

## What Changes

**Visual parity (presentation):**
- Redesign `/beans`, `/recipes`, `/equipment` to closely match the app's clean inventory screens: canonical page chrome (`<header>` logo + back + burger menu), a responsive card grid, app-style card information hierarchy (bold name, secondary lines, dot-joined attribute lines that omit missing fields), active-item accent highlight, drink-type / stale-bag chips, and friendly empty states.
- Introduce a **shared embedded-page style** (a `webtemplates/` constant) so the three pages are consistent and no page re-inlines a private copy of the CSS.

**Feature parity (functionality):**
- **Beans**: surface every field/action the app has — tea kind + tea fields, full bean attributes (origin/region/variety/process/elevation/producer/quality/etc.), yield anchor (grams or ratio), RPM, per-bag equipment link, freeze-lifecycle actions (thaw / mark opened), plus the two previously-deferred heavy features: **Bean Base search + canonical linking** (with verified badge and the full-detail info popup) and **AI "get info from page"** extraction. Add bean **card thumbnails**.
- **Recipes**: add the app's **search + sort bar**, the **full steam block** (milk weight, pitcher, duration, flow, temperature), **RPM**, and the **stale-bag re-point** action (choose new beans), plus recipe card thumbnails.
- **Equipment**: add the app's **puck-prep flags** (WDT / Shaker / Puck screen / Bottom paper filter / RDT spritz).
- **BREAKING (spec-level, not API):** the `shotserver-bags` "plain form fields (no URL/AI import)" v1 constraint is lifted, and the `shotserver-equipment` field set gains puck-prep.

- Keep the established write-through semantics, auth gate, and the single shared activation path unchanged. Most feature gaps are already accepted by the REST layer (`kind`, `yieldG`/`yieldRatio`, `rpmPinned`) and only need exposing in the web UI; Bean Base search, AI extraction, and bean images require **new read/async endpoints** that reuse the existing backends (`BeanBaseClient`, mirroring the MCP `bean_search` / `bag_extract_details` tools).
- Keep the pages responsive and touch-friendly for the DE1 tablet/mobile browsers.

## Capabilities

### New Capabilities
<!-- None — extends existing capabilities. -->

### Modified Capabilities
- `shotserver-bags`: `/beans` page gains app-matching presentation **and** full feature parity — tea, full bean attributes, yield anchor, RPM, equipment link, freeze-lifecycle actions, Bean Base search + linking (verified badge + info popup), AI page-extraction, and thumbnails; the v1 "plain form fields (no URL/AI import)" constraint is removed. Bags REST API gains a bean-search endpoint, an async page-extraction endpoint, and a bean-image endpoint.
- `shotserver-recipes`: `/recipes` page gains app-matching presentation **and** parity features — search/sort bar, full steam block, RPM, stale-bag re-point, and thumbnails.
- `shotserver-equipment`: `/equipment` page gains app-matching presentation **and** puck-prep flags; the Equipment REST API accepts/returns puck-prep flags.

## Impact

- **Code**: `src/network/shotserver_bags.cpp`, `shotserver_recipes.cpp`, `shotserver_equipment.cpp` (page HTML/CSS/JS + new endpoints), a new shared `webtemplates/` style header, and `src/network/shotserver.cpp` route dispatch for the new bean-search / extraction / image endpoints. Reuses `src/network/beanbaseclient.h` and the patterns in `src/mcp/mcptools_beansearch.cpp` / `mcptools_ai.cpp`.
- **API**: additive endpoints for bean search, async AI extraction, and bean images; puck-prep fields added to the equipment payload. No existing endpoint/payload changes; MCP and in-app surfaces untouched (MCP tool changes, if any, are non-breaking per project convention).
- **No data-model changes**: `CoffeeBagStorage`, `RecipeStorage`, `EquipmentStorage` untouched.
- **Docs**: `docs/CLAUDE_MD/SHOTSERVER.md` updated (shared style + parity + new endpoints); wiki manual updated for the newly web-available features.
