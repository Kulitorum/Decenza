#include "belkaportaldevice.h"
#include "bledeviceid.h"
#include "protocol/belkaportalprotocol.h"
#include "portallogging.h"
#include "models/portalsample.h"
#include <QDateTime>
#include <QLowEnergyCharacteristic>

namespace {
const QBluetoothUuid service(BelkaPortalProtocol::ServiceUuid);
const QBluetoothUuid measurement(BelkaPortalProtocol::MeasurementUuid);
const QBluetoothUuid command(BelkaPortalProtocol::CommandUuid);
}

BelkaPortalDevice::BelkaPortalDevice(TransportFactory factory, QObject* parent)
    : QObject(parent), m_transportFactory(std::move(factory))
{
    m_logClock.start();
    m_healthTimer.setInterval(1000);
    connect(&m_healthTimer, &QTimer::timeout, this, &BelkaPortalDevice::checkFreshness);
}

BelkaPortalDevice::BelkaPortalDevice(ScaleBleTransport* transport, QObject* parent)
    : BelkaPortalDevice([transport] { return transport; }, parent)
{
    ensureTransport();
}

void BelkaPortalDevice::ensureTransport()
{
    if (m_transport) return;
    auto* transport = m_transport = m_transportFactory();
    Q_ASSERT(transport);
    transport->setParent(this);
    transport->setConnectionPriorityManaged(false);
    connect(transport, &ScaleBleTransport::connected, this, [this] {
        if (!active()) return;
        setState(State::Discovering);
        m_transport->discoverServices();
    });
    connect(transport, &ScaleBleTransport::serviceDiscovered, this, [this](const QBluetoothUuid& uuid) {
        if (active() && uuid == service) m_serviceFound = true;
    });
    connect(transport, &ScaleBleTransport::servicesDiscoveryFinished, this, [this] {
        if (!active()) return;
        if (!m_serviceFound) { fail(QStringLiteral("PORTAL service 7400 not found")); return; }
        m_transport->discoverCharacteristics(service);
    });
    connect(transport, &ScaleBleTransport::characteristicDiscovered, this,
            [this](const QBluetoothUuid& svc, const QBluetoothUuid& chr, int properties) {
        if (!active() || svc != service) return;
        if (chr == measurement) m_properties = properties;
        if (chr == command) m_commandProperties = properties;
    });
    connect(transport, &ScaleBleTransport::characteristicsDiscoveryFinished, this,
            [this](const QBluetoothUuid& svc) {
        if (!active() || svc != service) return;
        if (!(m_properties & QLowEnergyCharacteristic::Notify)) {
            fail(QStringLiteral("PORTAL measurement 7410 does not support notifications"));
            return;
        }
        logEvent(QStringLiteral("ready"), QStringLiteral("Characteristics ready: measurement properties=%1, command properties=%2")
            .arg(m_properties).arg(m_commandProperties));
        m_savedAddress = m_selectedAddress;
        m_savedName = m_name;
        emit savedDeviceChanged();
        setState(State::Waiting);
        // Only notifications are measurements; an optional read adds no usable data.
        m_transport->enableNotifications(service, measurement);
    });
    connect(transport, &ScaleBleTransport::characteristicChanged, this,
            [this](const QBluetoothUuid& chr, const QByteArray& packet) { receive(chr, packet, true); });
    connect(transport, &ScaleBleTransport::characteristicRead, this,
            [this](const QBluetoothUuid& chr, const QByteArray& packet) { receive(chr, packet, false); });
    connect(transport, &ScaleBleTransport::characteristicWritten, this, [this](const QBluetoothUuid& chr) {
        if (chr == command) finishDisplayWrite(true);
    });
    connect(transport, &ScaleBleTransport::gattOperationFailed, this, [this](const QBluetoothUuid& key) {
        if (!active()) return;
        if (key == command) finishDisplayWrite(false);
        else if (key == measurement) fail(QStringLiteral("PORTAL measurement subscription failed"));
        else if (m_state == State::Discovering) fail(QStringLiteral("PORTAL discovery failed"));
    });
    connect(transport, &ScaleBleTransport::notificationsIssued, this, [this](const QBluetoothUuid& key) {
        if (key != measurement || m_state != State::Waiting) return;
        // Count from radio dispatch, not from time spent behind other GATT work.
        m_lastMeasurement.start();
        m_healthTimer.start();
    });
    connect(transport, &ScaleBleTransport::error, this, [this](const QString& error) {
        if (!active()) return;
        if (m_state == State::Connecting || m_state == State::Discovering || m_state == State::Waiting
            || !m_transport->isConnected()) {
            fail(error);
        } else {
            // Both transports also emit error after an optional operation fails.
            // The live stream and disconnected signal determine link health.
            PORTAL_WARN(error);
        }
    });
    connect(transport, &ScaleBleTransport::disconnected, this, &BelkaPortalDevice::finishDisconnect);
}

