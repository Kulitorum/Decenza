#include "usb/usbscalemanager.h"
#include "usb/usbdecentscale.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbscalehelper.h"
#endif

#include <QDebug>

UsbScaleManager::UsbScaleManager(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(POLL_INTERVAL_MS);
    connect(&m_pollTimer, &QTimer::timeout, this, &UsbScaleManager::onPollTimerTick);
}

UsbScaleManager::~UsbScaleManager()
{
    stopPolling();
#ifdef Q_OS_ANDROID
    cleanupAndroidProbe(true);
#else
    cleanupProbe();
#endif
}

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------

bool UsbScaleManager::isScaleConnected() const
{
    return m_scale != nullptr;
}

void UsbScaleManager::setScaleAvailable(bool available)
{
    if (m_scaleAvailable == available) return;
    m_scaleAvailable = available;
    if (available) {
        emit usbScaleAvailable();
    } else {
        emit usbScaleUnavailable();
    }
}

// ---------------------------------------------------------------------------
// Connect on demand (selection / saved-primary auto-reconnect)
// ---------------------------------------------------------------------------

void UsbScaleManager::connectToScale()
{
    if (m_scale) return;            // Already connected
    if (!m_scaleAvailable) {
        qDebug() << "[USB Scale] connectToScale() called but no scale available";
        return;
    }

#ifdef Q_OS_ANDROID
    // Re-validate presence; the JNI side still owns the (probe-opened) connection.
    if (!AndroidUsbScaleHelper::hasDevice()) {
        qWarning() << "[USB Scale] connectToScale(): device no longer present";
        setScaleAvailable(false);
        return;
    }
    if (!AndroidUsbScaleHelper::hasPermission()) {
        qWarning() << "[USB Scale] connectToScale(): no USB permission";
        return;
    }

    qDebug() << "[USB Scale] Connecting (Android)";
    emit logMessage(QStringLiteral("[USB Scale] Connecting"));

    m_scale = new UsbDecentScale(this);
    m_scale->open();
#else
    if (m_confirmedPortName.isEmpty()) {
        qWarning() << "[USB Scale] connectToScale(): no confirmed port";
        return;
    }

    qDebug() << "[USB Scale] Connecting on" << m_confirmedPortName;
    emit logMessage(QStringLiteral("[USB Scale] Connecting on %1").arg(m_confirmedPortName));

    m_scale = new UsbDecentScale(this);
    m_scale->open(m_confirmedPortName);
#endif

    // open() can fail (port grabbed by another app, JNI open refused). If it did,
    // the scale never connected — don't emit scaleDiscovered (that would wire a
    // dead object and, on desktop, leave m_confirmedPortName/m_knownPorts pinned
    // so polling never re-probes the still-plugged-in scale). Instead, tear the
    // half-open scale down and re-enable recovery so the next poll re-confirms it.
    if (!m_scale->isConnected()) {
        qWarning() << "[USB Scale] open() failed — scale did not connect";
        emit logMessage(QStringLiteral("[USB Scale] Connect failed — retrying discovery"));
        m_scale->deleteLater();
        m_scale = nullptr;
#ifdef Q_OS_ANDROID
        // Release the JNI connection so the next poll re-probes from scratch.
        AndroidUsbScaleHelper::close();
#else
        // Drop the confirmed port from the known set so onPollTimerTickDesktop's
        // candidate filter (!m_knownPorts.contains(port)) re-probes it next tick.
        m_knownPorts.remove(m_confirmedPortName);
        m_confirmedPortName.clear();
#endif
        setScaleAvailable(false);
        return;
    }

#ifdef Q_OS_ANDROID
    // Recover from a stale-but-enumerated connection: on Android the device can
    // stay plugged in while its serial link silently dies (no port-disappear
    // event for the poll to catch). Watch the scale's connected state and tear
    // it down when it drops. Guarded on m_scale so it stays idempotent with the
    // poll-based unplug detection in onPollTimerTickAndroid.
    connect(m_scale, &ScaleDevice::connectedChanged, this, [this] {
        if (m_scale && !m_scale->isConnected()) {
            qWarning() << "[USB Scale] Connection dropped (device still enumerated)";
            emit logMessage(QStringLiteral("[USB Scale] Connection lost"));
            teardownConnectedScale();
        }
    });
#endif

    emit scaleConnectedChanged();
    emit scaleDiscovered(m_scale);
}

