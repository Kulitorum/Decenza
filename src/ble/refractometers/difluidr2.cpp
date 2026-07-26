#include "difluidr2.h"
#include "../protocol/de1characteristics.h"
#include "../transport/scalebletransport.h"

// Logging macros — same pattern as scale drivers but emits logMessage() directly
#define R2_LOG(msg) do { \
    QString _msg = QString("[BLE DiFluidR2] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define R2_WARN(msg) do { \
    QString _msg = QString("[BLE DiFluidR2] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

// Protocol constants
static constexpr uint8_t PACKET_HEADER = 0xDF;
static constexpr int PACKET_MIN_LENGTH = 6;  // header(2) + func(1) + cmd(1) + datalen(1) + checksum(1)

// R2 test status codes (protocolR2.md "Device Status"), cross-checked against
// Beanconqueror's `diFluid.R2.action.test.status` enum. Returns an empty string for
// a code we have no name for — the caller always logs the number, so an unnamed
// code is still visible rather than silently swallowed.
//
// These names are read by people and by AI assistants triaging a user's log over
// MCP. "Status: 5" says nothing; "Average test ongoing" says the device is healthy
// and working, which is the difference between a bug report and a non-report.
static QString r2StatusName(uint8_t status) {
    switch (status) {
    case 0:  return QStringLiteral("Test finished");
    case 1:  return QStringLiteral("Calibration finished");
    case 2:
    case 3:  return QStringLiteral("reserved");
    case 4:  return QStringLiteral("Average test started");
    case 5:  return QStringLiteral("Average test ongoing");
    case 6:  return QStringLiteral("Average test finished");
    case 7:  return QStringLiteral("Loop test started");
    case 8:  return QStringLiteral("Loop test ongoing");
    case 9:  return QStringLiteral("Loop test finished");
    // The R2 emits 10 specifically to say an individual test is taking a long time.
    // It is a liveness signal, not a fault — see the watchdog handling below.
    case 10: return QStringLiteral("Average test ongoing (this test is running long)");
    case 11: return QStringLiteral("Test started");
    case 12: return QStringLiteral("Calibration started");
    default: return QString();
    }
}

// Statuses that say the device is still working on the measurement. Restarting the
// liveness watchdog on these is what lets a multi-test averaged run outlive a single
// watchdog interval without the driver having to guess how long the run should take.
static bool r2StatusIsProgress(uint8_t status) {
    switch (status) {
    case 4:   // Average test started
    case 5:   // Average test ongoing
    case 7:   // Loop test started
    case 8:   // Loop test ongoing
    case 10:  // Average test ongoing — this individual test is running long
    case 11:  // Test started
        return true;
    default:
        return false;
    }
}

// R2 error classes and codes (protocolR2.md "Error Code"). Always returns something
// printable, so an unrecognised pair degrades to its numbers rather than to silence.
static QString r2ErrorDescription(uint8_t errClass, uint8_t errCode) {
    if (errClass == 2) {  // General (software) errors
        switch (errCode) {
        case 1:  return QStringLiteral("Test error");
        case 2:  return QStringLiteral("Calibration failed");
        case 3:  return QStringLiteral("No liquid");
        case 4:  return QStringLiteral("Beyond range");
        default: return QStringLiteral("unrecognised general error (code %1)").arg(errCode);
        }
    }
    if (errClass == 3) {  // Hardware — the code is what the device itself displays
        return QStringLiteral("hardware error — the device screen is showing code %1")
            .arg(errCode);
    }
    if (errClass == 0) {
        // Observed in the field around SUCCESSFUL reads; see the note at the call site
        // on why these are log-only.
        return QStringLiteral("benign device status (code %1), not a fault").arg(errCode);
    }
    return QStringLiteral("unrecognised error (class %1, code %2)").arg(errClass).arg(errCode);
}

// R2-specific note on the shared plausibility ceiling: when an R2 measurement
// fails mid-flight it emits an out-of-range sentinel in the TDS field —
// observed as raw 0xFFE5 (65509 → 655.09%) one packet before an
// `R2 error class=0 code=2` storm — which used to flow through as a real
// reading and get autosaved onto the shot. The threshold lives on
// RefractometerDevice; this comment captures the R2-specific failure mode.

DiFluidR2::DiFluidR2(ScaleBleTransport* transport, QObject* parent)
    : RefractometerDevice(parent)
    , m_transport(transport)
{
    // Liveness watchdog, NOT a bound on how long a measurement may take. A device that
    // goes silent (out of range, powered off mid-measurement) emits no event at all, and
    // no event-based mechanism can detect a missing event — which is why this timer is
    // the documented exception to the project's no-timers-as-guards rule.
    //
    // The interval is restarted by any packet saying the device is still working (see
    // r2StatusIsProgress). That distinction matters: armed once at request time, this
    // was effectively a 15-second ceiling, and an averaged run of several tests would
    // trip it and abort a perfectly healthy device. Silence still recovers, which is
    // the only property the exception was granted for.
    m_measurementTimer.setSingleShot(true);
    m_measurementTimer.setInterval(15000);
    connect(&m_measurementTimer, &QTimer::timeout, this, [this]() {
        if (!m_measuring) return;
        // Every other failure in this driver reaches the error dialog. This is the
        // most common real one — R2 out of range, asleep, or a write that never
        // landed — and it used to stop the spinner with no message at all, leaving
        // the user to conclude the button was broken.
        R2_WARN(QString("Measurement timeout — no packet from the R2 for %1 ms")
                    .arg(m_measurementTimer.interval()));
        emit errorOccurred("The refractometer stopped responding. Check it is "
                           "switched on and in range, then try again.");
        m_measuring = false;
        emit measuringChanged();
    });

    // BLE stack constraint: Qt's BLE layer (Android BluetoothLE + iOS CoreBluetooth)
    // provides no "ready after characteristic discovery" signal. This 100ms delay is
    // inherited from de1app and required for reliable CCCD writes. No event-based
    // alternative exists — this is a platform limitation, not a workaround.
    m_initTimer.setSingleShot(true);
    m_initTimer.setInterval(100);
    connect(&m_initTimer, &QTimer::timeout, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        m_transport->enableNotifications(Refractometer::DiFluidR2::SERVICE,
                                         Refractometer::DiFluidR2::CHARACTERISTIC);
        R2_LOG(QString("[R2-diag] connectedChanged -> TRUE (instance=%1)")
               .arg(QString::number(reinterpret_cast<quintptr>(this), 16)));
        m_connected = true;
        emit connectedChanged();
        R2_LOG("Connected and ready for measurements");

        // Put the R2 into Celsius (Func=1 Settings, Cmd=0 Temperature Unit, Data=0).
        // Doubles as the init handshake the connect path has always sent — it
        // confirms the BLE link and may wake the R2 — but as a write rather than a
        // query, so the device also stops reporting in whatever unit it was left in.
        // Belt and braces with the pack-1 Data5 conversion below, which stays: the
        // set is not instantaneous, packets from a device-initiated measurement can
        // already be in flight, and Data5 is authoritative for the packet carrying it.
        R2_LOG("Setting device temperature unit to Celsius");
        sendCommand(QByteArray::fromHex("DFDF01000100C0"));  // Set Temperature Unit = °C

        // Instrumentation: identify the unit. Per DiFluid's official protocolR2.md, a
        // genuine R2 Extract (model "DFT-R102") transmits coffee *TDS* in pack 2, while
        // Brix-only variants (R2 PU/PP) and rebrands/clones transmit *Brix* in the same
        // "concentration" field — which we'd then mislabel as TDS. Logging the model
        // string lets us tell them apart when a reading looks like Brix, not TDS.
        // These are fixed DataLen=0 queries straight from the spec (checksum baked in).
        // Read back the device's Auto Test setting so the UI reflects the device rather
        // than a guess. Query only — enabling it is the user's call, not ours.
        sendCommand(QByteArray::fromHex("DFDF010100C0"));  // Get Auto Test Status

        R2_LOG("Querying serial + device model + firmware (instrumentation)");
        sendCommand(QByteArray::fromHex("DFDF000000BE"));  // Get SN (Func 0, Cmd 0)
        sendCommand(QByteArray::fromHex("DFDF000100BF"));  // Get Device Model (Func 0, Cmd 1)
        sendCommand(QByteArray::fromHex("DFDF000200C0"));  // Get Firmware Version (Func 0, Cmd 2)
    });

    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &DiFluidR2::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &DiFluidR2::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &DiFluidR2::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &DiFluidR2::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &DiFluidR2::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &DiFluidR2::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &DiFluidR2::onCharacteristicChanged);
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &DiFluidR2::logMessage);
    }
}

