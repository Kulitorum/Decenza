# AppShell, popup, and saved-layout route ledger

Source audit refreshed: 2026-10-26. This records routing and service wiring, not runtime parity. After main's #2117 removal of the community widget library, `qml/AppShell.qml` declares 40 signals; browser leave-cancellation is browser-only and Wasm intentionally ignores both Quit signals. The community signal and saved-layout action no longer exist. The remaining rows record owners, not proof of behavior or complete transitive-popup coverage.

## Shared shell ownership update (2026-10-23)

`SharedAppNavigation.qml` now owns the single page catalog and ordinary AppShell routing for both clients. The root/window handler names in the historical table below describe the pre-extraction wiring; their shared equivalents live in that component. Browser callbacks retain local history/tracked stack mutations, while native retains device/window lifecycle. Current paired Home/Settings/Equipment, Back/Forward/Escape and popup-cleanup evidence is in `navigation-language-validation.md`. Domain workflow holds in the table are unchanged; sharing the router does not accept their runtime behavior.

## AppShell destinations

| Signal | Native handler and destination | Browser handler and destination | Owner | Host/service operation or open dependency |
|---|---|---|---:|---|
| `backRequested` | `root.goBack()` | `window.goBack()` | 16 | Local stack policy; paired Home, Settings and domain returns are recorded in 16.1 and the workflow evidence. Complete browser-history coverage remains 16.4. |
| `browserBackCancelled` | — | Cancel the pending browser pop | 16.4 | Local asynchronous leave-guard result; browser-only, with no host operation. |
| `idleFromScreensaverRequested` | `root.goToIdleFromScreensaver()` | Observe host sleep state | 25 | Host-owned sleep/wake revision (2026-10-25): both clients invoke MachineControlService; ScreensaverService publishes host state. The obsolete screensaverRequested signal and local-only dismissal were removed. |
| `profileEditorRequested` | `root.goToProfileEditor()` | `window.openProfileEditor()` from current profile type | 18 | `ProfileService` + portable editor draft/save/try operations. A temporary custom shortcut opened the current D-Flow editor in both clients; Edge reload preserved it, and the layout was restored. |
| `profileDraftEditorRequested(filename,type)` | `root.goToProfileEditor(filename,type)` | `window.openProfileEditor(filename,type)` | 18 | `ProfileService` detail and draft operations. |
| `newProfileDraftRequested(type,title)` | `root.goToNewProfileDraft(type,title)` | `window.openNewProfileDraft(type,title)` | 18 | Local `ProfileEditorDraft`; host save/try only on explicit action. |
| `profileSelectorRequested` | `root.goToProfileSelector()` | Push `ProfileSelectorPage` | 18 | `ProfileService`; Mac selector, editor routes and draft cancellation are covered by the 18.1 evidence in `evaluation.md`. |
| `profileImportRequested` | `root.goToProfileImport()` | Push `ProfileImportPage` | 18 | ProfileImportService + requester-local file transfer; Mac native/browser folder selection, import and duplicate cancellation are covered by 18.3. |
| `visualizerBrowserRequested` | `root.goToVisualizerBrowser()` | Push `VisualizerBrowserPage` | 18 | VisualizerProfileImportService; Mac shared-code fetch/import and duplicate cancellation are covered by 18.3. |
| `descalingRequested` | `root.goToDescaling()` | Push `DescalingPage` | 19 | `MaintenanceService`; simulator Start/Back/reopen/Stop/Done, weighted progress, natural completion and browser rejoin are Mac-verified. Error/connection-loss policy is fixture-covered; physical descale remains unverified. |
| `transportRequested` | `root.goToTransport()` | Push `TransportPage` | 19 | `MaintenanceService`; simulator Back/reopen/explicit Stop/Done, natural completion and shared stopped outcome are Mac-verified. Connection-loss policy is fixture-covered; physical draining remains unverified. |
| `sensorCalibrationRequested(sensor)` | `root.goToSensorCalibration(sensor)` | Push `SensorCalibrationPage` | 19 | `SensorCalibrationService` plus `ProfileService` test-profile activation/restoration; Mac entry, refusal, confirmation-cancel, Back and disconnect/reconnect, plus fixture bounds/CAS/failure checks are verified (19.3). External-instrument calibration remains unverified. |
| `brewSettingsRequested` | `root.openBrewSettings()` | `globalBrewDialog.open()` | 17 | Shared `BrewDialog`; `MachineControlService`, `SteamService`, `RecipeService`, `RecipeContext`, `ProfileService`, `EquipmentService`, and `BagService`. Task 17.2 accepts the recorded Mac dialog, edits, conflicts and capture checks; mobile input and full child-component coverage remain separate. |
| `quitRequested` | Confirmation or firmware-flash warning | — | 16 | Accepted Wasm local policy: intentionally ignored; retain the browser context and do not present a successful quit state. |
| `immediateQuitRequested` | `Qt.quit()` | — | 16 | Accepted Wasm local policy: intentionally ignored; retain the browser context. |
| `espressoRequested` | `root.goToEspresso()` | `showOperationPage(EspressoPage)` | 17 | `MachineControlService`, `SteamService`, `LiveShotGraph`; Mac simulator join, graph continuity, stop-at-weight save, paired review and view selection accepted under 17.1/17.3. Connected PORTAL and physical-device checks remain 28.3. |
| `steamRequested` | `root.goToSteam()` | `showOperationPage(SteamPage)` | 17 | `SteamService`, `MachineControlService`, `RecipeContext`; Mac Stop/Purge, reload and timeout reset accepted under 17.3; nonzero simulator capture accepted under 2.35/2.41. |
| `hotWaterRequested` | `root.goToHotWater()` | `showOperationPage(HotWaterPage)` | 17 | `MachineControlService`, `RecipeContext`, `SteamService`; Mac active join, explicit Stop, natural completion and independent return accepted under 17.3. |
| `flushRequested` | `root.goToFlush()` | `showOperationPage(FlushPage)` | 17 | `MachineControlService`, `RecipeContext`, `SteamService`; Mac active reload, explicit Stop, natural completion and independent return accepted under 17.3. |
| `settingsRequested(tabId)` | `root.goToSettings(tabId)` | Push `SettingsPage` | 20 | Paired Mac checks opened all 13 tabs and all 272 available search results; search focus and return are accepted under 20.1. Device-specific input remains separate. |
| `dflowEditorRequested` | `root.goToDFlowEditor()` | `window.openProfileEditor("","dflow")` | 18 | `ProfileService` and the shared draft model. A temporary custom-layout shortcut opened the editor and returned Home in native and Edge; the layout was restored. |
| `recipesRequested` | `root.goToRecipes()` | Push `RecipesPage` | 12 | `RecipeService`, `ProfileService`, `BagService`, `RecipeContext`; task 12 route/workflow evidence is accepted; only task 26 transitive child closure remains open. |
| `recipeWizardRequested(mode,options)` | `root.goToRecipeWizard(mode,options)` | Push `RecipeWizardPage` with options | 12 | `RecipeService`, `ProfileService`, `RecipeContext`, `HistoryService`, `BagService`, `EquipmentService`; task 12 evidence is accepted; task 26 loader/child-popup closure remains open. |
| `shotHistoryRequested(filter)` | `root.goToShotHistory(filter)` | Refilter current History or push `ShotHistoryPage` | 12, 23, 26.3 | `HistoryService`; Mac route and child workflow accepted under 12/26.3; filters use Recipe/Bag/Profile/RecipeContext IDs and labels. |
| `shotDetailRequested(id,ids)` | `root.goToShotDetail(id,ids)` | Push `PostShotReviewPage` | 12, 23, 26.3 | `HistoryService`, `ShotReviewActions`, `ProfileService`, `RecipeService`; Mac child, metadata/Undo and action evidence accepted under 26.3. |
| `shotComparisonRequested` | `root.goToShotComparison()` | Push `ShotComparisonPage` | 12, 23, 26.3 | `ComparisonService`; Mac selection, graph inspection, preferences and context accepted under 26.3. |
| `postShotReviewRequested(id,autoClose)` | `root.goToPostShotReview(id,autoClose)` | `window.openPostShotReview(id,autoClose)` | 12, 17.3, 23, 26.3 | `HistoryService` + `ShotReviewActions`; Mac operation-completion, latest-shot and app-start entry evidence accepted under 17.3/26.3. |
| `profileInfoRequested(filename,title)` | `root.goToProfileInfo(filename,title)` | Push `ProfileInfoPage` | 18 | `ProfileService`; native and browser Recipe → Profile Info → Back returned to the same Recipe on 2026-10-26. Other information/editor child-dialog closure remains open. |
| `beanInfoRequested` | `root.goToBeanInfo()` | Push `BeanInfoPage` | 13 | `BeanUiService`, `BeanSearchService`, `BagService`, `HistoryService`; paired Home → Beans → details → Back checks passed on 2026-10-26. Broader link/recovery and child-dialog closure remains 26.5. |
| `equipmentRequested` | `root.goToEquipment()` | Push `EquipmentPage` | 11, 26 | `EquipmentService`; shared editing, conflict and lifecycle checks are accepted under 11 and 26.2. A fresh read-only tablet-hosted browser also opened and cancelled an edit form. |
| `autoFavoritesRequested` | `root.goToAutoFavorites()` | Push `AutoFavoritesPage` | 18 | `HistoryService`; list, details, activation and concurrency are accepted under 18.2. |
| `autoFavoriteInfoRequested(options)` | `root.goToAutoFavoriteInfo(options)` | Push `AutoFavoriteInfoPage` | 18 | `HistoryService`; 18.2 accepts the Mac workflow, and copied-link reconstruction is recorded in `evaluation.md`. |
| `visualizerMultiImportRequested` | `root.goToVisualizerMultiImport()` | Push `VisualizerMultiImportPage` | 18 | VisualizerProfileImportService; paired Mac entry and Back returned to Visualizer on 2026-10-26. Validation and cancellation are recorded under 18.3. |
| `flowCalibrationRequested` | `root.goToFlowCalibration()` | Push `FlowCalibrationPage` | 19 | `FlowCalibrationService`; Mac browser/native render, draft-discard Back, reset/save placement and fixture save/refusal checks are verified (19.3). Physical flow-standard verification remains unverified. |
| `aiSettingsRequested` | `root.goToAISettings()` | Push `AISettingsPage` | 21 | AISettingsService + Preferences + ConversationSession; the paired Mac custom shortcut now opens and returns through a shared Back bar. Unsent drafts are guarded on both clients and browser Back. No provider call was made. |
| `stringBrowserRequested` | `root.goToStringBrowser()` | Push `StringBrowserPage` | 24 | `LanguageService`; Mac language editing/refusal and browser query/filter reload are covered by task 24.1 and `navigation-language-validation.md`. Device and screen-reader gates remain separate. |
| `addLanguageRequested` | `root.goToAddLanguage()` | Push `AddLanguagePage` | 24 | `LanguageService`; Mac native and Edge entry/Back return to Language settings are verified on 2026-10-26. The catalog was exhausted, so adding or downloading a language was not exercised. |
| `dismissRequested` | Pop or return to Idle by `OperationNavigationPolicy` | `window.dismissCurrentPage()` | 16 | Local stack policy; paired operation completion/return and covered-popup checks are recorded under 17.3 and 16.1. Remaining child coverage is 26.5. |
| `completionSuspendRequested` | Suspend operation completion overlay | Suspend shared completion overlay | 16 | `OperationCompletionState`; dialog/operation ordering uses focused state tests and the accepted 16.3 lifecycle evidence. |
| `completionFinishRequested` | Finish operation completion overlay | Finish shared completion overlay | 16 | `OperationCompletionState`; completion and independent-client lifecycle are accepted under 16.3/17.3. |

