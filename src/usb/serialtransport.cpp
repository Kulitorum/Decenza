#include "usb/serialtransport.h"
#include "ble/protocol/de1characteristics.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbhelper.h"
#endif

#include <QDebug>

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

SerialTransport::SerialTransport(const QString& portName, QObject* parent)
    : DE1Transport(parent)
    , m_portName(portName)
{
#ifdef Q_OS_ANDROID
    // On Android, the connection is already open via AndroidUsbHelper (opened by USBManager probe).
    // The read timer will be started in open().
    connect(&m_readTimer, &QTimer::timeout, this, &SerialTransport::onAndroidReadTimer);
#else
    m_port = new QSerialPort(this);
    m_port->setBaudRate(115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    // Connect signals once here (not in open()) to prevent stacking on reconnect
    connect(m_port, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialTransport::onErrorOccurred);
#endif
}

SerialTransport::~SerialTransport()
{
#ifdef Q_OS_ANDROID
    m_readTimer.stop();
    // Don't close AndroidUsbHelper here — USBManager manages the connection lifecycle
#else
    if (m_port && m_port->isOpen()) {
        m_port->close();
    }
#endif
}

// ===========================================================================
// DE1Transport interface
// ===========================================================================

void SerialTransport::write(const QBluetoothUuid& uuid, const QByteArray& data)
{
    if (!m_connected) {
        emit errorOccurred(QStringLiteral("[USB] Serial port not open"));
        return;
    }

    char letter = uuidToLetter(uuid);
    if (letter == '\0') {
        emit errorOccurred(QStringLiteral("[USB] Unknown UUID for serial write: %1").arg(uuid.toString()));
        return;
    }

    // Protocol: <LETTER>hexdata\n
    QString command = QStringLiteral("<%1>%2\n").arg(QChar(letter), bytesToHexString(data));
    writeRaw(command.toLatin1());

    emit logMessage(QStringLiteral("[USB] TX <%1> %2 bytes: %3")
                        .arg(QChar(letter))
                        .arg(data.size())
                        .arg(bytesToHexString(data)));

    // Serial writes are effectively synchronous (buffered by OS/USB), so signal
    // completion immediately. DE1Device relies on this to track pending writes.
    emit writeComplete(uuid, data);
}

void SerialTransport::read(const QBluetoothUuid& uuid)
{
    // The serial protocol has no dedicated "read" command separate from
    // subscribe. Data for a characteristic arrives via notification after
    // subscribing. Ensure we are subscribed so the caller gets a response.
    if (!m_subscribed.contains(uuidToLetter(uuid))) {
        subscribe(uuid);
    }
}

void SerialTransport::subscribe(const QBluetoothUuid& uuid)
{
    if (!m_connected) {
        return;
    }

    char letter = uuidToLetter(uuid);
    if (letter == '\0') {
        emit errorOccurred(QStringLiteral("[USB] Unknown UUID for serial subscribe: %1").arg(uuid.toString()));
        return;
    }

    if (m_subscribed.contains(letter)) {
        return;  // Already subscribed
    }

    // Protocol: <+LETTER>\n
    QString command = QStringLiteral("<+%1>\n").arg(QChar(letter));
    writeRaw(command.toLatin1());
    m_subscribed.insert(letter);

    emit logMessage(QStringLiteral("[USB] Subscribe %1 (UUID %2)").arg(QChar(letter), uuid.toString()));
}

void SerialTransport::subscribeAll()
{
    // Subscribe to the same set of characteristics as BleTransport
    subscribe(DE1::Characteristic::STATE_INFO);       // N (0xA00E)
    subscribe(DE1::Characteristic::SHOT_SAMPLE);      // M (0xA00D)
    subscribe(DE1::Characteristic::WATER_LEVELS);     // Q (0xA011)
    subscribe(DE1::Characteristic::SHOT_SETTINGS);    // K (0xA00B)
    subscribe(DE1::Characteristic::READ_FROM_MMR);    // E (0xA005)
    subscribe(DE1::Characteristic::FW_MAP_REQUEST);   // I (0xA009)
    subscribe(DE1::Characteristic::TEMPERATURES);     // J (0xA00A)
}

void SerialTransport::disconnect()
{
#ifdef Q_OS_ANDROID
    m_readTimer.stop();
    AndroidUsbHelper::close();
#else
    if (m_port && m_port->isOpen()) {
        m_port->close();
    }
#endif

    bool wasConnected = m_connected;
    m_connected = false;
    m_subscribed.clear();
    m_buffer.clear();

    if (wasConnected) {
        emit logMessage(QStringLiteral("[USB] Disconnected: %1").arg(m_portName));
        emit disconnected();
    }
}

bool SerialTransport::isConnected() const
{
    return m_connected;
}

// ===========================================================================
// Serial-specific API
// ===========================================================================

QString SerialTransport::portName() const
{
    return m_portName;
}

QString SerialTransport::serialNumber() const
{
    return m_serialNumber;
}

void SerialTransport::setSerialNumber(const QString& sn)
{
    m_serialNumber = sn;
}

void SerialTransport::open()
{
    if (m_connected) {
        return;
    }

#ifdef Q_OS_ANDROID
    // On Android, AndroidUsbHelper is already open (USBManager probe opened it).
    // Just verify the connection is live.
    if (!AndroidUsbHelper::isOpen()) {
        emit errorOccurred(QStringLiteral("[USB] Android USB connection not open"));
        return;
    }

    m_connected = true;
    m_buffer.clear();
    m_subscribed.clear();

    // Start polling for incoming data (20ms = 50Hz — responsive for ~5Hz shot data)
    m_readTimer.start(20);

    emit logMessage(QStringLiteral("[USB] Android USB connection active"));
#else
    m_port->setPortName(m_portName);

    if (!m_port->open(QIODevice::ReadWrite)) {
        QString err = QStringLiteral("Failed to open serial port %1: %2")
                          .arg(m_portName, m_port->errorString());
        qWarning() << "[USB]" << err;
        emit errorOccurred(err);
        return;
    }

    // DE1 serial protocol requires DTR and RTS off
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);

    m_connected = true;
    m_buffer.clear();
    m_subscribed.clear();

    emit logMessage(QStringLiteral("[USB] Port opened: %1 (115200 8N1)").arg(m_portName));
#endif

    // Subscribe to all standard DE1 notifications
    subscribeAll();

    emit connected();

    // Request current machine state (Idle = 0x02)
    write(DE1::Characteristic::REQUESTED_STATE, QByteArray::fromHex("02"));

    // Request firmware version — the response will arrive as a notification
    char versionLetter = uuidToLetter(DE1::Characteristic::VERSION);
    if (versionLetter != '\0') {
        QString cmd = QStringLiteral("<%1>\n").arg(QChar(versionLetter));
        writeRaw(cmd.toLatin1());
        emit logMessage(QStringLiteral("[USB] Requested version (<%1>)").arg(QChar(versionLetter)));
    }
}