DiFluidR2::~DiFluidR2() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

bool DiFluidR2::isR2Device(const QString& name) {
    QString lower = name.toLower();
    // Match "R2 Extract", "DiFluid R2", etc.
    // Exclude plain "difluid" (that's the Microbalance scale)
    return lower.contains("r2 extract") || lower.contains("r2extract")
        || (lower.contains("difluid") && lower.contains("r2"));
}

void DiFluidR2::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        R2_WARN("connectToDevice called with no transport");
        return;
    }

    const QString newName = device.name();
    const bool nameChange = (newName != m_name);
    m_name = newName;
    m_serviceFound = false;
    m_characteristicsReady = false;
    // Identity belongs to the device we are connecting to, not to the driver — a
    // different R2 must not inherit the last one's serial.
    m_serialNumber.clear();
    m_serialParts.clear();
    m_serialPartsSeen = 0;
    // Auto Test is likewise the device's state, not ours — drop it until this device
    // answers the query rather than carrying the last device's answer forward.
    if (m_autoTest) {
        m_autoTest = false;
        emit autoTestChanged();
    }
    if (nameChange) emit nameChanged();

    R2_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name())
               .arg(device.address().isNull() ? device.deviceUuid().toString()
                                              : device.address().toString()));

    m_transport->connectToDevice(device);
}

