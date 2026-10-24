# Shared component and resource audit — task 26

This is source/build-closure evidence for the Mac native and Wasm targets, not acceptance of every parent workflow. The baseline supplied by the parent is package `7c918`: matching builds, 349 clean native QML files, and 203/203 tests without warnings. The auditing agent did not run builds, tests or UI checks. Parent integration subsequently passed package `df398f8e` in both kits and all 203 CTest suites; runtime scope is stated separately below.

## Canonical source inclusion

### Current audit — package ca809b5f (2026-10-24)

The source tree and ledger match exactly: **361 unique app sources** (343 QML,
16 JavaScript, 2 ES modules). Active native/Wasm manifests contain **359/358**
app sources, with **356** identical canonical sources shared; all declared rows
match, with no missing, extra or duplicate entries. Native-only files are main,
SessionAppearancePublisher and the explicitly adapted UgsGuideDialog; Wasm-only
files are Main and BrowserRouteHistory. The guide opens externally in Wasm.

Group 26 has **200 rows**, of which **199** use the same files in both targets;
the remaining row is that native WebView guide. The audit includes 32 named
dialogs/popups, 23 styles and 13 keyboard/focus/accessibility helpers (overlapping
subsets). AccessibleButton, StyledTextField, KeyboardAwareContainer, DecenzaDialog
and the bar/icon/switch/selection/suggestion/equipment layers retain production
callers and distinct injected style, interaction, logging or service contracts.
Native-prefixed adapters are compiled by both targets. FocusIndicator and
SharedFocusIndicator have distinct live callers; neither is obsolete. The
browser directory contains only its Main shell, with no component/style copies.

No obsolete duplicate wrapper was found to remove. AccessibleLabel is an unused
18-line leaf, not a duplicate wrapper; its source inclusion does not establish
runtime coverage. Existing alternate-style/interaction fixtures, paired build
**1792763852563**, focused **1792763853166** and full **1792763853167** (206/206)
support this source/control audit. The shared modal focus correction also passed
the actual-dialog fixture and paired live support checks documented in
`diagnostics-support-validation.md`.

Task 26.1's audit is complete. Parent-workflow runtime acceptance remains under
26.2/26.3/26.5; device/screen-reader gates remain separate. Earlier numerical
counts below are historical package evidence, superseded by this audit.

The inspected generated native and Wasm QML manifests each name 349 application sources. Their intersection is 347 files, resolved to the same repository paths. The subsequent canonical `LanguageStringEditor.qml` addition is declared in both targets and the inventory: that tested package denominator is 352 QML/JS/module files, with 350 included per target and 348 shared after generation for `df398f8e`. These counts were subsequently verified by the paired `df398f8e` builds and the full 203/203 suite pass (Qt Creator run 1792683626101). The intentional differences match the inventory:

| Native only | Wasm only |
|---|---|
| `qml/main.qml` | `qml/webclient/Main.qml` |
| `qml/components/SessionAppearancePublisher.qml` | `qml/components/BrowserRouteHistory.js` |

All 193 previously generated inventory rows assigned wholly or partly to group 26 resolve to the same source file in both targets; the new group 24/26 language draft helper raises the declared group 26 count to 194. Both targets include the same 16 common JavaScript/module helpers: DateUtils, DrinkTypeCatalog, EquipmentSummaryFormat, EspressoCompletionPolicy, GraphUtils, OperationNavigationPolicy, RecipeCardText, RecipeSearch, SettingsSearchEntries, SettingsSearchMatcher, SharedShotPlanFormat, layout/EmojiData, layout/PillFit, layout/ShotPlanConfig, qrcode, and third_party/fuse.

Controls and their style/interaction adapters also resolve to the same files. For example, AccessibleButton supplies NativeButtonStyle/NativeButtonInteraction to SharedAccessibleButton on both targets; StyledTextField supplies NativeFormControlStyle to SharedTextField. The service-free shared-form harness also supplies alternate styles/interactions and exercises font/scale changes. These are live injection boundaries, so this audit does not justify deleting their wrappers. It does not establish full keyboard, accessibility, focus or dialog-workflow acceptance.

Pure formatting/selection behavior continues to use common helpers, including the JS modules above and the shared C++ SteamCalculations, TemperatureDisplay, PreparationState, LayoutCatalog, ProfileCatalogLogic and graph preference definitions. There is no separate browser copy of those definitions in the audited source manifests. Browser history remains an explicitly classified platform adapter.

