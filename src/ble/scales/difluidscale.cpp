#include "difluidscale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <QTimer>

#define DIFLUID_LOG(msg)  SCALE_LOG("DifluidScale", msg)
#define DIFLUID_INFO(msg) SCALE_INFO("DifluidScale", msg)
#define DIFLUID_WARN(msg) SCALE_WARN("DifluidScale", msg)

DifluidScale::DifluidScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &DifluidScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &DifluidScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &DifluidScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &DifluidScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &DifluidScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &DifluidScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &DifluidScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }
}

DifluidScale::~DifluidScale() {
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void DifluidScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        DIFLUID_WARN("connectToDevice called with no transport");
        return;
    }

    m_name = device.name();
    resetLinkState();

    DIFLUID_LOG(QString("Connecting to %1 (%2)")
                .arg(device.name())
                .arg(device.address().toString()));

    m_transport->connectToDevice(device);
}

void DifluidScale::onTransportConnected() {
    DIFLUID_LOG(DECENZA_BLE_MSG_TRANSPORT_CONNECTED);
    m_transport->discoverServices();
}

void DifluidScale::onTransportDisconnected() {
    DIFLUID_INFO(DECENZA_BLE_MSG_TRANSPORT_DISCONNECTED);
    resetLinkState();
    setConnected(false);
}

void DifluidScale::resetLinkState() {
    // Mirrors DiFluidR2's disconnect handling. Without this, m_characteristicsReady
    // and m_service survive a drop, so sendCommand's guards pass and every
    // subsequent tare/timer write goes to a torn-down transport.
    m_service = QBluetoothUuid();
    m_characteristicsReady = false;
    m_discoveredServices.clear();
}

void DifluidScale::onTransportError(const QString& message) {
    DIFLUID_WARN(QString("Transport error: %1").arg(message));
    resetLinkState();
    setConnected(false);
}

void DifluidScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    DIFLUID_LOG(QString("Service discovered: %1").arg(uuid.toString()));
    // Microbalance (0x00EE) and Microbalance Ti (0x00DD) are the same protocol
    // on different services — accept either and remember which.
    if (uuid != Scale::DiFluid::SERVICE && uuid != Scale::DiFluid::SERVICE_TI) {
        m_discoveredServices.append(uuid.toString());
        return;
    }
    // First match wins. A device could advertise both — a vendor keeping the old
    // service for compatibility is exactly the shape of this change — and BLE
    // discovery order is not guaranteed, so "last one seen" would be a coin toss.
    if (!m_service.isNull()) {
        DIFLUID_LOG(QString("Also advertises %1; staying on %2")
                        .arg(uuid.toString(), m_service.toString()));
        return;
    }
    // Bind the ternary to a local: SCALE_LOG concatenates its argument onto a
    // prefix, so an unparenthesised ternary would bind as (prefix + cond) ? a : b.
    const QString found = uuid == Scale::DiFluid::SERVICE_TI
                              ? QStringLiteral("Found DiFluid Microbalance Ti service")
                              : QStringLiteral("Found DiFluid Microbalance service");
    DIFLUID_LOG(found);
    m_service = uuid;
}

void DifluidScale::onServicesDiscoveryFinished() {
    DIFLUID_LOG(QString("Service discovery finished, service found: %1")
                .arg(m_service.isNull() ? QStringLiteral("none") : m_service.toString()));
    if (m_service.isNull()) {
        // Name what was actually there. Two bare UUIDs and a negative give a reader
        // nothing to compare against — and this path is reachable for any device the
        // name matcher pulled in that is not a DiFluid at all.
        DIFLUID_WARN(QString("\"%1\" advertises neither DiFluid service "
                             "(%2 Microbalance / %3 Ti). Services found: %4. "
                             "This is probably not a DiFluid scale — check the scale "
                             "type in Connections.")
                         .arg(m_name,
                              Scale::DiFluid::SERVICE.toString(),
                              Scale::DiFluid::SERVICE_TI.toString(),
                              m_discoveredServices.isEmpty()
                                  ? QStringLiteral("none")
                                  : m_discoveredServices.join(QStringLiteral(", "))));
        emit errorOccurred(QString("%1 does not look like a DiFluid scale.").arg(m_name));
        return;
    }
    m_transport->discoverCharacteristics(m_service);
}

void DifluidScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    if (m_service.isNull()) {
        DIFLUID_WARN(QString("Characteristics discovered for %1 but no DiFluid service "
                             "was adopted — ignoring").arg(serviceUuid.toString()));
        return;
    }
    if (serviceUuid != m_service) {
        DIFLUID_LOG(QString("Ignoring characteristics for unrelated service %1")
                        .arg(serviceUuid.toString()));
        return;
    }
    if (m_characteristicsReady) {
        DIFLUID_LOG(DECENZA_BLE_MSG_DUPLICATE_CHARACTERISTICS);
        return;
    }

    DIFLUID_LOG("Characteristics discovered");
    m_characteristicsReady = true;
    setConnected(true);

    // de1app uses 100ms delay for Difluid
    DIFLUID_LOG("Scheduling notification enable in 100ms (de1app timing)");
    QTimer::singleShot(100, this, [this]() {
        // resetLinkState() clears m_characteristicsReady and m_service together, so
        // this one check covers a link that dropped inside the 100ms window.
        if (!m_transport || !m_characteristicsReady) return;
        DIFLUID_LOG("Enabling notifications (100ms)");
        m_transport->enableNotifications(m_service, Scale::DiFluid::CHARACTERISTIC);

        // Enable auto-notifications and set to grams
        DIFLUID_LOG("Sending enable notifications and set grams commands");
        enableNotifications();
        setToGrams();
    });
}

void DifluidScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid,
                                           const QByteArray& value) {
    if (characteristicUuid != Scale::DiFluid::CHARACTERISTIC) return;

    // Sensor data notification: DF DF 03 00 <len> <weight:4> <timer:4> … , weight
    // signed and scaled ×10 in grams. DiFluid's worked example in
    // protocolMicrobalance.md is
    //   DF DF 03 00 0D 00 00 02 F8 00 00 00 00 00 0A 27 B0 00 A9
    // where bytes 5..8 = 0x000002F8 = 760 → 76.0 g.
    //
    // This used to read `value.mid(5, 8)` — eight bytes, not the four the field
    // occupies — hex it to sixteen characters and parse that with toUInt(), which
    // overflows and returns ok=false for any non-zero weight. So setWeight() was
    // reached only when the weight was exactly zero, and then it was parsing the
    // timer bytes. The comment already said "bytes 5-8" (four bytes); the code
    // took a character count for a byte count.
    //
    // One onCharacteristicChanged delivery is not guaranteed to hold exactly one
    // 19-byte frame: the sibling DiFluidR2 driver — same ScaleBleTransport, same
    // vendor DF-DF-framed protocol — was confirmed via a captured log to have two
    // back-to-back notifications coalesced by the BLE stack into a single
    // delivery (see difluidr2.cpp's handlePacket). The single-shot version here
    // only ever looked at the first 19 bytes, so a second weight sample riding
    // along in the same delivery was silently discarded past byte 18 with no log
    // line at all. Walk the delivery one 19-byte sensor frame at a time instead.
    qsizetype offset = 0;
    while (offset < value.size()) {
        const QByteArray remaining = value.mid(offset);

        // Sensor frames only. This characteristic also carries the device's echoes of our
        // own settings writes (Func 1), which are 6-7 bytes — warning about those would
        // describe healthy traffic as malformed, and at 5Hz a mismatched format would
        // evict the whole 1000-entry scale log, destroying the artifact a diagnostician
        // asks for. Anything that is not a sensor frame is logged quietly and dropped.
        if (!remaining.startsWith(QByteArray::fromHex("dfdf0300"))) {
            DIFLUID_LOG(QString("Non-sensor notification: %1").arg(QString(remaining.toHex(' '))));
            return;
        }
        if (remaining.size() < 19) {
            DIFLUID_WARN(QString("Sensor frame too short (%1 bytes, need 19): %2")
                             .arg(remaining.size()).arg(QString(remaining.toHex(' '))));
            return;
        }

        // Signed: the field goes negative after a tare drift, and reading it unsigned
        // turned −0.5 g into 4294967291, which the range gate below then discarded —
        // freezing the displayed weight at its last non-negative value.
        const qint32 weightRaw = static_cast<qint32>(
              (static_cast<quint32>(static_cast<quint8>(remaining[5])) << 24)
            | (static_cast<quint32>(static_cast<quint8>(remaining[6])) << 16)
            | (static_cast<quint32>(static_cast<quint8>(remaining[7])) << 8)
            |  static_cast<quint32>(static_cast<quint8>(remaining[8])));

        if (weightRaw <= -20000 || weightRaw >= 20000) {
            DIFLUID_WARN(QString("Weight out of range: raw=%1 (%2 g) — ignoring")
                             .arg(weightRaw).arg(weightRaw / 10.0));
        } else {
            setWeight(weightRaw / 10.0);
        }

        offset += 19;
    }
}

void DifluidScale::sendCommand(const QByteArray& cmd) {
    // Every caller here is either a user action (tare, the timer buttons, and the
    // MCP tools behind them) or a connect-handshake step. Dropping one silently
    // means the MCP layer answers "Scale tared" for a write that never happened.
    if (!m_transport || !m_characteristicsReady || m_service.isNull()) {
        DIFLUID_WARN(QString("Dropping command %1 — transport=%2 characteristicsReady=%3 service=%4")
                         .arg(QString(cmd.toHex(' ')),
                              m_transport ? QStringLiteral("yes") : QStringLiteral("no"),
                              m_characteristicsReady ? QStringLiteral("yes") : QStringLiteral("no"),
                              m_service.isNull() ? QStringLiteral("none") : m_service.toString()));
        return;
    }
    m_transport->writeCharacteristic(m_service, Scale::DiFluid::CHARACTERISTIC, cmd);
}

void DifluidScale::sendKeepAlive() {
    // No keep-alive needed — notifications stay active without periodic CCCD re-writes.
    // Re-writing the CCCD risks AuthorizationError disconnects.
}

void DifluidScale::enableNotifications() {
    // Enable auto-notifications message
    sendCommand(QByteArray::fromHex("DFDF01000101C1"));
}

void DifluidScale::setToGrams() {
    // Set unit to grams
    sendCommand(QByteArray::fromHex("DFDF01040100C4"));
}

void DifluidScale::tare() {
    sendCommand(QByteArray::fromHex("DFDF03020101C5"));
}

void DifluidScale::startTimer() {
    sendCommand(QByteArray::fromHex("DFDF03020100C4"));
}

void DifluidScale::stopTimer() {
    sendCommand(QByteArray::fromHex("DFDF03010100C3"));
}

void DifluidScale::resetTimer() {
    sendCommand(QByteArray::fromHex("DFDF03020100C4"));
}
