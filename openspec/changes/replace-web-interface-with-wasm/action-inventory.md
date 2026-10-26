# App actions, routes, and evidence inventory

Source audit: 2026-10-22; acceptance reconciliation: 2026-10-26. This is the compact task 15.3 gap ledger; the detailed
signal, popup/Loader, and saved-layout mapping remains in
[`appshell-route-ledger.md`](appshell-route-ledger.md). Source presence, build
success, or a page-render screenshot is not behavioral acceptance.

## Route and workflow ownership

| Workflow | Shared entry/actions and host boundary | Owner | Evidence state |
|---|---|---:|---|
| Shell and global overlays | `qml/AppShell.qml`; native `qml/main.qml`; browser `qml/webclient/Main.qml`; shell-owned navigation and shared global dialogs. | 15.3, 16 | Current name audit: 38 shared AppShell handlers; native handles 40/41 signals (the exception is browser-only `browserBackCancelled`), and browser handles 39/41. The two browser omissions, `quitRequested` and `immediateQuitRequested`, are accepted local policy: Wasm ignores Quit and does not claim it succeeded. Route reachability, return paths, popup close policy, and independent-client behavior remain open. |
| Home and saved layout | `LayoutCatalog` → `LayoutActions` → AppShell destinations or `HomePresetActions`/`MachineControlService`; layout edits use `LayoutService`. | 14, 15.3, 16, 22 | Source dispatcher/catalog and browser Home are present. The existing route ledger maps the 41 catalog actions. Full saved-layout render, gesture, action refusal, GHC/readiness, and return-path checks remain open. |
| Recipes, History, and review | Shared pages emit AppShell requests; `RecipeService`, `HistoryService`, `RecipeContext`, and review/comparison services own data and mutations. | 12, 15.3, 23, 26 | Task 12 Recipe/History route and workflow evidence remains accepted. Only the additional task 26 transitive child-popup/loader closure and any separately owned review/comparison checks remain pending; current graph and Settings screenshots do not establish those items. |
| Profiles and imports | `ProfileSelectorPage`, `ProfileImportPage`, `VisualizerBrowserPage`, and `VisualizerMultiImportPage`; `ProfileService`, `ProfileImportService`, `VisualizerProfileImportService`, and `LocalFileTransfer` own host data and bounded file transfer. | 18, 26 | Source routes and correlated operations are present. Accepted 18.1–18.5 records cover Mac profile/import/favorite actions, both duplicate resolvers, cancellation and real native/browser file selection. See the dated profile acceptance in `evaluation.md`; cross-device gates remain separate. |
| Community and local library | `CommunityBrowserPage` browse/download; `LibraryPanel` add/apply/delete/upload; `LibraryItemCard` preview and selection; correlated `LibraryService` operations. | 18, 26 | `tst_librarysharing` covers focused service behavior, and the evaluation records community-thumbnail transport findings. Double-click now selects an entry and calls the same `applySelected()` path as the toolbar; its live workflow remains unverified. The other local catalog, upload, ownership-gated delete, and community filter/error workflows also lack complete live UI evidence. |
| Settings and maintenance/calibration | Shared `SettingsPage` tabs and destination pages; operation-specific services own writes and hardware effects. | 19–21, 24–26 | Current source mounts Settings and `SettingsLayoutTab` includes `LibraryPanel`. Mac runtime checks opened Connections, Machine, Calibration, Themes, Layout, Screensaver, Shot Upload, AI, MQTT, Language & Access, About, and Debug; this verifies entry/rendering only. Simulator Descaling Start/Back/reopen/Stop/Done and Transport Back/reopen/Stop verify the shared stopped outcome. Remaining tab actions, natural maintenance completion, reconnect during maintenance, calibration behavior, and physical hardware remain open. |

The older source-audit paragraph in `evaluation.md` (2026-10-22, “AppShell and
saved-layout source audit”) says the browser starts on Recipes and has only 20
handlers. It predates the current Home and route work. The current counts above
come from the checked-in AppShell declarations and current native/browser roots.
The route ledger now records that the browser mounts `SettingsPage` and its
`SettingsLayoutTab`, which contains `LibraryPanel`. This corrects source
inventory only; it does not close Settings or layout behavior acceptance.

## Remaining action accounting and maintenance opportunities

| Priority | Finding | Source and impact | Required validation / owner |
|---|---|---|---|
| P2 | Profile duplicate resolution has two page-specific presentations. | `ProfileImportPage.qml` owns a bespoke `duplicateDialog`; the Visualizer pages use `VisualizerDuplicateResolver`. Both reach correlated service resolution, but the dialog/close/error behavior can drift. | Both paths have accepted 18.3 runtime checks (2026-10-24 in `evaluation.md`). Consolidation is a later maintenance opportunity, not an unverified or broken workflow. |

## Popup and saved-layout closure

The detailed ledger remains the source map for all 41 AppShell signals, page
routes, saved-layout action families, and direct popup/Loader declarations.
Current source inspection found no other missing AppShell handler beyond the
two browser Quit omissions, which are accepted local policy rather than gaps.
The LibraryPanel double-click now selects the card and invokes the same
`applySelected()` path as the toolbar, including its existing zone validation,
community download, and correlated apply behavior; paired live runtime
validation remains pending. The other findings above remain open.
`ProfileImportPage`/Visualizer duplicate dialogs, `LibraryPanel` add/delete
dialogs, Community download feedback, root Brew dialog, and page-level async
dialogs still require parent-workflow checks; declarations and service tests
alone do not prove they open, correlate the right request, retain drafts on
failure, or close at the right navigation boundary.

For saved layouts, the 41-action catalog and shared `LayoutActions` dispatcher
are source-present. `navigate:` actions resolve to the AppShell mapping;
`command:` actions route to host services, while browser Quit is ignored by
accepted local policy and must not display a successful-quit state.
`command:loadProfile` expands to a submenu in native and is intentionally
excluded from pickers that cannot present that submenu. Context-dependent
History filters and reserved gesture destinations still need exercised layout
fixtures. The browser must not report a command as successful when a host
service rejects it or when its prerequisites are absent.

Task 15.3 remains open until every row has an owner, a concrete operation or
explicit local policy, and workflow evidence for route entry, action result,
failure/cancel behavior, popup closure, and return path. HTML/API routes remain
unchanged; this inventory does not mark source coverage as accepted behavior.

## Reconciled findings — 2026-10-26

- Library Theme save is present in the shared Add menu and dispatches `addTheme`.
  Accepted 22.1/18.4/7.5 evidence covers the controls, catalog and canonical
  thumbnails; saved appearance is recorded in the latest evaluation. The earlier
  missing-affordance finding is resolved.
- Actual native/browser ZIP selection, cancellation, invalid-file feedback and
  restore are accepted under 23; see `data-update-validation.md`. The missing
  automation file-chooser event was not a product defect.
- AI archive browsing/export closed the remaining concrete 8.2 gap. The shared
  Debug dialog and ConversationSession operations passed native/Edge browsing,
  byte-identical Text/JSON downloads and final modal/focus checks; see the
  2026-10-26 archive acceptance in `evaluation.md`. Qt's Wasm input-name issue
  remains under the separate screen-reader gate.

The historical route table still contains dated pending notes. Use each owner's
latest acceptance entry in `evaluation.md` rather than treating those old notes
as newly discovered defects. Complete transitive widget/popup accounting remains
under 15.3/22.3/26.5; this reconciliation does not close their broader scope.