## Bundled resource closure

Native `resources/resources.qrc`/`resources/emoji.qrc` and the active Wasm resource groups resolve these URLs to identical asset files:

| Resource class | Shared URLs |
|---|---:|
| UI and symbol fonts | 5 |
| Icons | 75 |
| Emoji SVGs | 4,009 |
| CoffeeCup images | 3 |
| Map textures | 3 |
| Background pattern SVGs | 6 |
| GHC and QR images | 2 |

Both targets register fonts through `src/core/bundledfontregistration.h`. The three shader URLs are also identical (`crt`, `cup_mask`, `cup_lighten`); their platform build outputs come from the same three shader source files, with cup registration shared through `cmake/cup_rendering.cmake`. Different generated QSB paths are expected and are not duplicate visual definitions.

No current missing asset or conflicting same-URL asset source was found. Resource declarations/import closure prove availability in the build graph; they do not prove every dynamic emoji/icon selection, every glyph, shader backend, graphics effect or media decoder has rendered correctly. Parent-reported font/language/background/shot-wallpaper checks retain their own limited runtime scope. Remote/mobile graphics and input gates, media workflows, and the parent component workflows remain open where their own tasks lack evidence.

## Two checking gaps and narrow fixes

1. `check_qml_inventory.py` previously rejected unknown manifest paths but only reported inclusion counts. A ledger row declaring native + Wasm could therefore disappear from one target without failing. The check now retains the inventory's inclusion column and requires every declared native/Wasm row in the corresponding manifest. Existing self-tests now cover missing shared QML, native-only JS and Wasm-only module rows, while allowing the intentional target-only exclusions.
2. Resource closure previously globbed every generated `.qrc` in the build directory. Four retired manifests remain there (`recipe_icons`, `card_icons`, `steam_icons`, `shared_equipment_icons`), although no corresponding translation units remain in the active build rules. Such a stale file could falsely supply a missing asset. CMake now exports the current target's source list at generation time; the closure check selects only the resource manifests compiled by that target, plus its explicitly supplied classic emoji QRC. It rejects missing/unsupported manifests, unresolved source expressions and unused extra manifests. Regression fixtures include an asset available only in an unlinked stale QRC, absolute/relative source normalization, classic QRC ownership and malformed/missing source manifests.

The resource-source mapping follows the installed Qt 6.12 macros: target resource XML is `${binary}/.qt/rcc/${resourceName}.qrc` and generated C++ is `qrc_${resourceName}.cpp` (`Qt6CoreMacros.cmake:2553,2676`); classic QRC output uses `${binary}/qrc_${outfilename}.cpp` (`:472`). The source-list export uses CMake's target `SOURCES` property rather than a Ninja or internal autogen schema.

These fixes strengthen source/resource accounting. The paired builds and regression fixtures close the source/definition and build-closure obligation in task 26.4. They do not close runtime controls/workflows in tasks 26.1/26.5, or deferred platform capability acceptance.

## Integrated verification update

Package `df398f8e` passed Wasm/native builds and all 203 CTest suites; the native QML gate reported 350/350 clean. This verifies the active-manifest checks and regression fixtures, while the runtime limitations above remain. The subsequent shared Language file-action helper passed paired builds 1792683623512/3513 and the full 203/203 run 1792683626105 on `f58f1937`. The inventory now contains 353 QML/JS/module files, with 351 per target, 349 shared sources and 195 group-26 rows. Runtime workflow acceptance remains separate.

### Script coverage finding (2026-10-23)

The maintainer observed `OpenType support missing for "Decenza Sans", script 11` while testing languages. In the pinned Qt source, `QChar::Script_Devanagari` is enum value 11 (`qchar.h:210-224`); `QFontDatabasePrivate::loadSingleEngine` logs this informational message when the chosen face fails `supportsScript` and rejects that face (`qfontdatabase.cpp:781-786,807-813`). The shared AddLanguage page includes Hindi's native name हिन्दी. Bundled Decenza Sans is present in all four weights but covers Latin/Greek/Cyrillic; the additional Noto Sans Math supplies symbols, not Devanagari shaping. Shared resource closure therefore does not establish multilingual glyph/shaping coverage. Native fallback and actual Wasm rendering of non-Latin scripts remain explicit font/platform acceptance checks; no warning was suppressed and no additional font was downloaded.

