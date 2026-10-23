# Dependency audit: graph and inventory slice

This document preserves the initial source audit and the later form-field boundary. Statements below about unbuilt candidates describe the pre-implementation audit; the current implementation and runtime evidence are in [evaluation.md](evaluation.md).

## Implemented Mac boundary (2026-10-20)

The native graph now adapts its singletons into shared `ShotGraphData`, `ShotGraphOptions`, and `ShotGraphStyle` and renders `ShotGraphView`. The shared view uses the actual `FastLineRenderer` through `FastLineTraceLayer`, Qt Graphs axes/goals, `GraphRightAxisLabelsView`, `GraphEndMarkerArea`, and `GraphUtils.js`. Native portal presentation is supplied as an optional component; no portal driver/data is linked into the browser. Both builds consume these same files. The native `EquipmentSummary` similarly wraps `EquipmentSummaryRenderer`; the complete equipment card and native suggestion/input controls remain unextracted.

Qt Creator verified both targets and their QML diagnostics, and the Mac browser rendered/replayed the graph. This supersedes the initial unverified Qt Graphs/custom-renderer/build concerns for this one Mac only. Fonts were bundled and visually exercised; emoji, mobile inputs and accessibility still need their own checks.

## Initial source audit

## Finding

The proposed two-component rendering slice is plausible only with a typed presentation boundary. It is not a small source-list extraction from the native executable. `ShotGraph.qml` has a focused visual dependency set, but its data and settings are supplied by native singletons, and `FastLineRenderer` is wired directly to the live `ShotDataModel`. The smallest representative complete inventory card, `EquipmentCard.qml`, also embeds host storage and settings operations in the view. A browser target that compiles these exact files unchanged would need extensive local-object stubs or would pull native application services across the boundary, contrary to the proposal's architecture.

The smallest worthwhile experiment keeps the actual graph and card visuals shared, changes their inputs and actions to explicit typed interfaces, and supplies browser implementations of those interfaces. Reusing only a screenshot, copied page, or a separate browser-only graph would not establish the intended maintenance benefit.

## ShotGraph and FastLineRenderer

### Source facts

- `qml/components/ShotGraph.qml` imports `QtQuick`, `QtGraphs`, `Decenza`, and `GraphUtils.js`. It composes `PortalGraphOverlay`, `GraphRightAxisLabels`, `DecenzaGraphsTheme`, Qt Graphs `GraphsView`/`ValueAxis`/`LineSeries`, and nine `FastLineRenderer` items.
- Direct singleton reads include `Settings.graph`, `Theme`, `TranslationManager`, `ShotDataModel`, `ProfileManager`, and `MachineState`. It calls `ShotDataModel.registerFastSeries(...)` with all nine renderer objects. The graph also reads raw time, goal points, phase markers, stop/final weight, and portal data. `PortalGraphOverlay.qml` adds portal settings, samples, bounds, and live sample notifications.
- `FastLineRenderer` is a compile-time `QML_ELEMENT` in `src/rendering/fastlinerenderer.h`. Its class derives from `QQuickItem`; its visible header dependencies are Qt Quick/QML integration and Qt value/scene-graph types. The implementation creates `QSGGeometryNode`/`QSGGeometry` with `QSGFlatColorMaterial` in `updatePaintNode()` and includes `core/diagnosticlogging.h` for overflow/truncation warnings. No BLE, database, machine-control, image, or platform type is included by this renderer's own header/implementation.
- `ShotDataModel` is not just a QML DTO. Its header owns shot/portal vectors, a timer, phase markers, and nine `QPointer<FastLineRenderer>` references; the implementation includes `machine/frameexitreason.h`, `ai/conductance.h`, `FastLineRenderer`, and diagnostic logging. It batches data into renderer instances and exposes QVariantList properties. Its QML singleton registration is in `src/core/contextsingletons_qml.h`.
- The CMake source list currently compiles both `src/models/shotdatamodel.cpp` and `src/rendering/fastlinerenderer.cpp` into `Decenza`. The native target links Qt Graphs and Qt Quick as well as Bluetooth, SQL, Multimedia, TextToSpeech, WebSockets, and other application modules. The current `QML_FILES` list is for the broad `Decenza` module and contains both graph and application pages/components; reusing that module wholesale would not be a narrow target.
- Graph visuals contain `Text`, but no `Image` in `ShotGraph.qml`. `Theme` supplies font roles and emoji replacement. Bundled Decenza Sans and Noto Sans Math fonts are registered in `src/main.cpp` from Qt resources; SVG icons are referenced with `qrc:/icons/...` where used by shared controls. The graph's actual point rendering uses the Qt Quick scene graph. Font initialization, Qt Graphs, SVG resource packaging, and scene-graph behavior therefore remain target concerns.
- Compile-time registration is a project invariant. `QML_ELEMENT`/`QML_SINGLETON` metadata and CMake `qt_add_qml_module` inputs generate the QML type registry; `main.cpp` explicitly calls `qml_register_types_Decenza()` because this module also has runtime registrations. The `QML_SINGLETON` wrappers in `contextsingletons_qml.h` include many host classes. A new target must register only its own typed browser-facing types and QML files rather than reuse that broad registration header/module unchanged.

