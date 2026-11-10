# Design

## Context

`shotserver.cpp` resolves `/legacy/` through `shotserver_webroute.h` and dispatches both page generation and non-page routes. HTML generators are embedded alongside APIs in the split `shotserver_*.cpp` files. Templates under `src/network/webtemplates/` include both legacy page helpers and assets for other purposes; authentication and the Wasm loader must be classified separately. The public route's allowlist is checked before legacy resolution. See the proposal for the behavioral boundary.

## Goals / Non-Goals

**Goals:** Delete interface-exclusive code and evaluate every affected shared path for simplification. Retain endpoints used by Wasm, device migration, MCP, documented home automation and Pocket; remove other endpoints even if external scripts may have used them.

**Non-Goals:** Replace legacy pages with new pages, alter stored data, or remove an externally supported API just because its original consumer was HTML.

## Decisions

1. Remove page dispatch, old bookmark redirects, page generators and helpers whose only consumer is the legacy interface. Remove `/upload`, `/upload/media`, `/api/backup/restore`, and the legacy shot JSON and profile downloads: the user confirmed removal of endpoints used only by the old interface. Requests to removed pages return 404. Preserve the separately used device migration backup endpoints, documented home-automation and Pocket routes, MCP, and shared file-transfer services.
2. Classify callers before deleting any helper, asset, upload handler or endpoint. The HTML generators share files with REST, backup, authentication and media operations, and identify device migration and other independent consumers. The user accepts breaking external callers of endpoints used only by the old interface. Remove interface-exclusive support code, including tests and build references; review the remaining shared routing, upload handling, templates and conditionals for simplification and record decisions with reasons.
3. Any new code, including tests or replacement paths, requires the user's prior verification that its value justifies adding it. Present concrete proposed additions and rationale before writing them; do not infer approval from this proposal. Deletions and simplifications remain in scope. Existing tests may be removed or simplified; propose any new regression tests for review before adding them.
4. Keep the remote deny-by-default policy and request guards intact. The public route must not expand merely because the local interface is removed. Preserve the Wasm error/retry experience without the old fallback, authentication pages and browser icons.

## Removal estimate

A preliminary count of the 14 `ShotServer::generate*Page` function bodies in `src/network/shotserver_*.cpp` is **11,074 physical lines**, including embedded HTML/JavaScript, comments and blank lines. Examples: layout 4,102; settings 1,294; shot list 1,083; beans 989; shot detail 685. This is a candidate inventory, not a confirmed deletion total: restore, debug, upload and other generators need route and consumer review; helper templates and routing could add deletions, and retained code or edits reduce the count. Report the measured gross deletions and net change after implementation, separately from this estimate.

## Risks / Trade-offs

- [Breaking old bookmarks and losing a fallback] → Document the change and use `/` or `/wasm/`; report Wasm loading failures with retry.
- [Deleting a shared endpoint or security helper] → Inventory contracts and consumers, preserve documented external clients, and validate local and remote behavior.
- [Estimating from mixed files overstates savings] → Use the page-function count as an initial candidate only; verify reachability and report actual diff.

## Migration Plan

Ship with updated manual and release notes identifying broken old links. No database migration is needed. Rollback is a prior application version; any compatibility of data written by later releases follows the existing downgrade policy.
