# Tasks: Qt 6.12 upgrade and chart improvements

One change owns the framework upgrade and the former chart-polish work. Implementation starts after GA; the September 10 review records planned RC **2026-09-14** and GA **2026-09-30**. Choose a released patch version before changing pins. Optional evaluations end with an explicit adopt/defer result; declining adoption does not block the framework upgrade.

## 0. Completed decisions and investigation

- [x] 0.1 Retain the July 29 decision: one Qt version across platforms, accepting iOS 18 and explaining the inherited floor; decision preserved in the proposal.
- [x] 0.2 Preserve the July 29 source review of values, valueMapping, valueMin, stepSize, labelPostFormat, dynamicLabelMargins, and the backend gate; evidence and limitations are in the proposal.
- [x] 0.3 Preserve the July 29 legend/auto-ranging/dashed-stroke/coordinate-mapping investigation: no replacement found; keep bridges.
- [x] 0.4 Preserve the July 29 GraphsLine review: no configurable main-tick length; source findings are in the proposal.
- [x] 0.5 Preserve the July 29 search for tick/clipping APIs: only private/3D/bar-series hits; retain the visual clipping check below.
- [x] 0.6 Preserve the July 29 leftmost-label investigation: no public alignment property; accepted gap recorded.
- [x] 0.7 Preserve the July 29 confirmation of ValueAxis.labelPostFormat; adoption remains pending below.
- [x] 0.8 Consolidate the two changes and review September 10 notes against current code; remove superseded override work, retain both capability deltas, and validate the combined plan.

## 1. Release and local prerequisites

- [ ] 1.1 At RC/GA, verify schedule, known issues, release tag, and installer availability; record the exact patch and recheck chart API/feature assumptions against that tag.
- [ ] 1.2 Install the chosen Qt on Mac (macOS/iOS) and Windows with existing addons, including Canvas Painter and Graphs; verify Qt Creator kits resolve it. Keep the macOS signing identity in Initial Configuration; record the actual CMake executable/version (3.25+).
- [ ] 1.3 Reconfirm iOS hardware and simulator architecture support; record a usable iPadOS 18+ device and app/widget validation route. The previously recorded iPad7,4 cannot install this build; compatibility is not an A12 cutoff.
- [ ] 1.4 Record graphs-2d-high-performance-backend availability for every shipping platform from installed feature configuration/link dependencies; if absent, close backend adoption for that platform under the stock-runtime policy.
- [ ] 1.5 Check the released qtbase for the deadlock fix (Gerrit 735089 / QTBUG-140490 / QTBUG-144207); record fixed or known-issue status with tagged-source/review evidence. Do not rebuild the removed override.

## 2. Build configuration and packaging

- [ ] 2.1 Update six release workflows and nightly-sanitizers.yml to one Qt patch, including Windows sccache and Android Gradle cache keys; verify no active pin/key retains the old version.
- [ ] 2.2 Verify actual CMake is 3.25+ in kits and runners, separately from the policy baseline; configure without new Qt policy warnings. Set applicable policies only; QTP0006's Wayland generators are currently unused.
- [ ] 2.3 Set iOS to 18.0 and macOS to 14.4, reconciling CMake/CI overrides; inspect generated app/widget targets and packaged minimum-OS metadata for agreement.
- [ ] 2.4 Diff android/build.gradle and the manifest against the installed 6.12 templates; reconcile AGP/Gradle, resConfig filtering, and packaging hooks. Verify signed, correctly named APK/AAB output and native widget resources. JDK 21 is already configured; confirm NDK derivation and compile SDK while retaining API 28 minimum.
- [ ] 2.5 Review predictive-back manifest settings with DecenzaActivity and QML handlers; inspect the packaged manifest rather than assuming Qt's stock template replaces ours.
- [ ] 2.6 Re-test the Windows aqtsource pin and iOS Xcode pin against the chosen binaries; retain/remove each based on successful installation/build.
- [ ] 2.7 Inspect the generated Windows executable manifest for duplicate resources, long paths, compatibility, and elevation behavior; verify normal launch and installer/update operation. Check packaged OpenSSL libraries against Qt's actual ABI.
- [ ] 2.8 Verify CMake integrations including qmlcachegen generated-path derivation and qmllint target/response files; clean configure and lint coverage checks must reach every intended QML file.

## 3. QML runtime and tooling compatibility

- [ ] 3.1 Rebuild QML type information and run the existing qmllint gate; inspect opaque-type, shadowing, recursion, filename, and Array diagnostics by file/category. Fix real defects before baseline updates and keep full default warnings.
- [ ] 3.2 Exercise dynamic pages/components, Loaders, and delegates under the new missing-required-property behavior; verify screens instantiate without errors and consumed model roles remain available.
- [ ] 3.3 Verify CupFillView's existing C++ wrapper on Metal and Android; compare fills/animation before and after the upgrade and investigate new rendering warnings.

## 4. Chart data and labels