BelkaPortalDevice::~BelkaPortalDevice()
{
    flushLogs();
    if (m_transport) {
        disconnect(m_transport, nullptr, this, nullptr);
        m_transport->disconnectFromDevice();
    }
}

void BelkaPortalDevice::observeDevice(const QBluetoothDeviceInfo& info)
{
    if (!isPortal(info)) return;
    for (auto& known : m_devices) {
        if (deviceIdentifiersMatch(known, getDeviceIdentifier(info))) {
            const bool renamed = known.name() != info.name();
            known = info;
            if (renamed) emit devicesChanged();
            tryReconnect();
            return;
        }
    }
    m_devices.append(info);
    emit devicesChanged();
    tryReconnect();
}

QVariantList BelkaPortalDevice::devices() const
{
    QVariantList result;
    for (const auto& device : m_devices)
        result.append(QVariantMap{{"identifier", getDeviceIdentifier(device)},
                                  {"label", device.name().isEmpty() ? QStringLiteral("PORTAL") : device.name()}});
    return result;
}

void BelkaPortalDevice::clearDevices() { m_devices.clear(); emit devicesChanged(); }

void BelkaPortalDevice::setMachineBusy(bool busy)
{
    if (m_machineBusy == busy) return;
    m_machineBusy = busy;
    emit machineBusyChanged();
    if (busy && active() && m_state != State::Streaming && m_state != State::Stale) {
        stopConnection(false);
        m_reconnectEnabled = true;
    }
    if (!busy) { m_reconnectAttempted = false; tryReconnect(); }
}

void BelkaPortalDevice::connectDevice(const QString& identifier)
{
    if (m_machineBusy || m_extractionActive || active()
        || (m_transport && (m_transport->isConnected() || m_transport->isConnecting()
                           || m_transport->isDisconnecting()))) return;
    for (const auto& device : m_devices) {
        if (!deviceIdentifiersMatch(device, identifier)) continue;
        ensureTransport();
        m_serviceFound = false;
        m_properties = 0;
        m_commandProperties = 0;
        m_displayCommandStatus.clear();
        m_displayWrites.clear();
        m_reconnectEnabled = true;
        m_reconnectAttempted = true;
        m_error.clear();
        m_lastPacket.clear();
        m_packetCount = 0;
        m_loggedPacketShape = false;
        m_name = device.name().isEmpty() ? QStringLiteral("PORTAL") : device.name();
        if (m_selectedAddress != identifier) flushLogs();
        m_selectedAddress = identifier;
        emit displayCommandStatusChanged();
        emit readingInterrupted();
        setState(State::Connecting);
        emit readingChanged();
        logEvent(QStringLiteral("connecting"), QStringLiteral("Connecting to %1 (%2)").arg(m_name, identifier));
        m_transport->connectToDevice(device);
        return;
    }
}

void BelkaPortalDevice::disconnectDevice()
{
    if (m_machineBusy || m_extractionActive) return;
    stopConnection(true);
}

void BelkaPortalDevice::stopConnection(bool manual)
{
    if (manual) flushLogs();
    m_reconnectEnabled = false;
    m_error.clear();
    m_name.clear();
    setState(State::Disconnecting);
    clearConnectionData();
    if (m_transport) m_transport->disconnectFromDevice();
    // QtScaleBleTransport tears down and drops controller signals synchronously.
    // CoreBluetooth exposes its outstanding cancellation through isDisconnecting().
    if (!m_transport || !m_transport->isDisconnecting()) finishDisconnect();
}

void BelkaPortalDevice::clearConnectionData()
{
    m_healthTimer.stop();
    m_displayWrites.clear();
    m_commandProperties = 0;
    m_displayCommandStatus.clear();
    emit displayCommandStatusChanged();
    emit readingInterrupted();
    emit readingChanged();
}

void BelkaPortalDevice::finishDisconnect()
{
    const bool unexpected = active();
    if (unexpected) {
        flushLogs();
        m_error = QStringLiteral("PORTAL disconnected; reconnect when the machine is idle");
        PORTAL_WARN(m_error);
    }
    if (m_state != State::Error) setState(unexpected ? State::Error : State::Disconnected);
    clearConnectionData();
    logEvent(QStringLiteral("disconnected"), QStringLiteral("Disconnected"));
    tryReconnect();
}

