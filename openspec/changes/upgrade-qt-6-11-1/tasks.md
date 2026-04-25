# Tasks: Upgrade Qt from 6.10.3 to 6.11.1

## Prerequisites
- [ ] Install Qt 6.11.1 on the Windows dev machine (MSVC 2022 x64 target, same Qt Maintenance Tool procedure as 6.10.3)
- [ ] Install Qt 6.11.1 on Jeff's Mac (macOS + iOS targets; run Qt Maintenance Tool)

## Source Changes

- [ ] Update `CMakeLists.txt`:
  - Update the comment on the `CMAKE_OSX_DEPLOYMENT_TARGET` line from "Qt 6.10.3 was built for iOS 17.0" to "Qt 6.11.1 was built for iOS 17.0" (verify iOS minimum still 17.0)
  - After first Windows build, check for any new QTP policy warnings in the CMake configure output; add any new `qt_policy(SET QTPxxxx NEW)` entries inside the existing `VERSION_GREATER_EQUAL "6.5.0"` guard
- [ ] Update `CLAUDE.md`:
  - `**Qt version**: 6.10.3` → `6.11.1`
  - `**Qt path**: C:/Qt/6.10.3/msvc2022_64` → `C:/Qt/6.11.1/msvc2022_64`
  - All three command-line build commands referencing `C:/Qt/6.10.3/msvc2022_64`
  - macOS `qt-cmake` paths (`/Users/mic/Qt/6.10.3/ios/bin/qt-cmake` and `macos/bin/qt-cmake`)
  - macOS build directory names (`build/Qt_6_10_3_for_iOS` → `build/Qt_6_11_1_for_iOS`, etc.)
- [ ] Update `openspec/project.md`:
  - Tech stack line: `Qt 6.10.3` → `Qt 6.11.1`
  - Windows path: `C:/Qt/6.10.3/msvc2022_64` → `C:/Qt/6.11.1/msvc2022_64`

## CI/CD Workflow Updates

- [ ] Update `.github/workflows/windows-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`
  - `key: sccache-windows-x64-qt6.10.3` → `key: sccache-windows-x64-qt6.11.1`
  - Update in-comment reference "Qt 6.10.3 for Windows" / "ABI-incompatible with Qt 6.10.3" to 6.11.1
- [ ] Update `.github/workflows/macos-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`
- [ ] Update `.github/workflows/ios-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`
- [ ] Update `.github/workflows/android-release.yml`:
  - `version: '6.10.3'` → `version: '6.11.1'`
  - Update "Fallback to known-good version for Qt 6.10.x" comment to 6.11.x
- [ ] Update `.github/workflows/linux-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`
- [ ] Update `.github/workflows/linux-arm64-release.yml`: `version: '6.10.3'` → `version: '6.11.1'`

## Build Validation

- [ ] Build on Windows (Qt Creator) — confirm zero errors, note any new QTP warnings and resolve per CMakeLists.txt task above
- [ ] Build on macOS (Qt Creator) — confirm zero errors
- [ ] Push a tag to trigger CI; confirm all 6 platform builds pass:
  - Windows build passes
  - macOS build passes
  - iOS build passes
  - Android build passes
  - Linux x64 build passes
  - Linux arm64 build passes

## Smoke Testing

- [ ] Smoke test on Windows: launch app, connect to DE1 (or simulator), pull a shot, verify graphs render
- [ ] Smoke test on macOS: launch app, connect to DE1 (or simulator), verify BLE and graphs
- [ ] Smoke test on Android (Decent tablet or phone): verify BLE scanning, scale connection, live shot graph
- [ ] Smoke test on iOS (device): verify BLE, shot history, graphs

## Qt Canvas Painter — CupFillView

- [ ] Add `find_package(Qt6 QUIET COMPONENTS CanvasPainter)` to `CMakeLists.txt` (after the ShaderTools block; same quiet/optional pattern)
- [ ] Replace both `Canvas` items in `qml/components/CupFillView.qml` with `CanvasPainterItem`:
  - `liquidCanvas` (line ~153) — liquid fill, crema, waves, ripples
  - `effectsCanvas` (line ~370) — portafilter stream, steam wisps, completion glow
  - Import `QtCanvasPainter` at the top of the file
  - Keep all `ctx.*` drawing code unchanged — the 2D API is identical
  - Replace `renderStrategy: Canvas.Threaded` (not applicable to CanvasPainterItem — remove it)
- [ ] Evaluate on Windows: visual match against Canvas baseline (side-by-side at idle and during live extraction)
- [ ] Evaluate on macOS: same visual match check
- [ ] Evaluate on Android (Decent tablet): run a live shot and assess animation smoothness vs. Canvas baseline
- [ ] Evaluate on iOS: visual match check
- [ ] Decision: if all platforms pass visual + smoothness evaluation, ship; if any platform has rendering issues, revert `CupFillView.qml` and note the blocker for Qt 6.12 follow-up

## Follow-up

- [ ] Update `migrate-charting-to-qt-graphs/proposal.md`: mark gate condition #4 (Qt 6.11.1 upgrade) as ✅ satisfied
- [ ] Update `migrate-charting-to-qt-graphs/tasks.md`: set Stage 0 status to ready-to-start
