# App actions, routes, and evidence inventory

Source audit: 2026-10-22; acceptance reconciliation: 2026-10-27. This is the compact task 15.3 gap ledger; the detailed
signal, popup/Loader, and saved-layout mapping remains in
[`appshell-route-ledger.md`](appshell-route-ledger.md). Source presence, build
success, or a page-render screenshot is not behavioral acceptance.

## Route and workflow ownership

| Workflow | Shared entry/actions and host boundary | Owner | Evidence state |
|---|---|---:|---|
| Shell and global overlays | `qml/AppShell.qml`; native `qml/main.qml`; browser `qml/webclient/Main.qml`; shell-owned navigation and shared global dialogs. | 15.3, 16 | AppShell declares 40 signals after #2117 removed the community-library route. Browser-only cancellation and the two intentionally ignored Wasm Quit signals remain platform policies. Mac entry and return paths are accepted under 16.2; Mac browser-history closure is accepted under 16.4. |
| Home and saved layout | `LayoutCatalog` → `LayoutActions` → AppShell destinations or `HomePresetActions`/`MachineControlService`; layout edits use `LayoutService`. | 14, 15.3, 16, 22 | Tasks 14.1–14.4 accept paired Home rendering and core operations; the route ledger maps the catalog action families. Mac optional-widget/gesture closure is accepted under 22.3. |
| Recipes, History, and review | Shared pages emit AppShell requests; `RecipeService`, `HistoryService`, `RecipeContext`, and review/comparison services own data and mutations. | 12, 15.3, 23, 26 | Task 12 Recipe/History route and workflow evidence remains accepted. Mac transitive child and review/comparison closure is accepted under 26.2/26.3/26.5, using their workflow evidence. |
| Beans and equipment | Bag creation, edits, lifecycle, extraction and links use `BagService`/`BeanUiService`; equipment inventory, activation, editing, removal and calibration use `EquipmentService`. | 11, 13, 26.2 | Paired parent workflows and canonical service/storage checks are accepted under 11, 13 and 26.2; broader common-component and device checks remain 26.5/28.3. |
| Profiles and imports | `ProfileSelectorPage`, `ProfileImportPage`, `VisualizerBrowserPage`, and `VisualizerMultiImportPage`; `ProfileService`, `ProfileImportService`, `VisualizerProfileImportService`, and `LocalFileTransfer` own host data and bounded file transfer. | 18, 26 | Source routes and correlated operations are present. Accepted 18.1–18.5 records cover Mac profile/import/favorite actions, both duplicate resolvers, cancellation and real native/browser file selection. See the dated profile acceptance in `evaluation.md`; cross-device gates remain separate. |
| Local library | `LibraryPanel` add/apply/delete; `LibraryItemCard` preview and selection; correlated `LibraryService` operations. | 18, 26 | Main's #2117 removed the community widget/theme service and page; the local library remains. Historical community evidence does not describe current routes. Local catalog and apply behavior retain their existing parent-workflow checks. |
| Settings and maintenance/calibration | Shared `SettingsPage` tabs and destination pages; operation-specific services own writes and hardware effects. | 19–21, 24–26 | Tasks 19–21 accept the recorded Mac settings, maintenance and calibration checks, including simulator completion and reconnect/refusal fixtures. Physical hardware and broader device/accessibility checks remain separate gates. |
| Operations and screensavers | Machine start/stop and adjustments use `MachineControlService`/`SteamService`; graph data uses the shared live graph; screensaver selection/media and host sleep state use `ScreensaverService`. | 14, 17, 25, 26 | Mac operation and sleep checks are accepted under 14, 17 and 25; each conditional component and physical-device check remains with its owner. |
| Support, data and integrations | AI conversations, downloads, language, logs, migration, backup, updates and integrations use their shared domain services and file-transfer helpers. | 8, 20–21, 23–24, 26 | Recorded Mac checks and canonical failure fixtures are assigned in those groups; actual screen-reader, network and device gates remain 2–4/28.3. |

The older source-audit paragraph in `evaluation.md` (2026-10-22, “AppShell and
saved-layout source audit”) says the browser starts on Recipes and has only 20
handlers. It predates the current Home and route work. The current count above
comes from the checked-in AppShell declarations.
The route ledger now records that the browser mounts `SettingsPage` and its
`SettingsLayoutTab`, which contains `LibraryPanel`. Settings and layout runtime acceptance is recorded under 20.1/22.2–22.4.

## Remaining action accounting and maintenance opportunities

| Priority | Finding | Source and impact | Required validation / owner |
|---|---|---|---|
| P2 | Profile duplicate resolution has two page-specific presentations. | `ProfileImportPage.qml` owns a bespoke `duplicateDialog`; the Visualizer pages use `VisualizerDuplicateResolver`. Both reach correlated service resolution, but the dialog/close/error behavior can drift. | Both paths have accepted 18.3 runtime checks (2026-10-24 in `evaluation.md`). Consolidation is a later maintenance opportunity, not an unverified or broken workflow. |

## Popup and saved-layout closure

The detailed ledger remains the source map for the current AppShell signals, page
routes, saved-layout action families, and direct popup/Loader declarations.
Current source inspection found no other missing AppShell handler beyond the
two browser Quit omissions, which are accepted local policy rather than gaps.
The LibraryPanel double-click now selects the card and invokes the same
`applySelected()` path as the toolbar, including its existing zone validation
and correlated apply behavior. Paired native/Edge double-clicks restored a
temporarily changed clock color from an existing matching layout; see
`evaluation.md`. The duplicate-dialog consolidation above remains a later maintenance opportunity.
`ProfileImportPage`/Visualizer duplicate dialogs, Library dialogs, root Brew
and page-level async dialogs have accepted Mac parent-workflow checks under
18.3/17.2/22/26.2–26.5. Device and actual reader checks remain separate.

For saved layouts, the catalog and shared `LayoutActions` dispatcher
are source-present. `navigate:` actions resolve to the AppShell mapping;
`command:` actions route to host services, while browser Quit is ignored by
accepted local policy and must not display a successful-quit state.
`command:loadProfile` expands to a submenu in native and is intentionally
excluded from pickers that cannot present that submenu. Context-dependent History filters and reserved gestures have Mac runtime and
existing fixture coverage under 22.3/22.4. The browser must not report a command as successful when a host
service rejects it or when its prerequisites are absent.

The 2026-10-27 cross-check accounts for all 40 AppShell signals and 40 catalog
entries (39 picker choices plus the legacy scan alias), and maps all 38 named dialog/popup/loader
components to parent owners. Files emitting AppShell requests, calling services,
or declaring named popups or loaders have file-level owners in `design.md`;
these sets overlap. Dynamic and conditional behavior is
not inferred from the source scan. Task 15.3 closes the inventory and owner
mapping; Mac gesture/common-popup closure is accepted under 22.3/26.5; device behavior
remains under 28.3. HTML/API routes remain unchanged.

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
as newly discovered defects. Mac widget/popup acceptance is now recorded under 22.3/26.5; actual readers,
remote devices and physical-machine acceptance remain in their separate gates.
