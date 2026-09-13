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

BelkaPortalDevice::BelkaPortalDevice(ScaleBleTransport* transport, QObject* parent)
    : QObject(parent), m_transport(transport)
{
    Q_ASSERT(transport);
    m_logClock.start();
    transport->setParent(this);
    transport->setConnectionPriorityManaged(false);
    connect(transport, &ScaleBleTransport::connected, this, [this] {
        if (!m_requested) return;
        setState(QStringLiteral("discovering"));
        m_transport->discoverServices();
    });
    connect(transport, &ScaleBleTransport::serviceDiscovered, this, [this](const QBluetoothUuid& uuid) {
        if (m_requested && uuid == service) m_serviceFound = true;
    });
    connect(transport, &ScaleBleTransport::servicesDiscoveryFinished, this, [this] {
        if (!m_requested) return;
        if (!m_serviceFound) { fail(QStringLiteral("PORTAL service 7400 not found")); return; }
        m_transport->discoverCharacteristics(service);
    });
    connect(transport, &ScaleBleTransport::characteristicDiscovered, this,
            [this](const QBluetoothUuid& svc, const QBluetoothUuid& chr, int properties) {
        if (!m_requested || svc != service) return;
        if (chr == measurement) m_properties = properties;
        if (chr == command) m_commandProperties = properties;
    });
    connect(transport, &ScaleBleTransport::characteristicsDiscoveryFinished, this,
            [this](const QBluetoothUuid& svc) {
        if (!m_requested || svc != service) return;
        if (!(m_properties & QLowEnergyCharacteristic::Notify)) {
            fail(QStringLiteral("PORTAL measurement 7410 does not support notifications"));
            return;
        }
        logEvent(QStringLiteral("ready"), QStringLiteral("Characteristics ready: measurement properties=%1, command properties=%2")
            .arg(m_properties).arg(m_commandProperties));
        m_savedAddress = m_selectedAddress;
        m_savedName = m_name;
        emit savedDeviceChanged();
        setState(QStringLiteral("waiting"));
        // Both requests pass through the existing shared GATT queue.
        if (m_properties & QLowEnergyCharacteristic::Read)
            m_transport->readCharacteristic(service, measurement);
        m_transport->enableNotifications(service, measurement);
    });
    connect(transport, &ScaleBleTransport::characteristicChanged, this,
            [this](const QBluetoothUuid& chr, const QByteArray& packet) { receive(chr, packet, true); });
    connect(transport, &ScaleBleTransport::characteristicRead, this,
            [this](const QBluetoothUuid& chr, const QByteArray& packet) { receive(chr, packet, false); });
    connect(transport, &ScaleBleTransport::characteristicWritten, this, [this](const QBluetoothUuid& chr) {
        if (chr != command || !m_displayWritesPending) return;
        if (--m_displayWritesPending > 0) return;
        m_displayCommandStatus = QStringLiteral("acknowledged");
        emit displayCommandStatusChanged();
        PORTAL_INFO(QStringLiteral("Display command write acknowledged; session state is not reported by PORTAL"));
    });
    connect(transport, &ScaleBleTransport::gattOperationFailed, this, [this](const QBluetoothUuid& key) {
        if (key != command || !m_displayWritesPending) return;
        m_displayWritesPending = 0;
        m_displayCommandStatus = QStringLiteral("failed");
        emit displayCommandStatusChanged();
        logEvent(QStringLiteral("display-failed"), QStringLiteral("PORTAL display command was not acknowledged"), QtWarningMsg);
    });
    connect(transport, &ScaleBleTransport::error, this, &BelkaPortalDevice::fail);
    connect(transport, &ScaleBleTransport::disconnected, this, [this] {
        const bool unexpected = m_requested;
        m_waitingForDisconnect = false;
        m_requested = false;
        m_valid = false;
        m_healthTimer.stop();
        m_displayWritesPending = 0;
        m_commandProperties = 0;
        m_displayCommandStatus.clear();
        emit displayCommandStatusChanged();
        emit readingInterrupted();
        if (unexpected) m_error = QStringLiteral("PORTAL disconnected; reconnect when the machine is idle");
        if (m_state != QStringLiteral("error")) setState(QStringLiteral("disconnected"));
        emit readingChanged();
        logEvent(QStringLiteral("disconnected"), QStringLiteral("Disconnected"));
        tryReconnect();
    });
    // Periodic freshness inspection, not a reconnect or GATT polling loop.
    m_healthTimer.setInterval(1000);
    connect(&m_healthTimer, &QTimer::timeout, this, &BelkaPortalDevice::checkFreshness);
}

BelkaPortalDevice::~BelkaPortalDevice()
{
    flushLogs();
    disconnect(m_transport, nullptr, this, nullptr);
    m_transport->disconnectFromDevice();
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
    if (busy && m_requested && m_state != QStringLiteral("streaming") && m_state != QStringLiteral("stale")) {
        stopConnection(false);
        m_reconnectEnabled = true;
    }
    if (!busy) { m_reconnectAttempted = false; tryReconnect(); }
}

void BelkaPortalDevice::connectDevice(const QString& identifier)
{
    if (m_machineBusy || m_extractionActive || m_requested || m_transport->isConnected() || m_transport->isConnecting()
        || m_waitingForDisconnect) return;
    for (const auto& device : m_devices) {
        if (!deviceIdentifiersMatch(device, identifier)) continue;
        m_requested = true;
        m_serviceFound = false;
        m_properties = 0;
        m_commandProperties = 0;
        m_displayCommandStatus.clear();
        m_displayWritesPending = 0;
        m_reconnectEnabled = true;
        m_reconnectAttempted = true;
        m_valid = false;
        m_error.clear();
        m_lastPacket.clear();
        m_packetCount = 0;
        m_loggedPacketShape = false;
        m_name = device.name().isEmpty() ? QStringLiteral("PORTAL") : device.name();
        if (m_selectedAddress != identifier) flushLogs();
        m_selectedAddress = identifier;
        emit displayCommandStatusChanged();
        emit readingInterrupted();
        setState(QStringLiteral("connecting"));
        emit readingChanged();
        logEvent(QStringLiteral("connecting"), QStringLiteral("Connecting to %1 (%2)").arg(m_name, identifier));
        m_transport->connectToDevice(device);
        return;
    }
}

