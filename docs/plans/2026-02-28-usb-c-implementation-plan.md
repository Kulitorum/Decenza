# USB-C Connection Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add USB-C serial connectivity to the DE1 as an alternative to BLE, with auto-detection on all platforms.

**Architecture:** Extract a `DE1Transport` abstract interface from DE1Device's BLE code. Implement `BleTransport` (extracted existing code) and `SerialTransport` (new USB-C serial). `USBManager` polls for USB serial devices and auto-connects. QML tab renamed from "Bluetooth" to "Connections" with left column switching between BLE and USB-C views.

**Tech Stack:** Qt 6.10.2, C++17, QSerialPort, QML, JNI (Android USB permissions)

---

### Task 1: Create DE1Transport abstract interface

**Files:**
- Create: `src/ble/de1transport.h`

**Step 1: Write the abstract transport interface**

```cpp
// src/ble/de1transport.h
#pragma once

#include <QObject>
#include <QBluetoothUuid>
#include <QByteArray>
#include <QString>

/**
 * Abstract transport interface for DE1 communication.
 *
 * Both BLE and USB Serial implement this interface.
 * All data is binary QByteArray keyed by BLE characteristic UUID.
 * The serial implementation handles text/hex encoding internally.
 */
class DE1Transport : public QObject {
    Q_OBJECT

public:
    explicit DE1Transport(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~DE1Transport() = default;

    virtual bool isConnected() const = 0;
    virtual QString transportName() const = 0;  // "BLE" or "USB-C"

    /**
     * Write binary data to a characteristic UUID.
     * BLE: writes to QLowEnergyService characteristic
     * Serial: encodes as <LETTER>hexdata\n and sends over serial
     */
    virtual void write(const QBluetoothUuid& uuid, const QByteArray& data) = 0;

    /**
     * Read a characteristic value (one-shot).
     * Result arrives via dataReceived signal.
     */
    virtual void read(const QBluetoothUuid& uuid) = 0;

    /**
     * Subscribe to notifications for a characteristic.
     * Subsequent value changes arrive via dataReceived signal.
     * BLE: writes CCCD descriptor
     * Serial: sends <+LETTER> command
     */
    virtual void subscribe(const QBluetoothUuid& uuid) = 0;

    /**
     * Subscribe to all standard DE1 notification endpoints.
     * Called after connection is established.
     */
    virtual void subscribeAll() = 0;

    /**
     * Disconnect and clean up resources.
     */
    virtual void disconnect() = 0;

signals:
    /** Emitted when transport is fully ready (BLE: service discovered; Serial: port open + probed). */
    void connected();

    /** Emitted when transport is disconnected. */
    void disconnected();

    /** Emitted when data is received from any characteristic. */
    void dataReceived(const QBluetoothUuid& uuid, const QByteArray& data);

    /** Emitted when a write completes successfully. */
    void writeComplete(const QBluetoothUuid& uuid, const QByteArray& data);

    /** Emitted on transport error. */
    void errorOccurred(const QString& error);

    /** Emitted for debug log messages. */
    void logMessage(const QString& message);
};
```

**Step 2: Commit**

```bash
git add src/ble/de1transport.h
git commit -m "feat: add DE1Transport abstract interface for BLE/USB-C"
```

---

### Task 2: Extract BleTransport from DE1Device

This is the biggest refactoring task. We extract all BLE-specific code from DE1Device into a `BleTransport` class, then make DE1Device delegate through `DE1Transport*`.

**Files:**
- Create: `src/ble/bletransport.h`
- Create: `src/ble/bletransport.cpp`
- Modify: `src/ble/de1device.h` — remove BLE members, add `DE1Transport* m_transport`
- Modify: `src/ble/de1device.cpp` — delegate writes through transport
- Modify: `CMakeLists.txt` — add `src/ble/bletransport.cpp` to SOURCES

**Step 1: Create BleTransport header**

`BleTransport` wraps `QLowEnergyController` + `QLowEnergyService` and implements `DE1Transport`. It contains:
- All BLE connection logic (currently in `DE1Device::connectToDevice()`)
- Service discovery (`onServiceDiscovered`, `onServiceStateChanged`, `setupService`)
- Notification subscription (`subscribeToNotifications`)
- Write with retry logic (command queue, timeout, retry)
- The `onCharacteristicChanged` handler emits `dataReceived(uuid, data)`
- The `onCharacteristicWritten` handler emits `writeComplete(uuid, data)`

