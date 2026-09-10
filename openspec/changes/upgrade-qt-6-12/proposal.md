# Change: Upgrade to Qt 6.12 and adopt relevant chart improvements

## Status

Reviewed **2026-09-10** against the [Qt 6.12 snapshot notes](https://doc-snapshots.qt.io/qt6-6.12/whatsnew612.html) and the current Decenza tree. This is the single plan for the framework upgrade and the work formerly tracked in `charts-qt-6-12-polish`.

The consolidation PR changes planning only. Keep this change active and its implementation tasks open; archive and apply its deltas to the main specs with the future implementation PR, not with this planning update.

Implementation starts after GA. The [release schedule](https://wiki.qt.io/Qt_6.12_Release) now plans **RC September 14 and GA September 30**, replacing September 8/22. Beta 4 shipped September 4; the `6.12.0` branch was cut September 9. Snapshot documentation remains work in progress; verify the chosen release tag and installer artifacts before relying on a fix.

## Why

Decenza is on stock Qt **6.11.2**. Qt 6.12 makes Canvas Painter a supported module, enables useful chart data/label improvements, and changes platform and QML behavior that we must validate before shipping. Combining the upgrade and chart follow-ups gives them one dependency order and acceptance checklist.

## What Changes

- Upgrade all platforms and the sanitizer workflow to the same released Qt 6.12 patch version, retaining stock runtime binaries.
- **BREAKING:** inherit Qt's iOS/iPadOS **18.0** and macOS **14.4** minimums. The July 29 decision to take iOS 18 and keep one Qt version across platforms stands.
- Reconcile Android build templates and back navigation, Apple deployment targets, Windows manifest generation, and QML tooling/runtime changes.
- Adopt declarative chart data and single-unit label formatting where useful; evaluate automatic label margins and the Canvas Painter graph backend where the stock binaries provide it.
- Evaluate new APIs that could remove existing glue: QtCanvas2D, externally supplied QML singletons, and HTML/color helpers. Each evaluation ends with an explicit adopt/defer result.

### Findings since the earlier review

| Area | Relevance to Decenza | Action |
|---|---|---|
| **QtCanvas2D** | New QML drawing API overlaps JsCanvasPainterItem / JsCanvasContext, used twice in CupFillView. The C++ Canvas Painter module leaves TP; this QML API remains TP. | Evaluate wrapper deletion after checking drawing semantics, synchronization, lint visibility, and Metal/Android parity. Our wrapper already uses GPU rendering; no speedup is assumed. |
| **External QML singletons** | setExternalSingletonInstance() and QML_UNCREATABLE + QML_SINGLETON address app-owned objects in contextsingletons_qml.h. | Evaluate simpler instance provisioning while retaining compile-time registration, identity, lifetime, and platform guards. |
| **Missing required properties** | Qt now reports instantiation errors rather than constructing incomplete components. | Exercise dynamic pages, Loaders, and delegates. Do not blindly add required properties and break injected model roles. |
| **qmllint / qmlls** | Opaque type information, fewer duplicate unknown-type warnings, new shadowing/recursion/filename warnings, and navigation through QML_FOREIGN affect our tooling. | Rebuild type information; compare diagnostics by file/category. Keep default warning coverage. Smaller totals alone are not proof of fixes. |
| **Android back** | Predictive back uses OnBackInvokedCallback; Qt enables its manifest attribute by default. Unhandled back now backgrounds the task. | Check our custom manifest against DecenzaActivity and main.qml's Keys.onReleased navigation; test dialogs, editors, root back, resume, and active BLE/machine operation. |
| **Android build** | Qt specifies Gradle 9.5.1 / AGP 9.2.1. Our custom build.gradle pins AGP 8.10.1, retains resConfig "en", and has a buildFinished packaging hook. | Reconcile with the installed template and verify signed APK/AAB output. CI already uses JDK 21; delete the obsolete Java-17-to-21 task. |
| **CMake** | Release notes now require a 3.25+ configuring tool. Policy-baseline guidance still permits cmake_minimum_required(3.16+); ours is 3.21. | Verify actual kit/runner CMake versions separately from the policy baseline. QTP0006 concerns Wayland generators we do not call. |
| **Windows manifest** | qt_add_executable() generates compatibility, long-path, and UAC settings. | Verify the final executable/installer has no duplicate manifest or unintended elevation change. |
| **Input/accessibility** | ImhNoFullscreen, positive decimal input, TalkBack scrolling/expanded states, and reduced-motion hints fit our landscape UI. | Keep TalkBack as a regression gate; evaluate input/motion hints on actual controls without replacing validation. |
| **Small helpers and inherited wins** | Qt.escapeHtml(), Color, faster date parsing, and resource-content deduplication. | Replace only equivalent QML helpers; preserve escaping/color contracts. No speculative performance refactors. |

References: [QtCanvas2D](https://doc-snapshots.qt.io/qt6-6.12/qtcanvas2d-qmlmodule.html), [Canvas2D API](https://doc-snapshots.qt.io/qt6-6.12/qml-qtcanvas2d-canvas2d.html), [singleton provisioning](https://doc-snapshots.qt.io/qt6-6.12/qqmlengine.html#setExternalSingletonInstance), [CMake versions](https://doc-snapshots.qt.io/qt6-6.12/cmake-supported-cmake-versions.html), [platform matrix](https://doc-snapshots.qt.io/qt6-6.12/supported-platforms.html).

### Platform floors and superseded work

- Reconcile iOS deployment settings: CMake currently sets 17.0 while the iOS workflow passes 14.0. Inspect generated app/widget targets and set the effective floor consistently to 18.0.
- Update the macOS workflow's explicit 13.0 target to 14.4. The old task claiming desktop users were unaffected was wrong; release notes must explain **both** Apple floors as Qt requirements.
- Jeff's previously recorded iPad7,4 (10.5-inch iPad Pro) cannot run iPadOS 18. Reconfirm available hardware and simulator slices before claiming coverage. Do **not** call this an “A12+” minimum: [Apple's list](https://support.apple.com/en-us/104986) includes the 7th-generation iPad. Devices losing support between iPadOS 17 and 18 include the 6th-generation iPad, 10.5-inch iPad Pro, and 12.9-inch iPad Pro (2nd generation). Avoid listing devices already unsupported by iOS/iPadOS 17 as newly dropped.
- Android stays API 28+. The current matrix specifies NDK r27c / 27.2.12479018; continue deriving its exact revision from Qt and verify the compile SDK separately.
- **android/qt-overrides/ was deleted by the 6.11.2 upgrade.** Both TalkBack fixes shipped upstream; the deadlock-protector patch was dropped and preserved only as source in docs/qt-patches/. No plugin/jar rebuild or version-lock restoration belongs here. Preserve the existing **Decenza Ships Stock Qt Runtime Binaries** requirement. Check Gerrit 735089 against the chosen release to record whether the accepted crash is fixed.

### Chart work and retained investigation

The completed source investigation was on **2026-07-29**, against qt/qtgraphs's **6.12 branch**. Preserve it as dated evidence, not proof of the final release. Current notes confirm values, labelPostFormat, and the configure-time painter backend; recheck the remaining details against the release tag.

1. **Declarative data:** use XYSeries.values for static append loops in HistoryShotGraph, FlowCalibrationPage, then ProfileGraph. Preserve timestamps, flow multipliers, missing data, and clearing. valueMapping/valueMin/stepSize are useful only for uniform sampling. The earlier review found setValues() converts the list and calls replace() (`src/graphs2d/xychart/qxyseries.cpp:729-743`); conversion cost still needs measurement. Live ShotGraph/SteamGraph traces actually use **FastLineRenderer**, not the C++ QXYSeries::replace() path the old plan described. Keep that direct scene-graph renderer.
2. **Labels:** try ValueAxis.labelPostFormat for single-unit axes, starting with flow calibration seconds. Multi-unit axes retain units in their titles; preserve translated suffixes.
3. **Margins:** verify the shared-axis margin fix and try GraphsView.dynamicLabelMargins on history/comparison graphs. The July review found it at `qgraphsview_p.h:87`, default false. Check narrow layouts and translations before removing tuned margins.
4. **Backend:** the [notes](https://doc-snapshots.qt.io/qt6-6.12/whatsnew612.html#qt-graphs-module) require configuring Qt with `-feature-graphs-2d-high-performance-backend`. July source had AUTODETECT OFF (`src/configure.cmake:49-54`) and a silent no-op setter without USE_PAINTER_BACKEND (`qgraphsview.cpp:1900-1925`). Check **each platform's stock binaries**. If absent, close adoption for that platform and keep Quick Shapes. If present, prove the backend is active and compare tablet performance before adopting. No custom Qt module builds.
5. **Tick length — previously closed:** GraphsLine exposed mainColor, subColor, mainWidth, subWidth, and labelTextColor. `src/graphs2d/data/tickershader.frag` had subTickLength but no main-tick length uniform; major bars span the ticker, which axisrenderer.cpp sizes to the axis rect. Private AxisTicker.subTickLength is overwritten internally. Keep current theme levers (subWidth: 0, mainWidth: 1, subTickCount: 0). A remaining clipping check determines whether the apparent ticks are actually spilling gridlines, which changes the upstream suggestion. clipPlotArea predates 6.12.
6. **Leftmost label — accepted gap:** July source had no labelsAnchor/labelsAlignment/firstLabelAnchor. Recheck after margin changes; no new overlay to imitate Qt Charts.

The July review also found no replacements for our legend, auto-ranging, dashed overlays, or coordinate mapping. Keep those bridges unless release-source evidence demonstrates equivalent behavior. All six graphs are already migrated: FlowCalibrationPage, SteamGraph, ShotGraph, HistoryShotGraph, ComparisonGraph, and ProfileGraph.

### Optional evaluations and exclusions

QtCanvas2D's `paint(region)` / `context` interface differs from our typed `paint(ctx)` signal. Compare the full drawing API and frame behavior before replacement. A supported C++ module does not make its new QML API stable.

External singleton instances must be provided before first use, failures checked, and objects kept alive past engine destruction. This may simplify app-owned factories; it does not justify changing engine-owned singletons or discarding compile-time registration.

Qt.escapeHtml is relevant to Theme.escapeHtml and ExpandableTextArea only after checking content-versus-attribute escaping contracts. ShotServer's browser JavaScript has no Qt object. Color utilities likewise do not automatically replace our WCAG contrast calculations.

No immediate work for BLE advertising (we are a central), Qt HTTP Server limits (ShotServer uses QTcpServer), Qt MQTT (we use Paho), native controls styling, design tooling, HarmonyOS, or log axes. The macOS QScreen::grabWindow permission change does not directly describe our ScreenCaptureService, which uses **QQuickWindow::grabWindow**. Keep screenshot smoke testing without inventing a permission requirement. Optional Quick3D screensavers still merit a smoke test; no new sky/XR features are planned.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `build-config`: Qt 6.12 on every platform, Apple deployment floors, required configuring tools, and accurate release communication. Preserve the stock-runtime policy.
- `charting`: conditional use of an available stock Canvas Painter backend and evidence for backend-attributed performance claims; retain graph behavior and performance targets.

## Impact

Implementation affects CMakeLists.txt, six release workflows and nightly-sanitizers.yml, Android templates, graph QML, and version/platform documentation. Optional adoption may touch contextsingletons_qml.h, src/ui/jscanvas*, CupFillView, and shared QML helpers. Baseline module lists already include Canvas Painter and Graphs; verify QML plugin deployment if QtCanvas2D is adopted.

Use Qt Creator MCP for local builds/tests and dispatch platform CI before release. linux-release.yml can run the suite on demand; text-invariants.yml runs build-free checks on relevant PRs. Device checks cover Android back/resume, TalkBack, BLE discovery/cancellation, full shots, graph performance, and iOS app/widget behavior. Update the wiki for visible changes and release notes for both Apple floors.
