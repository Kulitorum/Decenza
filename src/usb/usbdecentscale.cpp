#include "usb/usbdecentscale.h"
#include "ble/protocol/decentscaleprotocol.h"
#include "ble/scales/scalelogging.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbscalehelper.h"
#endif

#include <QDateTime>
#include <QDebug>

// Alias over the shared macro (ble/scales/scalelogging.h) — the [Scale] marker
// and the double-write shape live there, not in a copy here. Note the shared
// header also defines a two-argument SCALE_LOG, so this driver's one-argument
// spelling gets its own name rather than shadowing it.
#define USB_SCALE_LOG(msg)  SCALE_LOG_TAGGED("USB Scale", msg)
#define USB_SCALE_INFO(msg) SCALE_INFO_TAGGED("USB Scale", msg)
#define USB_SCALE_WARN(msg) SCALE_WARN_TAGGED("USB Scale", msg)

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

UsbDecentScale::UsbDecentScale(QObject* parent)
    : ScaleDevice(parent)
{
#ifdef Q_OS_ANDROID
    connect(&m_readTimer, &QTimer::timeout, this, &UsbDecentScale::onReadTimer);
#else
    m_port = new QSerialPort(this);
    m_port->setBaudRate(115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_port, &QSerialPort::readyRead, this, &UsbDecentScale::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &UsbDecentScale::onErrorOccurred);
#endif

    // Heartbeat every 1 second (same as BLE DecentScale)
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &UsbDecentScale::onHeartbeatTimer);
}

UsbDecentScale::~UsbDecentScale()
{
    close();
}

// ===========================================================================
// ScaleDevice interface
// ===========================================================================

void UsbDecentScale::connectToDevice(const QBluetoothDeviceInfo& device)
{
    // USB scale doesn't use BLE device info — call open() directly
    Q_UNUSED(device);
}

void UsbDecentScale::tare()
{
    // Same as BLE: 0x0F 0x01 0x00
    sendCommand(QByteArray::fromHex("0F0100"));
}

void UsbDecentScale::startTimer()
{
    sendCommand(QByteArray::fromHex("0B0300"));
}

void UsbDecentScale::stopTimer()
{
    sendCommand(QByteArray::fromHex("0B0000"));
}

void UsbDecentScale::resetTimer()
{
    sendCommand(QByteArray::fromHex("0B0200"));
}

void UsbDecentScale::wake()
{
    // LCD enable (grams mode): 0A 01 01 00 01
    sendCommand(QByteArray::fromHex("0A01010001"));
}

void UsbDecentScale::sleep()
{
    // LCD disable + sleep: 0A 02 00
    sendCommand(QByteArray::fromHex("0A0200"));
    emit sleepCompleted();
}

void UsbDecentScale::startFirmwareUpdate(const QString& targetVersion)
{
    if (!supportsFirmwareUpdate()) {
        USB_SCALE_WARN(DecentScaleProtocol::firmwareUpdateUnknownVersionMessage());
        return;
    }
    // The version is required: a bare command starts the scale's own picker.
    // See DecentScaleProtocol::buildTargetedFirmwareUpdateCommand.
    const QByteArray command = DecentScaleProtocol::buildTargetedFirmwareUpdateCommand(targetVersion);
    if (command.isEmpty()) {
        USB_SCALE_WARN(DecentScaleProtocol::firmwareUpdateBadTargetMessage(targetVersion));
        return;
    }
    // writeRaw skips the isConnected() check sendCommand opens with, so make it
    // here rather than logging a start and writing into a closed port.
    if (!isConnected()) {
        USB_SCALE_WARN(DecentScaleProtocol::firmwareUpdateNotConnectedMessage());
        return;
    }
    USB_SCALE_INFO(DecentScaleProtocol::firmwareUpdateStartingMessage(targetVersion));

    // Written raw rather than through sendCommand: USB is framed, not
    // packetised, and a targeted 0x1B is exactly a five-byte frame
    // (openscale include/decent_protocol_frame.h, decentCommandFrameLength).
    // sendCommand's padding would leave the pad and checksum to the scale's
    // text path, which splits a run at the first 0x03 — harmless in practice,
    // but writing the frame the framer expects needs no such argument.
    QByteArray frame;
    frame.append(DecentScaleProtocol::PacketHeader);
    frame.append(command);
    writeRaw(frame);
}

