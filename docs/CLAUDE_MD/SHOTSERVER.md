# ShotServer Web UI

The ShotServer is the in-app HTTP server that exposes shot history, settings, layout editor, and AI endpoints to the browser. It is split across multiple files; this doc captures the conventions that the routing layer and the embedded JavaScript must follow.

## File layout

- `shotserver.cpp` — core server + route dispatch
- `shotserver_layout.cpp` — layout editor web UI (inline HTML/JS)
- `shotserver_shots.cpp` — shot history endpoints
- `shotserver_backup.cpp` — backup/restore endpoints
- `shotserver_settings.cpp` — settings endpoints
- `shotserver_ai.cpp` — AI assistant endpoints
- `shotserver_auth.cpp` — authentication
- `shotserver_theme.cpp` — theme endpoints
- `shotserver_upload.cpp` — file upload handling
- `shotserver_recipes.cpp` — recipes REST + `/recipes` page (add-recipes)
- `shotserver_bags.cpp` — coffee-bag REST + `/beans` page (add-recipes), plus Bean Base search / AI extraction / bean image endpoints
- `shotserver_equipment.cpp` — equipment-package REST + `/equipment` page (add-recipes)
- `webtemplates/management_{css,html,js}.h` — shared style/shell/JS for the three management pages (see below)

The layout editor web UI is served as inline HTML/JS from `shotserver_layout.cpp`.

## Shared management-page style (beans / recipes / equipment)

The `/beans`, `/recipes`, and `/equipment` pages are designed to closely match their in-app QML screens (`BagCard` / `RecipeDrinkCard` / `EquipmentCard`) in **look and features** — see the CLAUDE.md ShotServer rule. They share one set of `webtemplates/` helpers instead of each re-inlining CSS/JS:

- `management_css.h` → `WEB_CSS_MANAGEMENT` — the card grid, cards (`.card` / `.card.active` / `.card.dimmed`), thumbnail, title/verified/roaster/attr/notes/meta/plan lines, `.badge` / `.chip` / `.chip.warn`, `.actions` buttons, `#status`, `.empty` state, toolbar/`.searchbar`, the `dialog` form (`.grid-2`, `.check-row`, `.dialog-section`, `.dialog-actions`), and `.search-results`.
- `management_html.h` → `generateManagementHeader(titleHtml)` — the canonical `<header class="header">` (☕ Decenza logo + page title + burger menu), the same chrome the Shot History page uses. It emits the opening `<body>` tag.
- `management_js.h` → `WEB_JS_MANAGEMENT` — the shared JS utilities `el`, `status`, `esc`, `bullet(parts)` (dot-joiner mirroring `Theme.joinWithBullet`), `getJson`, and `post`.

When adding or restyling any of these pages, use these helpers — do not paste a private `<style>` block or re-declare `esc`/`status`/`post`. When you touch the in-app version of one of these screens, update the web page to match (and vice-versa).