All destination handlers are source-wired. The two Quit signals are ignored under
accepted Wasm local policy. The paired domain evidence above, plus the current
AI shortcut correction and fresh import/calibration/editor returns in
`evaluation.md`, completes the Mac shell entry/return scope of 16.2. Browser
history, transitive child components and device checks remain 16.4/26.5/28.3.

## Browser root, popup, loader, and operation routes

| Shell route or component | Current source behavior | Service/owner | Closure still required |
|---|---|---|---|
| Startup and service readiness | `qml/webclient/Main.qml` waits for session and the listed QtRO facades; the default stack root is the shared IdlePage. Optional `initialRoute` pushes the registered shared pages, including maintenance, calibration, language and screensaver destinations, or opens brew. | 16; all route owners | Settings and import workflows have paired Mac evidence under 20.1 and 18.3. Full reconnect and deep-link/history behavior remain 3.6 and 16.4. |
| Debug archive dialog | `SettingsDebugTab` opens shared `ConversationArchiveDialog`; Escape/Close returns focus to Browse. | 8.2, 24; ConversationSession + canonical AIConversation serializer/file transfer | Native/Edge search, transcript, byte-identical Text/JSON downloads and modal/focus are accepted in the 2026-10-26 evaluation. Qt Wasm input-name/screen-reader limits remain under 2.9/28.3. |
| Root `BrewDialog` | One shared modal is declared at `qml/webclient/Main.qml`; opened by `brewSettingsRequested`, the `brew` initial route and deferred `pendingBrewDialog`. | 17; MachineControl/Steam/Recipe/Profile/Equipment/Bag services | Android-hosted Wasm opened recipe suggestions, ratio help and the grinder picker, then cancelled without changing the shot plan. The remaining nested pickers, async mutation errors, focus and operation-suspend behavior still need end-to-end coverage. |
| Shot-background `Loader` | `LastShotChartRenderer` is loaded only when `Settings.theme.backgroundSource === "shot"`; `CurrentPageBackground` is shared with the page stack. | 22, 26; LiveShotGraph/History/Profile/Recipe projections | Theme photo/pattern/shot source transitions, cache/error behavior and page/popup layering remain part of 22/25/26 acceptance. |
| Operation completion and navigation | `OperationCompletionOverlay` is global. Phase changes call `showOperationPage`; operation navigation may replace the current operation page, and `PopupCloser.closeAllUnder(current)` runs before pushing an operation page over a different current page. | 16, 17; Steam/MachineControl/LiveShotGraph | Verify child popups close or remain appropriately across each operation transition, stop, review, reconnect and independent navigation. |
| Espresso / Steam / Hot Water / Flush dialogs and loaders | `EspressoPage` has a Loader; Steam declares three `DecenzaDialog`s; HotWater and Flush declare two each. Shared controls also own selection/suggestion popups. | 17, 26; MachineControl/Steam/RecipeContext | Validate every dialog’s pending/refusal result and close policy with the mounted page; source presence alone is not closure. |
| Recipes / Wizard / History / Review | Recipes declares a `DecenzaDialog`; RecipeWizard has two Loaders; History has three `DecenzaDialog`s; PostShotReview has two. Graph/readout/selection components add nested overlays. | 12, 23, 26; Recipe/Profile/Bag/History/ShotReview/Comparison | Parent/child Mac workflows are accepted under 26.2–26.3. Complete common-component inventory and device/screen-reader gates remain 26.5/28.3. |
| Profiles and favorites | ProfileSelector has two `DecenzaDialog`s; AutoFavorites has one; ProfileEditor has six plus a Loader; DFlow editor has three. | 18, 26; ProfileService | Create/import/favorite/editor operations, conflicts and draft checks are accepted under 18.1–18.5. Common-component closure remains 26.5. |
| Beans and Equipment | The pages use shared dialog/search components rather than a single page-level modal declaration; `BeanBaseSearchBar` and `SharedSuggestionField` own popups, and shared equipment controls own selection/edit dialogs. | 11, 13, 26; BeanUi/BeanSearch/Bag/Equipment services | Parent and child Mac workflows are accepted under 26.2; actual screen-reader/device checks remain 2.9/28.3. |
| Shared component overlays | `SharedDecenzaDialog`, `SharedSelectionDialog`, `SharedSuggestionField`, `SharedSuggestionFieldChrome`, `PopupCloser`, `OnDemandLoader`, `HoverTip`, graph inspect/readout overlays and image loaders are part of the reachable source graph. | 26; domain service varies | Reuse the completed 26.1–26.4 audits and workflow checks; remaining parent-workflow closure is 26.5. |