Key members to move from DE1Device:
- `m_controller` (QLowEnergyController*)
- `m_service` (QLowEnergyService*)
- `m_characteristics` (QMap)
- `m_commandQueue`, `m_commandTimer`, `m_writePending`
- `m_writeTimeoutTimer`, `m_writeRetryCount`, `MAX_WRITE_RETRIES`
- `m_retryTimer`, `m_retryCount`, `m_pendingDevice`
- All BLE slot methods (`onControllerConnected`, `onControllerDisconnected`, etc.)

**Step 2: Create BleTransport implementation**

Move the following methods from `de1device.cpp` to `bletransport.cpp`:
- `connectToDevice()` (both overloads)
- `disconnect()` — the BLE-specific parts (controller cleanup, Android service stop)
- `onControllerConnected()`, `onControllerDisconnected()`, `onControllerError()`
- `onServiceDiscovered()`, `onServiceDiscoveryFinished()`, `onServiceStateChanged()`
- `setupService()`, `subscribeToNotifications()`
- `writeCharacteristic()`, `queueCommand()`, `processCommandQueue()`
- `onCharacteristicWritten()`
- Retry timer setup from constructor

The `onCharacteristicChanged` handler in BleTransport should emit `dataReceived(uuid, data)` instead of calling parse methods directly.

**Step 3: Refactor DE1Device to use DE1Transport**

Remove from `de1device.h`:
- All `QLowEnergyController`, `QLowEnergyService`, `QBluetoothDeviceInfo` members
- Command queue, write retry, and service discovery members
- BLE slot declarations

Add to `de1device.h`:
```cpp
public:
    void setTransport(DE1Transport* transport);
    DE1Transport* transport() const { return m_transport; }

private:
    DE1Transport* m_transport = nullptr;  // Not owned
    void onTransportDataReceived(const QBluetoothUuid& uuid, const QByteArray& data);
    void onTransportWriteComplete(const QBluetoothUuid& uuid, const QByteArray& data);
    void onTransportConnected();
    void onTransportDisconnected();
```

In `de1device.cpp`:
- `setTransport()` connects transport signals to DE1Device slots
- `onTransportDataReceived()` dispatches to existing parse methods (same as current `onCharacteristicChanged`)
- `requestState()` calls `m_transport->write(REQUESTED_STATE, data)` instead of `writeCharacteristic()`
- `uploadProfile()` calls `m_transport->write(HEADER_WRITE, ...)` and `m_transport->write(FRAME_WRITE, ...)`
- `writeMMR()` calls `m_transport->write(WRITE_TO_MMR, data)`
- `setShotSettings()` calls `m_transport->write(SHOT_SETTINGS, data)`
- `stopOperationUrgent()` calls `m_transport->write()` directly (bypassing queue is now the transport's job — BleTransport has the queue, serial doesn't need one)
- `connectToDevice()` now creates a `BleTransport`, calls `setTransport()`, then calls `m_transport->connectToDevice()`
- `disconnect()` calls `m_transport->disconnect()` then `setTransport(nullptr)`

**Important:** The command queue and write retry logic stay in `BleTransport` since they're BLE-specific. `SerialTransport` won't need a queue (serial writes are synchronous).

**Important:** Keep the SAW latency instrumentation in DE1Device — it hooks into `writeComplete` from the transport.

**Important:** Keep `stopOperationUrgent()` working — for BLE, we need the bypass-queue behavior. Add a method to BleTransport: `writeUrgent(uuid, data)` that clears the queue and writes immediately. The `DE1Transport` interface gets `virtual void writeUrgent(const QBluetoothUuid& uuid, const QByteArray& data) { write(uuid, data); }` — default impl just calls write(), BLE overrides to bypass queue.

**Step 4: Update main.cpp wiring**

In `main.cpp`, after creating `DE1Device` and `BLEManager`:
```cpp
// BleTransport is created lazily when user clicks a device in the BLE scan list
// (inside DE1Device::connectToDevice). No changes needed in main.cpp for BLE path.
```

**Step 5: Update CMakeLists.txt**

Add `src/ble/bletransport.cpp` to the `SOURCES` list (after `src/ble/de1device.cpp` at line 182).