**Deliberate divergence — grind/RPM entry.** In the app, grind and RPM are edited through the tap-to-open grind picker (wheels + keyboard mode, `GrindField`/`GrindPickerDialog`). The web forms deliberately do NOT reproduce the wheel: the tumbler is a touch-first affordance, and these pages are used with a keyboard where a text input is the better control. The web equivalent is `<input>` + `<datalist>` via the shared `webtemplates/grind_datalist_js.h` helper (`attachGrindDatalist`), fed by `GET /api/grind-candidates` — candidates are computed server-side by the same C++ stepping machinery as the app's wheels and resolved against the record's own grinder (the shot's, the bag's linked package, the recipe's package), never the active one. Free text stays accepted (grinder notation is opaque). This is a recorded exception to the keep-the-surfaces-in-sync rule, not drift — see `openspec` change `replace-grind-inputs-with-picker` (design D6).

## Async community endpoints (signal-based)

- Community API calls (`browse`, `download`, `upload`, `delete`) are async — they connect to `LibrarySharing` signals and wait for a callback. Use the established pattern: `QPointer<QTcpSocket>` + `std::shared_ptr<bool>` fired guard + `QTimer` timeout + `PendingLibraryRequest` tracking.
- **Always verify the operation was accepted** after calling `LibrarySharing` methods. Methods like `browseCommunity()` and `downloadEntry()` silently return if already busy (`m_browsing`/`m_downloading`). Check `isBrowsing()`/`isDownloading()` after the call and send an immediate error response if rejected — otherwise the request hangs until the 60s timeout.
- **Always log timeout and cleanup events.** Use `qWarning()` when a timeout fires, `qDebug()` when a response is dropped (socket disconnected) or when a duplicate callback is blocked by the fired guard.
- **Only one request per type** is allowed at a time (`hasInFlightLibraryRequest`), because `LibrarySharing` is a singleton that emits one signal consumed by whichever handler is connected.

## JavaScript `fetch()` calls

- **Every `fetch()` must have a `.catch()` handler.** Never leave a fetch chain without error handling — silent failures leave the UI in a broken state (spinner stuck, editor blank, no feedback).
- **Check `r.ok` before `r.json()`** in fetch chains. Non-2xx responses with non-JSON bodies will throw on `.json()` and produce a misleading "Network error" instead of "Server error (500)".
- **Use `AbortController` with a timeout** for community API calls (client-side 45s, server-side 60s safety net).
- **Don't mutate state before async success.** For example, increment a page counter only after the fetch succeeds, not before — otherwise failed requests skip pages permanently.

## Recipes / bags / equipment surfaces (add-recipes)

- Endpoints: `GET/POST /api/recipes`, `POST /api/recipes/from-shot/<shotId>`, `GET/POST /api/recipe/<id>` plus `/clone`, `/archive` (body: `restore`/`delete`), `/activate`; `GET/POST /api/bags`, `GET/POST /api/bag/<id>` plus `/finish`, `/delete`, `/activate`, `GET /api/bag/<id>/image`; `GET /api/beans/search?q=…`, `POST /api/beans/extract` (async AI "get info from page"); `GET/POST /api/equipment`, `GET/POST /api/equipment/<id>` plus `/remove`, `/delete`, `/activate`. All behind the auth gate.
- **Full app parity (polish-shotserver-inventory-pages):** the bag create/update payloads accept the full app field set — `kind` (coffee/tea, create-only), the yield anchor (`yieldG`/`yieldRatio`), `rpmPinned`/`rpm`, `equipmentId`, freeze-lifecycle dates (`openedDate`/`storageHint`), and the descriptive attributes carried in the `beanBaseData` JSON blob (the page parses/merges it client-side); `beanBaseId` + `beanBaseData` together set a Bean Base canonical link (the update route propagates it to the bag's shots, like the in-app edit dialog). Equipment payloads accept the puck-prep flags as `puckPrep_<key>` booleans (`PuckPrep::flagKeys`). `/api/beans/search` and `/api/beans/extract` reuse `BeanBaseClient` + `AIManager` (the same backends as the MCP `bean_search` / `bag_extract_details` tools) via the async socket pattern (fired guard + timeout + always-respond).
- **Bag photos follow the product URL.** `GET /api/bag/<id>/image` keys the cache the way `BagCard.qml` does — the canonical id when the bag is linked, else `bag-<rowid>` for a manual bag that has a `link` in its blob (canonical-only meant a manually entered bag showed a placeholder on the web forever while the app card showed its photo). The create route warms the photo from the `bagCreated` handler, where the row id first exists. The update route accepts a **`refreshImage`** boolean: the page sets it when the user edited the URL to a new non-empty value, and the route then calls `BeanBaseClient::refreshBagImage()` — from the *success* handler, since the cache key is shared by every bag on the same canonical bean and must not be evicted for a write that failed. The reply echoes `imageRefreshed` so a caller can tell a refresh it asked for from one it did not get. The client owns the changed/unchanged decision because only the form knows the URL it opened with. **Known gap:** MCP `bag_update` can also change `link` and does not refresh the photo.
- Reads use storage statics on background threads (recipes) or one-shot `inventoryReady`/`*Ready` connections (bags/equipment); mutations always go through the app's storage instances via one-shot signal connections so in-app views refresh exactly as for local edits.
- `POST /api/recipe/<id>/activate` calls `MainController::activateRecipe` — the single activation path shared with the idle pills and MCP; respond only on the matching `recipeActivated(id, success)`.
- Lifecycle guards are storage-enforced and surface as HTTP 409 (delete refused for rows with shots/references).