This is a source-level inventory of the browser root and direct page declarations, not a claim that every transitive popup/Loader has been validated. Each row stays open until its parent page has an accepted workflow record.

On 2026-10-26 a source cross-check found 217 direct Loader, OnDemandLoader,
DecenzaDialog, SharedDecenzaDialog, Dialog and Popup declarations in 101 QML
files; every containing file has an owner in the design inventory. This does
not enumerate custom popup types or validate their entry and return paths.
An additional filename/caller cross-check identified 38 named Dialog, Popup or
Loader components. Most are instantiated directly by their parent workflows;
`UgsGuideDialog` is loaded dynamically by `GrindCalibrationSection` and belongs
to the Equipment/Review checks (11/23/26). This is source accounting, not proof
that every custom popup or dynamically chosen component has been exercised.
An expanded QML source scan maps AppShell callers, service callers, and files
declaring named dialogs/popups or Loaders to rows in the design inventory.
These sets overlap and the scan is syntactic: it does not prove dynamic dispatch
or runtime behavior. The dedicated AI page now adds a guarded exit dialog and
AppShell Back caller to those sets.

Named child dialogs and loaders follow these parent-workflow owners:

| Components | Parent operations and validation owner |
|---|---|
| EquipmentInfoDialog, SharedEquipmentDialog, SwitchEquipmentDialog, ChangeBeansDialog, LinkBeanBaseDialog, BeanBaseDetailsPopup, BeanSuggestionsDialog, UgsGuideDialog | EquipmentService, BeanUiService, BagService and their parent Recipe/Review calls; 11–13, 23, 26.2–26.3. The UGS guide is local presentation. |
| BrewDialog, TempPickerDialog, RatioPresetDialog, GrindPickerDialog | MachineControlService, RecipeContext and parent editor services; 17.2, 22.3, 26. |
| ProfileKnowledgeDialog, ProfileFavoritesOrderDialog, ProfilePreviewPopup, GraphOptionsDialog, ShotAnalysisDialog, DatePickerDialog | Profile, History, graph and parent draft/upload services; 18, 21, 23, 26. |
| SettingsSearchDialog, DeviceMigrationDialog, ConversationArchiveDialog, BackgroundPickerDialog, ThemeAdvancedDialog | Settings search, data migration, ConversationSession, ScreensaverService and ThemeManagementService; 20–25. |
| De1CommunicationErrorDialog, McpConfirmDialog, ProfileRefusedDialog, CrashReportDialog | GlobalSupportDialogs and the corresponding machine, MCP, profile or diagnostics services; 16.3 and 24. |
| CustomEditorPopup, ReadoutOptionsPopup, SleepEditorPopup, ZoneOptionsPopup, ScreensaverEditorPopup | LayoutService and parent screensaver preferences; 22 and 25. |
| DecenzaDialog, SharedDecenzaDialog, SelectionDialog, SharedSelectionDialog, UnsavedChangesDialog, OnDemandLoader | Shared presentation/lifecycle wrappers; host operation belongs to the caller, with navigation and child-workflow validation under 16 and 26. |