bool UsbScaleManager::teardownConnectedScale()
{
    if (!m_scale) return false;     // Already torn down — idempotent

    // Emit scaleLost() FIRST, while m_scale is still valid: main.cpp's handler
    // calls scale() to unwire the weight signals. Nulling before the emit would
    // make those disconnects dead (the signals would keep feeding a stale scale).
    emit scaleLost();

    m_scale->close();
    m_scale->deleteLater();
    m_scale = nullptr;

#ifdef Q_OS_ANDROID
    m_androidPermissionRequested = false;
    // Close the JNI USB connection (UsbDecentScale::close() delegates this to us)
    AndroidUsbScaleHelper::close();
#else
    m_confirmedPortName.clear();
#endif
    m_hasLoggedInitialPorts = false;

    setScaleAvailable(false);
    emit scaleConnectedChanged();
    return true;
}

void UsbScaleManager::disconnectScale()
{
    if (!m_scale) return;           // Nothing connected — no-op

    // Switching away to a BLE/WiFi scale: the USB scale is still physically
    // plugged in, so this is NOT a loss. Deliberately do NOT emit scaleLost()
    // and do NOT touch m_scaleAvailable — we only stop feeding the USB weight so
    // the new primary scale doesn't double-feed WeightProcessor. The USB entry
    // stays selectable; reselecting it calls connectToScale() again.
    qDebug() << "[USB Scale] Disconnecting (switching to another scale)";
    emit logMessage(QStringLiteral("[USB Scale] Disconnected (switched to another scale)"));

    m_scale->close();
    m_scale->deleteLater();
    m_scale = nullptr;

#ifdef Q_OS_ANDROID
    // Release the JNI connection; the device stays enumerated, so the next poll
    // re-probes and re-confirms availability without changing m_scaleAvailable here.
    AndroidUsbScaleHelper::close();
#endif

    emit scaleConnectedChanged();
}

// ---------------------------------------------------------------------------
// Polling control
// ---------------------------------------------------------------------------

void UsbScaleManager::startPolling()
{
    if (m_pollTimer.isActive()) return;

    qDebug() << "[USB Scale] Starting port polling every" << POLL_INTERVAL_MS << "ms";
    emit logMessage(QStringLiteral("[USB Scale] Polling started"));

    onPollTimerTick();
    m_pollTimer.start();
}

void UsbScaleManager::stopPolling()
{
    m_pollTimer.stop();
#ifdef Q_OS_ANDROID
    cleanupAndroidProbe(true);
#else
    cleanupProbe();
#endif
}

// ---------------------------------------------------------------------------
// Port polling — dispatches to platform-specific implementation
// ---------------------------------------------------------------------------

void UsbScaleManager::onPollTimerTick()
{
#ifdef Q_OS_ANDROID
    onPollTimerTickAndroid();
#else
    onPollTimerTickDesktop();
#endif
}

// ===========================================================================
// Android implementation
// ===========================================================================

#ifdef Q_OS_ANDROID

void UsbScaleManager::onPollTimerTickAndroid()
{
    bool devicePresent = AndroidUsbScaleHelper::hasDevice();

    // Log when a scale first appears
    if (!m_hasLoggedInitialPorts && devicePresent) {
        m_hasLoggedInitialPorts = true;
        QString info = AndroidUsbScaleHelper::deviceInfo();
        qDebug() << "[USB Scale] Device found:" << info;
        emit logMessage(QStringLiteral("[USB Scale] Device found: %1").arg(info));
    }

    // Check if connected scale disappeared
    if (m_scale && !devicePresent) {
        qWarning() << "[USB Scale] Connected scale disappeared";
        emit logMessage(QStringLiteral("[USB Scale] Scale disconnected"));
        // teardownConnectedScale() emits scaleLost() while m_scale is still
        // valid (so main.cpp can unwire weight signals), then deletes + nulls
        // it, releases the JNI connection, and clears availability.
        teardownConnectedScale();
        return;
    }

    // Available-but-not-connected scale unplugged: drop availability and release
    // the JNI connection held open since the probe confirmed.
    if (!m_scale && m_scaleAvailable && !devicePresent) {
        qDebug() << "[USB Scale] Available scale unplugged before connect";
        emit logMessage(QStringLiteral("[USB Scale] Scale unplugged"));
        AndroidUsbScaleHelper::close();
        m_androidPermissionRequested = false;
        m_hasLoggedInitialPorts = false;
        setScaleAvailable(false);
        return;
    }

    // Check if probing device disappeared
    if (m_androidProbing && !devicePresent) {
        qDebug() << "[USB Scale] Probing device disappeared";
        cleanupAndroidProbe(true);
        return;
    }

    if (m_scale) return;            // Already connected
    if (m_scaleAvailable) return;   // Already confirmed + listed; awaiting selection
    if (m_androidProbing) return;   // Already probing
    if (!devicePresent) {
        m_androidPermissionRequested = false;
        m_hasLoggedInitialPorts = false;
        return;
    }

    // Check permission
    if (!AndroidUsbScaleHelper::hasPermission()) {
        if (!m_androidPermissionRequested) {
            m_androidPermissionRequested = true;
            qDebug() << "[USB Scale] Requesting USB permission...";
            emit logMessage(QStringLiteral("[USB Scale] Requesting USB permission..."));
            AndroidUsbScaleHelper::requestPermission();
        }
        return;
    }

    // Device present with permission — probe it
    probeAndroid();
}