- [ ] 4.1 Capture pre-upgrade graph screenshots and tablet frame-time/history-scroll measurements using PERFORMANCE_BASELINE.md; preserve existing baseline rows.
- [ ] 4.2 Replace HistoryShotGraph's static append loops with values assignments; compare pressure, flow, weight-flow, resistance, conductance, and Darcy series, including multipliers, empty input, and shot switching. Preserve mix-temperature/goal behavior outside these loops.
- [ ] 4.3 Replace FlowCalibrationPage's two static append loops with values assignments; verify coordinates and calibration readouts remain identical.
- [ ] 4.4 Update ProfileGraph preview series last, collecting arrays before assignment; compare every frame-shape branch and edits. Use implicit X spacing only for proven uniform samples. Keep live FastLineRenderer and DashedLineSeries unchanged.
- [ ] 4.5 Apply labelPostFormat on useful single-unit axes, starting with flow calibration seconds; compare screenshots, preserve translated bindings, and keep multi-unit axes' units in titles.
- [ ] 4.6 Compare shared-axis margins and evaluate dynamicLabelMargins on history/comparison graphs; verify narrow screens, translated labels, hand-tuned margins, and zero-label placement. Remove only overrides made redundant by the result.

## 5. Conditional chart backend and closed visual gaps

- [ ] 5.1 Where the stock painter backend is available, prove a graph selects it before benchmarking, through a controlled transition/readback or equivalent runtime evidence. A change signal alone cannot prove a selection already at its default.
- [ ] 5.2 Where supported and beneficial, adopt the backend on the six migrated GraphsViews; verify overlays, clipping, colors, live traces, and frame times. Handle differing platform availability; no no-op flags or custom Qt binaries.
- [ ] 5.3 Record backend-confirmed tablet measurements in PERFORMANCE_BASELINE.md without overwriting earlier rows; accept/decline adoption against rendering parity and existing frame-time targets, not an assumed FastLineRenderer speedup.
- [ ] 5.4 Test whether clipPlotArea removes the apparent long ticks; if true ticks remain unconfigurable, retain the gap and prepare an upstream suggestion with shader evidence. Filing externally requires separate authorization.
- [ ] 5.5 Recheck leftmost label alignment after margin changes; record resolved-by-reflow or accepted gap, without a new overlay.

## 6. Bounded API evaluations

- [ ] 6.1 Compare QtCanvas2D with both CupFillView wrapper uses: drawing operations, paint/context semantics, synchronization, QML type visibility, and Metal/Android output/performance. Record adopt/defer with the TP tradeoff; remove the wrapper only if adopted and parity/deployment are verified.
- [ ] 6.2 Evaluate setExternalSingletonInstance for app-owned objects; retain compile-time registration, identity assertions, setup-before-use, platform guards, and lifetime past engine destruction. Check failure returns and use existing registration tests where appropriate; record adoption or reason to defer.
- [ ] 6.3 Compare Qt.escapeHtml and Color against helper contracts, including special characters, markup, alpha, and contrast; adopt only equivalents and record the result. Keep browser JavaScript independent of Qt.
- [ ] 6.4 Evaluate ImhNoFullscreen, ImhDecimalNumbersOnly, and motionPreference on tablet controls/decorative animations; preserve signed inputs, numeric validation, IME commit, and telemetry. Record verified adoption or reason to defer.

## 7. Integration and device validation

- [ ] 7.1 Build and run the full suite through Qt Creator MCP; verify desktop Debug still enables ASan/UBSan, record results, and investigate new warnings. Ask the user to start/restart the app for live checks.
- [ ] 7.2 Dispatch six platform workflows and the sanitizer workflow on the branch; verify artifacts/tests, including linux-release.yml's suite, and read text-invariants PR results. Record platform-conditional and tsnet coverage accurately.
- [ ] 7.3 Test Android gesture/button back through dialogs, unsaved editors, nested pages, root, and resume; verify root back backgrounds the task and machine operation, BLE, and foreground service remain correct.
- [ ] 7.4 Test TalkBack typing echo, keyboard focus, scroll/show-on-screen actions, expanded states, and navigation; regressions require app/upstream remedies or holding the upgrade, not a patched Qt binary.
- [ ] 7.5 Verify Android DE1/scale discovery, scan cancellation/restart, and connection on hardware; recheck earlier UUID/length parsing and canceled() timing findings against the release. Complete a shot with weight, graphs, save, and Visualizer upload.
- [ ] 7.6 Run macOS simulated extraction, WiFi scale, screenshots, cup rendering, and optional Quick3D screensavers; verify signing identity and font/rendering behavior. Check representative history/import dates after the parser change.
- [ ] 7.7 On supported iOS hardware, verify launch, BLE scale, a shot, and Home Screen widget snapshots; record device/OS coverage and any remaining validation hold.
- [ ] 7.8 Compare tablet graph frame times/history scrolling with the baseline and screenshots for visible changes; verify adopted changes and record every declined conditional item.

## 8. Documentation and completion

- [ ] 8.1 Update versions, paths, tool/platform requirements, and badges in CLAUDE.md, README.md, openspec/config.yaml, PLATFORM_BUILD.md, TESTING.md, IOS_CI_SETUP.md, and IOS_CI_FOR_CLAUDE.md; label historical BUILD_PERFORMANCE measurements accurately. Have Jeff update uncommitted kit/path notes.
- [ ] 8.2 Write short release notes explaining iOS/iPadOS 18 and macOS 14.4 as Qt requirements, with accurate affected-device/OS wording; check final Qt and Apple compatibility sources.
- [ ] 8.3 Review/update wiki platform minimums and adopted user-visible changes; prepare concrete edits and follow the repository's release timing for publishing.
- [ ] 8.4 Reconcile tasks with results, validate OpenSpec, and review the implementation PR; archive upgrade-qt-6-12 as its final commit before a requested merge, then push/read checks. Declining an optional adoption resolves its evaluation; required validation not performed remains a hold.