### Plausible narrow source/resource set (unverified)

For a graph rendering probe using the real visual component and recorded data, the candidate QML set is:

- `qml/components/ShotGraph.qml`
- `qml/components/PortalGraphOverlay.qml`
- `qml/components/GraphRightAxisLabels.qml`
- `qml/components/graphs/DecenzaGraphsTheme.qml`
- `qml/components/graphs/GraphSeriesInstantiator.qml` (used by the portal overlay)
- `qml/components/GraphUtils.js`
- `qml/components/Tr.qml`
- `qml/components/AccessibleMouseArea.qml` (used by the end-marker affordance)
- A narrow graph palette/font module replacing or constraining `Theme`, plus bundled font and icon resources actually referenced by that selected QML closure

Candidate compiled Qt/C++ pieces are Qt Quick, Qt Graphs, the existing `FastLineRenderer` class, and a new small typed graph data/view model. The probe should not compile `ShotDataModel` unchanged: that class is the live native accumulator and directly owns renderers. The browser model can provide the properties/signals the graph needs and append samples to renderers, while the native side adapts `ShotDataModel` into the same contract. Retaining the current `ShotDataModel.registerFastSeries()` path for native use is possible, but the graph-facing interface must no longer require that concrete singleton.

The list is plausible from imports and direct references, not confirmed by `qmllint`, qmlcachegen, or a link. Some listed UI components may add transitive dependencies; their closure must be established in the first target build.

## Representative inventory card: EquipmentCard

`EquipmentCard.qml` is the smallest complete inventory-card candidate in this audit; the much richer `BagCard.qml` also owns Bean Base link recovery, image lookup, lifecycle, and photo handling, so it is not a minimal slice.

### Source facts

- `qml/pages/EquipmentPage.qml` requests inventory from `MainController.equipmentStorage`, listens for `inventoryReady` and `packagesChanged`, and creates `EquipmentCard` delegates from the returned package maps.
- `EquipmentCard.qml` directly uses `Theme`, `Settings.dye`, `TranslationManager`, `AccessibilityManager`, and `MainController.equipmentStorage`. Its card-wide tap calls `Settings.dye.switchToEquipment`; its action row calls `requestMarkRemoved` or `requestDeletePackage`; a `Connections` block listens for `packageDeleted`.
- The card composes `EquipmentSummary`, `CardTapArea`, `Tr`, `AccessibleButton`, and `StyledIconButton`; the buttons reference bundled `grind.svg`, `edit.svg`, and `trash.svg`. `EquipmentSummary` reads `PuckPrepLabels`, which obtains the label table from `MainController.equipmentStorage.puckPrepFlags`, and uses `Theme`/`TranslationManager` for text and emoji rendering.
- `EquipmentStorage` is a compile-time `QML_ELEMENT`/`QML_UNCREATABLE` with SQLite-backed asynchronous CRUD and inventory signals. It is reached through `MainController`; `MainController`'s compile-time QML type exposes numerous native services, including this storage and `BeanBaseClient`. `contextsingletons_qml.h` includes the `MainController` class and many other host singleton headers. Those source facts make registering the native singleton wrapper or `MainController` in the browser an inappropriate shortcut.
- `EquipmentSummary.qml` is the most isolated shared presentational subcomponent, but even it currently reads the native `puckPrepFlags` table via `PuckPrepLabels.qml`. Making just that summary shareable would require passing normalized display labels (or a bounded lookup interface) rather than reaching `MainController` from its descendant singleton.

### Plausible narrow source/resource set (unverified)

For the actual card, the candidate QML set is `EquipmentCard.qml`, `EquipmentSummary.qml`, `CardTapArea.qml`, `AccessibleMouseArea.qml`, `PuckPrepLabels.qml`, `Tr.qml`, `AccessibleButton.qml`, and `StyledIconButton.qml`, plus any small transitive component imports revealed by the target build. It also needs the card's three SVG icons and a browser-safe palette/font/localization interface. `EquipmentPage.qml` adds list layout, inventory loading, and the empty state; it is not needed to validate a single card against a representative package map.