void UsbScaleManager::probeAndroid()
{
    m_androidProbing = true;
    m_probeBuffer.clear();

    QString info = AndroidUsbScaleHelper::deviceInfo();
    qDebug() << "[USB Scale] Probing:" << info;
    emit logMessage(QStringLiteral("[USB Scale] Probing: %1").arg(info));

    if (!AndroidUsbScaleHelper::open()) {
        QString err = AndroidUsbScaleHelper::lastError();
        qWarning() << "[USB Scale] Failed to open:" << err;
        emit logMessage(QStringLiteral("[USB Scale] Failed to open: %1").arg(err));
        m_androidProbing = false;
        return;
    }

    // Send init command to wake the scale: [0x03, 0x20, 0x01, 0x00, 0x00, 0x00, XOR]
    QByteArray initCmd(7, 0);
    initCmd[0] = 0x03;
    initCmd[1] = 0x20;
    initCmd[2] = 0x01;
    // Calculate XOR over bytes 0..5
    uint8_t xorVal = 0;
    for (int i = 0; i < 6; i++) xorVal ^= static_cast<uint8_t>(initCmd[i]);
    initCmd[6] = static_cast<char>(xorVal);

    AndroidUsbScaleHelper::write(initCmd);
    qDebug() << "[USB Scale] Sent init command";

    // Set up timeout
    m_androidProbeTimer = new QTimer(this);
    m_androidProbeTimer->setSingleShot(true);
    m_androidProbeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_androidProbeTimer, &QTimer::timeout, this, &UsbScaleManager::onAndroidProbeTimeout);

    // Poll for response every 50ms
    m_androidReadTimer = new QTimer(this);
    m_androidReadTimer->setInterval(50);
    connect(m_androidReadTimer, &QTimer::timeout, this, &UsbScaleManager::onAndroidProbeRead);

    m_androidProbeTimer->start();
    m_androidReadTimer->start();
}

void UsbScaleManager::onAndroidProbeRead()
{
    QByteArray data = AndroidUsbScaleHelper::readAvailable();
    if (data.isEmpty()) return;

    m_probeBuffer.append(data);

    // Look for a valid weight packet: 0x03 followed by 0xCE or 0xCA
    for (int i = 0; i <= m_probeBuffer.size() - 7; i++) {
        uint8_t b0 = static_cast<uint8_t>(m_probeBuffer[i]);
        uint8_t b1 = static_cast<uint8_t>(m_probeBuffer[i + 1]);

        if (b0 == 0x03 && (b1 == 0xCE || b1 == 0xCA)) {
            // Validate XOR checksum
            uint8_t xorVal = 0;
            for (int j = i; j < i + 6; j++) {
                xorVal ^= static_cast<uint8_t>(m_probeBuffer[j]);
            }
            if (xorVal == static_cast<uint8_t>(m_probeBuffer[i + 6])) {
                qDebug() << "[USB Scale] Scale confirmed! Got weight packet";
                emit logMessage(QStringLiteral("[USB Scale] Half Decent Scale confirmed"));

                // Stop probe timers but DON'T close — connectToScale() reuses
                // the JNI connection when the user selects the USB entry.
                cleanupAndroidProbe(false);

                // Record availability only — do NOT auto-connect. main.cpp lists
                // it as selectable and connects on selection / saved-primary.
                setScaleAvailable(true);
                return;
            }
        }
    }
}

