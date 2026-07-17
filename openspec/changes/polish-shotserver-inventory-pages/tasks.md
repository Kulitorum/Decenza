## 1. Shared style, shell, and JS helpers (de-duplicate first)

- [x] 1.1 Add a `webtemplates/` header (e.g. `management_css.h` → `WEB_CSS_MANAGEMENT`) with app-matching styles: responsive card grid, rounded card + `.active` accent-border state, buttons/`button.primary`, `.badge` + drink-type/warning chips, `#status`, empty state, form inputs, and `dialog`/modal — consolidating the copy-pasted blocks in the three page files.
- [x] 1.2 Add a shared page-shell/header helper that emits the canonical `<header class="header">` (☕ Decenza logo, back link, `.header-right` burger) so no page pastes header markup.
- [x] 1.3 Extract the duplicated JS utilities into a shared JS constant: `esc`, `status`, `post`, a `joinWithBullet`-style dot-joiner (mirroring `Theme.joinWithBullet`), and the card/list-render + modal open/save helpers.
- [x] 1.4 Wire the new `webtemplates/` header(s) into the build/include set like `base_css.h`/`menu_css.h`. (Headers are include-only; no CMake entry needed — added `#include`s to each page file.)

## 2. /beans — visual parity (`shotserver_bags.cpp`)

- [x] 2.1 Replace the private `<style>` block with the shared style; use the shared header shell; drop the duplicated JS utilities in favor of the shared ones.
- [x] 2.2 Render the responsive card grid with the app's `BagCard` hierarchy: thumbnail, coffee name (title) + verified badge when linked, roaster (secondary), dot-joined attribute line (origin · variety · process, omitting missing), tasting-notes line, freshness/roast-date meta line, wrapping action row.
- [x] 2.3 Highlight the active bag with the accent border/`.active` style; add the "No bags yet" empty state.

## 3. /beans — feature parity (`shotserver_bags.cpp`)

- [x] 3.1 Add separate **Add Coffee** / **Add Tea** creation (set `kind`) and expose the app's tea fields for tea bags.
- [x] 3.2 Expose the full bean-attribute fields (origin, region, farm/producer, variety, elevation, process, harvest, quality score, place of purchase, tasting notes, product link) in the create/edit form.
- [x] 3.3 Add yield anchor (grams or ratio, mutually exclusive), RPM, and per-bag equipment link controls (API already accepts these).
- [x] 3.4 Add discrete freeze-lifecycle actions — Thaw and Mark Opened — alongside the editable frozen/defrost/opened dates.
- [x] 3.5 Add `GET /api/beans/search` reusing `BeanBaseClient` (mirror `mcptools_beansearch.cpp`), running off the request thread; wire a search-first create/link flow in the page with the verified badge and a full-detail info popup (matching `BeanBaseDetailsPopup`).
- [x] 3.6 Add `POST /api/beans/extract` reusing `BeanBaseClient` (mirror `bag_extract_details` in `mcptools_ai.cpp`) using the async ShotServer pattern (fired guard + timeout + always-respond); wire a "get info from page" affordance that prefills the form.
- [x] 3.7 Add `GET /api/bag/<id>/image` (reuse the app's bean-photo cache path) with a placeholder fallback; show the thumbnail on the card.
- [ ] 3.8 Verify create/edit/finish/activate/delete and the new search/extract/link/thaw/open actions all round-trip to the app unchanged; every `fetch()` has `.catch()` and checks `r.ok`.

## 4. /recipes — visual parity (`shotserver_recipes.cpp`)

- [x] 4.1 Replace the private `<style>` block with the shared style; use the shared header shell and shared JS utilities; keep the archived section behavior, restyled.
- [x] 4.2 Render the responsive card grid with the app's `RecipeDrinkCard` hierarchy: thumbnail, name (title), drink line (drink-type chip + profile + milk), bean line (roaster/coffee · shot count), plan line (dose/yield · temp · grind), wrapping action row.
- [x] 4.3 Highlight the active recipe with the accent border + "Active" badge; surface a stale bag link as a warning-styled affordance; add the empty state.

## 5. /recipes — feature parity (`shotserver_recipes.cpp`)

- [x] 5.1 Add the search + sort bar (search text; sort by date used / created / coffee / profile / name; ascending/descending toggle) implemented client-side over the fetched list.
- [x] 5.2 Expose the full steam block in the editor — milk weight, pitcher, duration, flow, temperature — and RPM (`rpmPinned`).
- [x] 5.3 Add the stale-bag re-point action: a "choose beans" affordance that lets the user pick another open, kind-matched bag and re-points the recipe (reuses the existing bag-update / `bagId` path).
- [x] 5.4 Show recipe card thumbnails (reuse the bean-image route).
- [ ] 5.5 Verify create/edit/clone/archive/activate, the steam/RPM/re-point/search/sort features round-trip unchanged; fetch error handling intact.

## 6. /equipment — visual + feature parity (`shotserver_equipment.cpp`)

- [x] 6.1 Replace the private `<style>` block with the shared style; use the shared header shell and shared JS utilities.
- [x] 6.2 Render the responsive card grid with the app's `EquipmentCard`/`EquipmentSummary` hierarchy: name (or grinder brand+model fallback) title, burrs line, basket line, dot-joined puck-prep line; wrapping action row.
- [x] 6.3 Highlight the active package with the accent border/`.active` style; add the "No equipment yet" empty state.
- [x] 6.4 Extend the Equipment API editable-key set + JSON to accept/return the puck-prep flags (WDT, Shaker, Puck screen, Bottom paper filter, RDT spritz), routing through `EquipmentStorage`. (Storage already emits `puckPrep_<key>` on read and reads them on write via `PuckPrep::canonicalMerged`; added the keys to `kPackageEditableKeys`.)
- [x] 6.5 Add the puck-prep flag checkboxes to the create/edit form and render the puck-prep line on the card.
- [ ] 6.6 Verify create/edit/remove/activate (including delete→remove fallback) and puck-prep round-trip unchanged. (Needs a build — user verifies.)

## 7. Consistency & verification

- [x] 7.1 Confirm all three pages render identically-styled cards/buttons/badges/status/modals from the single shared style and shared JS — no page re-inlines private CSS or duplicates the JS utilities.
- [ ] 7.2 Verify responsive behavior and touch targets at DE1 tablet width and phone width (grid collapses to one column, buttons ≥36px).
- [x] 7.3 Confirm every `fetch()` has `.catch()` and checks `r.ok` before `.json()`, and the async extract endpoint never leaves a hung socket (timeout + acceptance check).

## 8. Docs

- [x] 8.1 Update `docs/CLAUDE_MD/SHOTSERVER.md`: the shared embedded-page style + shell helper, the new bean-search / extraction / image endpoints, and that `/beans`, `/recipes`, `/equipment` now match the app in look and features.
- [ ] 8.2 Update the wiki manual for the newly web-available features (Bean Base search/link, AI import, tea bags, equipment puck-prep, recipe search/sort), per CLAUDE.md's user-visible-change rule. (Held until release, per the wiki-edits-held convention — do at ship time.)
