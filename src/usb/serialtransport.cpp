#include "usb/serialtransport.h"
#include "ble/de1logging.h"
#include "ble/protocol/de1characteristics.h"
#include "usb/serialstall.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbhelper.h"
#endif

#include <QDebug>
#include <QMetaEnum>

// Alias the shared DE1 helpers — never copy a body. Tag "Serial" so the DE1's
// serial link is distinguishable from the USB discovery that found it, while
// one [DE1] search still returns both.
#define SERIAL_LOG(msg)  DE1_LOG_TAGGED("Serial", msg)
#define SERIAL_INFO(msg) DE1_INFO_TAGGED("Serial", msg)
#define SERIAL_WARN(msg) DE1_WARN_TAGGED("Serial", msg)

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

SerialTransport::SerialTransport(const QString& portName, QObject* parent)
    : DE1Transport(parent)
    , m_portName(portName)
{
    m_clock.start();
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
    // Log-only, like every other diagnostic in this file. errorOccurred on a
    // DE1Transport raises a MODAL (BLEManager::onDe1Error → main.qml's
    // bleErrorDialog), and neither of these is a thing a user can act on: a
    // write against a closed port is a dropped command the reconnect path
    // handles, and an unmapped UUID is a programming error in our own
    // characteristic table. The second was the worse offender — it put a raw
    // "{0000a002-0000-1000-8000-00805f9b34fb}" in a dialog box, which is the
    // same unreportable-diagnostic failure as #1586's "Service error: 5". (#1658)
    if (!m_connected) {
        SERIAL_WARN(QStringLiteral("Serial port not open — dropping write to %1")
                        .arg(uuid.toString()));
        return;
    }

    char letter = uuidToLetter(uuid);
    if (letter == '\0') {
        SERIAL_WARN(QStringLiteral("Unknown UUID for serial write: %1").arg(uuid.toString()));
        return;
    }

    // The stall check. Deliberately placed before the write rather than after, so
    // the elapsed time reported is the gap the machine has actually left us in
    // rather than one including this command's own round trip.
    if (m_stall.shouldWarn(m_clock.elapsed())) {
        SERIAL_WARN(QStringLiteral(
                        "Port %1 is open and accepting writes but the DE1 has sent "
                        "nothing for %2 s — the machine is not notifying. Subscriptions "
                        "were written at open; nothing will report a disconnect because "
                        "the port never closed. Unplug and replug the USB cable, or "
                        "power-cycle the machine.")
                        .arg(m_portName)
                        .arg(m_stall.staleForMs(m_clock.elapsed()) / 1000));
    }

    // Protocol: <LETTER>hexdata\n
    QString command = QStringLiteral("<%1>%2\n").arg(QChar(letter), bytesToHexString(data));
    writeRaw(command.toLatin1());

    // Serial writes are synchronous — signal immediately that the queue is drained
    emit queueDrained();

    SERIAL_LOG(QStringLiteral("TX <%1> %2 bytes: %3")
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
        // Programming error in our characteristic table, not a user condition.
        SERIAL_WARN(QStringLiteral("Unknown UUID for serial subscribe: %1").arg(uuid.toString()));
        return;
    }

    if (m_subscribed.contains(letter)) {
        return;  // Already subscribed
    }

    // Protocol: <+LETTER>\n
    QString command = QStringLiteral("<+%1>\n").arg(QChar(letter));
    writeRaw(command.toLatin1());
    m_subscribed.insert(letter);

    SERIAL_LOG(QStringLiteral("Subscribe %1 (UUID %2)").arg(QChar(letter), uuid.toString()));
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
    // Disarmed, not just left running: a closed port has no stream to be stale,
    // and leaving it armed would make the next write after a reconnect measure
    // the gap across the outage.
    m_stall.disarm();
    m_buffer.clear();

    if (wasConnected) {
        SERIAL_INFO(QStringLiteral("Disconnected: %1").arg(m_portName));
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
        SERIAL_WARN(QStringLiteral("Android USB connection not open — connect aborted"));
        return;
    }

    m_connected = true;
    m_buffer.clear();
    m_subscribed.clear();

    // Start polling for incoming data (20ms = 50Hz — responsive for ~5Hz shot data)
    m_readTimer.start(20);

    SERIAL_INFO(QStringLiteral("Android USB connection active"));
#else
    m_port->setPortName(m_portName);

    if (!m_port->open(QIODevice::ReadWrite)) {
        // Log-only, and this is the one case in this file where that deserves
        // an argument rather than an assertion. USBManager has just probed this
        // exact port and confirmed a DE1 on it, so an open failure here is a
        // narrow race (something grabbed the port in between) — EXCEPT for
        // PermissionError, which on Linux is the recurring "user is not in the
        // dialout group" case and IS actionable. Rather than reinstate a modal
        // carrying an untranslated raw QSerialPort string, the advisory goes in
        // the log naming the exact remedy: the debug log is what the issue
        // template collects and what users' own assistants read. USBManager also
        // discards the unopened transport and re-arms discovery, and the status
        // bar's machineStatus widget shows the DE1 as Disconnected meanwhile, so
        // the failure is neither invisible nor terminal. (#1658)
        SERIAL_WARN(QStringLiteral("Failed to open serial port %1: %2")
                        .arg(m_portName, m_port->errorString()));
        if (m_port->error() == QSerialPort::PermissionError) {
            SERIAL_WARN(QStringLiteral(
                            "*** ADVISORY: the OS refused access to %1 — on Linux add your "
                            "user to the 'dialout' group (sudo usermod -aG dialout $USER) and "
                            "log out and back in. Otherwise another application is holding "
                            "the port.")
                            .arg(m_portName));
        }
        return;
    }

    // DE1 serial protocol requires DTR and RTS off
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);

    m_connected = true;
    m_buffer.clear();
    m_subscribed.clear();

    SERIAL_INFO(QStringLiteral("Port opened: %1 (115200 8N1)").arg(m_portName));