The 2026-10-26 paired Profile Preview → More Info → Back and native/browser
UGS guide checks are recorded in `evaluation.md`; the Android-hosted bean
details, date picker, suggestions and recipe knowledge checks supplement their
existing parent evidence. These checks do not accept the entire component group.

## Saved-layout action routes

`LayoutActions.execute()` is shared source, and `LayoutCatalog` defines the action IDs. The browser mounts IdlePage and the saved Home layout. `SettingsPage` includes `SettingsLayoutTab`, which contains the layout editor and `LibraryPanel`. `navigate:` entries resolve to the AppShell rows above; `command:` entries invoke host services as shown here.

| Saved action IDs | Current dispatcher/host operation | Browser route status | Owner / evidence |
|---|---|---|---|
| `navigate:settings`, `navigate:profiles`, `navigate:profileEditor`, `navigate:dflowEditor`, `navigate:recipeList`, `navigate:equipment`, `navigate:descaling`, `navigate:ai`, `navigate:visualizer`, `navigate:autofavorites`, `navigate:steam`, `navigate:hotwater`, `navigate:flush`, `navigate:beaninfo`, `navigate:espresso`, `navigate:flowCalibration`, `navigate:profileImport` | `AppShell` destination signals; see full route table | Mac destination entry and return paths are accepted under 16.2. Exhaustive widget/gesture coverage remains 22.3. | 11–21, 22.2–22.4, 16.2 |
| `navigate:history`, `historyRecipe`, `historyBean`, `historyBag`, `historyProfile` | `AppShell.shotHistoryRequested`; context comes from RecipeService, RecipeContext, BagService or ProfileService. | Browser custom-widget checks opened the active recipe's, bean's, bag's and profile's filtered shots; Back returned Home, and the profile filter survived reload on 2026-10-26. | 12, 22.3; missing-record behavior and full widget coverage remain open. |
| `navigate:shotReview` | Reads `HistoryService.lastSavedShotId`, emits `postShotReviewRequested(id,false)`. | A temporary custom widget opened the same latest shot in native and Edge; Edge reload restored it. The layout was restored. | 12, 17.3, 23; operation replacement remains covered by 17.3. |
| `command:sleep`, `wake`, `idle`, `disconnectDE1`, `scanScale` / legacy `scanDE1`, `toggleCharging`, `tempToggleSteam`, `previousProfile` | `HomePresetActions` → `MachineControlService` (sleep/wake, idle, disconnect, scan, charging, steam heater and previous profile operations). | Simulator Sleep/browser Wake and native/browser temporary steam-heater toggles are Mac-verified; the other commands remain unverified. | 14.2, 16.2, 22.3; validate context, readiness, refusal and no replay for remaining commands. |
| `command:startEspresso`, `startSteam`, `startHotWater`, `startFlush` | `HomePresetActions` → `MachineControlService` select/start operations, with host phase/readiness/GHC checks. | Four-operation Mac simulator lifecycle accepted under 17.3 using the dated evidence in `evaluation.md` and existing canonical no-replay/context tests. | 14.2/17.3 accepted; exhaustive custom/gesture actions remain 22.3, physical/remote-device gates remain separate. |
| `command:tare` | `LayoutActions` → `SteamService.request("tare", ..., controlContextId)` when the service is ready. | Home scale widget tared the host simulator to zero; native and browser readouts followed. | 14.2, 17.1, 22.3; remaining widget contexts retain their parent checks. |
| `command:brewSettings` | Emits `brewSettingsRequested`; root opens BrewDialog. | Browser handler opens the same shared dialog. | 17.2, 22.3; nested popup and host-write checks remain open. |
| `command:uploadVisualizer` | `ShotReviewActions.uploadShot(lastSavedShotId)` or logs when absent. | Shared Home caller uses ShotReviewActions; acceptance remains open. | 21.2, 22.3; account/progress/refusal/retry checks remain open. |
| `command:loadProfile:<filename>` | `HomePresetActions.loadProfile(filename)` → `ProfileService.loadProfile`. Shared `CustomEditorPopup` expands the action to a profile submenu in native and Wasm. `ReadoutOptionsPopup` and the legacy HTML editor omit it because they lack that submenu. | The submenu opened and cancelled in both Mac clients; no profile was loaded. | 18.1, 22.3–22.4; actual action and refusal remain open. |
| `command:quit`, `command:quitConfirm` | AppShell quit path / firmware-flash warning. | Accepted Wasm local policy: ignored; browser context remains open and no successful-quit state is shown. | 16.2, 22.3; no browser quit action is required. |
| `togglePreset:{espresso,steam,hotwater,flush,beans,recipes,equipment}` | `LayoutActions` calls `IdlePage.togglePresetFunction(target)`; custom and built-in widgets share this dispatcher. | Shared IdlePage and layout root are mounted; acceptance remains open. | 14.3, 22.3–22.4; preserve selected-preset state and action reachability. |
| `longPressAction`, `doubleclickAction`, `command:loadProfile` submenu; `togglePreset`/reserved gesture destinations | Shared `LayoutActions`/`LayoutCatalog` and widget option schema provide picker labels, filters and default/reserved destinations. | Shared Home and Settings layout editor are mounted. | 22.3–22.4; validate all widget contexts, saved values, reserved gestures and accessibility. |