**Step 6: Build and test**

Build the project to verify BLE path still works:
```bash
# User builds in Qt Creator (don't build on CLI unless asked)
```

**Step 7: Commit**

```bash
git add src/ble/bletransport.h src/ble/bletransport.cpp src/ble/de1device.h src/ble/de1device.cpp src/ble/de1transport.h CMakeLists.txt
git commit -m "refactor: extract BleTransport from DE1Device, add transport abstraction"
```

---

### Task 3: Create SerialTransport

**Files:**
- Create: `src/usb/serialtransport.h`
- Create: `src/usb/serialtransport.cpp`
- Modify: `CMakeLists.txt` — add Qt::SerialPort and new source files

**Step 1: Create the endpoint mapping**

The serial protocol uses single-letter codes for BLE characteristic UUIDs. This mapping is static and bidirectional:

```cpp
// UUID 0000A001 → 'A', 0000A002 → 'B', ..., 0000A012 → 'R'
// The letter is computed: char letter = 'A' + (uuid_short - 0xA001)
// Where uuid_short is the 16-bit UUID value (e.g., 0xA00D for ShotSample → 'M')
```

**Step 2: Create SerialTransport header**

```cpp
// src/usb/serialtransport.h
#pragma once

#include "../ble/de1transport.h"
#include <QSerialPort>
#include <QByteArray>

class SerialTransport : public DE1Transport {
    Q_OBJECT

public:
    explicit SerialTransport(const QString& portName, QObject* parent = nullptr);
    ~SerialTransport() override;

    bool isConnected() const override;
    QString transportName() const override { return "USB-C"; }
    void write(const QBluetoothUuid& uuid, const QByteArray& data) override;
    void read(const QBluetoothUuid& uuid) override;
    void subscribe(const QBluetoothUuid& uuid) override;
    void subscribeAll() override;
    void disconnect() override;

    QString portName() const;
    QString serialNumber() const { return m_serialNumber; }
    void setSerialNumber(const QString& sn) { m_serialNumber = sn; }

    /** Open the serial port and begin communication. */
    void open();

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    void processLine(const QString& line);
    static char uuidToLetter(const QBluetoothUuid& uuid);
    static QBluetoothUuid letterToUuid(char letter);
    static QByteArray hexStringToBytes(const QString& hex);
    static QString bytesToHexString(const QByteArray& data);

    QSerialPort* m_port = nullptr;
    QString m_portName;
    QString m_serialNumber;
    QByteArray m_buffer;  // Line buffer for incomplete reads
    bool m_connected = false;
};
```

**Step 3: Implement SerialTransport**

Key implementation details:

- **Constructor**: Creates `QSerialPort` with 115200/8N1, DTR/RTS off
- **open()**: Opens port, connects `readyRead` signal, emits `connected()`
- **write()**: Converts UUID to letter, data to hex string, sends `<LETTER>hexdata\n`
- **read()**: For serial, same as subscribe — data comes via notification stream
- **subscribe()**: Sends `<+LETTER>\n`
- **subscribeAll()**: Subscribes to N, M, Q, K, E, I (stateInfo, shotSample, waterLevels, shotSettings, readFromMMR, fwMapRequest)
- **onReadyRead()**: Appends to `m_buffer`, splits on `\n`, processes complete lines
- **processLine()**: Parses `[X]hexdata` format, converts hex to binary, emits `dataReceived(letterToUuid(X), binaryData)`
- **uuidToLetter()**: Extracts 16-bit value from UUID, computes `'A' + (value - 0xA001)`. Example: `0xA00E` → `'A' + 13` → `'N'`
- **letterToUuid()**: Inverse: `0xA001 + (letter - 'A')` → construct full UUID string
- **disconnect()**: Closes port, emits `disconnected()`

**Step 4: Add Qt::SerialPort to CMakeLists.txt**

At line 37 in CMakeLists.txt, add `SerialPort` to the `find_package` list:
```cmake
find_package(Qt6 REQUIRED COMPONENTS
    ...
    Sql
    Positioning
    SerialPort
)
```

Add to target link libraries (search for `Qt6::Bluetooth` and add `Qt6::SerialPort` nearby).

Add to SOURCES list:
```cmake
    src/usb/serialtransport.cpp
```

**Step 5: Commit**