Maintainer decision: leave font coverage unchanged for now given the current user base and absence of reported rendering problems. This informational message is not a blocker for the Mac implementation; no suppression or additional font package is requested.

### Shared shell integration (2026-10-23)

Package `734aca78` adds one canonical `SharedAppNavigation.qml`: 354 application QML/JS/module files, 352 per target, 350 shared sources; the same four lifecycle/history exclusions remain. Paired builds 1792683623547/3548 and focused QML inventory/control/service run 1792683626112 pass. The connection-list fixture is test-only and does not inflate application sharing counts. Runtime shell/equipment outcomes and the remaining resize/focus checks are recorded in `navigation-language-validation.md`.

The Community correction adds one 11-line shared navigation helper: the current declaration inventory is 355 sources, 353 per target and 351 canonical shared sources. Paired builds 1792683623551/3552 pass, including the 353-file native QML gate. Final full run 1792763853090 passed 203/203 suites; the matching original-workspace host and browser passed Community restoration and shared-shell resize/input checks recorded in `navigation-language-validation.md`.

## Recipe/Beans/Equipment child follow-up — task 26.2 (2026-10-24)

The child audit found a real Get Info ownership defect: closing a bean form
retired its page request but left AI extraction children live. A late reply for
the same URL could populate a newer form. ChangeBeansDialog now owns the exact
page and extraction IDs, and facade/source parent cancellation retires children
even after an empty-page diagnostic has completed. Stage-2 URL extraction remains
available through the existing selected provider/model rules. No editor-type
metadata or copied browser form was introduced.

Real-source held-page/selected-fake-provider, two-client QtRO and actual shipping
QML-handler regressions cover close/reopen at the same URL, late success/failure,
parent cancellation and reconnect without replay. The fake provider makes no paid
request. Combined build 1792763852572, focused 1792763853170 and full
1792763853171 (206/206, no skips/test warnings) passed. Test contract generation
and BagLink support compile once across their consumers.

Remaining task-26.2 runtime gaps are manual/Tea/restock/contextual bag entry and
canonical details/link/revert confirmation cancellation; BeanSuggestions history/
community state and Apply/Create confirmation cancellation; and lifecycle button
dispatch on disposable Bag/Equipment records. Earlier accepted CRUD/conflict/
reconnect tests remain evidence and are not repeated merely to increase counts.
Shot-context components remain under 26.3/17.2. Task 26.2 is not complete yet.

During the next live form-entry check on package 3b2f64d8, the host became
unresponsive with 100% CPU. A read-only thread sample identified a shared search
loop: BeanBaseClient's deferred cached `unknown` link verdict reentered
NativeBeanSearchServiceSource::publish, which queued the same unknown probe again.
Qt Creator stopped the single simulator host; no bag was saved. This is a real
task-26.2 defect, not a completed parity check. The correction and producer-lifetime regression passed combined build
1792763852579, focused 1792763853173 and full 1792763853174 (206/206); the
following runtime checks use that corrected host. The earlier full-suite pass
predated this discovery.

### Search and child-dialog correction acceptance

The source admits each product URL once per query. A cached unknown answer now
retires that probe, preserves other pending probes and eventually publishes one
terminal result. Cancellation/deletion from result handlers prevents later probe
dispatch. The real BeanBaseClient producer also guards its lifetime after a
link-result listener deletes its owning source. Two held-link fixtures cover
partial and terminal reply deletion without sending network requests.

Tea inventory searches previously omitted the canonical model's kind filter.
`UnifiedBeanSearchModel::inventoryForKind` now owns that rule once for both paths;
the mixed coffee/Tea source regression checks Tea-only and unchanged default
scope. Actual native and Edge Tea search each displayed only the two Tea bags.
The shared Tea form rendered Type, Garden, Cultivar, Flush, brew temperature, leaf
ratio and steep time, with scrolling and Cancel.

Package **3c77ef05** stayed responsive through repeated Tea/manual-coffee search,
finished-bag Restock, details and Revert-preview checks. Restock of the same
finished Sublunar Espresso bag retained identity, equipment, dose/yield and frozen
storage habit, used today's frozen date, and cleared the old roast date. Local
Unlink and detail changes were discarded. Both native and Edge opened the Revert
confirmation and cancelled it; the immediate Revert & save action was not used.
Canonical detail dialogs rendered and dismissed. No paid provider request was
made. Both open/finished bag JSON responses exactly matched the pre-check
baseline after all those Cancel-only actions.