`command:scanDE1` is a legacy catalog alias of `command:scanScale`; both use the existing scan dispatch. The catalog’s `None`, gesture overrides and submenu exclusion are picker policies, not host operations, and therefore need their own layout-editor checks.

The current catalog-to-dispatch source cross-check covers 39 navigation and
command entries. All have a dispatcher; `loadProfile` is the one parameterized
submenu case rather than a literal switch case. This does not replace the
remaining live gesture and context checks.

Mac layout editor acceptance for 22.2 is recorded in [the evaluation](evaluation.md); per-widget action and editor validation remains assigned to 22.3–22.4.

For 22.3, the paired Home/layout checks already render the saved default's Page
Title, Spacer, Separator, Temperature, Steam Temperature, Water Level, Scale
Weight, Machine Status, Clock, Battery, Recipes, Beans, Steam, Hot Water, Shot
Plan, Grind, Weather, Sleep, Flush, History, Equipment, Profiles and Settings
widgets. The dated evaluation also records browser Quit in both sizes, Custom's
profile-submenu and contextual History actions, paired Dose/Milk readouts and
Ratio/Temp picker entry and dismissal, Grind manual-entry cancellation, Last Shot and Scale Battery rendering,
compact and full Last Shot and Favorites navigation, and
Profile Name in both clients. Compact and full Mini GHC rendered in both;
its compact browser Flush started simulator flushes, and after a shared-widget
fix its always-visible Stop control stopped simulated steam while the start
controls were disabled. Compact and expanded Flip Clock widgets expose named
buttons in both clients; compact native and expanded browser activation each
put the simulator to sleep and displayed the selected screensaver.
These checks do not accept all options, gestures or compact forms.
Other optional widgets and unexercised actions stay open; reuse this coverage
rather than repeating the default-layout rendering pass.