void BelkaPortalDevice::disconnectDevice()
{
    stopConnection(true);
}

void BelkaPortalDevice::stopConnection(bool manual)
{
    if (manual) flushLogs();
    m_reconnectEnabled = false;
    m_displayWritesPending = 0;
    emit readingInterrupted();
    m_requested = false;
    m_valid = false;
    m_error.clear();
    m_name.clear();
    m_healthTimer.stop();
    // CoreBluetooth clears isConnected before delivering its disconnect callback.
    // Keep the outstanding lifecycle explicit so reconnect cannot overtake that callback.
    m_waitingForDisconnect = m_waitingForDisconnect || m_transport->isConnected() || m_transport->isConnecting();
    setState(m_waitingForDisconnect ? QStringLiteral("disconnecting") : QStringLiteral("disconnected"));
    m_transport->disconnectFromDevice();
    emit readingChanged();
}

void BelkaPortalDevice::setState(const QString& value)
{
    m_state = value;
    emit stateChanged();
}

void BelkaPortalDevice::fail(const QString& error)
{
    if (!m_requested) return;
    m_requested = false;
    m_valid = false;
    m_error = error;
    m_healthTimer.stop();
    setState(QStringLiteral("error"));
    emit readingChanged();
    emit readingInterrupted();
    logEvent(QStringLiteral("error"), error, QtWarningMsg);
    m_waitingForDisconnect = m_waitingForDisconnect || m_transport->isConnected() || m_transport->isConnecting();
    m_transport->disconnectFromDevice();
}

void BelkaPortalDevice::receive(const QBluetoothUuid& chr, const QByteArray& packet, bool notification)
{
    if (!m_requested || !m_serviceFound || chr != measurement || !m_properties) return;
    m_lastPacket = QString::fromLatin1(packet.toHex(' '));
    ++m_packetCount;
    if (!m_loggedPacketShape) {
        m_loggedPacketShape = true;
        logEvent(QStringLiteral("packet-shape"), QStringLiteral("First packet: source=%1, length=%2")
            .arg(notification ? QStringLiteral("notify") : QStringLiteral("read")).arg(packet.size()), QtDebugMsg);
    }
    const auto value = BelkaPortalProtocol::decodeMeasurement(QByteArrayView(packet));
    if (!value) {
        m_valid = false;
        emit readingInterrupted();
        m_error = QStringLiteral("Unknown PORTAL measurement format; raw packet retained");
        logEvent(QStringLiteral("malformed"), m_error, QtWarningMsg);
        setState(QStringLiteral("waiting"));
    } else if (notification) {
        m_ec = value->ecRaw;
        m_temperature = value->temperatureC;
        m_valid = true;
        m_error.clear();
        m_lastMeasurement.start();
        m_healthTimer.start();
        if (m_state != QStringLiteral("streaming")) {
            logEvent(QStringLiteral("streaming"), QStringLiteral("Receiving measurements; EC and status units unverified"));
            setState(QStringLiteral("streaming"));
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
    if (m_valid && ageMs > PortalSamples::StaleAfterMs) {
        m_valid = false;
        emit readingInterrupted();
        setState(QStringLiteral("stale"));
        emit readingChanged();
        logEvent(QStringLiteral("stale"), QStringLiteral("No measurement for more than %1 seconds")
            .arg(PortalSamples::StaleAfterSeconds));
    }
}

bool BelkaPortalDevice::canControlDisplay() const
{
    return m_requested && m_transport->isConnected() && (m_commandProperties & QLowEnergyCharacteristic::Write);
}

void BelkaPortalDevice::restoreSavedDevice(const QString& address, const QString& name)
{
    if (m_savedAddress == address && m_savedName == name) return;
    const bool replaced = m_selectedAddress != address;
    if (replaced && (m_requested || m_waitingForDisconnect)) stopConnection(true);
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
        || m_machineBusy || m_extractionActive || m_requested || m_waitingForDisconnect) return;
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
    if (m_machineBusy || m_extractionActive || m_requested) return;
    m_reconnectEnabled = true;
    m_reconnectAttempted = false;
    tryReconnect();
    if (!m_reconnectAttempted && !m_waitingForDisconnect) emit scanRequested();
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
    ++m_displayWritesPending;
    emit displayCommandStatusChanged();
    const auto bytes = BelkaPortalProtocol::graphCommand(show);
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
    }
}

void BelkaPortalDevice::logEvent(const QString& key, const QString& message, QtMsgType level)
{
    LogCollapse::Collapsed collapsed;
    if (!m_logCollapse.shouldLog(key, message, m_logClock.elapsed(), &collapsed)) return;
    const auto line = message + LogCollapse::suffix(collapsed);
    if (level == QtWarningMsg) { PORTAL_WARN(line); }
    else if (level == QtDebugMsg) { PORTAL_DEBUG(line); }
    else { PORTAL_INFO(line); }
}

void BelkaPortalDevice::flushLogs()
{
    for (const auto& entry : m_logCollapse.flushAll(m_logClock.elapsed()))
        PORTAL_INFO(QStringLiteral("%1 repeats").arg(entry.first) + LogCollapse::suffix(entry.second));
}