// ===========================================================================
// Private slots — platform-specific data handling
// ===========================================================================

#ifdef Q_OS_ANDROID

void SerialTransport::onAndroidReadTimer()
{
    // Check if connection was lost
    if (!AndroidUsbHelper::isOpen()) {
        qWarning() << "[USB] Android USB connection lost";
        emit errorOccurred(QStringLiteral("[USB] USB connection lost"));
        disconnect();
        return;
    }

    QByteArray data = AndroidUsbHelper::readAvailable();
    if (data.isEmpty()) return;

    m_buffer.append(data);
    processBuffer();
}

#else // Desktop

void SerialTransport::onReadyRead()
{
    m_buffer.append(m_port->readAll());
    processBuffer();
}

void SerialTransport::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }

    QString errorStr = m_port->errorString();
    qWarning() << "[USB] Port error:" << error << errorStr;

    // Resource errors (device unplugged, etc.) are fatal
    if (error == QSerialPort::ResourceError
        || error == QSerialPort::DeviceNotFoundError
        || error == QSerialPort::PermissionError) {
        emit errorOccurred(QStringLiteral("[USB] Serial port lost: %1").arg(errorStr));
        disconnect();
    } else {
        emit errorOccurred(QStringLiteral("[USB] Serial error: %1").arg(errorStr));
    }
}

