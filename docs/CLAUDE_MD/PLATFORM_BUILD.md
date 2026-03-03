## Windows Installer

The Windows installer is built with Inno Setup (`installer/setup.iss`). It uses a local config file `installer/setupvars.iss` (gitignored) to define machine-specific paths.

### Local setupvars.iss
Copy `installer/TEMPLATE_setupvars.iss` to `installer/setupvars.iss` and adjust paths for your machine:
```iss
#define SourceDir "C:\CODE\de1-qt"
#define AppBuildDir "C:\CODE\de1-qt\build\Desktop_Qt_6_10_1_MSVC2022_64bit-Release"
#define AppDeployDir "C:\CODE\de1-qt\installer\deploy"
#define QtDir "C:\Qt\6.10.2\msvc2022_64"
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