// ===========================================================================
// USB-specific API
// ===========================================================================

void UsbDecentScale::open(const QString& portName)
{
    if (isConnected()) {
        return;
    }

#ifdef Q_OS_ANDROID
    Q_UNUSED(portName);
    // On Android, AndroidUsbScaleHelper is already open (UsbScaleManager opened it)
    // Log-only, like the DE1's SerialTransport. errorOccurred on a ScaleDevice
    // reaches a MODAL (main.cpp wires it to BLEManager::errorOccurred), and
    // UsbScaleManager::connectToScale() already handles every failure below:
    // it checks isConnected() after open(), tears the half-open scale down, and
    // re-arms discovery so the next poll re-probes. A dialog here interrupts a
    // recovery already under way — and did it with a raw "[USB Scale]" log
    // prefix in the message. (#1658)
    if (!AndroidUsbScaleHelper::isOpen()) {
        USB_SCALE_WARN(QStringLiteral("Android USB connection not open — open aborted"));
        return;
    }

    m_buffer.clear();
    m_readTimer.start(20);  // 50Hz polling
#else
    if (portName.isEmpty()) {
        // Programming error — the caller resolved a confirmed port before this.
        USB_SCALE_WARN(QStringLiteral("No port name specified — open aborted"));
        return;
    }

    m_port->setPortName(portName);
    if (!m_port->open(QIODevice::ReadWrite)) {
        USB_SCALE_WARN(QStringLiteral("Failed to open %1: %2")
                           .arg(portName, m_port->errorString()));
        if (m_port->error() == QSerialPort::PermissionError) {
            USB_SCALE_WARN(QStringLiteral(
                "*** ADVISORY: the OS refused access to %1 — on Linux add your user to "
                "the 'dialout' group (sudo usermod -aG dialout $USER) and log out and "
                "back in. Otherwise another application is holding the port.").arg(portName));
        }
        return;
    }

    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    m_buffer.clear();
#endif

    setConnected(true);
    USB_SCALE_INFO("Connected");

    // Send init command (from Decaid): 0x20 0x01
    sendCommand(QByteArray::fromHex("200100"));

    // Enable LCD
    wake();

    // Start heartbeat (keeps scale connection alive)
    m_heartbeatTimer.start(1000);
}

void UsbDecentScale::close()
{
    m_heartbeatTimer.stop();

#ifdef Q_OS_ANDROID
    m_readTimer.stop();
    // Don't close AndroidUsbScaleHelper here — UsbScaleManager manages the lifecycle
#else
    if (m_port && m_port->isOpen()) {
        m_port->close();
    }
#endif

    m_buffer.clear();

    // Run end for the frame-shape collapse, which is keyed by shapes this
    // driver does not enumerate — hence flushAll (core/logcollapse.h). Without
    // it a port's tally would surface on the next port's first odd frame.
    for (const auto& [shape, collapsed] :
         m_frameShapeLog.flushAll(QDateTime::currentMSecsSinceEpoch())) {
        USB_SCALE_LOG(shape + LogCollapse::suffix(collapsed));
    }

    if (!m_firmwareVersion.isEmpty()) {
        m_firmwareVersion.clear();
        emit firmwareVersionChanged();
    }

    if (isConnected()) {
        setConnected(false);
        USB_SCALE_INFO("Disconnected");
    }
}

// ===========================================================================
// Private slots — platform-specific data handling
// ===========================================================================

#ifdef Q_OS_ANDROID