#endif // Q_OS_ANDROID

// ===========================================================================
// Private helpers
// ===========================================================================

void SerialTransport::writeRaw(const QByteArray& data)
{
#ifdef Q_OS_ANDROID
    int written = AndroidUsbHelper::write(data);
    if (written < 0) {
        qWarning() << "[USB] Android USB write failed";
    }
#else
    if (m_port && m_port->isOpen()) {
        m_port->write(data);
    }
#endif
}

void SerialTransport::processBuffer()
{
    // Process complete lines (terminated by \n)
    while (true) {
        int idx = m_buffer.indexOf('\n');
        if (idx == -1) {
            break;
        }

        QString line = QString::fromLatin1(m_buffer.left(idx)).trimmed();
        m_buffer.remove(0, idx + 1);

        if (!line.isEmpty()) {
            processLine(line);
        }
    }

    // Safety: prevent unbounded buffer growth from garbage data
    if (m_buffer.size() > 4096) {
        qWarning() << "[USB] Buffer overflow, discarding" << m_buffer.size() << "bytes";
        m_buffer.clear();
    }
}

void SerialTransport::processLine(const QString& line)
{
    // Response format: [LETTER]hexdata
    // Minimum valid line: [X] (3 chars, possibly with empty hex data)
    if (line.length() < 3 || line[0] != QLatin1Char('[') || line[2] != QLatin1Char(']')) {
        emit logMessage(QStringLiteral("[USB] RX unknown: %1").arg(line));
        return;
    }

    char letter = line[1].toLatin1();
    QBluetoothUuid uuid = letterToUuid(letter);
    if (uuid.isNull()) {
        emit logMessage(QStringLiteral("[USB] RX unknown letter: %1").arg(QChar(letter)));
        return;
    }

    QString hexData = line.mid(3);  // Everything after [X]
    QByteArray data = hexStringToBytes(hexData);

    emit logMessage(QStringLiteral("[USB] RX [%1] %2 bytes").arg(QChar(letter)).arg(data.size()));
    emit dataReceived(uuid, data);
}

char SerialTransport::uuidToLetter(const QBluetoothUuid& uuid)
{
    // DE1 UUIDs are 0000XXXX-0000-1000-8000-00805F9B34FB
    // Extract the 16-bit short UUID (XXXX) from the string representation.
    QString uuidStr = uuid.toString();

    // Handle both "{0000xxxx-...}" and "0000xxxx-..." formats
    int hexStart = uuidStr.startsWith(QLatin1Char('{')) ? 5 : 4;

    bool ok;
    uint16_t shortUuid = uuidStr.mid(hexStart, 4).toUInt(&ok, 16);
    if (!ok || shortUuid < 0xA001 || shortUuid > 0xA012) {
        return '\0';
    }

    return static_cast<char>('A' + (shortUuid - 0xA001));
}

QBluetoothUuid SerialTransport::letterToUuid(char letter)
{
    if (letter < 'A' || letter > 'R') {
        return QBluetoothUuid();
    }

    uint16_t shortUuid = 0xA001 + static_cast<uint16_t>(letter - 'A');
    return QBluetoothUuid(
        QStringLiteral("0000%1-0000-1000-8000-00805F9B34FB")
            .arg(shortUuid, 4, 16, QLatin1Char('0')));
}

QByteArray SerialTransport::hexStringToBytes(const QString& hex)
{
    return QByteArray::fromHex(hex.toLatin1());
}

QString SerialTransport::bytesToHexString(const QByteArray& data)
{
    return QString::fromLatin1(data.toHex());
}