## Operation control/dependency coverage — 2026-10-26 (17.4)

| Shared surface | Controls and canonical host owner | Evidence / explicit hold |
|---|---|---|
| Common preparation and operation entry | Home selection/start and BrewDialog dose, ratio/yield, temperature, grind/RPM, flow, vessel/pitcher and capture controls; PreparationService, MachineControlService, SteamService and RecipeContext. | 14.1–14.4/17.2; one host capture owner, contextual readiness/GHC/refusal and no replay. |
| Espresso | Stop/Back, ±5/±10 g target changes, Skip frame, Chart/Cup Fill selector, phase/stats and graph preferences; MachineControlService and shared preference services. | 17.1–17.3 and paired Chart/Cup Fill rejoin/target/stop-at-weight/review evidence. Connected PORTAL presentation remains under 28.3. |
| Espresso display tree | ShotGraph → ShotGraphView/options/style, shared traces/inspection/labels and optional PortalGraphOverlay/PortalGraphLiveSource; CupFillView and its shared image/shader renderer; PortalReadout. | 26.4 confirms identical source/resource closure; graph/cup rendering and phase transitions have Mac evidence. Connected PORTAL display needs conditional sensor acceptance. CircularGauge has no production caller and is not counted as rendered. |
| Steam | Time ±5 s, flow, Stop/Purge, view mode, milk/pitcher selection and add/edit/delete, last-steam rate actions, visual/audio coach controls and warning dismissal; SteamService and MachineControlService. | Completed Steam shared-service/editor and 17.2/17.3 evidence. LiveCoachingBanner belongs here, not Espresso; sensor-driven coach presentation remains distinct from tested calculation/state fixtures. |
| Hot Water | Vessel selection/add/edit/delete, mass/volume mode, amount ±10, flow, temperature, Stop/Back and host-owned tare; SteamService, RecipeContext and MachineControlService. | Accepted shared preset/editor, live adjustment, active join, Stop/natural completion and independent return evidence. |
| Flush | Preset selection/add/edit/delete, time ±5 s, duration/flow, Stop/Back; SteamService, RecipeContext and MachineControlService. | Accepted shared editor/cancellation, active reload, explicit Stop/natural completion and independent return evidence. |
| Completion/errors | Shared OperationCompletionOverlay and review entry; correlated pending/refusal feedback and global support dialogs. | 17.3/16.3/24.1–24.4. Real machine operation, instrument accuracy, mobile graphics/input and screen-reader use stay under 28.3. |