void UsbDecentScale::onReadTimer()
{
    if (!AndroidUsbScaleHelper::isOpen()) {
        // Unplugged. close() drops the scale, UsbScaleManager's poll notices and
        // emits scaleLost(), and the UI follows — no dialog needed for an action
        // the user just took. (#1658)
        USB_SCALE_WARN(QStringLiteral("Android USB connection lost"));
        close();
        return;
    }

    QByteArray data = AndroidUsbScaleHelper::readAvailable();
    if (data.isEmpty()) return;

    m_buffer.append(data);
    processBuffer();
}

#else // Desktop

void UsbDecentScale::onReadyRead()
{
    m_buffer.append(m_port->readAll());
    processBuffer();
}

void UsbDecentScale::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) return;

    QString errorStr = m_port->errorString();
    USB_SCALE_WARN(QStringLiteral("Port error %1: %2")
                       .arg(static_cast<int>(error)).arg(errorStr));

    if (error == QSerialPort::ResourceError
        || error == QSerialPort::DeviceNotFoundError
        || error == QSerialPort::PermissionError) {
        USB_SCALE_WARN(QStringLiteral("Serial port lost: %1").arg(errorStr));
        close();
    }
}

#endif // Q_OS_ANDROID

void UsbDecentScale::onHeartbeatTimer()
{
    if (!isConnected()) return;

    // Heartbeat: 0A 03 FF FF (same as BLE DecentScale)
    sendCommand(QByteArray::fromHex("0A03FFFF"));
}

// ===========================================================================
// Private helpers — protocol handling
// ===========================================================================

void UsbDecentScale::processBuffer()
{
    // The scale sends 7-byte binary packets starting with 0x03.
    // Scan for valid packets, skipping any garbage bytes.
    while (m_buffer.size() >= 7) {
        // Find the next 0x03 marker byte
        qsizetype startIdx = m_buffer.indexOf(static_cast<char>(0x03));
        if (startIdx == -1) {
            // No marker found — discard entire buffer
            m_buffer.clear();
            return;
        }

        // Discard bytes before the marker
        if (startIdx > 0) {
            m_buffer.remove(0, startIdx);
        }

        // Need at least 7 bytes for a complete packet
        if (m_buffer.size() < DecentScaleProtocol::StandardFrameLength) {
            return;
        }

        const uint8_t command = static_cast<uint8_t>(m_buffer[1]);
        const qsizetype frameLen =
            DecentScaleProtocol::notifiedFrameLength(command, m_buffer.size());
        if (frameLen == 0) {
            // A frame longer than what has arrived — today only the 41-byte ADS
            // debug frame. Wait for the rest rather than resyncing through it.
            return;
        }

        if (command == DecentScaleProtocol::TypeAdsDebug) {
            // Consumed whole, not decoded: nothing in the app reads ADS
            // internals. Byte-at-a-time resync used to chew through all 41
            // bytes in silence, and each 7-byte window it tried had a 1-in-256
            // chance of a checksum that happened to match — a fabricated weight
            // or button event out of debug telemetry.
            if (DecentScaleProtocol::checksumMatches(m_buffer, frameLen)) {
                logFrameShapeOnce(QStringLiteral("ADS debug frame (type 0x25)"),
                                  m_buffer.left(frameLen));
                m_buffer.remove(0, frameLen);
                continue;
            }
            // Checksum fails, so this 0x03 0x25 is payload rather than a frame
            // start. Resync, reported by the same line as any other bad
            // checksum below rather than a second one saying the same thing.
            logFrameShapeOnce(QString("Bad checksum on type 0x%1, resyncing")
                                  .arg(command, 2, 16, QChar('0')),
                              m_buffer.left(frameLen));
            m_buffer.remove(0, 1);
            continue;
        }

        QByteArray packet = m_buffer.left(frameLen);

        // Validate XOR checksum — skip for LED response (0x0A) which uses
        // all 7 bytes for data (byte 6 is firmware version, not checksum)
        if (command != DecentScaleProtocol::TypeLedResponse
            && !DecentScaleProtocol::checksumMatches(packet, frameLen)) {
            // Bad checksum — skip this byte and try again. Logged once per
            // shape: a stream this driver cannot frame was otherwise discarded
            // in total silence.
            logFrameShapeOnce(QString("Bad checksum on type 0x%1, resyncing")
                                  .arg(command, 2, 16, QChar('0')),
                              packet);
            m_buffer.remove(0, 1);
            continue;
        }

        // Valid packet — consume and process
        m_buffer.remove(0, frameLen);
        processPacket(packet);
    }

    // Safety: prevent unbounded buffer growth
    if (m_buffer.size() > 1024) {
        USB_SCALE_WARN(QStringLiteral("Buffer overflow, discarding %1 bytes").arg(m_buffer.size()));
        m_buffer.clear();
    }
}

