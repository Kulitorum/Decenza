# Building Decenza on Raspberry Pi 5

This guide covers building and running Decenza on a Raspberry Pi 5. Decenza requires **Qt 6.12.0 or newer**; the current project toolchain uses Qt 6.12.0.

## Prerequisites

- Raspberry Pi 5 (4GB+ RAM recommended)
- 16GB+ SD card (32GB recommended for comfortable development)
- **Good power supply** (5V/5A, 27W - Pi 5 needs proper power during compilation!)
- Network connection (Ethernet or WiFi)
- Micro-HDMI cable for display

## 1. Flash Raspberry Pi OS

Use [Raspberry Pi Imager](https://www.raspberrypi.com/software/) to flash **Raspberry Pi OS Lite (64-bit)** to SD card.

In the imager settings (gear icon), configure:
- Enable SSH
- Set username and password
- Configure WiFi (optional, if not using Ethernet)

## 2. Install Dependencies

SSH into your Pi and run:

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    python3 \
    libssl-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev
```

These are system dependencies, **not the Qt SDK**. The previously documented Debian Qt 6.8.2 packages are too old.

Install a Qt 6.12.0 AArch64 development kit compatible with your Pi OS, including the add-on modules listed under profiles `app` and `desktop` in [`.github/qt-modules.txt`](../.github/qt-modules.txt). Include Qt's private development headers and EGLFS/KMS support for running on Raspberry Pi OS Lite. If building Qt yourself, follow Qt's [Linux source build instructions](https://doc.qt.io/qt-6.12/linux-building.html) and [embedded Linux configuration guide](https://doc.qt.io/qt-6.12/configure-linux-device.html); additional graphics and module dependencies depend on the kit configuration.

Set the prefix to your actual AArch64 Qt installation:

```bash
export DECENZA_QT_PREFIX=/usr/local/Qt-6.12.0
```

## 3. Clone and Build

```bash
cd ~
git clone https://github.com/Kulitorum/Decenza.git
cd Decenza

# Create logo directory and copy qrcode.png (gitignored)
mkdir -p logo
# Copy logo/qrcode.png from another source if needed

# Build the native app without the optional bundled browser client
"$DECENZA_QT_PREFIX/bin/qt-cmake" -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DDECENZA_WEB_BUNDLE_MODE=OFF
cmake --build build --parallel 2
```

For a build with the bundled browser client, follow the [web client toolchain setup](../tools/webclient/README.md#one-time-toolchain-setup) and leave `DECENZA_WEB_BUNDLE_MODE=LOCAL`, or supply a matching prebuilt bundle as documented there.

**Important:** Start with 2 parallel jobs. Using all 4 cores simultaneously can cause power issues and crash the Pi if your power supply isn't robust enough.

## 4. Run

### With a display connected (HDMI)

```bash
cd ~/Decenza/build
./Decenza -platform eglfs
```

The `eglfs` platform renders directly to the framebuffer, which is ideal for kiosk/embedded use.

### Alternative platforms

- `linuxfb` - Linux framebuffer (fallback)
- `xcb` - X11 (requires desktop environment)
- `wayland` - Wayland (requires Wayland compositor)

### Auto-start on boot

Create a systemd service:

```bash
sudo nano /etc/systemd/system/decenza.service
```

```ini
[Unit]
Description=Decenza
After=network.target

[Service]
Type=simple
User=pi
Environment=QT_QPA_PLATFORM=eglfs
ExecStart=/home/pi/Decenza/build/Decenza
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable decenza
sudo systemctl start decenza
```

## Troubleshooting

### Power issues (red LED, crashes during build)

The Pi 5 requires a good 5V/5A power supply. During heavy compilation, it draws significant power. If you see:
- Red power LED
- Random crashes when building with 4 parallel jobs
- System becoming unresponsive

Solutions:
- Use the official Raspberry Pi 5 27W power supply
- Reduce parallel jobs: from the checkout, use `cmake --build build --parallel 1` or `--parallel 2`
- Use a high-quality USB-C cable (some cables can't handle high current)

### Display issues

If the app fails with "no Qt platform plugin":

```bash
# Check available platforms
./Decenza -platform help

# Try different platforms
./Decenza -platform eglfs    # Direct framebuffer (recommended)
./Decenza -platform linuxfb  # Fallback framebuffer
./Decenza -platform vnc      # VNC server (access remotely)
```

### BLE permissions

For full Bluetooth functionality:

```bash
sudo setcap 'cap_net_admin+eip' ~/Decenza/build/Decenza
```

Or run as root (not recommended for production).

### Missing qrcode.png

The `logo/qrcode.png` file is gitignored. If the build fails with:
```
No rule to make target 'logo/qrcode.png'
```

Create the logo directory and add the file:
```bash
mkdir -p ~/Decenza/logo
# Copy qrcode.png from another source
```

## Hardware Notes

### Historical Tested Configuration

The configuration below predates the current Qt requirement. Its Qt 6.8.2 installation cannot build current Decenza; a Qt 6.12 build and runtime check on the Pi is still needed.

- Raspberry Pi 5 Model B (4GB RAM)
- Raspberry Pi OS Lite (Debian Trixie, kernel 6.12)
- Qt 6.8.2 from Debian repositories
- 16GB SD card

### Display Connection

The Pi 5 uses **micro-HDMI** ports (not full-size HDMI). You need either:
- A micro-HDMI to HDMI cable
- A micro-HDMI to HDMI adapter + regular HDMI cable

### USB-C Port

The Pi 5's USB-C port is **power-only** - it does not support USB gadget/OTG mode like the Pi 4 or Pi Zero. You cannot use it for USB serial console or USB networking.