void DiFluidR2::disconnectFromDevice() {
    m_measurementTimer.stop();
    m_initTimer.stop();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
    m_measuring = false;
    m_connected = false;
    m_serviceFound = false;
    m_characteristicsReady = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR2::requestMeasurement() {
    if (!m_connected || !m_characteristicsReady) {
        R2_WARN("Cannot read — not connected");
        return;
    }

    m_measuring = true;
    emit measuringChanged();
    R2_LOG("Requesting single test from R2");

    // Official protocol: Func=3 (Device Action), Cmd=0 (Single Test), DataLen=0
    // Command: DF DF 03 00 00 <checksum>
    QByteArray cmd;
    cmd.append(static_cast<char>(0xDF));  // Header
    cmd.append(static_cast<char>(0xDF));  // Header
    cmd.append(static_cast<char>(0x03));  // Func: Device Action
    cmd.append(static_cast<char>(0x00));  // Cmd: Single Test
    cmd.append(static_cast<char>(0x00));  // DataLen: 0

    // Checksum: sum of all bytes & 0xFF
    uint8_t checksum = 0;
    for (qsizetype i = 0; i < cmd.size(); ++i)
        checksum += static_cast<uint8_t>(cmd[i]);
    cmd.append(static_cast<char>(checksum));

    sendCommand(cmd);
    m_measurementTimer.start();
}

void DiFluidR2::setAutoTest(bool enabled) {
    if (!m_connected || !m_characteristicsReady) {
        R2_WARN("Cannot change Auto Test — not connected");
        return;
    }

    // Func=1 (Device Settings), Cmd=1 (Auto Test Status), DataLen=1, Data0 = 0 off / 1 on.
    // The setting lives on the device and persists there, so this is a one-time action
    // rather than something Decenza stores and re-applies on every connect.
    R2_LOG(QString("Setting Auto Test %1").arg(enabled ? "on" : "off"));
    QByteArray cmd;
    cmd.append(static_cast<char>(0xDF));
    cmd.append(static_cast<char>(0xDF));
    cmd.append(static_cast<char>(0x01));  // Func: Device Settings
    cmd.append(static_cast<char>(0x01));  // Cmd: Auto Test Status
    cmd.append(static_cast<char>(0x01));  // DataLen: 1
    cmd.append(static_cast<char>(enabled ? 0x01 : 0x00));

    uint8_t checksum = 0;
    for (qsizetype i = 0; i < cmd.size(); ++i)
        checksum += static_cast<uint8_t>(cmd[i]);
    cmd.append(static_cast<char>(checksum));

    // m_autoTest is NOT set here — the device echoes the setting back and that echo is
    // what moves our state. Assuming the write took would show the user a state their
    // device might not be in.
    sendCommand(cmd);
}

void DiFluidR2::setDeviceTestCount(int count) {
    if (!m_connected || !m_characteristicsReady) {
        R2_WARN("Cannot set the device test count — not connected");
        return;
    }

    const int clamped = qBound(MIN_TEST_COUNT, count, MAX_TEST_COUNT);

    // Func=1 (Device Settings), Cmd=3 (Number of Tests), DataLen=1, Data0 = count.
    // Per protocolR2.md this "only takes effect on offline test" — a measurement the
    // device starts itself. Above 1 it also converts the loop test the R2 falls back
    // to on an unsettled prism into a real average test, which is what makes a
    // button-press or Auto Test reading averaged rather than single.
    R2_LOG(QString("Setting device-initiated test count to %1").arg(clamped));
    QByteArray cmd;
    cmd.append(static_cast<char>(0xDF));
    cmd.append(static_cast<char>(0xDF));
    cmd.append(static_cast<char>(0x01));  // Func: Device Settings
    cmd.append(static_cast<char>(0x03));  // Cmd: Number of Tests
    cmd.append(static_cast<char>(0x01));  // DataLen: 1
    cmd.append(static_cast<char>(clamped));

    uint8_t checksum = 0;
    for (qsizetype i = 0; i < cmd.size(); ++i)
        checksum += static_cast<uint8_t>(cmd[i]);
    cmd.append(static_cast<char>(checksum));

    sendCommand(cmd);
}

void DiFluidR2::requestAveragedMeasurement(int testCount) {
    if (!m_connected || !m_characteristicsReady) {
        R2_WARN("Cannot read — not connected");
        return;
    }

    const int clamped = qBound(MIN_TEST_COUNT, testCount, MAX_TEST_COUNT);
    if (clamped != testCount) {
        R2_WARN(QString("Averaged test count %1 outside the device range %2-%3 — using %4")
                    .arg(testCount).arg(MIN_TEST_COUNT).arg(MAX_TEST_COUNT).arg(clamped));
    }

    m_measuring = true;
    emit measuringChanged();
    R2_LOG(QString("Requesting averaged test (%1 tests) from R2").arg(clamped));

    // Official protocol: Func=3 (Device Action), Cmd=1 (Average Test), DataLen=1,
    // Data0 = number of tests. Command: DF DF 03 01 01 <count> <checksum>
    QByteArray cmd;
    cmd.append(static_cast<char>(0xDF));  // Header
    cmd.append(static_cast<char>(0xDF));  // Header
    cmd.append(static_cast<char>(0x03));  // Func: Device Action
    cmd.append(static_cast<char>(0x01));  // Cmd: Average Test
    cmd.append(static_cast<char>(0x01));  // DataLen: 1
    cmd.append(static_cast<char>(clamped));

    uint8_t checksum = 0;
    for (qsizetype i = 0; i < cmd.size(); ++i)
        checksum += static_cast<uint8_t>(cmd[i]);
    cmd.append(static_cast<char>(checksum));

    sendCommand(cmd);
    m_measurementTimer.start();
}

// === Transport callbacks ===

void DiFluidR2::onTransportConnected() {
    R2_LOG(QString("[R2-diag] transport connected (instance=%1) — starting service discovery")
           .arg(QString::number(reinterpret_cast<quintptr>(this), 16)));
    R2_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void DiFluidR2::onTransportDisconnected() {
    R2_LOG(QString("[R2-diag] %1 (instance=%2) reason=transport-disconnected")
           .arg(m_connected ? QStringLiteral("connectedChanged -> FALSE")
                            : QStringLiteral("connect attempt failed before ready (was not connected)"),
                QString::number(reinterpret_cast<quintptr>(this), 16)));
    R2_LOG("Transport disconnected");
    m_measurementTimer.stop();
    m_initTimer.stop();
    m_connected = false;
    m_characteristicsReady = false;
    m_serviceFound = false;
    m_measuring = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR2::onTransportError(const QString& message) {
    R2_WARN(QString("[R2-diag] %1 (instance=%2) reason=transport-error")
            .arg(m_connected ? QStringLiteral("connectedChanged -> FALSE")
                             : QStringLiteral("connect attempt failed before ready (was not connected)"),
                 QString::number(reinterpret_cast<quintptr>(this), 16)));
    R2_WARN(QString("Transport error: %1").arg(message));
    m_measurementTimer.stop();
    m_initTimer.stop();
    m_connected = false;
    m_characteristicsReady = false;
    m_serviceFound = false;
    m_measuring = false;
    emit connectedChanged();
    emit measuringChanged();
}

void DiFluidR2::onServiceDiscovered(const QBluetoothUuid& uuid) {
    R2_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Refractometer::DiFluidR2::SERVICE) {
        R2_LOG("Found DiFluid R2 service");
        m_serviceFound = true;
    }
}

void DiFluidR2::onServicesDiscoveryFinished() {
    R2_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        R2_WARN(QString("DiFluid R2 service %1 not found!")
                    .arg(Refractometer::DiFluidR2::SERVICE.toString()));
        return;
    }
    m_transport->discoverCharacteristics(Refractometer::DiFluidR2::SERVICE);
}

void DiFluidR2::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Refractometer::DiFluidR2::SERVICE) return;
    if (m_characteristicsReady) {
        R2_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    R2_LOG("Characteristics discovered, enabling notifications");
    m_characteristicsReady = true;
    m_initTimer.start();
}