```bash
git add src/usb/serialtransport.h src/usb/serialtransport.cpp CMakeLists.txt
git commit -m "feat: add SerialTransport for USB-C DE1 communication"
```

---

### Task 4: Create USBManager

**Files:**
- Create: `src/usb/usbmanager.h`
- Create: `src/usb/usbmanager.cpp`
- Modify: `CMakeLists.txt` — add source file

**Step 1: Create USBManager header**

```cpp
// src/usb/usbmanager.h
#pragma once

#include <QObject>
#include <QTimer>
#include <QSerialPortInfo>

class SerialTransport;

class USBManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool de1Connected READ isDe1Connected NOTIFY de1ConnectedChanged)
    Q_PROPERTY(QString portName READ portName NOTIFY de1ConnectedChanged)
    Q_PROPERTY(QString serialNumber READ serialNumber NOTIFY de1ConnectedChanged)

public:
    explicit USBManager(QObject* parent = nullptr);
    ~USBManager();

    bool isDe1Connected() const;
    QString portName() const;
    QString serialNumber() const;

    /** Start polling for USB serial devices. */
    void startPolling();

    /** Stop polling. */
    void stopPolling();

    /** Get the active SerialTransport (nullptr if not connected). */
    SerialTransport* transport() const { return m_transport; }

signals:
    void de1ConnectedChanged();
    void de1Discovered(SerialTransport* transport);
    void de1Lost();
    void logMessage(const QString& message);

private slots:
    void pollPorts();

private:
    void probePort(const QSerialPortInfo& portInfo);
    void onProbeResult(const QString& portName, bool isDe1);

    QTimer m_pollTimer;
    SerialTransport* m_transport = nullptr;  // Owned
    QString m_connectedPortName;
    QString m_connectedSerialNumber;
    QSet<QString> m_knownPorts;        // Ports we've already seen
    QSet<QString> m_probingPorts;      // Ports currently being probed
    static constexpr int POLL_INTERVAL_MS = 2000;
    static constexpr uint16_t VENDOR_ID_WCH = 0x1A86;
};
```

**Step 2: Implement USBManager**

Key implementation:

- **pollPorts()**: Called every 2s by timer
  - `QSerialPortInfo::availablePorts()` — get all serial ports
  - Filter by `vendorIdentifier() == 0x1A86`
  - New ports (not in `m_knownPorts`): start probe
  - Missing ports (in `m_knownPorts` but not in current list): if it was our connected port, emit `de1Lost()`
  - Update `m_knownPorts`

- **probePort()**: Opens port, sends `<+M>\n`, starts 2-second timer
  - On data received: check for `[M]` prefix → it's a DE1
  - On timeout: not a DE1, close port
  - On success: create `SerialTransport`, emit `de1Discovered()`

- **Probing approach**: Create a temporary `QSerialPort`, open it, send probe, listen for response with a single-shot timer. Use a dedicated method to avoid blocking the main thread.

**Step 3: Add to CMakeLists.txt**

Add `src/usb/usbmanager.cpp` to SOURCES list.

**Step 4: Commit**

```bash
git add src/usb/usbmanager.h src/usb/usbmanager.cpp CMakeLists.txt
git commit -m "feat: add USBManager for auto-detecting DE1 over USB-C"
```

---

### Task 5: Wire USBManager into main.cpp and DE1Device

**Files:**
- Modify: `src/main.cpp` — create USBManager, wire signals
- Modify: `src/ble/de1device.h` — add `Q_PROPERTY(QString connectionType)` for QML

**Step 1: Add USBManager to main.cpp**

After creating DE1Device (around line 156 in main.cpp):

```cpp
#include "usb/usbmanager.h"

// ... after DE1Device creation ...
USBManager usbManager;
usbManager.startPolling();

// When USB DE1 discovered: disconnect BLE, switch transport
QObject::connect(&usbManager, &USBManager::de1Discovered,
    [&de1Device, &bleManager](SerialTransport* transport) {
        // Disconnect BLE if connected
        if (de1Device.isConnected()) {
            de1Device.disconnect();
        }
        // Switch to USB transport
        de1Device.setTransport(transport);
    });

// When USB DE1 lost: clear transport, BLE can reconnect
QObject::connect(&usbManager, &USBManager::de1Lost,
    [&de1Device]() {
        de1Device.setTransport(nullptr);
    });
```

