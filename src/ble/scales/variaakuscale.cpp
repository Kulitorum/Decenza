#include "variaakuscale.h"
#include "../protocol/de1characteristics.h"
#include <QDebug>

// Helper macro that logs to both qDebug and emits signal for UI/file logging
#define VARIA_LOG(msg) do { \
    QString _msg = QString("[BLE VariaAkuScale] ") + msg; \
    qDebug().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

#define VARIA_WARN(msg) do { \
    QString _msg = QString("[BLE VariaAkuScale] ") + msg; \
    qWarning().noquote() << _msg; \
    emit logMessage(_msg); \
} while(0)

VariaAkuScale::VariaAkuScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &VariaAkuScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &VariaAkuScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &VariaAkuScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &VariaAkuScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &VariaAkuScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &VariaAkuScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &VariaAkuScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }

    // Create watchdog timer (fires if no updates received after enable)
    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setSingleShot(true);
    connect(m_watchdogTimer, &QTimer::timeout, this, &VariaAkuScale::onWatchdogTimeout);

    // Create tickle timer (fires if updates stop arriving)
    m_tickleTimer = new QTimer(this);
    m_tickleTimer->setSingleShot(true);
    connect(m_tickleTimer, &QTimer::timeout, this, &VariaAkuScale::onTickleTimeout);
}

VariaAkuScale::~VariaAkuScale() {
    stopWatchdog();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void VariaAkuScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        emit errorOccurred("No transport available");
        return;
    }

    m_name = device.name();
    m_serviceFound = false;
    m_characteristicsReady = false;

    VARIA_LOG(QString("Connecting to %1 (%2)")
              .arg(device.name())
              .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void VariaAkuScale::onTransportConnected() {
    VARIA_LOG("Transport connected, starting service discovery");
    m_transport->discoverServices();
}

void VariaAkuScale::onTransportDisconnected() {
    VARIA_WARN("Transport disconnected - BLE connection lost!");
    stopWatchdog();
    setConnected(false);
}

void VariaAkuScale::onTransportError(const QString& message) {
    VARIA_WARN(QString("Transport error: %1").arg(message));
    emit errorOccurred("Varia Aku scale connection error");
    setConnected(false);
}

void VariaAkuScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    VARIA_LOG(QString("Service discovered: %1").arg(uuid.toString()));

    if (uuid == Scale::VariaAku::SERVICE) {
        VARIA_LOG("Found Varia service (FFF0)");
        m_serviceFound = true;
    }
}

void VariaAkuScale::onServicesDiscoveryFinished() {
    VARIA_LOG(QString("Service discovery finished, service found: %1").arg(m_serviceFound));

    if (!m_serviceFound) {
        VARIA_WARN(QString("Varia Aku service %1 not found!").arg(Scale::VariaAku::SERVICE.toString()));
        emit errorOccurred("Varia Aku service not found");
        return;
    }

    m_transport->discoverCharacteristics(Scale::VariaAku::SERVICE);
}

void VariaAkuScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (serviceUuid != Scale::VariaAku::SERVICE) return;
    if (m_characteristicsReady) {
        VARIA_LOG("Characteristics already set up, ignoring duplicate callback");
        return;
    }

    VARIA_LOG("Characteristics discovered");

    m_characteristicsReady = true;

    // Delay notification enable by 200ms (matching de1app pattern)
    // The Varia Aku scale needs time to stabilize after service discovery
    VARIA_LOG("Scheduling notification enable in 200ms...");
    QTimer::singleShot(200, this, [this]() {
        // Check if still connected (scale may have disconnected during delay)
        if (!m_transport || !m_characteristicsReady) {
            VARIA_WARN("Transport gone before notification enable!");
            return;
        }

        VARIA_LOG("200ms delay complete, enabling notifications...");

        // Enable notifications and start watchdog
        enableNotifications();
        startWatchdog();

        // Don't call setConnected(true) here — wait until the first weight
        // update actually arrives (in tickleWatchdog). This prevents the
        // scale from appearing "connected" when notifications never work,
        // which would fool the shot-abort-no-scale safety check.
        VARIA_LOG("Notifications enabled, waiting for first weight data before reporting connected");
    });
}

void VariaAkuScale::enableNotifications() {
    if (!m_transport || !m_characteristicsReady) {
        VARIA_WARN("enableNotifications() - transport or characteristics not ready!");
        return;
    }

    VARIA_LOG("Enabling notifications on STATUS characteristic");
    m_transport->enableNotifications(Scale::VariaAku::SERVICE, Scale::VariaAku::STATUS);
}