The current card cannot be run unchanged against remote DTOs: its selection, removal/deletion requests, deletion result, and puck-prep lookup are concrete host calls. A first display-only probe can use the actual `EquipmentSummary` with a typed package value and explicit localized puck-prep labels. The complete `EquipmentCard` requires typed callbacks/properties for select, remove, delete, failure feedback, and edit request. The native implementation and browser implementation can then bridge those to `SettingsDye`/`EquipmentStorage` and the remote session API respectively.

## Extraction cost and bounded next step

The blocking cost is the presentation/API seam, rather than the renderer class by itself. Reusing `ShotGraph` requires a graph contract to replace multiple concrete singleton reads and the `ShotDataModel` renderer-registration call. Reusing the inventory card requires the same kind of seam for storage actions, state notifications, selection, accessibility announcements, and lookup data. `Theme.qml` and `MainController` are application-wide services; importing them as-is would undermine a small browser target even if the linker could be made to succeed.

**Estimate (inference from the source closure; unverified):** allocate 1–2 developer-days for a bounded Gate 1 spike that factors the graph's typed presentation contract, adapts one native source and one recorded browser fixture, and compiles/runs the actual graph plus the display-only `EquipmentSummary` in a separate Qt Wasm target. Include Qt Graphs and the CMake/QML registration work in that box. If Graphs or the custom QSG renderer cannot compile/render with the pinned stock Wasm Qt, stop the spike at that gate. Do not include full inventory actions, remote synchronization, mobile-device performance, or parity migration in this estimate.

The full `EquipmentCard` remote-action seam and one edit flow are additional work; the proposal's Phase 2 estimate of 2–3 days should be reviewed after the rendering spike shows how much common theme/accessibility code can really be shared. No browser or Qt Wasm behavior is claimed as proven here. In particular, Qt Graphs availability/rendering, `QSGGeometryNode` compatibility/performance, bundled font initialization, touch/accessibility behavior, and the true minimal CMake source closure remain unverified.

## Source facts versus unverified assumptions

**Verified by reading source/build declarations:** direct QML imports and references listed above; current component composition; compile-time singleton/type registration; `FastLineRenderer`'s Qt Quick scene-graph implementation; `ShotDataModel`'s concrete renderer ownership and Qt/core includes; broad native target link/source declarations; font/resource initialization site; and the inventory card's local storage/settings wiring.

**Not verified:** that Qt 6.12's installed WebAssembly kit includes Qt Graphs; that `QSGGeometryNode`/the custom material works or performs adequately in the target browsers; that CMake can form the candidate source list without further transitive classes/resources; that fonts, SVGs, touch, keyboard, and screen-reader behavior match native; or that the adapter estimate will hold. Those require a small separate-target build and browser/device evidence, not inference from native source compatibility.

## 2026-10-20 form-field extraction boundary

**Source-audited:** `qml/components/SwitchEquipmentDialog.qml` uses `SuggestionField` for the optional name, grinder brand/model/burrs, and basket brand/model. It passes each field's `textField` alias into `KeyboardAwareContainer`, supplies registry/history suggestions and model descriptions, and handles text edits and suggestion selection separately. The existing `SuggestionField` API is already typed for its core data (`label`, `text`, `suggestions`, `descriptions`, `accessibleName`, `textEdited`, `suggestionSelected`, focus/blur signals), but its implementation is coupled to app services and UI components.

`qml/components/SuggestionField.qml` owns input composition behavior as well as suggestion presentation: its filtering reads `displayText` so IME preedit is reflected, Return/arrow/Escape keys operate on its popup, selection defers focus and keyboard changes, and the exported `textField` is part of keyboard-aware form handling. It also depends on `Theme`, `TranslationManager`, `Keyboard`, `AccessibilityManager`, `StyledTextField`, `AccessibleButton`, `AccessibleMouseArea`, `SelectionDialog`, and an app resource icon. `qml/components/StyledTextField.qml` supplies the accessible-name/value contract, deliberate keyboard activation and focus suppression in accessibility mode, keyboard show/hide behavior, Material field styling, and its custom placeholder.

**Implemented boundary (source facts):** `qml/components/SharedTextField.qml` and `qml/components/SharedSuggestionField.qml` are now the common typed controls. Both consume `FormControlStyle`; the native `StyledTextField`/`SuggestionField` path uses `NativeFormControlStyle` and thin adapters to preserve the existing app-facing field API. The shared suggestion control owns editing, IME-aware suggestion filtering, selection, popup behavior, and focus requests. It exposes typed keyboard and browse callbacks instead of importing application singletons. The native adapter retains the existing `SelectionDialog`, keyboard bridge, accessibility/button chrome, and native styling. `tools/wasm-feasibility/FormProbe.qml` consumes the shared controls with its own `FormControlStyle`, the portable `Keyboard` QML singleton backed by `QInputMethod`, and a host-owned standard Qt browse dialog. The Wasm form fixture does not compile the native `SelectionDialog` or access inventory storage.