void UsbScaleManager::onAndroidProbeTimeout()
{
    qDebug() << "[USB Scale] Probe timeout (received" << m_probeBuffer.size()
             << "bytes:" << m_probeBuffer.toHex() << ")";
    emit logMessage(QStringLiteral("[USB Scale] Probe timeout (%1 bytes)")
                        .arg(m_probeBuffer.size()));
    cleanupAndroidProbe(true);
}

void UsbScaleManager::cleanupAndroidProbe(bool closeConnection)
{
    if (m_androidProbeTimer) {
        m_androidProbeTimer->stop();
        m_androidProbeTimer->deleteLater();
        m_androidProbeTimer = nullptr;
    }

    if (m_androidReadTimer) {
        m_androidReadTimer->stop();
        m_androidReadTimer->deleteLater();
        m_androidReadTimer = nullptr;
    }

    if (closeConnection) {
        AndroidUsbScaleHelper::close();
    }

    m_probeBuffer.clear();
    m_androidProbing = false;
}

#endif // Q_OS_ANDROID

// ===========================================================================
// Desktop implementation
// ===========================================================================

#ifndef Q_OS_ANDROID

void UsbScaleManager::onPollTimerTickDesktop()
{
    const auto ports = QSerialPortInfo::availablePorts();

    // Log ports on first poll
    if (!m_hasLoggedInitialPorts && !ports.isEmpty()) {
        m_hasLoggedInitialPorts = true;
        for (const auto& port : ports) {
            if (port.vendorIdentifier() == VENDOR_ID_WCH
                && isScalePid(port.productIdentifier())) {
                qDebug() << "[USB Scale] Found scale port:" << port.portName()
                         << "VID:" << Qt::hex << port.vendorIdentifier()
                         << "PID:" << port.productIdentifier() << Qt::dec;
                emit logMessage(QStringLiteral("[USB Scale] Found %1 (VID:%2 PID:%3)")
                                    .arg(port.portName())
                                    .arg(port.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                                    .arg(port.productIdentifier(), 4, 16, QLatin1Char('0')));
            }
        }
    }

    // Build set of scale ports
    QSet<QString> currentPorts;
    QList<QSerialPortInfo> candidatePorts;

    for (const auto& port : ports) {
        if (port.vendorIdentifier() == VENDOR_ID_WCH
            && isScalePid(port.productIdentifier())) {
            currentPorts.insert(port.portName());

            if (!m_knownPorts.contains(port.portName())
                && !m_probePort) {
                candidatePorts.append(port);
            }
        }
    }

    // Check if connected scale port disappeared
    if (m_scale && !m_scale->isConnected()) {
        // Scale already disconnected itself (port error)
        qWarning() << "[USB Scale] Scale port lost";
        emit logMessage(QStringLiteral("[USB Scale] Scale disconnected"));
        // teardownConnectedScale() emits scaleLost() while m_scale is still
        // valid (so main.cpp can unwire weight signals), then deletes + nulls
        // it, clears m_confirmedPortName + m_hasLoggedInitialPorts, and drops
        // availability — matching the Android path and the available-unplugged
        // branch below.
        teardownConnectedScale();
    }

    // Available-but-not-connected scale unplugged: its confirmed port is gone.
    if (!m_scale && m_scaleAvailable && !m_confirmedPortName.isEmpty()
        && !currentPorts.contains(m_confirmedPortName)) {
        qDebug() << "[USB Scale] Available scale unplugged before connect:" << m_confirmedPortName;
        emit logMessage(QStringLiteral("[USB Scale] Scale unplugged"));
        m_confirmedPortName.clear();
        m_hasLoggedInitialPorts = false;
        setScaleAvailable(false);
    }

    // Check if probing port disappeared
    if (m_probePort && m_probingPortInfo.portName().isEmpty() == false
        && !currentPorts.contains(m_probingPortInfo.portName())) {
        qDebug() << "[USB Scale] Probing port disappeared";
        cleanupProbe();
    }

    m_knownPorts = currentPorts;

    // Probe new candidates (one at a time). Skip while a scale is already
    // connected OR available (single-scale invariant — don't grab a second).
    if (!m_probePort && !candidatePorts.isEmpty() && !m_scale && !m_scaleAvailable) {
        probePort(candidatePorts.first());
    }
}

void UsbScaleManager::probePort(const QSerialPortInfo& portInfo)
{
    if (m_probePort || m_scale) return;

    qDebug() << "[USB Scale] Probing port" << portInfo.portName();
    emit logMessage(QStringLiteral("[USB Scale] Probing %1").arg(portInfo.portName()));

    m_probingPortInfo = portInfo;
    m_probeBuffer.clear();

    m_probePort = new QSerialPort(this);
    m_probePort->setPortName(portInfo.portName());
    m_probePort->setBaudRate(115200);
    m_probePort->setDataBits(QSerialPort::Data8);
    m_probePort->setStopBits(QSerialPort::OneStop);
    m_probePort->setParity(QSerialPort::NoParity);
    m_probePort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_probePort->open(QIODevice::ReadWrite)) {
        qWarning() << "[USB Scale] Failed to open" << portInfo.portName()
                    << ":" << m_probePort->errorString();
        emit logMessage(QStringLiteral("[USB Scale] Failed to open %1: %2")
                            .arg(portInfo.portName(), m_probePort->errorString()));
        cleanupProbe();
        return;
    }

    m_probePort->setDataTerminalReady(false);
    m_probePort->setRequestToSend(false);

    connect(m_probePort, &QSerialPort::readyRead, this, &UsbScaleManager::onProbeReadyRead);

    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    m_probeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_probeTimer, &QTimer::timeout, this, &UsbScaleManager::onProbeTimeout);
    m_probeTimer->start();

    // Send init command: [0x03, 0x20, 0x01, 0x00, 0x00, 0x00, XOR]
    QByteArray initCmd(7, 0);
    initCmd[0] = 0x03;
    initCmd[1] = 0x20;
    initCmd[2] = 0x01;
    uint8_t xorVal = 0;
    for (int i = 0; i < 6; i++) xorVal ^= static_cast<uint8_t>(initCmd[i]);
    initCmd[6] = static_cast<char>(xorVal);

    m_probePort->write(initCmd);
}

