# Building Decenza on Raspberry Pi 5

This guide covers building and running Decenza DE1 on a Raspberry Pi 5.

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
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-connectivity-dev \
    qt6-webengine-dev \
    qt6-quick3d-dev \
    qt6-charts-dev \
    qt6-svg-dev \
    qt6-multimedia-dev \
    qt6-speech-dev \
    qt6-positioning-dev \
    libxkbcommon-dev \
    libgl1-mesa-dev
```

This installs Qt 6.8.2 from Debian repositories (close to the project's target of 6.10.1).

## 3. Clone and Build

```bash
cd ~
git clone https://github.com/Kulitorum/Decenza.git
cd Decenza

# Create logo directory and copy qrcode.png (gitignored)
mkdir -p logo
# Copy logo/qrcode.png from another source if needed

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j2
```

**Important:** Use `make -j2` (2 parallel jobs), not `make -j4`. Using all 4 cores simultaneously can cause power issues and crash the Pi if your power supply isn't robust enough.

Build time: ~10 minutes with `-j2`

## 4. Run

### With a display connected (HDMI)

```bash
cd ~/Decenza/build
./Decenza_DE1 -platform eglfs
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
Description=Decenza DE1
After=network.target

[Service]
Type=simple
User=pi
Environment=QT_QPA_PLATFORM=eglfs
ExecStart=/home/pi/Decenza/build/Decenza_DE1
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
- Random crashes during `make -j4`
- System becoming unresponsive

Solutions:
- Use the official Raspberry Pi 5 27W power supply
- Reduce parallel jobs: `make -j1` or `make -j2`
- Use a high-quality USB-C cable (some cables can't handle high current)

### Display issues

If the app fails with "no Qt platform plugin":

```bash
# Check available platforms
./Decenza_DE1 -platform help

# Try different platforms
./Decenza_DE1 -platform eglfs    # Direct framebuffer (recommended)
./Decenza_DE1 -platform linuxfb  # Fallback framebuffer
./Decenza_DE1 -platform vnc      # VNC server (access remotely)
```

### BLE permissions

For full Bluetooth functionality:

```bash
sudo setcap 'cap_net_admin+eip' ~/Decenza/build/Decenza_DE1
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

### Tested Configuration

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
