# DE1 Controller

A Qt/QML controller application for the Decent DE1 espresso machine with Bluetooth LE connectivity.

![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux%20%7C%20Android%20%7C%20iOS-blue)
![Qt](https://img.shields.io/badge/Qt-6.8+-green)
![License](https://img.shields.io/badge/license-LGPL--3.0-orange)

## Features

- **Bluetooth LE connectivity** to DE1 espresso machine and compatible scales
- **Real-time shot monitoring** with pressure, flow, temperature, and weight graphs
- **Profile editor** with visual frame-based editing
- **Multiple scale support**: Acaia, Decent, Felicita, Skale, Bookoo, and more
- **Cross-platform**: Windows, macOS, Linux, Android, iOS

## Screenshots

*Coming soon*

## Requirements

### Qt Installation

1. Download and install Qt from [qt.io](https://www.qt.io/download-qt-installer)
2. Run the Qt Online Installer
3. Select **Qt 6.8** or newer (6.10+ recommended for best Android support)

### Required Qt Modules

Select the following components during installation:

**Core Modules:**
- Qt 6.x.x (select your target platforms)
  - Desktop (MSVC/GCC/Clang depending on your OS)
  - Android (if targeting Android)
  - iOS (if targeting iOS)

**Additional Libraries:**
- Qt Bluetooth
- Qt Charts
- Qt Multimedia
- Qt Quick (included by default)

**Developer Tools:**
- Qt Creator (recommended IDE)
- CMake (usually bundled)
- Ninja (usually bundled)

### Platform-Specific Setup

#### Windows
- Install Visual Studio 2022 with C++ workload, or
- Install MSVC Build Tools from Qt Maintenance Tool

#### macOS
- Install Xcode from the App Store
- Run `xcode-select --install` to install command line tools

#### Linux
```bash
# Ubuntu/Debian - install Bluetooth development libraries
sudo apt install libgl1-mesa-dev libbluetooth-dev
```

## Android Setup

### 1. Install Android Development Tools

In Qt Maintenance Tool, install:
- Android SDK (API 28+)
- Android NDK (r25+)
- OpenJDK 17

Or configure manually in Qt Creator: **Edit > Preferences > Devices > Android**

### 2. Configure Android SDK

Qt Creator should auto-detect the SDK. If not, set paths manually:
- **JDK Location**: Path to OpenJDK 17
- **Android SDK**: Usually `~/Android/Sdk` or `C:\Users\<name>\AppData\Local\Android\Sdk`
- **Android NDK**: Inside SDK folder, e.g., `ndk/26.1.10909125`

### 3. Create Android Kit

1. Go to **Edit > Preferences > Kits**
2. Qt should auto-create Android kits for arm64-v8a and armeabi-v7a
3. Ensure the kit shows green checkmarks (no errors)

### 4. Build for Android

1. Select the Android kit in the bottom-left kit selector
2. Click Build (Ctrl+B / Cmd+B)
3. Deploy to connected device or emulator

**Note:** A physical device is required for Bluetooth testing (emulators don't support BLE).

## Building

### From Qt Creator (Recommended)

1. Open `CMakeLists.txt` in Qt Creator
2. Select your kit (Desktop or Android)
3. Click Configure Project
4. Build and Run (Ctrl+R / Cmd+R)

### From Command Line

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64 ..

# Build
cmake --build . --parallel

# Run
./de1-qt
```

## Project Structure

```
de1-qt/
├── src/
│   ├── ble/           # Bluetooth LE device communication
│   │   ├── de1device  # DE1 machine protocol
│   │   ├── scaledevice # Scale abstraction
│   │   └── scales/    # Scale implementations (Acaia, Decent, etc.)
│   ├── controllers/   # Business logic
│   ├── core/          # Settings, utilities
│   ├── machine/       # Machine state management
│   ├── models/        # Data models (shot data, profiles)
│   └── profile/       # Profile parsing and management
├── qml/
│   ├── components/    # Reusable QML components
│   └── pages/         # Application pages
├── resources/         # Icons, images
├── profiles/          # Built-in espresso profiles
└── android/           # Android-specific files
```

## Supported Scales

| Scale | Status |
|-------|--------|
| Acaia Lunar/Pearl/Pyxis | Supported |
| Decent Scale | Supported |
| Felicita Arc | Supported |
| Skale | Supported |
| Bookoo | Supported |
| Eureka Precisa | Supported |
| DiFluid Microbalance | Supported |
| Hiroia Jimmy | Supported |
| Varia Aku | Supported |
| SmartChef | Supported |

## Troubleshooting

### Bluetooth not working on Linux
```bash
# Add user to bluetooth group
sudo usermod -aG bluetooth $USER
# Restart or re-login
```

### Android build fails with "SDK not found"
- Open Qt Creator Preferences > Devices > Android
- Click "Set Up SDK" to auto-download components
- Ensure all status indicators show green checkmarks

### Scale not connecting
- Ensure scale is in pairing mode
- Check that Bluetooth is enabled on your device
- On Android, grant Location permission (required for BLE scanning)

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## License

This project is licensed under the **GNU Lesser General Public License v3.0 (LGPL-3.0)**.

This means:
- You can use this software freely, including in commercial applications
- You can modify the source code
- If you distribute modified versions, you must make your modifications available under LGPL-3.0
- You must provide attribution to the original project

### Third-Party Licenses

- **Qt Framework**: LGPL-3.0 / Commercial ([qt.io/licensing](https://www.qt.io/licensing))
- **FFmpeg** (via Qt Multimedia): LGPL-2.1+

### DE1 Protocol

This application implements the DE1 Bluetooth LE protocol based on publicly available documentation from Decent Espresso.

## Acknowledgments

- [Decent Espresso](https://decentespresso.com/) for creating the DE1 machine
- The Qt Project for the excellent cross-platform framework
- The open-source coffee community for protocol documentation