void DiFluidR2::onCharacteristicChanged(const QBluetoothUuid& /*characteristicUuid*/,
                                        const QByteArray& value) {
    // Accept data from any characteristic on our service
    handlePacket(value);
}

// === Packet parsing ===

void DiFluidR2::handlePacket(const QByteArray& packet) {
    // Official DiFluid protocol: DF DF <Func> <Cmd> <DataLen> <Data0..DataN> <Checksum>
    // Minimum packet: header(2) + func(1) + cmd(1) + datalen(1) + checksum(1) = 6 bytes
    if (packet.size() < PACKET_MIN_LENGTH) {
        return;
    }

    // Validate header (0xDF 0xDF)
    if (static_cast<uint8_t>(packet[0]) != PACKET_HEADER ||
        static_cast<uint8_t>(packet[1]) != PACKET_HEADER) {
        R2_LOG(QString("Non-protocol packet (%1 bytes): %2")
            .arg(packet.size()).arg(QString(packet.left(8).toHex(' '))));
        return;
    }

    if (!validateChecksum(packet)) {
        R2_WARN(QString("Checksum failed: %1").arg(QString(packet.toHex(' '))));
        return;
    }

    uint8_t func = static_cast<uint8_t>(packet[2]);
    uint8_t cmd = static_cast<uint8_t>(packet[3]);
    uint8_t dataLen = static_cast<uint8_t>(packet[4]);

    // Data starts at byte 5, length = dataLen
    // Verify packet length: 5 (2×header + func + cmd + datalen) + dataLen + 1 (checksum)
    if (packet.size() < 5 + dataLen + 1) {
        R2_WARN(QString("Packet too short for declared data length"));
        return;
    }

    // Func 0 = Device Info: decode the model/firmware strings for instrumentation.
    // Model "DFT-R102" == genuine R2 Extract (transmits coffee TDS); anything else is a
    // Brix variant / rebrand and the pack-2 concentration field is Brix, not TDS.
    if (func == 0) {
        const QByteArray data = packet.mid(5, dataLen);
        if (cmd == 0) {  // Serial Number (arrives in parts)
            handleSerialNumberPart(data);
        } else if (cmd == 1) {  // Device Model
            m_deviceModel = QString::fromLatin1(data);
            R2_LOG(QString("Device model: \"%1\"%2")
                       .arg(m_deviceModel,
                            m_deviceModel == QLatin1String("DFT-R102")
                                ? QStringLiteral(" (genuine R2 Extract — concentration = TDS)")
                                : QStringLiteral(" (NOT a standard R2 Extract — concentration may be Brix, not TDS)")));
        } else if (cmd == 2) {  // Firmware Version
            R2_LOG(QString("Firmware version: \"%1\"").arg(QString::fromLatin1(data)));
        } else {
            R2_LOG(QString("Device info response: Cmd=%1 data=%2")
                       .arg(cmd).arg(QString(data.toHex(' '))));
        }
        return;
    }

    // Func 1 = Device Settings. The R2 echoes the setting back, so the response to
    // the connect-time "set Celsius" write is the confirmation that it took. Worth
    // logging on its own line: if a reading ever looks unit-shifted, this says
    // whether the device was actually switched or the write went missing.
    if (func == 1 && cmd == 0) {
        // Absent data must not be printed as data: defaulting to 0 made a
        // zero-length response log a confident "°C", and this line is exactly what
        // someone will trust when diagnosing a unit-shifted reading.
        if (dataLen < 1) {
            R2_WARN(QString("Temperature-unit response carried no data byte: %1")
                        .arg(QString(packet.toHex(' '))));
            return;
        }
        const uint8_t unit = static_cast<uint8_t>(packet[5]);
        R2_LOG(QString("Temperature unit now: %1")
                   .arg(unit == 1 ? QStringLiteral("°F") : QStringLiteral("°C")));
        return;
    }

    // Number-of-Tests echo. Governs device-initiated runs only; logging it makes it
    // possible to tell from a log whether a button-press reading was averaged.
    if (func == 1 && cmd == 3) {
        const int count = dataLen >= 1 ? static_cast<uint8_t>(packet[5]) : 0;
        R2_LOG(QString("Device-initiated test count is now %1%2")
                   .arg(count)
                   .arg(count > 1 ? QStringLiteral(" (device-started reads are averaged)")
                                  : QStringLiteral(" (device-started reads are single)")));
        return;
    }

    // Auto Test Status echo — the device's answer to both our query and our write, and
    // the only thing that moves m_autoTest. Reading it back rather than assuming means
    // the UI shows what the device is actually doing.
    if (func == 1 && cmd == 1) {
        const bool enabled = dataLen >= 1 && static_cast<uint8_t>(packet[5]) == 1;
        R2_LOG(QString("Auto Test is %1").arg(enabled ? "on" : "off"));
        if (enabled != m_autoTest) {
            m_autoTest = enabled;
            emit autoTestChanged();
        }
        return;
    }

    // Func 3 = Device Action (test results)
    if (func == 3) {
        // Instrumentation: full raw bytes of every result packet, so a Brix-vs-TDS
        // mismatch (concentration field vs. the refractive index) is diagnosable from logs.
        R2_LOG(QString("Action packet raw: %1").arg(QString(packet.toHex(' '))));

        if (cmd == 254) {
            // Error response. class=0 code=0 is a real pair, so substituting zeros for
            // absent bytes both fabricates a plausible code and skips the class-2
            // branch below — a truncated "No liquid detected" would reach the user as
            // nothing at all.
            if (dataLen < 2) {
                R2_WARN(QString("Error packet truncated (dataLen=%1, need 2): %2 — "
                                "cannot classify, clearing the measurement")
                            .arg(dataLen).arg(QString(packet.toHex(' '))));
                finishMeasurement(/*complete=*/false);
                return;
            }
            uint8_t errClass = static_cast<uint8_t>(packet[5]);
            uint8_t errCode = static_cast<uint8_t>(packet[6]);
            R2_WARN(QString("R2 error: %1 (class=%2 code=%3)")
                        .arg(r2ErrorDescription(errClass, errCode))
                        .arg(errClass).arg(errCode));
            // Surface ONLY the user-actionable measurement failures. Class-2 are
            // the measurement errors; other class/code combos (notably 0/2) are
            // benign device status the R2 also emits around a SUCCESSFUL read, so
            // surfacing them spams the error dialog (they carry no useful info —
            // the data already arrived). Log-only for those; still clear the
            // measuring state so the UI doesn't hang.
            if (errClass == 2 && errCode == 3) emit errorOccurred("No liquid detected");
            else if (errClass == 2 && errCode == 4) emit errorOccurred("Beyond range");
            finishMeasurement(/*complete=*/false);
            return;
        }
        if (cmd == 255) {
            // Non-actionable — log only (see the cmd==254 note above).
            R2_WARN("R2 unknown error");
            finishMeasurement(/*complete=*/false);
            return;
        }

        // Test result packets: Data0 = package number.
        //
        // Which packet is THE reading depends on the action this response belongs to.
        // During an averaged run the R2 emits a full packet set per constituent test,
        // so pack 2 (single-test result) arrives once per test carrying that one test's
        // concentration — it is per-test detail, not the answer. Only under a single-test
        // action is pack 2 the final reading.
        //
        // Anything that is not explicitly the average action is treated as it was before
        // this dispatch existed. We do not know what action code a physical-button
        // measurement carries — the driver only ever observed that it "streams pack 2" —
        // so an unrecognised code must degrade to today's behaviour rather than to silence.
        const bool averagedRun = (cmd == 1);
        // Cmd 3 is a loop test: undocumented by DiFluid and absent from Beanconqueror's
        // enum, but observed on hardware as what Auto Test escalates to when the prism
        // is not thermally settled. It re-measures every ~3s until the reading stops
        // moving, then ends with status 9 — one settling measurement, not N finished
        // ones. Treating each reading as terminal fired measurementComplete five times
        // in a 16s run and let a mid-loop save persist a superseded value.
        const bool loopRun = (cmd == 3);
        if (dataLen < 1) return;
        uint8_t packNo = static_cast<uint8_t>(packet[5]);

        switch (packNo) {
        case 0: {
            // Status: Data1 = status code
            // Status 0 is "Test finished", so defaulting an absent byte to 0 turned a
            // truncated packet into a confident claim that the measurement completed.
            if (dataLen < 2) {
                R2_WARN(QString("Status packet carried no status byte: %1")
                            .arg(QString(packet.toHex(' '))));
                return;
            }
            uint8_t status = static_cast<uint8_t>(packet[6]);
            const QString name = r2StatusName(status);
            // Bind the ternary to a local: R2_LOG concatenates its argument onto a
            // prefix, so an unparenthesised ternary would bind as (prefix + cond) ? a : b.
            const QString statusLine =
                name.isEmpty() ? QString("Status %1 (no name for this code)").arg(status)
                               : QString("Status %1: %2").arg(status).arg(name);
            R2_LOG(statusLine);
            // The device is telling us it is still working — keep the run alive.
            if (m_measuring && r2StatusIsProgress(status))
                m_measurementTimer.start();
            // Status 6 (Average Test Finished) and 9 (Loop Test Finished) are the
            // device's terminal signals for the two multi-reading run types. The value
            // itself already arrived — these only end the run, which is why a dropped
            // terminal status costs the spinner and not the reading. The liveness
            // watchdog covers the case where one never comes.
            if (status == 6 || status == 9)
                finishMeasurement(/*complete=*/true);
            break;
        }
        case 1: {
            // Temperature: Data1-2 = prism temp * 10, Data3-4 = tank temp * 10,
            // Data5 = the unit those two are expressed in (0 = °C, 1 = °F).
            //
            // The unit is a device setting the user flips on the R2 itself, and it
            // changes the wire values — DiFluid's own worked example in protocolR2.md
            // is a °F packet (`... 03 17 03 14 01` = 79.1/78.8 °F). Ignoring Data5
            // meant an R2 set to Fahrenheit reported 79.1 as if it were Celsius.
            // RefractometerDevice::temperature() is Celsius (DiFluidR1 emits °C), so
            // convert here rather than let a unit-tagged number escape the driver.
            if (dataLen < 5) {
                // Every neighbouring malformed-packet path warns; this one used to
                // return in silence, so a framing change would show up as the
                // temperature simply never updating against a clean log.
                R2_WARN(QString("Temperature packet too short (dataLen=%1, need 5): %2")
                            .arg(dataLen).arg(QString(packet.toHex(' '))));
                return;
            }
            uint16_t prismTemp = static_cast<uint16_t>(
                (static_cast<uint8_t>(packet[6]) << 8) | static_cast<uint8_t>(packet[7]));
            uint16_t tankTemp = static_cast<uint16_t>(
                (static_cast<uint8_t>(packet[8]) << 8) | static_cast<uint8_t>(packet[9]));

            // Unit provenance matters as much as the unit. A packet with no Data5 and
            // a packet that says "Celsius" used to log identically, so the one case
            // where we know the unit and the one where we are guessing were
            // indistinguishable in the only record that exists.
            enum class Unit { Celsius, Fahrenheit, Unreported, Unrecognised };
            Unit unit = Unit::Unreported;
            if (dataLen >= 6) {
                const uint8_t raw = static_cast<uint8_t>(packet[10]);
                unit = raw == 0 ? Unit::Celsius
                     : raw == 1 ? Unit::Fahrenheit
                                : Unit::Unrecognised;
                if (unit == Unit::Unrecognised)
                    R2_WARN(QString("Unrecognised temperature unit byte 0x%1 — treating as °C")
                                .arg(raw, 2, 16, QLatin1Char('0')));
            }
            const bool fahrenheit = (unit == Unit::Fahrenheit);
            const auto toCelsius = [fahrenheit](double t) {
                return fahrenheit ? (t - 32.0) * 5.0 / 9.0 : t;
            };
            const double prismC = toCelsius(prismTemp / 10.0);
            const double tankC = toCelsius(tankTemp / 10.0);
            m_temperature = prismC;
            // A temperature packet lands per test, so it is progress too.
            if (m_measuring) m_measurementTimer.start();
            R2_LOG(QString("Temperature: prism=%1°C tank=%2°C (unit: %3)")
                .arg(prismC, 0, 'f', 1).arg(tankC, 0, 'f', 1)
                .arg(unit == Unit::Celsius      ? QStringLiteral("°C, reported by device")
                   : unit == Unit::Fahrenheit   ? QStringLiteral("°F, converted")
                   : unit == Unit::Unreported   ? QStringLiteral("not reported — assuming °C")
                                                : QStringLiteral("unrecognised — assuming °C")));
            emit temperatureChanged(m_temperature);
            break;
        }
        case 2: {
            // TDS result: Data1-2 = concentration * 100, Data3-6 = refractive index * 100000
            if (dataLen < 3) return;
            quint16 tdsRaw = static_cast<quint16>(
                (static_cast<uint8_t>(packet[6]) << 8) | static_cast<uint8_t>(packet[7]));
            logRefractiveIndex(packet, dataLen);
            if (averagedRun) {
                // One test of several. Informative in the log — it shows the scatter the
                // averaging exists to smooth — but it is not the reading.
                R2_LOG(QString("Individual test in averaged run: %1%% (raw=%2) — not emitted")
                           .arg(tdsRaw / 100.0, 0, 'f', 2).arg(tdsRaw));
                if (m_measuring) m_measurementTimer.start();
                break;
            }
            // In a loop run the reading is real and must reach consumers — latest wins,
            // so the settled value is the one that survives — but the run continues
            // until status 9.
            emitTdsResult(tdsRaw, /*isAverage=*/false, /*terminal=*/!loopRun);
            break;
        }
        case 3: {
            // Average result: same format as pack 2. Under an averaged run this lands once
            // per constituent test carrying the average so far, so each is emitted and the
            // last one wins. Delivery is never contingent on a terminal packet arriving:
            // a dropped terminal status would otherwise mean the user gets nothing.
            if (dataLen < 3) return;
            quint16 tdsRaw = static_cast<quint16>(
                (static_cast<uint8_t>(packet[6]) << 8) | static_cast<uint8_t>(packet[7]));
            // Data3-6 is NOT the refractive index here, despite occupying the same
            // offsets as in pack 2. Observed on hardware: a run whose per-test RI read
            // 1.34689 reported 782332 in this field alongside a 7.83% average — i.e. the
            // averaged concentration at higher precision, not an RI (which is ~1.3).
            // Logging it as "refractive index" would put a wrong number in the record
            // someone later reasons from.
            if (dataLen >= 7) {
                const quint32 extra = (static_cast<quint32>(static_cast<uint8_t>(packet[8])) << 24)
                                    | (static_cast<quint32>(static_cast<uint8_t>(packet[9])) << 16)
                                    | (static_cast<quint32>(static_cast<uint8_t>(packet[10])) << 8)
                                    | static_cast<quint32>(static_cast<uint8_t>(packet[11]));
                R2_LOG(QString("Average high-precision concentration: %1%% (raw=%2)")
                           .arg(extra / 100000.0, 0, 'f', 5).arg(extra));
            }
            emitTdsResult(tdsRaw, /*isAverage=*/true, /*terminal=*/!averagedRun);
            break;
        }
        case 4: {
            // Average temp + count info: Data5 = tests completed, Data6 = tests total.
            // One lands per completed test, so it is both a progress signal and a sign
            // the device is still working.
            if (dataLen >= 7) {
                const int completed = static_cast<uint8_t>(packet[10]);
                const int total = static_cast<uint8_t>(packet[11]);
                R2_LOG(QString("Averaged run progress: test %1 of %2").arg(completed).arg(total));
                emit averageProgress(completed, total);
            } else {
                R2_LOG(QString("Average temp/count packet (no counter, %1 data bytes)")
                           .arg(dataLen));
            }
            if (m_measuring) m_measurementTimer.start();
            break;
        }
        default:
            R2_LOG(QString("Unknown pack number: %1").arg(packNo));
            break;
        }
    } else {
        // Non-action responses (device info, settings)
        R2_LOG(QString("Response: Func=%1 Cmd=%2").arg(func).arg(cmd));
    }
}