Suggestions displayed actual history and a recoverable Visualizer failure in
native, plus an actual limited community summary in Edge (11 shots, one user).
Those were representative different coffees, not a same-record content parity
comparison. Search the web/external navigation were not invoked. The observed
summary did not expose actionable Apply/Create suggestions; their live confirmation
branches remain unverified, alongside their existing fixture coverage.

The empty-identity refusal was visually offscreen at the end of the form. One
shared PlainText/wrapped alert now sits below the sticky header, replacing both
mode-specific labels. Visible errors announce when accessibility is enabled
without taking draft focus. Shipping confirm/error-handler regressions cover
validation/host failure announcements, repeated refusals and hidden/disabled
guards. Combined build **1792763852583**, focused **1792763853175** and full
**1792763853176** passed (206/206, no skips/test warnings). Native and Edge actual
empty-name refusals remained visible at the top and after scrolling; Cancel
discarded the forms. No full screen-reader claim is made from these checks.

The authorized final restart is original-checkout PID85171, package
**c8163724**, epoch b20cab7d-b3b7-4e32-bcfd-bb439b0106e9. Root, Wasm alias, legacy
and session returned 200; read-only MCP initialization/list/read/ping/close
passed (67 tools, surface 1.20.0).

Evidence: `evidence/tea-form-{native,edge}-3c77ef05.png`,
`evidence/restock-{native,edge}-3c77ef05.png`,
`evidence/bean-details-{native,edge}-3c77ef05.png`,
`evidence/bean-revert-confirm-{native,edge}-3c77ef05.png`,
`evidence/bean-suggestions-{native,edge}-3c77ef05.png`, and
`evidence/bean-refusal-{native,edge}-c8163724.png`.

### Contextual wizard and equipment checks

Native and Edge each entered Recipe creation, chose Tea, opened Add a new tea,
entered manual details and cancelled back to the wizard. The same coffee path
opened the normal coffee form. Nested equipment details showed the same Niche
Zero package; Switch showed the same four choices and selected package. Closing
details and cancelling the chooser preserved the parent form; cancelling the
form and wizard saved nothing. Both bag lists and all 21 Recipes exactly matched
their pre-check JSON baselines.

The Tea form's visible Brand/Tea labels had coffee-only accessible names. The
single shared form now supplies Brand/Tea name in Tea mode, preserving the coffee
names. Native's actual accessibility tree confirms both corrected names; Edge
renders the same form but its native accessibility snapshot still omits text-field
names, so this is not a browser screen-reader pass. Combined build 1792763852587
and focused run 1792763853177 passed; package **2403b27d**, PID89737, was restarted
only through Qt Creator after confirming the old PID and listener were gone.

Evidence: `evidence/recipe-bag-equipment-{native,edge}-c8163724.png` and
`evidence/recipe-tea-child-{native,edge}-2403b27d.png`.

The remaining contextual relink, conditional community and lifecycle dispatch
checks were completed with the isolated shipping-handler evidence below. No existing Recipe references a finished
bag, and observed community summaries exposed no actionable recommendation.
Those branches need isolated shipping-handler/service evidence rather than
altering original user records to manufacture the state. Earlier accepted CRUD,
conflict and reconnect evidence remains valid.


### Lifecycle picker navigation

On package 2403b27d, native and the retained in-app browser opened Freeze and
Thaw date pickers from existing bag cards. Escape dismissed each picker without
choosing a date. Earlier read-only Details/Restock/Revert checks and current
Recipe child checks are paired UI evidence; destructive and lifecycle persistence
are exercised against isolated records through the canonical source tests.


### Task 26.2 Mac closure

The existing runtime EquipmentCardView fixture checks activation and archived
Restore, identity, visibility and disabled states. The focused shipping BagCard
regression retains Freeze/Thaw date-field identity and matching deletion refusal.
Canonical domain fixtures cover activation, finish, restore, deletion and the
apply-to-bag policy; a duplicate test of EquipmentCard's unchanged forwarding
handlers was removed during the maintenance review.
Shipping RecipesPage handlers check captured contextual repoint/restock IDs,
unavailable/missing context, foreign/duplicate/cancelled reads and correlated
failure feedback. These execute the actual handlers without mounting whole bag
or Recipe pages; they complement the paired live parent/child checks rather than
claiming live destructive actions.