void UsbDecentScale::logFrameShapeOnce(const QString& shape, const QByteArray& data)
{
    const QString line = scaleFrameShapeLine(m_frameShapeLog, shape, data,
                                             QDateTime::currentMSecsSinceEpoch());
    if (!line.isEmpty())
        USB_SCALE_LOG(line);
}

void UsbDecentScale::processPacket(const QByteArray& packet)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(packet.constData());
    uint8_t command = d[1];

    if (command == DecentScaleProtocol::TypeWeight || command == DecentScaleProtocol::TypeWeightAlt) {
        // Weight data: bytes 2-3 are big-endian signed int16, divide by 10 for grams
        int16_t weightRaw = (static_cast<int16_t>(d[2]) << 8) | d[3];
        double weight = weightRaw / 10.0;
        setWeight(weight);
    } else if (command == DecentScaleProtocol::TypeLedResponse) {
        // LED response packet (openscale/HDS format):
        // [0]=0x03 header, [1]=0x0A type, [2-3]=weight, [4]=battery, [5-6]=firmware version
        // Battery: 0-100 = percentage, 0xFF = charging.
        // Drive charging as a first-class signal so the scale battery widget
        // and Settings → Connections row can swap to the "Charging" icon
        // and label instead of showing "100%" — matches DecentScale (BLE)
        // and DecentScaleWifi (WiFi).
        uint8_t battByte = d[4];
        if (battByte <= 100) {
            setCharging(false);
            setBatteryLevel(battByte);
        } else if (battByte == 0xFF) {
            setCharging(true);
            setBatteryLevel(100);  // Keep "100" reporting so existing UI bindings don't regress
        }
        const QString version = DecentScaleProtocol::decodeHdsFirmwareVersion(d[5], d[6]);
        if (m_firmwareVersion != version) {
            USB_SCALE_LOG(QStringLiteral("Firmware version: %1 (raw 0x%2 0x%3)")
                              .arg(version)
                              .arg(d[5], 2, 16, QLatin1Char('0'))
                              .arg(d[6], 2, 16, QLatin1Char('0')));
            m_firmwareVersion = version;
            emit firmwareVersionChanged();
        }
    } else if (command == 0xAA) {
        // Button press
        int button = d[2];
        emit buttonPressed(button);
    }
}

void UsbDecentScale::sendCommand(const QByteArray& commandData)
{
    if (!isConnected()) return;

    // Build 7-byte packet: [0x03, commandData[0..4], XOR]
    QByteArray packet(7, 0);
    packet[0] = 0x03;

    for (int i = 0; i < qMin(commandData.size(), static_cast<qsizetype>(5)); i++) {
        packet[i + 1] = commandData[i];
    }

    packet[6] = static_cast<char>(DecentScaleProtocol::calculateXor(packet));

    writeRaw(packet);
}

void UsbDecentScale::writeRaw(const QByteArray& data)
{
#ifdef Q_OS_ANDROID
    int written = AndroidUsbScaleHelper::write(data);
    if (written < 0) {
        USB_SCALE_WARN(QStringLiteral("Android USB write failed"));
    }
#else
    if (m_port && m_port->isOpen()) {
        m_port->write(data);
    }
#endif
}