void VariaAkuScale::startWatchdog() {
    m_watchdogRetries = 0;
    m_updatesReceived = false;
    m_watchdogTimer->start(WATCHDOG_TIMEOUT_MS);
    VARIA_LOG(QString("Started watchdog, waiting for first weight update (timeout: %1ms)").arg(WATCHDOG_TIMEOUT_MS));
}

void VariaAkuScale::tickleWatchdog() {
    // First update received - watchdog succeeded
    if (!m_updatesReceived) {
        m_updatesReceived = true;
        m_watchdogTimer->stop();
        VARIA_LOG("First weight update received! Watchdog stopped.");

        // NOW we can report connected — we have proof the scale is functional
        if (!isConnected()) {
            setConnected(true);
            VARIA_LOG("Scale confirmed working, reporting connected");
        }
    }

    // Reset the tickle timer - if no updates for TICKLE_TIMEOUT_MS, log warning
    m_tickleTimer->start(TICKLE_TIMEOUT_MS);
}

void VariaAkuScale::stopWatchdog() {
    m_watchdogTimer->stop();
    m_tickleTimer->stop();
    m_updatesReceived = false;
    m_watchdogRetries = 0;
}

void VariaAkuScale::onWatchdogTimeout() {
    if (m_updatesReceived) {
        return;  // Updates started arriving, no need to retry
    }

    m_watchdogRetries++;

    if (m_watchdogRetries >= MAX_WATCHDOG_RETRIES) {
        VARIA_WARN(QString("No weight updates after %1 retries, giving up").arg(MAX_WATCHDOG_RETRIES));
        setConnected(false);
        emit errorOccurred("Varia Aku scale not sending weight updates");
        return;
    }

    VARIA_WARN(QString("No weight updates, retry %1 of %2").arg(m_watchdogRetries).arg(MAX_WATCHDOG_RETRIES));

    // Re-enable notifications
    enableNotifications();

    // Schedule next retry
    m_watchdogTimer->start(WATCHDOG_TIMEOUT_MS);
}

void VariaAkuScale::onTickleTimeout() {
    VARIA_WARN(QString("No weight updates for %1ms! Re-enabling notifications...").arg(TICKLE_TIMEOUT_MS));

    // Try re-enabling notifications
    m_updatesReceived = false;
    m_watchdogRetries = 0;
    enableNotifications();
    m_watchdogTimer->start(WATCHDOG_TIMEOUT_MS);
}

void VariaAkuScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value) {
    if (characteristicUuid == Scale::VariaAku::STATUS) {
        // Varia Aku format: header command length payload xor
        // Weight notification: command 0x01, length 0x03, payload w1 w2 w3 xor
        if (value.size() >= 4) {
            const uint8_t* d = reinterpret_cast<const uint8_t*>(value.constData());

            uint8_t command = d[1];
            uint8_t length = d[2];

            // Weight notification
            if (command == 0x01 && length == 0x03 && value.size() >= 7) {
                // Tickle watchdog on every weight update
                tickleWatchdog();

                uint8_t w1 = d[3];
                uint8_t w2 = d[4];
                uint8_t w3 = d[5];

                // Sign is in highest nibble of w1 (0x10 means negative)
                bool isNegative = (w1 & 0x10) != 0;

                // Weight is 3 bytes big-endian in hundredths of gram
                // Strip sign nibble from w1
                uint32_t weightRaw = ((w1 & 0x0F) << 16) | (w2 << 8) | w3;
                double weight = weightRaw / 100.0;

                if (isNegative) {
                    weight = -weight;
                }

                setWeight(weight);
            }
            // Battery notification
            else if (command == 0x85 && length == 0x01 && value.size() >= 5) {
                uint8_t battery = d[3];
                VARIA_LOG(QString("Battery update: %1%").arg(battery));
                setBatteryLevel(battery);
            }
        }
    }
}

void VariaAkuScale::sendCommand(const QByteArray& cmd) {
    if (!m_transport || !m_characteristicsReady) {
        VARIA_WARN("Cannot send command - transport or characteristics not ready");
        return;
    }
    m_transport->writeCharacteristic(Scale::VariaAku::SERVICE, Scale::VariaAku::CMD, cmd);
}

void VariaAkuScale::sendKeepAlive() {
    if (m_transport && m_characteristicsReady)
        m_transport->enableNotifications(Scale::VariaAku::SERVICE, Scale::VariaAku::STATUS);
}

void VariaAkuScale::tare() {
    VARIA_LOG("Sending tare command");
    sendCommand(QByteArray::fromHex("FA82010182"));
}
