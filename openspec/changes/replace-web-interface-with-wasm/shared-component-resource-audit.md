# Shared component and resource audit — tasks 26.1 and 26.4

This is source/build-closure evidence for the Mac native and Wasm targets, not acceptance of every parent workflow. The baseline supplied by the parent is package `7c918`: matching builds, 349 clean native QML files, and 203/203 tests without warnings. The auditing agent did not run builds, tests or UI checks. Parent integration subsequently passed package `df398f8e` in both kits and all 203 CTest suites; runtime scope is stated separately below.

## Canonical source inclusion

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
