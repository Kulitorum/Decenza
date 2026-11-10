# Proposal

## Why

The WebAssembly client now provides the shared application interface, while the separate HTML interface still carries its own pages, routing and maintenance burden. Remove that duplicate interface and its fallback so the browser has one supported UI.

## What Changes

- **BREAKING** Remove the legacy HTML pages, their assets and `/legacy/` namespace, including old page bookmarks and redirects into it. Requests for removed pages return 404; users open the current interface at `/` or `/wasm/`.
- Keep the current browser client, authentication, remote access, session, MCP, device-migration, documented home-automation and Pocket behavior. Delete code and endpoints not used by Wasm or those retained integrations, including its upload, restore and shot-file URLs, and evaluate formerly shared code for simplification. External callers of removed endpoints will break.
- Remove legacy-only UI code and references; update tests, developer documentation and the user manual. The Wasm loading failure no longer offers an HTML fallback.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `wasm-browser-client`: remove legacy pages, bookmark compatibility and fallback requirements; retain current client and API contracts.
- `remote-web-access`: remove the LAN legacy exception while preserving the remote access boundary.
- `remote-ui-session`: remove the legacy HTML workflow requirement while preserving shared preparation behavior.
- `recipe-model`: remove requirements specific to the deleted HTML form and HTTP recipe interface while preserving recipe data.
- `shotserver-bags`, `shotserver-equipment`, `shotserver-recipes`: retire the legacy management pages and their unused HTTP APIs.
- `layout-editor-reordering`, `layout-widget-instance-config`: retire requirements specific to the legacy HTML layout editor.
- `grind-value-entry`, `grinder-ugs-calibration`, `decent-shot-upload`: retire requirements specific to the removed HTML forms and endpoints.
- `plan-widgets`, `layout-widget-gesture-overrides`, `active-name-uniqueness`, `recipe-activation`, `grind-rpm-pairing`, `layout-clock-widget`, `frame-transition-reason`, `grind-step-derivation`, `visualizer-auto-update`, `custom-widget-history-actions`, `equipment-inventory-view`: retain shared behavior while removing references to legacy HTML and REST surfaces.

## Impact

ShotServer routing and inline HTML templates, route tests, shared-client integration checks, developer documentation and the wiki manual are affected. Old HTML page URLs and legacy-only upload, restore and shot-file endpoints stop working; stored data and independently used interfaces remain intact.