Native regression evidence reuses the accepted paired operation checks and full
207/207 Qt Creator run 1792869330687. The latest 1792869330556 build passed the
361-file QML gate; its changes concern archive presentation, not machine control.
The published manual's Extraction Views, Steam, Hot Water, Flush and Screensavers
sections already document these controls and shared operation/sleep behavior;
reuse that guidance. This inventory records coverage and holds; it does not turn
connected PORTAL rendering or exhaustive widget actions into passes. The
paired Mac simulator scope of 17.1 is accepted using the recorded graph,
view-selection and lifecycle evidence; real-device acceptance remains 28.3.

## Task 19 maintenance and calibration inventory

This is a source-level inventory; each evidence statement is limited to the paths actually exercised.

| Shared QML path | Canonical dependencies and actions | Evidence and limits |
|---|---|---|
| `DescalingPage` | `MaintenanceService` publishes workflow/phase/progress and accepts `beginDescaleHold`, `startDescale`, `stopDescale`, `cancelDescale`, and `finishDescale`. Back only navigates; Finish/Cancel/Stop end or dismiss the host workflow. | Mac simulator checks cover preparation, explicit stop/cancel/Back/reopen, weighted progress, natural completion and browser rejoin. Service/policy fixtures cover refusal and connection loss. No physical descale or consumables were used. |
| `TransportPage` | `MaintenanceService` handles start, progress, stop and finish. Back is navigation-only; Cancel Preparation and Stop are explicit host actions. | Mac simulator checks cover Back/reopen, explicit stop, natural completion, Done and the shared stopped outcome. Connection-loss behavior is fixture-covered. No physical draining was performed. |
| `SensorCalibrationPage` | `SensorCalibrationService` reads sensor specs/value/context, validates an entered instrument reading against context and calibration revisions, and applies only after explicit confirmation. `ProfileService` activates the test profile and restores the prior recipe. | Mac checks cover Pressure/Temperature entry, refusal/confirmation Cancel, Back, and recovery after host restart. Fixtures cover units, bounds, stale context/calibration, invalid values and refusal. No external gauge/thermometer or physical calibration write was used. |
| `FlowCalibrationPage` | `FlowCalibrationService` loads recent shots or revision-checked shot selection, replays data using the stored multiplier, and saves a draft against its captured calibration revision. Reset changes the draft until Save. | Mac browser/native checks cover latest-shot rendering, multiplier edit, Back-discard, Reset/Save placement and reopening at 1.00. Fixtures cover save, conflict/refusal and load failure. No external flow standard was used. |
| Settings → Calibration (`SettingsCalibrationTab`) | `CalibrationService` exposes the fixed actions `setAutoFlowCalibration`, `clearProfileFlowCalibration`, `resetSawLearningForProfile`, `resetSawLearning`, `setUseFlowScale`, `setIgnoreVolumeWithScale`, `setSteamTwoTapStop`, `clearSteamHealthHistory`, `setHeaterValue`, `setHeaterDefaults`, and `setHeaterVoltage`. The tab routes to Flow Calibration; sensor cards route to `SensorCalibrationPage`. | Mac checks cover settings entry, auto-calibration restoration and sensor/flow entry. `CalibrationService` policy/source fixtures cover the allowlisted writes, argument validation and refusal. The whole settings-control matrix and hardware effects were not individually live-tested. |