bool BelkaPortalDevice::active() const
{
    return m_state == State::Connecting || m_state == State::Discovering
        || m_state == State::Waiting || m_state == State::Streaming || m_state == State::Stale;
}

QString BelkaPortalDevice::state() const
{
    switch (m_state) {
    case State::Disconnected: return QStringLiteral("disconnected");
    case State::Connecting: return QStringLiteral("connecting");
    case State::Discovering: return QStringLiteral("discovering");
    case State::Waiting: return QStringLiteral("waiting");
    case State::Streaming: return QStringLiteral("streaming");
    case State::Stale: return QStringLiteral("stale");
    case State::Error: return QStringLiteral("error");
    case State::Disconnecting: return QStringLiteral("disconnecting");
    }
    Q_UNREACHABLE();
}

void BelkaPortalDevice::setState(State value)
{
    if (m_state == value) return;
    m_state = value;
    emit stateChanged();
}

void BelkaPortalDevice::fail(const QString& error)
{
    if (!active()) return;
    flushLogs();
    m_error = error;
    setState(State::Error);
    clearConnectionData();
    PORTAL_WARN(error);
    m_transport->disconnectFromDevice();
    if (!m_transport->isDisconnecting()) finishDisconnect();
}

void BelkaPortalDevice::finishDisplayWrite(bool succeeded)
{
    if (m_displayWrites.isEmpty()) return;
    const auto bytes = m_displayWrites.takeFirst();
    m_displayCommandStatus = m_displayWrites.isEmpty()
        ? (succeeded ? QStringLiteral("acknowledged") : QStringLiteral("failed"))
        : QStringLiteral("requested");
    emit displayCommandStatusChanged();
    if (succeeded) {
        PORTAL_INFO(QStringLiteral("Display command %1 acknowledged; session state is not reported by PORTAL")
            .arg(QString::fromLatin1(bytes.toHex(' '))));
    } else {
        PORTAL_WARN(QStringLiteral("PORTAL display command %1 was not acknowledged")
            .arg(QString::fromLatin1(bytes.toHex(' '))));
    }
}

void BelkaPortalDevice::receive(const QBluetoothUuid& chr, const QByteArray& packet, bool notification)
{
    if (!active() || !m_serviceFound || chr != measurement || !m_properties) return;
    m_lastPacket = QString::fromLatin1(packet.toHex(' '));
    ++m_packetCount;
    if (!m_loggedPacketShape) {
        m_loggedPacketShape = true;
        logEvent(QStringLiteral("packet-shape"), QStringLiteral("First packet: source=%1, length=%2")
            .arg(notification ? QStringLiteral("notify") : QStringLiteral("read")).arg(packet.size()), QtDebugMsg);
    }
    const auto value = BelkaPortalProtocol::decodeMeasurement(QByteArrayView(packet));
    if (!value) {
        emit readingInterrupted();
        m_error = QStringLiteral("Unknown PORTAL measurement format; raw packet retained");
        logEvent(QStringLiteral("malformed"), QStringLiteral("%1: length=%2")
            .arg(m_error).arg(packet.size()), QtWarningMsg,
            QStringLiteral(", hex=%1").arg(m_lastPacket));
        if (m_state != State::Waiting) setState(State::Stale);
    } else if (notification) {
        m_ec = value->ecRaw;
        m_temperature = value->temperatureC;
        m_error.clear();
        m_lastMeasurement.start();
        m_healthTimer.start();
        if (m_state != State::Streaming) {
            logEvent(QStringLiteral("streaming"), QStringLiteral("Receiving measurements; EC and status units unverified"));
            setState(State::Streaming);
        }
        emit measurementReceived(m_ec, m_temperature);
    }
    emit readingChanged();
}

void BelkaPortalDevice::checkFreshness()
{
    if (m_lastMeasurement.isValid()) expireReading(m_lastMeasurement.elapsed());
}

void BelkaPortalDevice::expireReading(qint64 ageMs)
{
    if (m_state == State::Waiting && ageMs > PortalSamples::StaleAfterMs) {
        fail(QStringLiteral("PORTAL sent no valid measurement after notification setup"));
    } else if (hasReading() && ageMs > PortalSamples::StaleAfterMs) {
        emit readingInterrupted();
        setState(State::Stale);
        emit readingChanged();
        logEvent(QStringLiteral("stale"), QStringLiteral("No measurement for more than %1 seconds")
            .arg(PortalSamples::StaleAfterSeconds));
    }
}

