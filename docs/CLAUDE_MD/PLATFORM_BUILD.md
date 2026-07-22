## Command Line Build (human / CI reference)

> **Not for assistant use.** An assistant builds and tests through the Qt Creator MCP
> (`mcp__qtcreator__build`, `mcp__qtcreator__run_tests`) and nothing else — see the Building
> section of the root `CLAUDE.md`. These commands are here for humans building by hand and for
> reproducing what CI does. If the MCP path is blocked, stop and ask rather than running them.

### Windows (MSVC)

MSVC environment variables (INCLUDE, LIB) are set permanently. Use Visual Studio generator (Ninja not in PATH).

**Configure Release:**
```bash
rm -rf build/Release && mkdir -p build/Release && cd build/Release && cmake ../.. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64"
```

**Build Release (parallel):**
```bash
cd build/Release && unset CMAKE_BUILD_PARALLEL_LEVEL && MSYS_NO_PATHCONV=1 cmake --build . --config Release -- /m
```

**Configure Debug:**
```bash
rm -rf build/Debug && mkdir -p build/Debug && cd build/Debug && cmake ../.. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64" -DCMAKE_BUILD_TYPE=Debug
```

**Build Debug (parallel):**
```bash
cd build/Debug && unset CMAKE_BUILD_PARALLEL_LEVEL && MSYS_NO_PATHCONV=1 cmake --build . --config Debug -- /m
```

Notes: `unset CMAKE_BUILD_PARALLEL_LEVEL` avoids conflicts with `/m`. `MSYS_NO_PATHCONV=1` prevents bash from converting `/m` to `M:/`. The `/m` flag enables MSBuild parallel compilation.

**Output locations:**
- Release: `build/Release/Release/Decenza.exe`
- Debug: `build/Debug/Debug/Decenza.exe`

### macOS / iOS (on Mac)

Use Qt's `qt-cmake` wrapper which handles cross-compilation correctly.

**Finding Qt paths.** Qt is installed at `~/Qt/`. Discover paths dynamically:
```bash
# Find qt-cmake for macOS
find ~/Qt -name "qt-cmake" -path "*/macos/*"
# Find Ninja (bundled with Qt)
find ~/Qt/Tools -name "ninja"
```

**Configure iOS (generates Xcode project):**
```bash
rm -rf build/Qt_6_11_1_for_iOS && mkdir -p build/Qt_6_11_1_for_iOS && cd build/Qt_6_11_1_for_iOS && /Users/mic/Qt/6.11.1/ios/bin/qt-cmake ../.. -G Xcode
```

**Inject the Home Screen widget extension (iOS only, after configure):**

`qt-cmake -G Xcode` emits only the single app target and the generated
`.xcodeproj` is not checked in, so the WidgetKit app-extension target is added
by a script that must be re-run after every (re)configure:
```bash
gem install xcodeproj   # once
ruby ios/inject_widget_extension.rb build/Qt_6_11_1_for_iOS/Decenza.xcodeproj "$(git rev-parse --show-toplevel)"
```
CI (`.github/workflows/ios-release.yml`) runs this automatically between
"Configure CMake" and "Build and Archive".

**One-time Apple Developer portal + GitHub secrets prerequisites** (human-only
— cannot be scripted; the App Store build will fail signing without them):

- Create a second App ID `io.github.kulitorum.decenza.widget`.
- Enable the **App Groups** capability `group.io.github.kulitorum.decenza` on
  **both** App IDs (`io.github.kulitorum.decenza` and `…​.widget`).
- Create a distribution provisioning profile for the widget App ID.
- Add GitHub Actions secrets:
  - `WIDGET_PROVISIONING_PROFILE_BASE64` — base64 of that `.mobileprovision`
  - `WIDGET_PROVISIONING_PROFILE_NAME` — the profile's exact name
- The existing app profile must also include the App Group entitlement
  (regenerate it after enabling the capability).

> **Custom bundle IDs:** the App Group is fixed at
> `group.io.github.kulitorum.decenza` (and the widget bundle id at
> `io.github.kulitorum.decenza.widget`). A local dev build with a custom
> `-DIOS_BUNDLE_ID` (e.g. `…decenza.jefftest`) will build, but the widget
> cannot share the App Group unless that group is also provisioned for the
> custom App ID — so on `jefftest` the widget shows "Disconnected". This is
> expected; the widget is validated on the production bundle id / TestFlight.