Existing real-source QtRO/storage fixtures cover persistence, references,
compare-and-set conflicts and reconnect without replay. CommunitySuggestionsCard's
shipping functions cover fetched-profile request correlation, confirmed Apply,
deferred saved-bag actions and Recipe-wizard prefill with the selected bag and
profile. Its positive conditional branches were not supplied by the public
community results observed during the live run; no public upload or paid provider
request was made. Full screen-reader, device and common-row visual acceptance
remain their separately assigned gates.

Combined build **1792763852591**, focused **1792763853178** (7/7) and full
**1792763853179** (206/206, no skips/test warnings) passed. Final exact baseline
comparisons preserve all **21 Recipes, 15 open bags and six finished bags**.
This closes task 26.2's Mac shared-component/action scope. Task 26.3 and wider
26.5/28 acceptance remain open.


## History/review/comparison follow-up — task 26.3

Two real defects were found while exercising the shared parents:

- A failed older/newer shot read left the review faded out and `_stepping` set.
  The replacement shot is now admitted only by its exact request completion and
  validated record identity. The current shot, draft, undo state, pending metadata
  and upload lease remain owned until success. Failure restores visibility and
  existing localized feedback; retry can succeed. Existing shipping-function
  regressions cover disconnected/not-found results, foreign/stale replies and
  release of only the old view's lease after a successful retry.
- Comparison inspector times/crosshairs moved while every readout was a dash.
  `QVariantList::append(QList)` flattened the channel lists. The facade now wraps
  each of the eleven channel lists explicitly, preserving missing-channel slots.
  A real ComparisonService QtRO fixture checks all channels, missing middle/last
  channels, a valid negative derivative, nearest samples, absent/out-of-range
  reads and independent client engines.

Combined build **1792763852591**, seven affected suites **1792763853178** and full
**1792763853179** passed (206/206, no skips/test warnings). Qt Creator stopped
PID89737; process absence and a free port were verified before Run. The sole new
host is original-checkout **PID95507**, package **4a7bc498**, epoch
68e4cfe7-b310-4049-bc48-51d7f48af7dc. Root/alias/legacy/session and read-only MCP
checks pass again (67 tools). This is a simulator host.

Paired actual checks on the October 23, 8:46 AM and October 22, 8:18 PM shots:
review/graph rendering, graph Options open/dismiss with existing Advanced/3x
values retained, profile Knowledge Base open/dismiss, native Shot Summary, and
older/newer round trips after the correction. Actual comparison numeric readouts
now render in native and Wasm. Four-shot comparison paging exposed the remaining
other-shot window correctly; changing the browser base to October 23, 8:46 AM
left native's October 20, 6:32 PM base unchanged. Browser reload restored its
four-shot selection and base. Bulk-delete confirmation opened and cancelled in
both clients; no original shot was deleted or metadata typed.

Evidence: `evidence/comparison-readout-before-2403b27d.png`,
`evidence/comparison-readout-native-before-2403b27d.png`,
`evidence/comparison-readout-fixed-4a7bc498.png`,
`evidence/comparison-readout-native-fixed-4a7bc498.png`, and
`evidence/review-step-{native,browser}-4a7bc498.png`.

This is partial task-26.3 evidence. Cold-start last-shot consumers, remaining
review child actions, external-action fixture coverage and graph resize/input
closure still require reconciliation. No public upload, external AI handoff or
paid provider request was made; broad device/screen-reader/hardware acceptance
is not implied.


### Final review child/consumer batch

Actual native/Wasm child checks also opened and dismissed the stored shot's
Debug Log, the same Sublunar Espresso canonical details, shot-context Equipment
and Change Beans pickers, and Shot Summary. Native and Wasm Search Help showed
the same keyword actions; their empty Saved Searches control was correctly
disabled. No original metadata was typed, bag/equipment choice applied, external
roaster link followed, upload performed or AI advice requested.

The shot-context bean picker incorrectly marked today's active bag selected.
It now defaults to the active bag for ordinary entries, but accepts the stored
shot's bag identity in review. A successful retag callback advances the cloned
shot's bag ID as well as its displayed bean fields. Actual native and Wasm
pickers on package **48c93612** marked Intelligentsia Sublunar Espresso selected,
while the live active bag remained Saka Gran Bar; Cancel preserved both records.
The shared binding/callback regression checks that independent active-bag changes
cannot move the shot selection marker and that unrelated shot/draft fields remain.

