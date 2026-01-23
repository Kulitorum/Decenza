# Project Context

## Purpose
Decenza DE1 is a cross-platform controller application for the Decent Espresso DE1 espresso machine. It communicates with the machine over Bluetooth Low Energy (BLE) to manage espresso extraction profiles, steaming, hot water, and flushing. It also integrates with various Bluetooth scales for stop-at-weight functionality, uploads shots to visualizer.coffee, and provides a rich QML-based UI for real-time shot graphing and profile editing.

## Tech Stack
- **Language**: C++17
- **Framework**: Qt 6.10.1 (Core, Gui, Widgets, Qml, Quick, QuickControls2, Bluetooth, Charts, Svg, Multimedia, TextToSpeech, Network, Sql, Positioning)
- **UI**: QML with Qt Quick Controls 2
- **Build system**: CMake (3.21+)
- **BLE**: Qt Bluetooth module for DE1 machine protocol and scale communication
- **MQTT**: Eclipse Paho MQTT C (v1.3.13, fetched via FetchContent)
- **Optional**: Qt Quick3D (for 3D screensavers)
- **Platforms**: Windows (MSVC 2022), macOS, Linux, Android (API 28+), iOS (14.0+)
- **CI/CD**: GitHub Actions (iOS builds with App Store upload)

## Project Conventions

### Code Style
- **Classes**: `PascalCase`
- **Methods/variables**: `camelCase`
- **Member variables**: `m_` prefix (e.g., `m_device`, `m_settings`)
- **Slots**: Named `onEventName()` (e.g., `onPhaseChanged()`)
- **Properties**: Use `Q_PROPERTY` with `NOTIFY` signal for QML-bindable properties
- **QML files**: `PascalCase.qml`
- **QML IDs/properties**: `camelCase`
- **Styling**: Use `Theme.qml` singleton for all colors, fonts, spacing
- **Text fields**: Use `StyledTextField` instead of raw `TextField`
- **Buttons**: `ActionButton` dims icon/text when disabled
- **Reserved words**: Use `nativeName` instead of `native` (JS reserved keyword)

### Architecture Patterns
- **Signal/Slot flow**: `DE1Device` (BLE) → signals → `MainController` → `ShotDataModel` → QML graphs
- **Scale architecture**: Abstract `ScaleDevice` base, `FlowScale` virtual fallback, factory pattern for 14+ physical scale types
- **Profile modes**: FrameBased (machine-autonomous) and DirectControl (app sends frame-by-frame)
- **Navigation**: QML `StackView` with auto-navigation on phase changes
- **Machine phases**: Disconnected → Sleep → Idle → Heating → Ready → Espresso/Steam/HotWater/Flush → Ending
- **Settings**: `QSettings`-based persistence via `Settings` class with Q_PROPERTY bindings
- **BLE protocol**: Command queue with 50ms spacing, 5-second timeout, up to 3 retries per write

### Testing Strategy
- No automated test suite currently in place
- Manual testing on physical DE1 hardware and Android/iOS devices
- BLE simulator available (`src/simulator/`) for development without hardware

### Git Workflow
- **Branch**: Single `main` branch
- **Versioning**: Display version in `CMakeLists.txt` (`project(... VERSION x.y.z)`), auto-incrementing version code in `versioncode.txt`
- **Tags**: Push with tags (`git push && git push --tags`); build tags created automatically
- **Commits**: Descriptive messages; always include changed version files (`versioncode.txt`, `android/AndroidManifest.xml`)
- **Releases**: Created via `gh release create` with APK attached; release notes cover user-facing changes only

## Build

**Important**: Prefer letting the user build in Qt Creator rather than CLI builds. Only use CLI commands if explicitly asked.

Full build commands for all platforms are documented in `claude.md` under "Command Line Build" and "macOS/iOS Build". Key points:

- **Windows**: Visual Studio 17 2022 generator, MSVC 2022, Qt at `C:/Qt/6.10.1/msvc2022_64`
- **macOS**: Use Qt's `qt-cmake` wrapper with Ninja generator; requires `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for paho_mqtt_c compatibility
- **iOS**: GitHub Actions CI/CD (see `docs/IOS_CI_FOR_CLAUDE.md`); bundle ID `io.github.kulitorum.decenza`
- **Android**: Built via Qt Creator's `androiddeployqt`; signed with keystore; outputs APK and AAB
- **Automation**: A `build.sh` script is available in the root directory for CLI builds:
  - `./build.sh --target <OSX|ANDROID> [--debug]`
- **Output**: `build/<config>/Decenza_DE1.exe` (Windows), `build/<config>/Decenza_DE1.app` (macOS)

## Domain Context
- **DE1 machine**: Decent Espresso DE1 is a high-end espresso machine with BLE connectivity, controllable extraction profiles (pressure/flow/temperature curves), and built-in sensors
- **Profiles**: Define extraction steps (frames) with target pressure/flow, temperature, exit conditions; uploaded to machine via BLE
- **Exit conditions**: Machine-side (pressure/flow thresholds checked by firmware) and app-side (weight from scale, app sends SkipToNext command) — these are independent
- **Scales**: Bluetooth scales (Acaia, Felicita, Decent, etc.) provide real-time weight for stop-at-weight and graphing
- **Shot data**: ~5Hz sampling during extraction; pressure, flow, weight, temperature tracked and graphed in real-time
- **Visualizer**: visualizer.coffee is a community shot-sharing platform; the app uploads shots and can import profiles from it
- **de1app**: The original Tcl/Tk controller app (reference implementation at `C:\code\de1app`)

## Important Constraints
- **BLE timing**: 50ms minimum between writes to prevent overflow; commands queued
- **No Google Play Services**: Decent tablets lack Google certification; GPS-only location
- **Android permissions**: Location permission required for BLE scanning
- **Qt WebEngine**: Required for Visualizer browser (not QtWebView — native WebView looks bad on old Android)
- **Font property conflict**: Cannot assign `font: Theme.bodyFont` and override sub-properties in QML; use individual properties instead
- **Keyboard handling**: Mobile pages with text inputs must use `KeyboardAwareContainer`
- **Battery management**: DE1 has 10-minute timeout that re-enables charger; app must resend charge commands every 60s
- **Build**: Don't build from CLI by default — let user build in Qt Creator (~50x faster)

## External Dependencies
- **Decent Espresso DE1**: BLE-connected espresso machine (DE1 Service UUID `0000A000-...`)
- **visualizer.coffee**: Community shot-sharing platform (upload shots, import profiles via REST API)
- **Eclipse Paho MQTT C**: For MQTT connectivity (v1.3.13)
- **Bluetooth scales**: Acaia, Felicita, Decent Scale, and 11+ other scale types via BLE
- **GitHub Actions**: iOS CI/CD with App Store Connect upload
- **App Store Connect**: iOS distribution
- **Google Play Store**: Android distribution (AAB format)