Register USBManager with QML engine:
```cpp
engine.rootContext()->setContextProperty("USBManager", &usbManager);
```

**Step 2: Add connectionType property to DE1Device**

For QML to know if connected via BLE or USB:
```cpp
Q_PROPERTY(QString connectionType READ connectionType NOTIFY connectedChanged)

QString connectionType() const {
    if (!m_transport) return "";
    return m_transport->transportName();
}
```

**Step 3: Commit**

```bash
git add src/main.cpp src/ble/de1device.h src/ble/de1device.cpp
git commit -m "feat: wire USBManager into app, auto-switch BLE/USB transport"
```

---

### Task 6: Rename Bluetooth tab to Connections and add USB-C UI

**Files:**
- Delete: `qml/pages/settings/SettingsBluetoothTab.qml` (rename to SettingsConnectionsTab.qml)
- Create: `qml/pages/settings/SettingsConnectionsTab.qml`
- Modify: `qml/pages/SettingsPage.qml` — rename tab label and source path

**Step 1: Rename the tab in SettingsPage.qml**

In `qml/pages/SettingsPage.qml`:

Change tab button (line 98-102):
```qml
StyledTabButton {
    id: connectionsTab
    text: TranslationManager.translate("settings.tab.connections", "Connections")
    tabLabel: TranslationManager.translate("settings.tab.connections", "Connections")
}
```

Change loader source (line 239-244):
```qml
Loader {
    id: connectionsLoader
    active: true
    asynchronous: false
    source: "settings/SettingsConnectionsTab.qml"
}
```

Update accessibility tab names array (line 63):
```javascript
var tabNames = ["Connections", "Preferences", ...]
```

**Step 2: Create SettingsConnectionsTab.qml**

Copy `SettingsBluetoothTab.qml` as base, then modify the left column to switch based on `USBManager.de1Connected`:

```qml
// Left column Machine section - replace the existing ColumnLayout content with:
ColumnLayout {
    anchors.fill: parent
    anchors.margins: Theme.scaled(15)
    spacing: Theme.scaled(10)

    // USB-C view (when USB connected)
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: USBManager.de1Connected
        spacing: Theme.scaled(10)

        // Title
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "USB-C"
                color: Theme.textColor
                font.pixelSize: Theme.scaled(16)
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            // Status badge
            Rectangle {
                width: statusText.implicitWidth + Theme.scaled(16)
                height: Theme.scaled(24)
                radius: Theme.scaled(12)
                color: Qt.rgba(Theme.successColor.r, Theme.successColor.g, Theme.successColor.b, 0.2)
                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: "Connected"
                    color: Theme.successColor
                    font.pixelSize: Theme.scaled(12)
                    font.bold: true
                }
            }
        }

        // Port info
        Text {
            text: "Port: " + USBManager.portName
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(13)
        }
        Text {
            text: "Serial: " + USBManager.serialNumber
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(13)
            visible: USBManager.serialNumber !== ""
        }
        Text {
            text: TranslationManager.translate("settings.bluetooth.firmware", "Firmware:") + " " + (DE1Device.firmwareVersion || "Unknown")
            color: Theme.textSecondaryColor
            visible: DE1Device.connected
        }

        // Disconnect button
        AccessibleButton {
            text: "Disconnect USB-C"
            accessibleName: "Disconnect USB-C connection"
            onClicked: DE1Device.disconnect()
        }

        // Serial log
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Qt.darker(Theme.surfaceColor, 1.2)
            radius: Theme.scaled(4)

            ScrollView {
                id: usbLogScroll
                anchors.fill: parent
                anchors.margins: Theme.scaled(8)
                clip: true

                TextArea {
                    id: usbLogText
                    readOnly: true
                    color: Theme.textSecondaryColor
                    font.pixelSize: Theme.scaled(11)
                    font.family: "monospace"
                    wrapMode: Text.Wrap
                    background: null
                    text: ""
                }
            }

            Connections {
                target: USBManager
                function onLogMessage(message) {
                    usbLogText.text += message + "\n"
                    usbLogScroll.ScrollBar.vertical.position = 1.0 - usbLogScroll.ScrollBar.vertical.size
                }
            }
        }
    }

    // BLE view (when no USB connection) — existing BLE UI unchanged
    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: !USBManager.de1Connected
        spacing: Theme.scaled(10)

        // ... existing BLE machine UI from SettingsBluetoothTab.qml ...
    }
}
```