The comparison graph's accessibility press action previously emitted a MouseArea
click signal that had no inspection handler. It now invokes the existing graph
inspection/announcement function with graph-local center coordinates. Actual
native and Wasm accessibility activation displayed a central crosshair and numeric
readouts; the source-handler regression guards against the old no-op path. This
is action evidence, not a full screen-reader acceptance claim.

Storage-startup, chart-readiness, canonical upload outcomes and local prose
generation retain their existing tests. Real facade lease/R2 fixtures retain
client and shot targeting, disconnected refusal and no replay. Source-extracted
reconstructions of unchanged LastShotItem, upload/prose and metadata controls
were removed during the maintenance review: their size and duplicated mock UI
state did not justify retaining them. No public upload, paid provider call or
mail/browser handoff was made.

Final combined build **1792763852595**, focused **1792763853180** (7/7) and full
**1792763853181** passed (206/206, no skips/test warnings). Original-workspace
Qt Creator restart stopped PID95507 and verified a free listener before starting
**PID815**, package **48c93612**, epoch 3a462f4f-5f21-4218-80bc-ca3c36909391.
Root, alias, retained HTML, session and read-only MCP passed again. The stable
copied shot-detail URL reloaded correctly once the matching client was connected.

Evidence: `evidence/review-bean-picker-before-4a7bc498.png`,
`evidence/review-bean-picker-{native,browser}-fixed-48c93612.png`, and
`evidence/comparison-accessible-{native,browser}-48c93612.png`.

### Approved live preference and startup checks

On package 48c93612, Advanced Curves, flow scale, pressure visibility and the
review right axis round-tripped between native and Wasm. Comparison height moved
238.203125 → 278.203125 → 238.203125 through the browser drag handle and updated
the native graph. Both clients exercised phase visibility, individual shot
visibility, pour alignment and crosshair scrubbing. All captured graph values
exactly matched their starting values afterwards.

The temporary `rating:>99` search was saved in Wasm, appeared in native, and loaded
in each client without changing the other's active query. Its approved removal
restored the empty saved-search list. Single-shot Delete confirmations opened
and cancelled in both clients; no shot was removed.

A temporary Last Shot widget was added through the shared layout picker. Qt
Creator stopped PID815; process absence and a free listener were verified before
starting original-checkout PID32832, same package, epoch
2fc08b9a-8e77-4fab-a4d6-883f74de7dfd. Native and the freshly loaded Wasm client
showed the October 23, 8:46 AM shot immediately and opened its review without a
new espresso. Native's existing History double-tap shortcut also opened it.
The widget was removed and the layout JSON exactly matches the starting copy.
Root, alias, legacy and read-only MCP checks passed after this restart.

Evidence: `evidence/comparison-modes-{native,browser}-48c93612.png`,
`evidence/history-saved-search-browser-48c93612.png` and
`evidence/last-shot-startup-{native,browser}-48c93612.png`.

Task 26.3 remains open for remaining metadata/Undo, conditional upload-status and
external handoff acceptance. The removed mock-control fixtures are not claimed
as coverage for those paths. Wider device/screen-reader gates remain separate.

### Landing checkpoint and maintenance review

The maintainer requested a landing checkpoint and a review of unnecessary
hardening/test code. Further feature work is frozen for that pass. Repeated
unchanged-control/forwarding reconstructions and literal source-syntax checks
are being removed in favor of the existing canonical service/storage tests and
paired live evidence. Concrete failed-step and cancelled-search regressions,
wrong-record/host checks, restore safety and real worker disposal remain.
After simplification, native/Wasm build 1792763852603 passed, and full-suite
run 1792763853183 passed all 206 suites with no failures or skips. Final build
1792763852609 and focused run 1792763853185 passed after correcting one
translation-key collision. The bounded screensaver live checks and final
restart/HTML/MCP checks passed; see `media-validation.md`. Wider acceptance stays
under the existing unchecked tasks.

Against merged PR #2075, this follow-up contains application/build +5,892/−2,642
lines and tests +7,627/−336 lines, excluding screenshots/generated files. The
maintenance review removed 1,419 net application/test lines from the earlier
snapshot, chiefly redundant reconstructed fixtures and repeated lifecycle cases.
No new test target was added by that review. Further feature work remains held
for after this PR lands.
