# Decenza DE1

Qt/C++ cross-platform controller for the Decent Espresso DE1 machine with BLE connectivity.

## Development Environment

- **ADB path**: `/c/Users/Micro/AppData/Local/Android/Sdk/platform-tools/adb.exe`
- **Uninstall app**: `adb uninstall io.github.kulitorum.decenza_de1`
- **Qt version**: 6.8+
- **C++ standard**: C++17

## Project Structure

```
src/
├── ble/                    # Bluetooth LE layer
│   ├── de1device.*         # DE1 machine protocol
│   ├── blemanager.*        # Device discovery
│   ├── scaledevice.*       # Abstract scale interface
│   ├── protocol/           # BLE UUIDs, binary codec
│   └── scales/             # Scale implementations (14+ types)
├── controllers/
│   ├── maincontroller.*    # App logic, profiles, shot processing
│   └── directcontroller.*  # Direct frame control mode
├── machine/
│   └── machinestate.*      # Phase tracking, stop-at-weight
├── profile/
│   ├── profile.*           # Profile container, JSON/TCL formats
│   └── profileframe.*      # Single extraction step
├── models/
│   └── shotdatamodel.*     # Shot data for graphing
├── core/
│   ├── settings.*          # QSettings persistence
│   ├── batterymanager.*    # Smart charging control
│   └── batterydrainer.*    # Battery drain test utility
└── main.cpp                # Entry point, object wiring

qml/
├── pages/                  # EspressoPage, SettingsPage, etc.
├── components/             # ShotGraph, StatusBar, etc.
└── Theme.qml               # Singleton styling
```

## Key Architecture

### Signal/Slot Flow
```
DE1Device (BLE) → signals → MainController → ShotDataModel → QML graphs
ScaleDevice     → signals → MachineState (stop-at-weight)
                          → MainController (graph data)
```

### Scale System
- **ScaleDevice**: Abstract base class
- **FlowScale**: Virtual scale from DE1 flow data (fallback when no physical scale)
- **Physical scales**: DecentScale, AcaiaScale, FelicitaScale, etc. (factory pattern)

### Machine Phases
```
Disconnected → Sleep → Idle → Heating → Ready
Ready → EspressoPreheating → Preinfusion → Pouring → Ending
Also: Steaming, HotWater, Flushing
```

## Conventions

### C++
- Classes: `PascalCase`
- Methods/variables: `camelCase`
- Members: `m_` prefix
- Slots: `onEventName()`
- Use `Q_PROPERTY` with `NOTIFY` for bindable properties

### QML
- Files: `PascalCase.qml`
- IDs/properties: `camelCase`
- Use `Theme.qml` singleton for all styling

## Profile System

- **FrameBased mode**: Upload to machine, executes autonomously
- **DirectControl mode**: App sends setpoints frame-by-frame
- Formats: JSON (native), TCL (de1app import)
- Tare happens when frame 0 starts (after machine preheat)

## BLE Protocol Notes

- DE1 Service: `0000A000-...`
- Command queue prevents BLE overflow (50ms between writes)
- Shot samples at ~5Hz during extraction
- Profile upload: header (5 bytes) + frames (8 bytes each)
- USB charger control: MMR address `0x803854` (1=on, 0=off)
- DE1 has 10-minute timeout that auto-enables charger; must resend command every 60s

## Battery Management

### Smart Charging (BatteryManager)
- **Off**: Charger always on (no control)
- **On** (default): Maintains 55-65% charge
- **Night**: Maintains 90-95% charge
- Commands sent every 60 seconds with `force=true` to overcome DE1 timeout

### Battery Drainer (testing utility)
- Spawns CPU workers on all cores (heavy math: primes, trig, matrix ops)
- Enables max screen brightness and flashlight (Android JNI)
- Real CPU usage from `/proc/stat`, GPU usage from sysfs (device-specific)
- Full-screen overlay with tap-to-stop

## Platforms

- Desktop: Windows, macOS, Linux
- Mobile: Android (API 28+), iOS (14.0+)
- Android needs Location permission for BLE scanning

## Android Build & Signing

### Build Process
- **Build tool**: Qt's `androiddeployqt.exe` handles APK creation and signing
- **Keystore**: `C:/CODE/Android APK keystore.jks` (configured in Qt Creator, stored in `CMakeLists.txt.user`)
- **Key alias**: `de1-key`
- **Signing**: Automatic during release build via `--sign` flag

### Output Files
- **APK output**: `build/Qt_6_10_1_for_Android_arm64_v8a-Release/android-build-Decenza_DE1/build/outputs/apk/release/`
- **AAB output**: `build/Qt_6_10_1_for_Android_arm64_v8a-Release/android-build-Decenza_DE1/build/outputs/bundle/release/`
- **Versioned naming**: Handled in `android/build.gradle` (renames to `Decenza_DE1_<version>.apk/.aab`)

### Gradle Tasks
- `assembleRelease`: Builds signed APK
- `bundleRelease`: Builds signed AAB (for Play Store)
- Post-build renaming tasks in `android/build.gradle` copy and rename outputs with version

## Git Workflow

- **IMPORTANT**: Always push with tags: `git push && git push --tags`
- Build numbers auto-increment and create `build-N` tags
- Tags allow users to reference exact versions by build number