bool BelkaPortalDevice::canControlDisplay() const
{
    return (m_state == State::Streaming || m_state == State::Stale) && m_transport->isConnected() && (m_commandProperties & QLowEnergyCharacteristic::Write);
}

void BelkaPortalDevice::restoreSavedDevice(const QString& address, const QString& name)
{
    if (m_savedAddress == address && m_savedName == name) return;
    const bool replaced = m_selectedAddress != address;
    if (replaced && (active() || (m_transport && m_transport->isDisconnecting()))) stopConnection(true);
    else if (replaced) flushLogs();
    m_savedAddress = address;
    m_savedName = name;
    m_reconnectEnabled = true;
    m_reconnectAttempted = false;
    emit savedDeviceChanged();
    tryReconnect();
}

void BelkaPortalDevice::beginScan()
{
    clearDevices();
    m_reconnectAttempted = false;
}

void BelkaPortalDevice::tryReconnect()
{
    if (!m_reconnectEnabled || m_reconnectAttempted || m_savedAddress.isEmpty()
        || m_machineBusy || m_extractionActive || active() || (m_transport && m_transport->isDisconnecting())) return;
    for (const auto& device : m_devices) {
        if (deviceIdentifiersMatch(device, m_savedAddress)) {
            // One attempt per discovery cycle; advertising cannot create a reconnect loop.
            m_reconnectAttempted = true;
            QMetaObject::invokeMethod(this, [this] {
                if (m_reconnectEnabled) connectDevice(m_savedAddress);
            }, Qt::QueuedConnection);
            return;
        }
    }
}

void BelkaPortalDevice::reconnect()
{
    if (m_machineBusy || m_extractionActive || active()) return;
    m_reconnectEnabled = true;
    m_reconnectAttempted = false;
    tryReconnect();
    if (!m_reconnectAttempted && !(m_transport && m_transport->isDisconnecting())) emit scanRequested();
}

void BelkaPortalDevice::forgetDevice()
{
    if (m_machineBusy || m_extractionActive) return;
    disconnectDevice();
    m_savedAddress.clear();
    m_savedName.clear();
    emit savedDeviceChanged();
}

void BelkaPortalDevice::setSyncDisplay(bool enabled)
{
    if (m_syncDisplay == enabled) return;
    m_syncDisplay = enabled;
    emit syncDisplayChanged();
}

void BelkaPortalDevice::setGraphView(bool show)
{
    if (m_machineBusy || m_extractionActive) return;
    writeGraphView(show);
}

void BelkaPortalDevice::writeGraphView(bool show)
{
    if (!canControlDisplay()) return;
    m_displayCommandStatus = QStringLiteral("requested");
    const auto bytes = BelkaPortalProtocol::graphCommand(show);
    m_displayWrites.append(bytes);
    emit displayCommandStatusChanged();
    PORTAL_INFO(QStringLiteral("Requesting display graph %1, bytes=%2")
        .arg(show ? QStringLiteral("on") : QStringLiteral("off"), QString::fromLatin1(bytes.toHex(' '))));
    m_transport->writeCharacteristic(service, command, bytes, ScaleBleTransport::WriteType::WithResponse);
}

void BelkaPortalDevice::setExtractionActive(bool active)
{
    if (active == m_extractionActive) return;
    m_extractionActive = active;
    if (active) {
        m_displayStartedForShot = m_syncDisplay && canControlDisplay();
        if (m_displayStartedForShot) writeGraphView(true);
    } else {
        if (m_displayStartedForShot) writeGraphView(false);
        m_displayStartedForShot = false;
        flushLogs();
    }
}

void BelkaPortalDevice::logEvent(const QString& key, const QString& message, QtMsgType level,
                                const QString& detail)
{
    LogCollapse::Collapsed collapsed;
    if (!m_logCollapse.shouldLog(key, message, m_logClock.elapsed(), &collapsed)) return;
    const auto line = message + detail + LogCollapse::suffix(collapsed);
    if (level == QtWarningMsg) { PORTAL_WARN(line); }
    else if (level == QtDebugMsg) { PORTAL_DEBUG(line); }
    else { PORTAL_INFO(line); }
}

void BelkaPortalDevice::flushLogs()
{
    for (const auto& entry : m_logCollapse.flushAll(m_logClock.elapsed()))
        PORTAL_INFO(QStringLiteral("%1 repeats").arg(entry.first) + LogCollapse::suffix(entry.second));
}
