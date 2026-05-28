#include "timemorescale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define TIMEMORE_LOG(msg)  SCALE_LOG("TimemoreScale", msg)
#define TIMEMORE_WARN(msg) SCALE_WARN("TimemoreScale", msg)

// Timemore Dot/Duo BLE protocol
// Commands: 8-10 bytes, header A5 5A, variable-length with trailing checksum.
// Tare command confirmed via HCI snoop of official Timemore app.
// Init sequence: 6 commands sent after connect to enable HW tare/timer.
// Notify packets: type 0x01 = weight data, weight at bytes[8-9] big-endian, signed, tenths of gram.
namespace {
    // Init commands (sent after connect — official app sends these twice)
    const QByteArray CMD_INIT[] = {
        QByteArray::fromHex("A55A02130000000000"),
        QByteArray::fromHex("A55A02080000000000"),
        QByteArray::fromHex("A55A02050000000000"),
        QByteArray::fromHex("A55A02020000000000"),
        QByteArray::fromHex("A55A02060000000000"),
        QByteArray::fromHex("A55A020C0000000000"),
    };
    // Tare command (HCI snoop confirmed — official app sends this to zero the scale)
    const QByteArray CMD_TARE = QByteArray::fromHex("A55A030D000200000071");
    // Timer commands (from HCI snoop)
    const QByteArray CMD_TIMER_START = QByteArray::fromHex("A55A03020001010020");
    const QByteArray CMD_TIMER_STOP  = QByteArray::fromHex("A55A030200010200FFD0");
    const QByteArray CMD_TIMER_RESET = QByteArray::fromHex("A55A030200010300FF81");
    // Polling / keepalive (official app sends every ~10-15s)
    const QByteArray CMD_POLL_1 = QByteArray::fromHex("A55A02080000000000");
    const QByteArray CMD_POLL_2 = QByteArray::fromHex("A55A0308000201000025");
}

TimemoreScale::TimemoreScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &TimemoreScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &TimemoreScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &TimemoreScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &TimemoreScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &TimemoreScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &TimemoreScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &TimemoreScale::onCharacteristicChanged);
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

TimemoreScale::~TimemoreScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void TimemoreScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        TIMEMORE_WARN("connectToDevice called with no transport");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    TIMEMORE_LOG(QString("Connecting to %1 (%2)")
                 .arg(device.name())
                 .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void TimemoreScale::onTransportConnected() {
    TIMEMORE_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void TimemoreScale::onTransportDisconnected() {
    TIMEMORE_LOG("Transport disconnected");
    setConnected(false);
}

void TimemoreScale::onTransportError(const QString& message) {
    TIMEMORE_WARN(QString("Transport error: %1").arg(message));
    setConnected(false);
}

void TimemoreScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    TIMEMORE_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::Generic::SERVICE) {
        TIMEMORE_LOG("Found Timemore service (FFF0)");
        m_serviceFound = true;
    }
}

void TimemoreScale::onServicesDiscoveryFinished() {
    TIMEMORE_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        TIMEMORE_WARN(QString("Timemore service %1 not found!").arg(Scale::Generic::SERVICE.toString()));
        return;
    }
    m_transport->discoverCharacteristics(Scale::Generic::SERVICE);
}

void TimemoreScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::Generic::SERVICE) return;
    if (m_characteristicsReady) {
        TIMEMORE_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    TIMEMORE_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    TIMEMORE_LOG("Scheduling notification enable in 200ms");
    QTimer::singleShot(200, this, [this]() {
        if (!m_transport || !m_characteristicsReady) return;
        TIMEMORE_LOG("Enabling notifications (200ms)");
        m_transport->enableNotifications(Scale::Generic::SERVICE, Scale::Generic::STATUS);

        // Send init sequence after notifications are enabled (official app sends twice)
        QTimer::singleShot(300, this, [this]() {
            sendInitSequence();
        });
    });
}

void TimemoreScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                            const QByteArray& value) {
    if (characteristicUuid != Scale::Generic::STATUS) return;

    // Timemore packet: header A5 5A, type at [2], weight at [8-9]
    if (value.size() < 10) return;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());

    // Verify header
    if (d[0] != 0xA5 || d[1] != 0x5A) return;

    // Weight data (type 0x01)
    if (d[2] == 0x01) {
        // Big-endian signed weight in tenths of gram
        int16_t weightRaw = static_cast<int16_t>((d[8] << 8) | d[9]);
        double weight = weightRaw / 10.0;
        setWeight(weight);
    }
}

void TimemoreScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Scale::Generic::SERVICE, Scale::Generic::CMD, cmd,
                                     ScaleBleTransport::WriteType::WithoutResponse);
}

void TimemoreScale::sendInitSequence() {
    if (!m_transport || !m_characteristicsReady) return;
    TIMEMORE_LOG("Sending init sequence (6 commands × 2 rounds)");

    // Send all 6 init commands twice with delays, using a chained timer approach
    int delay = 0;
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 6; ++i) {
            QByteArray cmd = CMD_INIT[i]; // copy for lambda capture
            QTimer::singleShot(delay, this, [this, cmd, round, i]() {
                if (!m_transport || !m_characteristicsReady) return;
                sendCommand(cmd);
            });
            delay += 150;
        }
        delay += 300; // gap between rounds
    }

    // Send initial poll after init completes
    QTimer::singleShot(delay + 200, this, [this]() {
        sendKeepAlive();
        TIMEMORE_LOG("Init sequence complete — HW tare/timer should be available");
    });
}

void TimemoreScale::sendKeepAlive() {
    // Polling pattern from official app — keeps connection alive and enables HW commands
    sendCommand(CMD_POLL_1);
    QTimer::singleShot(100, this, [this]() {
        sendCommand(CMD_POLL_2);
    });
}

void TimemoreScale::tare() {
    TIMEMORE_LOG("Sending tare command");
    sendCommand(CMD_TARE);
}

void TimemoreScale::startTimer() {
    TIMEMORE_LOG("Sending timer start command");
    sendCommand(CMD_TIMER_START);
}

void TimemoreScale::stopTimer() {
    TIMEMORE_LOG("Sending timer stop command");
    sendCommand(CMD_TIMER_STOP);
}

void TimemoreScale::resetTimer() {
    TIMEMORE_LOG("Sending timer reset command");
    sendCommand(CMD_TIMER_RESET);
}