void DiFluidR2::handleSerialNumberPart(const QByteArray& data) {
    // Data0 = part index, Data1..Data5 = five bytes of serial.
    if (data.size() < 1 + SERIAL_PART_BYTES) {
        R2_WARN(QString("Serial number packet too short (%1 bytes)").arg(data.size()));
        return;
    }
    const int part = static_cast<uint8_t>(data[0]);
    if (part >= SERIAL_PART_COUNT) {
        R2_WARN(QString("Serial number part index %1 out of range").arg(part));
        return;
    }

    if (m_serialParts.size() != SERIAL_PART_COUNT * SERIAL_PART_BYTES)
        m_serialParts = QByteArray(SERIAL_PART_COUNT * SERIAL_PART_BYTES, '\0');
    m_serialParts.replace(part * SERIAL_PART_BYTES, SERIAL_PART_BYTES,
                          data.mid(1, SERIAL_PART_BYTES));
    m_serialPartsSeen |= static_cast<quint8>(1u << part);

    // Only once every part is in. A partial buffer still contains zero bytes for the
    // parts that have not arrived, and logging that as the serial number would put a
    // wrong device identity into a log someone later reasons from.
    constexpr quint8 allParts = (1u << SERIAL_PART_COUNT) - 1;
    if (m_serialPartsSeen != allParts) {
        R2_LOG(QString("Serial number part %1 of %2 received")
                   .arg(part + 1).arg(SERIAL_PART_COUNT));
        return;
    }

    m_serialNumber = QString::fromLatin1(m_serialParts);
    R2_LOG(QString("Serial number: \"%1\"").arg(m_serialNumber));
}