The right column (Scale section) remains completely unchanged.

**Step 3: Commit**

```bash
git add qml/pages/settings/SettingsConnectionsTab.qml qml/pages/SettingsPage.qml
git rm qml/pages/settings/SettingsBluetoothTab.qml
git commit -m "feat: rename Bluetooth tab to Connections, add USB-C GUI"
```

---

### Task 7: Android USB permissions

**Files:**
- Create: `android/res/xml/device_filter.xml`
- Modify: `android/AndroidManifest.xml` — add USB intent filter and meta-data

**Step 1: Create device_filter.xml**

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- QinHeng/WCH USB-to-Serial chips (CH340, CH9102, etc.) -->
    <usb-device vendor-id="6790" />
</resources>
```

Note: We filter by vendor ID only (not product ID) to cover all WCH chip variants.

**Step 2: Update AndroidManifest.xml**

Add to the main activity's `<intent-filter>`:
```xml
<action android:name="android.hardware.usb.action.USB_DEVICE_ATTACHED" />
```

Add `<meta-data>` inside the `<activity>` tag:
```xml
<meta-data
    android:name="android.hardware.usb.action.USB_DEVICE_ATTACHED"
    android:resource="@xml/device_filter" />
```

**Step 3: Add USB host feature (optional, prevents non-USB-host devices from installing)**

Add to `<manifest>`:
```xml
<uses-feature android:name="android.hardware.usb.host" android:required="false" />
```

**Step 4: Android USB permission handling in USBManager**

On Android, `QSerialPort` may not work directly with USB OTG devices — it depends on the kernel exposing `/dev/ttyACM*`. Most Android tablets with USB-C host support do expose CDC-ACM devices via the kernel driver, which is what we saw on the Decent tablet (`/dev/ttyACM0`).

If `QSerialPort` doesn't work on some Android devices, we may need JNI-based `UsbManager.requestPermission()` and `UsbDeviceConnection.getFileDescriptor()`. Defer this to a follow-up task — start with `QSerialPort` which works on the confirmed hardware.

**Step 5: Commit**

```bash
git add android/res/xml/device_filter.xml android/AndroidManifest.xml
git commit -m "feat: add Android USB device filter for DE1 USB-C"
```

---

### Task 8: Integration testing and polish

**Files:**
- Modify: Various files for bug fixes discovered during testing

**Step 1: Test USB-C connection on tablet**

1. Build and deploy to tablet
2. Plug in DE1 via USB-C
3. Verify: USBManager detects the port (check logs)
4. Verify: SerialTransport opens and subscribes
5. Verify: DE1Device receives state notifications
6. Verify: Left column switches to USB-C view
7. Verify: Firmware version appears
8. Verify: Machine control works (espresso, steam, etc.)

**Step 2: Test BLE still works**

1. Unplug USB-C
2. Verify: Left column switches back to BLE view
3. Verify: BLE scanning and connecting still works
4. Verify: Full shot cycle works over BLE

**Step 3: Test USB → BLE transition**

1. Connect via USB-C (verify working)
2. Unplug USB-C cable
3. Verify: DE1Device disconnects cleanly
4. Verify: BLE scan can find and connect to DE1
5. Verify: No crashes or stuck states

**Step 4: Test BLE → USB transition**

1. Connect via BLE (verify working)
2. Plug in USB-C cable
3. Verify: USB auto-detected (DE1 may disconnect BLE itself)
4. Verify: Clean transition to USB transport

**Step 5: Final commit**

```bash
git add -A
git commit -m "fix: integration testing fixes for USB-C transport"
```

---

## Task Dependency Graph

```
Task 1 (DE1Transport interface)
  ↓
Task 2 (Extract BleTransport from DE1Device)  ← largest task
  ↓
Task 3 (SerialTransport)
  ↓
Task 4 (USBManager)
  ↓
Task 5 (Wire into main.cpp)
  ↓
Task 6 (QML Connections tab)
  ↓
Task 7 (Android USB permissions)
  ↓
Task 8 (Integration testing)
```

Tasks 1-5 are strictly sequential (each depends on the previous).
Tasks 6 and 7 could be done in parallel with tasks 3-5 but are ordered sequentially for simplicity.
Task 8 requires all previous tasks.