#endif

    // Arm stall detection from the moment the port is live, so a machine that
    // NEVER starts notifying is caught, not just one that stops. That is the case
    // with no other symptom at all: the subscribes below are written, the writes
    // succeed, and nothing ever comes back.
    m_stall.arm(m_clock.elapsed());

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
        SERIAL_LOG(QStringLiteral("Requested version (<%1>)").arg(QChar(versionLetter)));
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
        // The cable came out. disconnect() drops the DE1 to offline, which the
        // status bar's machineStatus widget already says in words — a modal
        // repeating it adds nothing to an action the user just took. (#1658)
        SERIAL_WARN(QStringLiteral("Android USB connection lost"));
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
    // valueToKey, not a bare int: QSerialPort::SerialPortError is a Q_ENUM, so the
    // `qDebug() << error` this replaced printed the ENUMERATOR NAME via QDebug's
    // generic Q_ENUM operator. Streaming it through arg() prints "2", which needs a
    // Qt header to decode — in a subsystem diagnosed from user-submitted logs. This
    // is the line that classifies a serial failure, so it keeps the name.
    const char* errorName =
        QMetaEnum::fromType<QSerialPort::SerialPortError>().valueToKey(error);
    SERIAL_WARN(QStringLiteral("Port error: %1 (%2) %3")
                    .arg(errorName ? QLatin1String(errorName) : QLatin1String("Unknown"))
                    .arg(static_cast<int>(error))
                    .arg(errorStr));

    // Resource errors (device unplugged, etc.) are fatal. Both arms are
    // log-only: the fatal one drops the DE1 to offline, which the connection
    // indicator shows, and the non-fatal one is a transient the link rides out.
    // Neither produced a message a user could act on — both put a raw
    // QSerialPort::errorString() behind a log prefix into a modal. (#1658)
    if (error == QSerialPort::ResourceError
        || error == QSerialPort::DeviceNotFoundError
        || error == QSerialPort::PermissionError) {
        SERIAL_WARN(QStringLiteral("Serial port lost: %1").arg(errorStr));
        disconnect();
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
        SERIAL_WARN(QStringLiteral("Android USB write failed"));
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
        qsizetype idx = m_buffer.indexOf('\n');
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
        SERIAL_WARN(QStringLiteral("Buffer overflow, discarding %1 bytes").arg(m_buffer.size()));
        m_buffer.clear();
    }
}

void SerialTransport::processLine(const QString& line)
{
    // Any complete inbound line proves the stream is alive, including the two
    // unrecognised kinds below — a DE1 answering with something we cannot parse is
    // a different fault from a DE1 not answering, and only the second is a stall.
    if (m_stall.noteInbound(m_clock.elapsed())) {
        SERIAL_INFO(QStringLiteral("Inbound traffic resumed"));
    }

    // Response format: [LETTER]hexdata
    // Minimum valid line: [X] (3 chars, possibly with empty hex data)
    if (line.length() < 3 || line[0] != QLatin1Char('[') || line[2] != QLatin1Char(']')) {
        SERIAL_LOG(QStringLiteral("RX unknown: %1").arg(line));
        return;
    }

    char letter = line[1].toLatin1();
    QBluetoothUuid uuid = letterToUuid(letter);
    if (uuid.isNull()) {
        SERIAL_LOG(QStringLiteral("RX unknown letter: %1").arg(QChar(letter)));
        return;
    }

    QString hexData = line.mid(3);  // Everything after [X]
    QByteArray data = hexStringToBytes(hexData);

    // No per-frame RX line. It used to log one "RX [M] 19 bytes" for every
    // notification. subscribeAll() subscribes to seven characteristics, of which
    // the periodically-notifying ones (ShotSample alone runs ~5 Hz — see
    // docs/CLAUDE_MD/BLE_PROTOCOL.md) put this on the order of 20 lines a second,
    // roughly 600 per shot.
    //
    // It went only to the connections-page DE1 window — which is not a bounded
    // ring but an uncapped `de1LogText.text += message` with no Clear button
    // (SettingsConnectionsTab.qml), so those 600 lines a shot accumulated in a
    // QML string for the process lifetime. That is a reason to delete it, not a
    // reason it was free.
    //
    // Routing it to the log the way the rest of this file now is would have made
    // the DE1's telemetry the single largest thing in a submitted log while
    // answering no question: that a frame arrived is what the parsed values and
    // the shot record already prove, and BleTransport::onCharacteristicChanged
    // logs no equivalent. The two anomaly cases above ARE logged — those are the
    // ones a reader is looking for.
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