**Local-preview scope:** the form section’s suggestions/descriptions and summary preview are fixture data. Apply updates only the fixture's preview; Cancel restores draft fields to the last applied preview; Clear and Browse operate on the shared control and fixture dialog. These actions do not persist a form, call equipment CRUD, modify the simulator, or write through the preview server. Existing HTML routes and behavior remain unchanged. See the feasibility README for a reproducible manual interaction sequence. Build and runtime evidence is maintained in `evaluation.md`; this audit records the source boundary, not test results.

## Qt 6.12 Wasm accessibility findings (2026-10-20)

**Popup/dialog root cause (Qt source):** `qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:494-496` creates a browser `<dialog>` for the Qt Dialog role. `setHtmlElementVisibility()` at `:684-688` only adds/removes `inert`, and the ObjectShow/ObjectHide path at `:1413-1425` calls that helper without opening/closing the HTML dialog. The dialog event handler at `:1017-1029` updates only `aria-label`. Qt Quick emits `DialogStart` when a popup opens (`qtdeclarative/src/quicktemplates/qquickpopup.cpp:942-951`), but the Wasm handler does not translate it to the HTML `open` state. This explains the observed DOM: rows existed under a closed `<dialog>` and were absent from browser accessibility inspection.

The feasibility host now includes `tools/wasm-feasibility/qt-dialog-accessibility.js`, which observes only Qt's shadow root, mirrors `inert` to `open`, and resets browser default dialog geometry. The reset follows the approach in Gerrit review 707847. The workaround does not call `show()` or `showModal()`; Qt retains ownership of visible popup UI, modality and focus. Browser accessibility-tree inspection now exposes the autocomplete and browse-dialog rows and Cancel; activating the Niche Duo browse row updates the visible QML field/status, closes the dialog, and returns focus to Browse. This is host-specific evidence, not actual screen-reader validation, a stock-Qt fix, or mobile coverage.

**Upstream status checked 2026-10-20:** Qt bug QTBUG-144330, “Complete accessibility support for basic UI elements on WebAssembly,” is In Progress. Qt Gerrit 707847 (“wasm:a11y Add support for menus and dialogs”) is NEW/unmerged at revision 21, updated October 13, targets 6.12, and references QTBUG-144330 and QTBUG-140108. The review explicitly opens generated dialogs and resets browser user-agent geometry/styles. The app workaround should be reevaluated against this change and removed when the shipped stock Qt includes a verified fix.

**Editable-field shadow gaps (Qt source):** the shared field correctly supplies `Accessible.name` (`qml/components/SharedTextField.qml:36-40`), and Qt's `QAccessibleQuickItem::text(Name)` returns the attached name (`qtdeclarative/src/quick/accessible/qaccessiblequickitem.cpp:648-660`). However, Qt Wasm's editable-text handler maps `NameChanged` to the shadow input's `value` instead of an accessible name (`qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:814-816`); the button handler does map names to `aria-label` (`:894-900`). Programmatic `TextField.text` changes emit `QAccessibleTextUpdateEvent`, which has type `TextUpdated` (`qtdeclarative/src/quick/items/qquicktextinput.cpp:3880-3889`; `qtbase/src/gui/accessible/qaccessible.h:546-560`). The Wasm line-edit handler handles TextInserted/TextRemoved and caret moves but omits TextUpdated (`qwasmaccessibility.cpp:802-840`), leaving the browser shadow input stale after selection even though the visible QML text updates. Qt's interface can read the current field display text (`qaccessiblequickitem.cpp:691-701`), so the missing behavior is the Wasm event-to-DOM synchronization.

A compile-time app-owned C++ bridge could send public `QAccessibleValueChangeEvent`/`QAccessible::updateAccessibility()` after programmatic edits to reach the existing Wasm ValueChanged path (`qtbase/src/gui/accessible/qaccessible.h:580-603`; `qwasmaccessibility.cpp:814-817`). That could address stale value synchronization but cannot fix editable names because Wasm's NameChanged handler still writes the value. A scoped DOM adapter using stable accessible identifiers or an upstream Qt correction is needed for correct browser accessible names. No such editable-field workaround or actual reader/mobile validation is claimed here; task 2.9 remains open.

Both editable-field defects were reproduced in `AccessibilityRepro.qml` using only QtQuick/QtQuick.Controls and filed with maintainer authorization as [QTBUG-151710](https://bugreports.qt.io/browse/QTBUG-151710) (name) and [QTBUG-151711](https://bugreports.qt.io/browse/QTBUG-151711) (programmatic value). The browser host Dialog now explicitly takes focus and returns it to Browse on close; cancellation was separately verified.
