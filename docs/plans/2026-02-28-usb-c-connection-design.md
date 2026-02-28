# USB-C Connection Support for DE1

**Date:** 2026-02-28
**Status:** Approved

## Overview

Add USB-C serial connectivity to the DE1 as an alternative to BLE. The DE1's USB-C interface uses a CH340/CH9102 USB-to-serial chip (VID `0x1A86`) that presents as a CDC-ACM device. When USB-C is connected, the DE1 disables Bluetooth, so only one transport is active at a time.

Target: all platforms (Android, Windows, macOS, Linux).

## Protocol

The USB serial protocol wraps the same binary payloads used by BLE in a text-based hex encoding:

- **Commands (host → DE1):** `<LETTER>hexdata\n` (e.g., `<B>02` = request idle)
- **Responses (DE1 → host):** `[LETTER]hexdata\n` (e.g., `[M]0102...` = shot sample)
- **Subscribe:** `<+LETTER>` / Unsubscribe: `<-LETTER>`
- **Serial config:** 115200 baud, 8N1, no flow control, DTR/RTS off

### Endpoint Mapping (letter → BLE UUID)

| Letter | UUID   | Purpose          |
|--------|--------|------------------|
| A      | A001   | Versions         |
| B      | A002   | RequestedState   |
| C      | A003   | SetTime          |
| D      | A004   | ShotDirectory    |
| E      | A005   | ReadFromMMR      |
| F      | A006   | WriteToMMR       |
| G      | A007   | ShotMapRequest   |
| H      | A008   | DeleteShotRange  |
| I      | A009   | FwMapRequest     |
| J      | A00A   | Temperatures     |
| K      | A00B   | ShotSettings     |
| L      | A00C   | DeprecatedShotDesc |
| M      | A00D   | ShotSample       |
| N      | A00E   | StateInfo        |
| O      | A00F   | HeaderWrite      |
| P      | A010   | FrameWrite       |
| Q      | A011   | WaterLevels      |
| R      | A012   | Calibration      |

## Architecture

### Transport Abstraction

Extract `DE1Transport` interface that both BLE and Serial implement:

```cpp
class DE1Transport : public QObject {
    Q_OBJECT
public:
    virtual void connectToDevice() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual void write(const QBluetoothUuid& uuid, const QByteArray& data) = 0;
    virtual void read(const QBluetoothUuid& uuid) = 0;
    virtual void subscribe(const QBluetoothUuid& uuid) = 0;

signals:
    void connected();
    void disconnected();
    void dataReceived(const QBluetoothUuid& uuid, const QByteArray& data);
    void errorOccurred(const QString& error);
};
```

Both transports deal in binary `QByteArray` payloads keyed by UUID. `SerialTransport` handles the text hex encoding/decoding internally.

### DE1Device Changes

- Holds `DE1Transport* m_transport`
- `setTransport(DE1Transport*)` swaps active transport
- All public slots (`requestState()`, `uploadProfile()`, etc.) call `m_transport->write()` instead of direct BLE calls
- Existing parsing methods (`parseShotSample()`, `parseStateInfo()`, etc.) remain unchanged — they receive the same binary data

### USB Discovery (USBManager)

- Polls `QSerialPortInfo::availablePorts()` every 2 seconds
- Filters by VID `0x1A86` (any PID from QinHeng/WCH)
- Probes new ports: opens 115200/8N1, sends `<+M>`, waits 2s for `[M]` response
- Auto-connects on DE1 detection, auto-disconnects when port disappears
- Future: detects scales (`0x03 0xCE` signature) and sensor baskets

### Android USB Permissions

Android requires explicit USB permission via Java API. A JNI helper class (`USBPermission`) handles:
- USB device permission dialog
- Intent filter for USB_DEVICE_ATTACHED

## UI Changes

### Tab Rename

"Bluetooth" tab → "Connections" in SettingsPage.

### Left Column Switching

The left column (Machine) switches entirely based on connection type:

**When USB-C connected:**
- Connection type indicator ("USB-C Connected")
- Port name and serial number
- Firmware version
- Disconnect button
- Serial communication log

**When no USB-C (existing behavior):**
- BLE status, scan button
- Discovered device list
- BLE scan log

Switching driven by `USBManager.de1Connected` boolean property.

### Right Column (Scales)

Unchanged. USB-C scales will be added in a future iteration.

## New Files

| File | Purpose |
|------|---------|
| `src/ble/de1transport.h` | Abstract transport interface |
| `src/ble/bletransport.h/.cpp` | BLE transport (extracted from DE1Device) |
| `src/usb/serialtransport.h/.cpp` | Serial transport (text hex <-> binary) |
| `src/usb/usbmanager.h/.cpp` | USB device discovery & hotplug polling |
| `src/usb/usbpermission.h/.cpp` | Android USB permission helper (JNI) |
| `qml/pages/settings/SettingsConnectionsTab.qml` | Renamed + extended tab |
| `android/res/xml/device_filter.xml` | USB device filter (VID 0x1A86) |

## Modified Files

| File | Change |
|------|--------|
| `src/ble/de1device.h/.cpp` | Delegate I/O through DE1Transport |
| `src/main.cpp` | Create USBManager, wire to DE1Device |
| `qml/pages/SettingsPage.qml` | Rename tab, load new QML |
| `CMakeLists.txt` | Add Qt::SerialPort, new sources |
| `android/AndroidManifest.xml` | USB intent filter |

## Future Extensions

- USB-C scale support (Half Decent Scale, Sensor Basket)
- USB-C firmware update capability
