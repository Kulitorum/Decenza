#include "usb/usbdecentscale.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbscalehelper.h"
#endif

#include <QDebug>

// Helper macro matching DecentScale BLE convention
#define SCALE_LOG(msg) do { \
    QString _msg = QString("[USB Scale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

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
    if (!AndroidUsbScaleHelper::isOpen()) {
        emit errorOccurred(QStringLiteral("[USB Scale] Android USB connection not open"));
        return;
    }

    m_buffer.clear();
    m_readTimer.start(20);  // 50Hz polling
#else
    if (portName.isEmpty()) {
        emit errorOccurred(QStringLiteral("[USB Scale] No port name specified"));
        return;
    }

    m_port->setPortName(portName);
    if (!m_port->open(QIODevice::ReadWrite)) {
        emit errorOccurred(QStringLiteral("[USB Scale] Failed to open %1: %2")
                               .arg(portName, m_port->errorString()));
        return;
    }

    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    m_buffer.clear();
#endif

    setConnected(true);
    SCALE_LOG("Connected");

    // Send init command (from reaprime): 0x20 0x01
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

    if (isConnected()) {
        setConnected(false);
        SCALE_LOG("Disconnected");
    }
}

// ===========================================================================
// Private slots — platform-specific data handling
// ===========================================================================

#ifdef Q_OS_ANDROID

void UsbDecentScale::onReadTimer()
{
    if (!AndroidUsbScaleHelper::isOpen()) {
        qWarning() << "[USB Scale] Android USB connection lost";
        emit errorOccurred(QStringLiteral("[USB Scale] USB connection lost"));
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
    qWarning() << "[USB Scale] Port error:" << error << errorStr;

    if (error == QSerialPort::ResourceError
        || error == QSerialPort::DeviceNotFoundError
        || error == QSerialPort::PermissionError) {
        emit errorOccurred(QStringLiteral("[USB Scale] Serial port lost: %1").arg(errorStr));
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
        int startIdx = m_buffer.indexOf(static_cast<char>(0x03));
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
        if (m_buffer.size() < 7) {
            return;
        }

        QByteArray packet = m_buffer.left(7);

        // Validate XOR checksum
        uint8_t expected = calculateXor(packet);
        uint8_t actual = static_cast<uint8_t>(packet[6]);
        if (expected != actual) {
            // Bad checksum — skip this byte and try again
            m_buffer.remove(0, 1);
            continue;
        }

        // Valid packet — consume and process
        m_buffer.remove(0, 7);
        processPacket(packet);
    }

    // Safety: prevent unbounded buffer growth
    if (m_buffer.size() > 1024) {
        qWarning() << "[USB Scale] Buffer overflow, discarding" << m_buffer.size() << "bytes";
        m_buffer.clear();
    }
}

void UsbDecentScale::processPacket(const QByteArray& packet)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(packet.constData());
    uint8_t command = d[1];

    if (command == 0xCE || command == 0xCA) {
        // Weight data: bytes 2-3 are big-endian signed int16, divide by 10 for grams
        int16_t weightRaw = (static_cast<int16_t>(d[2]) << 8) | d[3];
        double weight = weightRaw / 10.0;
        setWeight(weight);
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

    packet[6] = static_cast<char>(calculateXor(packet));

    writeRaw(packet);
}

void UsbDecentScale::writeRaw(const QByteArray& data)
{
#ifdef Q_OS_ANDROID
    int written = AndroidUsbScaleHelper::write(data);
    if (written < 0) {
        qWarning() << "[USB Scale] Android USB write failed";
    }
#else
    if (m_port && m_port->isOpen()) {
        m_port->write(data);
    }
#endif
}

uint8_t UsbDecentScale::calculateXor(const QByteArray& data)
{
    uint8_t result = 0;
    for (int i = 0; i < data.size() - 1; i++) {
        result ^= static_cast<uint8_t>(data[i]);
    }
    return result;
}