**Configure macOS (generates Xcode project):**
```bash
rm -rf build/Qt_6_11_1_for_macOS && mkdir -p build/Qt_6_11_1_for_macOS && cd build/Qt_6_11_1_for_macOS && /Users/mic/Qt/6.11.1/macos/bin/qt-cmake ../.. -G Xcode
```

**Open in Xcode:**
```bash
open build/Qt_6_11_1_for_iOS/Decenza.xcodeproj
# or
open build/Qt_6_11_1_for_macOS/Decenza.xcodeproj
```

Then in Xcode: Product → Archive for App Store submission.

## Windows Installer

The Windows installer is built with Inno Setup (`installer/setup.iss`). It uses a local config file `installer/setupvars.iss` (gitignored) to define machine-specific paths.

### Local setupvars.iss
Copy `installer/TEMPLATE_setupvars.iss` to `installer/setupvars.iss` and adjust paths for your machine:
```iss
#define SourceDir "C:\CODE\de1-qt"
#define AppBuildDir "C:\CODE\de1-qt\build\Desktop_Qt_6_10_1_MSVC2022_64bit-Release"
#define AppDeployDir "C:\CODE\de1-qt\installer\deploy"
#define QtDir "C:\Qt\6.11.1\msvc2022_64"
#define VcRedistDir "C:\Qt\vcredist"
#define VcRedistFile "vc14.50.35719_VC_redist.x64.exe"
; Optional - not in TEMPLATE; add manually if OpenSSL is installed separately
; #define OpenSslDir "C:\Program Files\OpenSSL-Win64\bin"
```

### OpenSSL Dependency
The app links OpenSSL directly (for TLS certificate generation in Remote Access). The installer must bundle `libssl-3-x64.dll` and `libcrypto-3-x64.dll`. If `OpenSslDir` is defined in `setupvars.iss`, the installer copies them automatically. Install OpenSSL for Windows from https://slproweb.com/products/Win32OpenSSL.html if you don't have it.

## Android Build & Signing

### Build Process
- **Local**: Qt Creator runs `androiddeployqt` with `--sign` flag
- **CI**: GitHub Actions workflow (`android-release.yml`) on ubuntu-24.04
- **Keystore**: `C:/CODE/Android APK keystore.jks` locally, `ANDROID_KEYSTORE_BASE64` secret on CI
- **Key alias**: `de1-key`
- **Keystore path**: `build.gradle` reads `ANDROID_KEYSTORE_PATH` env var, falls back to local path

### How Signing & Renaming Works
1. Build creates unsigned APK (`android-build-Decenza-release-unsigned.apk`)
2. `gradle.buildFinished` hook in `android/build.gradle` triggers:
   - Finds unsigned APK
   - Signs it with `apksigner` from Android SDK build-tools (cross-platform: `.bat` on Windows, no extension on Linux)
   - Outputs as `Decenza_<version>.apk`
3. For AAB: same hook copies (via `java.nio.file.Files.copy`) and signs with `jarsigner` to `Decenza-<version>.aab`
4. On CI, a fallback step signs the APK manually if the gradle hook fails

### Output Files
- **APK output**: `build/.../android-build-Decenza/build/outputs/apk/release/`
  - `Decenza_X.Y.Z.apk` (versioned, signed)
- **AAB output**: `build/.../android-build-Decenza/build/outputs/bundle/release/`
  - `Decenza-X.Y.Z.aab` (versioned, for Play Store)

## Decent Tablet Troubleshooting

The tablets shipped with Decent espresso machines have some quirks:

### GPS Not Working (Shot Map Location)
The GPS provider is **disabled by default** on these tablets. To enable:

**Via ADB:**
```bash
adb shell settings put secure location_providers_allowed +gps
```

**Via Android Settings:**
1. Settings → Location → Turn ON
2. Set Mode to "High accuracy" (not "Battery saving")

**Note:** These tablets don't have Google Play Services, so network-based location (WiFi/cell triangulation) won't work. GPS requires clear sky view (outdoors or near window) for first fix. The app supports manual city entry as a fallback.

### No Google Play Services
The tablet lacks Google certification, so:
- Network location unavailable (requires Play Services)
- Some Google apps may prompt to install Play Services
- GPS-only location works once enabled (see above)