void DiFluidR2::finishMeasurement(bool complete) {
    m_measurementTimer.stop();
    if (complete) emit measurementComplete();
    // measuringChanged is emitted unconditionally, matching what every other end-of-run
    // path in this driver has always done — a device-initiated run leaves m_measuring
    // false throughout, and consumers tolerate the redundant notification.
    m_measuring = false;
    emit measuringChanged();
}

void DiFluidR2::emitTdsResult(quint16 tdsRaw, bool isAverage, bool terminal) {
    const double tds = tdsRaw / 100.0;
    const QString label = isAverage ? QStringLiteral("Average TDS")
                                     : QStringLiteral("TDS");

    // The R2's failure sentinel lands in the same field as a real reading and
    // passes the checksum (it's a well-formed packet). The only thing that
    // distinguishes it is being physically impossible — gate on that. This is
    // the single chokepoint for every TDS that can reach a consumer: the app's
    // "Read TDS" button (single test → pack 2), the physical R2 Start button
    // (streamed pack 2), and the averaged result (pack 3) all arrive here.
    if (tds > MAX_PLAUSIBLE_TDS) {
        R2_WARN(QString("%1 out of range: %2% (raw=%3) — ignoring")
                    .arg(label).arg(tds, 0, 'f', 2).arg(tdsRaw));
        emit errorOccurred("R2 reported an out-of-range value");
        finishMeasurement(/*complete=*/false);
        return;
    }

    m_tds = tds;
    // A non-terminal reading means something different in the two multi-reading runs:
    // an averaged run is converging on a mean, a loop test is re-measuring the same
    // sample until it settles. Saying "running average" for a loop test would describe
    // the wrong mechanism to whoever reads the log next.
    const QString progressNote =
        terminal   ? QString()
        : isAverage ? QStringLiteral(" (running average, not final)")
                    : QStringLiteral(" (still settling, not final)");
    R2_LOG(QString("%1: %2% (raw=%3)%4").arg(label).arg(tds, 0, 'f', 2).arg(tdsRaw)
               .arg(progressNote));
    emit tdsChanged(m_tds);

    if (!terminal) {
        // A converging average: the value is delivered, but the run continues.
        if (m_measuring) m_measurementTimer.start();
        return;
    }
    finishMeasurement(/*complete=*/true);
}

