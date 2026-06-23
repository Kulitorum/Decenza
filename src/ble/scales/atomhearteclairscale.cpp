#include "atomhearteclairscale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define ECLAIR_LOG(msg)  SCALE_LOG("AtomheartEclairScale", msg)
#define ECLAIR_WARN(msg) SCALE_WARN("AtomheartEclairScale", msg)

AtomheartEclairScale::AtomheartEclairScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &AtomheartEclairScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &AtomheartEclairScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &AtomheartEclairScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &AtomheartEclairScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &AtomheartEclairScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &AtomheartEclairScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &AtomheartEclairScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }

    // Watchdog fires if no weight updates arrive after enabling notifications
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setSingleShot(true);
    connect(m_watchdogTimer, &QTimer::timeout, this, &AtomheartEclairScale::onWatchdogTimeout);

    // Tickle timer fires if weight updates stop arriving once streaming
    m_tickleTimer = new QTimer(this);
    m_tickleTimer->setSingleShot(true);
    connect(m_tickleTimer, &QTimer::timeout, this, &AtomheartEclairScale::onTickleTimeout);
}

AtomheartEclairScale::~AtomheartEclairScale() {
    stopWatchdog();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void AtomheartEclairScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        ECLAIR_WARN("connectToDevice called with no transport");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    ECLAIR_LOG(QString("Connecting to %1 (%2)")
               .arg(device.name())
               .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void AtomheartEclairScale::onTransportConnected() {
    ECLAIR_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void AtomheartEclairScale::onTransportDisconnected() {
    ECLAIR_LOG("Transport disconnected");
    stopWatchdog();
    setConnected(false);
}

void AtomheartEclairScale::onTransportError(const QString& message) {
    ECLAIR_WARN(QString("Transport error: %1").arg(message));
    stopWatchdog();
    setConnected(false);
}

void AtomheartEclairScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    ECLAIR_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    if (uuid == Scale::AtomheartEclair::SERVICE) {
        ECLAIR_LOG("Found Atomheart Eclair service");
        m_serviceFound = true;
    }
}

void AtomheartEclairScale::onServicesDiscoveryFinished() {
    ECLAIR_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));
    if (!m_serviceFound) {
        ECLAIR_WARN(QString("Atomheart Eclair service %1 not found!").arg(Scale::AtomheartEclair::SERVICE.toString()));
        return;
    }
    m_transport->discoverCharacteristics(Scale::AtomheartEclair::SERVICE);
}

void AtomheartEclairScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::AtomheartEclair::SERVICE) return;
    if (m_characteristicsReady) {
        ECLAIR_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    ECLAIR_LOG("Characteristics discovered");
    m_characteristicsReady = true;

    // Enable notifications after a 200ms settle (de1app timing), then start the
    // watchdog. We deliberately do NOT call setConnected(true) here — connected
    // state is deferred until the first weight frame actually arrives (see
    // tickleWatchdog), so a scale that never streams weight is reported as not
    // connected rather than appearing connected-but-stuck-at-0g.
    ECLAIR_LOG("Scheduling notification enable in 200ms (de1app timing)");
    QTimer::singleShot(200, this, [this]() {
        if (!m_transport || !m_characteristicsReady) {
            ECLAIR_WARN("Transport gone before notification enable");
            return;
        }
        enableNotifications();
        startWatchdog();
    });
}

bool AtomheartEclairScale::validateXor(const QByteArray& data) {
    if (data.size() < 2) return false;

    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.constData());

    // XOR all bytes except header and last (XOR) byte
    uint8_t xorResult = 0;
    for (int i = 1; i < data.size() - 1; i++) {
        xorResult ^= d[i];
    }

    return xorResult == d[data.size() - 1];
}

void AtomheartEclairScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                                    const QByteArray& value) {
    if (characteristicUuid == Scale::AtomheartEclair::STATUS) {
        // Atomheart Eclair format: 'W' (0x57) header, 4-byte weight in milligrams, 4-byte timer, XOR byte
        if (value.size() >= 9) {
            const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());

            // Check header is 'W' (0x57)
            if (d[0] != 0x57) return;

            // Validate XOR checksum
            if (!validateXor(value)) {
                ECLAIR_WARN("XOR checksum failed");
                return;
            }

            // Valid weight frame — feed the watchdog (also flips connected on first frame)
            tickleWatchdog();

            // Weight is 4-byte signed int32 in milligrams (little-endian)
            int32_t weightMg = d[1] | (d[2] << 8) | (d[3] << 16) | (d[4] << 24);
            double weight = weightMg / 1000.0;  // Convert to grams

            setWeight(weight);
        }
    }
}

void AtomheartEclairScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) return;
    m_transport->writeCharacteristic(Scale::AtomheartEclair::SERVICE, Scale::AtomheartEclair::CMD, cmd);
}

void AtomheartEclairScale::enableNotifications() {
    if (!m_transport || !m_characteristicsReady) {
        ECLAIR_WARN("enableNotifications() - transport or characteristics not ready");
        return;
    }
    ECLAIR_LOG("Enabling weight notifications on STATUS characteristic");
    m_transport->enableNotifications(Scale::AtomheartEclair::SERVICE, Scale::AtomheartEclair::STATUS);
}

void AtomheartEclairScale::startWatchdog() {
    m_watchdogRetries = 0;
    m_updatesReceived = false;
    m_watchdogTimer->start(WATCHDOG_TIMEOUT_MS);
    ECLAIR_LOG(QString("Started watchdog, waiting for first weight update (timeout: %1ms)").arg(WATCHDOG_TIMEOUT_MS));
}

void AtomheartEclairScale::tickleWatchdog() {
    // First valid weight frame — the scale is proven functional, so report connected now.
    if (!m_updatesReceived) {
        m_updatesReceived = true;
        m_watchdogTimer->stop();
        ECLAIR_LOG("First weight update received, reporting connected");
        if (!isConnected()) {
            setConnected(true);
        }
    }

    // Restart the no-update timeout; if weight stops streaming we re-enable notifications.
    m_tickleTimer->start(TICKLE_TIMEOUT_MS);
}

void AtomheartEclairScale::stopWatchdog() {
    if (m_watchdogTimer) m_watchdogTimer->stop();
    if (m_tickleTimer) m_tickleTimer->stop();
    m_updatesReceived = false;
    m_watchdogRetries = 0;
}

void AtomheartEclairScale::onWatchdogTimeout() {
    if (m_updatesReceived) return;  // Data started arriving, nothing to do

    m_watchdogRetries++;
    if (m_watchdogRetries >= MAX_WATCHDOG_RETRIES) {
        ECLAIR_WARN(QString("No weight updates after %1 retries, reporting disconnected").arg(MAX_WATCHDOG_RETRIES));
        setConnected(false);
        return;
    }

    ECLAIR_WARN(QString("No weight updates, re-enabling notifications (retry %1 of %2)")
                    .arg(m_watchdogRetries).arg(MAX_WATCHDOG_RETRIES));
    enableNotifications();
    m_watchdogTimer->start(WATCHDOG_TIMEOUT_MS);
}

void AtomheartEclairScale::onTickleTimeout() {
    // Weight was streaming and then stopped — try re-enabling notifications once more.
    ECLAIR_WARN(QString("No weight updates for %1ms, re-enabling notifications").arg(TICKLE_TIMEOUT_MS));
    m_updatesReceived = false;
    m_watchdogRetries = 0;
    enableNotifications();
    m_watchdogTimer->start(WATCHDOG_TIMEOUT_MS);
}

void AtomheartEclairScale::sendKeepAlive() {
    // No keep-alive needed — notifications stay active without periodic CCCD re-writes.
    // Re-writing the CCCD risks AuthorizationError disconnects, so the watchdog only
    // re-enables on missing data (onWatchdogTimeout/onTickleTimeout), never on a timer
    // while weight is streaming normally.
}

void AtomheartEclairScale::tare() {
    sendCommand(QByteArray::fromHex("540101"));
}

void AtomheartEclairScale::startTimer() {
    sendCommand(QByteArray::fromHex("530101"));
}

void AtomheartEclairScale::stopTimer() {
    sendCommand(QByteArray::fromHex("450101"));
}

void AtomheartEclairScale::resetTimer() {
    // de1app PR #349 gives the Eclair a dedicated timer-reset opcode, distinct from tare
    sendCommand(QByteArray::fromHex("520101"));
}