`SettingsDebugTab` is the additional `MaintenanceService` caller: it sends `setHeadless` for the simulator/debug window. This is a debug presentation control, not a maintenance workflow; it is included here so the service's user-facing callers are accounted for, without claiming a live check of that control.

Mac checks do not establish physical descale, transport or external-instrument calibration acceptance. These remain separate from the completed Mac/fixture scope of 19.3 and the inventory/manual deliverable of 19.4.

## Task 20 settings action inventory

Native and Wasm use the same settings shell and controls. Writes use the existing typed services and canonical managers, with per-domain revisions and correlated completion. The dated task-20 entries in `evaluation.md` hold build/test IDs and the paired Mac results; this map identifies ownership without duplicating those records.

| Shared controls/actions | Host path | Acceptance and limits |
|---|---|---|
| Tabs, search, hidden-control fallback, external Profiles shortcut, Back/focus | `SettingsPage`, `SettingsTabs`, search registry and generated index | All 272 Mac results visited in each client; focus, browser Back and return pass. Android/Windows-only results are excluded on this host. |
| DE1/scale/refractometer/PORTAL status, scanning and connections | `MachineControlService` and `SettingsConnectionsService` → native BLE/Wi-Fi/USB managers | Host scan, shared status, manual-host refusal, independent dialogs and Auto Test writes pass. Existing device/protocol fixtures cover connect/disconnect and failures; physical pairing remains unverified. |
| Units, refill threshold/override, Pocket integration, per-screen zoom | `SettingsPreferencesService` app domain → canonical app settings | Mac writes and restores pass. Pocket was unpaired, so no relay session was created. Android launcher and simulator availability use canonical fixtures; developer simulator workflows remain in task 27. |
| USB charger and Off/On/Night charging; steam two-tap stop | Battery/hardware domains → `BatteryManager` and hardware settings | Every choice written and restored; physical charging/heater behavior is outside this Mac check. |
| Keep-warm, recipe ownership, auto-flush; Edit After Shot and Clear Notes | Brew/visualizer domains → canonical settings managers | Every control written and restored. Existing fixtures cover apply-on-change, types/ranges and stale revision refusal. |
| Shot Map enable/manual city and location preview | `shotMap` preference projection / `ShotReporter`; `ScreensaverService` supplies map data | Enable round trip, isolated city draft, valid commit and native clear pass. Both previews show the same host location and populated shot data. No fresh GPS permission transition was tested. |
| Extraction view and review timeout | `RecipeContextService` → canonical context preferences | Cup Fill/Shot Chart and Never/30-minute transitions pass in both directions; fixtures cover typed/range/stale refusal. |
| Mode, dark/light preset selection, glass and background | `ThemeManagementService` → `NativeThemeManagementSource` | Mode/glass/presets pass after fixing notification order. Background drafts, guarded Apply, retained conflict, explicit retry and restoration pass. Full theme/library/media workflows remain in task 22. |
| Descale/transport navigation | `SettingsPage` → shared maintenance pages / `MaintenanceService` | Reuse accepted task-19 Mac/fixture checks; physical maintenance is not claimed. |

The existing `tst_settingspreferencesservice`, `tst_recipecontextservice`, `tst_themeservice`, connection and protocol suites supply the canonical setter/validation/CAS coverage. No arbitrary QObject setters, browser hardware implementation or replacement settings UI was added. Browser accessibility limitations are recorded in `tools/webclient/upstream-accessibility.md`; task 24 retains actual screen-reader acceptance. The user manual links the shared browser settings to the existing settings instructions; the legacy HTML documentation remains separately labelled.

## Open closure and evidence

Task 15.3's source ownership inventory is recorded in `action-inventory.md` and is complete as an inventory, not a runtime parity claim. Settings, AI settings, the profile/Visualizer import destinations, and the core Home workflows have paired Mac checks recorded under tasks 14, 18, 20 and 21. Remaining widget and common-component runtime checks are 22.3 and 26.5. Keep HTML/API routes in `web-parity.md`; this ledger covers app-QML routes only. HTML remains available, and deferred mobile, screen-reader and real-machine gates remain separate.