void DiFluidR2::logRefractiveIndex(const QByteArray& packet, quint8 dataLen) {
    // Data3-6 (packet bytes 8..11) = refractive index * 100000. Caller already verified
    // the packet holds dataLen data bytes, so bytes 8..11 are in range when dataLen >= 7.
    if (dataLen < 7) {
        R2_LOG("No refractive index in packet (short packet / older firmware)");
        return;
    }
    quint32 riRaw = (static_cast<quint32>(static_cast<uint8_t>(packet[8])) << 24)
                  | (static_cast<quint32>(static_cast<uint8_t>(packet[9])) << 16)
                  | (static_cast<quint32>(static_cast<uint8_t>(packet[10])) << 8)
                  | static_cast<quint32>(static_cast<uint8_t>(packet[11]));
    R2_LOG(QString("Refractive index: %1 (raw=%2)")
               .arg(riRaw / 100000.0, 0, 'f', 5).arg(riRaw));
}

bool DiFluidR2::validateChecksum(const QByteArray& packet) const {
    if (packet.size() < PACKET_MIN_LENGTH) return false;

    // Checksum = sum of all bytes from index 0 to N-2, mod 256
    uint8_t calculated = 0;
    for (qsizetype i = 0; i < packet.size() - 1; ++i) {
        calculated += static_cast<uint8_t>(packet[i]);
    }
    uint8_t received = static_cast<uint8_t>(packet[packet.size() - 1]);
    return calculated == received;
}

void DiFluidR2::sendCommand(const QByteArray& cmd) {
    // Silence here is the empty-catch-block of BLE drivers: requestMeasurement()
    // has already set the measuring state and armed the watchdog by this point, so
    // a dropped write shows up as a spinner that runs to timeout with nothing in
    // the log to say the request never left.
    if (!m_transport || !m_characteristicsReady) {
        R2_WARN(QString("Dropping command %1 — %2")
                    .arg(QString(cmd.toHex(' ')),
                         m_transport ? QStringLiteral("characteristics not ready")
                                     : QStringLiteral("no transport")));
        return;
    }
    m_transport->writeCharacteristic(Refractometer::DiFluidR2::SERVICE,
                                     Refractometer::DiFluidR2::CHARACTERISTIC, cmd);
}