void UsbScaleManager::onProbeReadyRead()
{
    if (!m_probePort) return;

    m_probeBuffer.append(m_probePort->readAll());

    // Look for valid weight packet: 0x03, 0xCE/0xCA, ..., XOR
    for (int i = 0; i <= m_probeBuffer.size() - 7; i++) {
        uint8_t b0 = static_cast<uint8_t>(m_probeBuffer[i]);
        uint8_t b1 = static_cast<uint8_t>(m_probeBuffer[i + 1]);

        if (b0 == 0x03 && (b1 == 0xCE || b1 == 0xCA)) {
            uint8_t xorVal = 0;
            for (int j = i; j < i + 6; j++) {
                xorVal ^= static_cast<uint8_t>(m_probeBuffer[j]);
            }
            if (xorVal == static_cast<uint8_t>(m_probeBuffer[i + 6])) {
                QString confirmedPort = m_probingPortInfo.portName();
                qDebug() << "[USB Scale] Scale confirmed on" << confirmedPort;
                emit logMessage(QStringLiteral("[USB Scale] Half Decent Scale found on %1")
                                    .arg(confirmedPort));

                // Close the probe port — connectToScale() reopens it on demand.
                cleanupProbe();

                // Record availability only — do NOT auto-connect. main.cpp lists
                // it as selectable and connects on selection / saved-primary.
                m_confirmedPortName = confirmedPort;
                setScaleAvailable(true);
                return;
            }
        }
    }
}

void UsbScaleManager::onProbeTimeout()
{
    if (!m_probePort) return;

    qDebug() << "[USB Scale] Probe timeout on" << m_probingPortInfo.portName()
             << "(received" << m_probeBuffer.size() << "bytes)";
    emit logMessage(QStringLiteral("[USB Scale] Probe timeout on %1")
                        .arg(m_probingPortInfo.portName()));

    cleanupProbe();
}

void UsbScaleManager::cleanupProbe()
{
    if (m_probeTimer) {
        m_probeTimer->stop();
        m_probeTimer->deleteLater();
        m_probeTimer = nullptr;
    }

    if (m_probePort) {
        if (m_probePort->isOpen()) {
            m_probePort->close();
        }
        m_probePort->deleteLater();
        m_probePort = nullptr;
    }

    m_probeBuffer.clear();
}

#endif // !Q_OS_ANDROID
