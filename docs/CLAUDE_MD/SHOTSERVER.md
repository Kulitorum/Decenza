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
- `shotserver_bags.cpp` — coffee-bag REST + `/beans` page (add-recipes)
- `shotserver_equipment.cpp` — equipment-package REST + `/equipment` page (add-recipes)

The layout editor web UI is served as inline HTML/JS from `shotserver_layout.cpp`.

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

- Endpoints: `GET/POST /api/recipes`, `POST /api/recipes/from-shot/<shotId>`, `GET/POST /api/recipe/<id>` plus `/clone`, `/archive` (body: `restore`/`delete`), `/activate`; `GET/POST /api/bags`, `GET/POST /api/bag/<id>` plus `/finish`, `/delete`, `/activate`; `GET/POST /api/equipment`, `GET/POST /api/equipment/<id>` plus `/remove`, `/delete`, `/activate`. All behind the auth gate.
- Reads use storage statics on background threads (recipes) or one-shot `inventoryReady`/`*Ready` connections (bags/equipment); mutations always go through the app's storage instances via one-shot signal connections so in-app views refresh exactly as for local edits.
- `POST /api/recipe/<id>/activate` calls `MainController::activateRecipe` — the single activation path shared with the idle pills and MCP; respond only on the matching `recipeActivated(id, success)`.
- Lifecycle guards are storage-enforced and surface as HTTP 409 (delete refused for rows with shots/references).
