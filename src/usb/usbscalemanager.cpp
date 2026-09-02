#include "usb/usbscalemanager.h"
#include "usb/usbdecentscale.h"

#include "ble/scales/scalelogging.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbscalehelper.h"
#endif

#include <QDebug>

// Single source of the prefix, the severity and the emit — see the declarations
// in usbscalemanager.h for why nothing here logs any other way.
void UsbScaleManager::log(const QString& message)
{
    SCALE_LOG_TAGGED("USB Scale", message);
}

void UsbScaleManager::info(const QString& message)
{
    SCALE_INFO_TAGGED("USB Scale", message);
}

void UsbScaleManager::warn(const QString& message)
{
    SCALE_WARN_TAGGED("USB Scale", message);
}

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
        log(QStringLiteral("connectToScale() called but no scale available"));
        return;
    }

#ifdef Q_OS_ANDROID
    // Re-validate presence; the JNI side still owns the (probe-opened) connection.
    if (!AndroidUsbScaleHelper::hasDevice()) {
        warn(QStringLiteral("connectToScale(): device no longer present"));
        setScaleAvailable(false);
        return;
    }
    if (!AndroidUsbScaleHelper::hasPermission()) {
        warn(QStringLiteral("connectToScale(): no USB permission"));
        return;
    }

    info(QStringLiteral("Connecting (Android)"));

    m_scale = new UsbDecentScale(this);
    m_scale->open();
#else
    if (m_confirmedPortName.isEmpty()) {
        warn(QStringLiteral("connectToScale(): no confirmed port"));
        return;
    }

    info(QStringLiteral("Connecting on %1").arg(m_confirmedPortName));

    m_scale = new UsbDecentScale(this);
    m_scale->open(m_confirmedPortName);
#endif

    // open() can fail (port grabbed by another app, JNI open refused). If it did,
    // the scale never connected — don't emit scaleDiscovered (that would wire a
    // dead object and, on desktop, leave m_confirmedPortName/m_knownPorts pinned
    // so polling never re-probes the still-plugged-in scale). Instead, tear the
    // half-open scale down and re-enable recovery so the next poll re-confirms it.
    if (!m_scale->isConnected()) {
        warn(QStringLiteral("open() failed — scale did not connect, retrying discovery"));
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

    // Unconditional, not Android-only: with USB scanning off (the default) desktop
    // has no poll and no hotplug, so a manual "Scan for Devices" was the only
    // trigger left. Measured on macOS before this: port died t=253.7, scale still
    // held until the scan at t=272.9 — 19 s with neither the USB scale nor its
    // FlowScale fallback.
    connect(m_scale, &ScaleDevice::connectedChanged, this, [this] {
        if (m_scale && !m_scale->isConnected()) {
            warn(QStringLiteral("Connection lost — scale reported disconnected"));
            teardownConnectedScale();
        }
    });

    emit scaleConnectedChanged();
    emit scaleDiscovered(m_scale);
}

bool UsbScaleManager::teardownConnectedScale()
{
    if (!m_scale) return false;     // Already torn down — idempotent

    // Drop the Android connectedChanged watchdog (wired in connectToScale) BEFORE
    // close() below: close() emits connectedChanged synchronously, which would
    // otherwise re-enter this function and double-free m_scale. Load-bearing on
    // EVERY platform — the watchdog stopped being Android-only when USB scanning
    // became opt-in and desktop lost the poll that used to catch an unplug.
    disconnect(m_scale, &ScaleDevice::connectedChanged, this, nullptr);

    // Emit scaleLost() FIRST, while m_scale is still valid: main.cpp's handler
    // calls scale() to unwire the weight signals. Nulling before the emit would
    // make those disconnects dead (the signals would keep feeding a stale scale).
    emit scaleLost();

    m_scale->close();
    m_scale->deleteLater();
    m_scale = nullptr;

#ifdef Q_OS_ANDROID
    m_androidPermissionRequested = false;
    // Close the JNI USB connection — UsbDecentScale::close() deliberately does
    // NOT (UsbScaleManager owns the JNI connection lifecycle).
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
    info(QStringLiteral("Disconnected (switched to another scale)"));

    // Drop the Android connectedChanged watchdog before close() — same re-entrancy
    // hazard as teardownConnectedScale(): close() emits connectedChanged, which the
    // watchdog would turn into a teardownConnectedScale() re-entry. On every
    // platform now, not just Android — see connectToScale().
    disconnect(m_scale, &ScaleDevice::connectedChanged, this, nullptr);

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

    info(QStringLiteral("Polling started (every %1 ms)").arg(POLL_INTERVAL_MS));

    onPollTimerTick();
    m_pollTimer.start();
}

void UsbScaleManager::onHotplugEvent()
{
    // info(), not log(): the views default to minLevel INFO.
    info(QStringLiteral("Hotplug event — running a probe pass now"));
    onPollTimerTick();
}

void UsbScaleManager::probeNow()
{
    log(QStringLiteral("On-demand probe requested (scan)"));
    m_scanProbePending = true;

#ifndef Q_OS_ANDROID
    // Forget which ports have already been probed, so a user-initiated scan
    // RETRIES one that previously failed to answer.
    //
    // The background poll deliberately probes each port once — re-probing every
    // 2 s would hammer a device that isn't a scale. But "Scan for Devices" is an
    // explicit request to look again, and a port that timed out earlier (a scale
    // still booting, a cable reseated, a transient) would otherwise be skipped
    // for the rest of the session with "nothing new to probe".
    //
    // Not cleared when a scale is already connected: m_confirmedPortName is in
    // this set and re-probing the live port would disturb the connection.
    if (!m_scale)
        m_knownPorts.clear();
#endif

    onPollTimerTick();

    // A pass does not always start a probe: onPollTimerTick only probes when
    // there is an un-probed candidate port and no scale is already connected.
    // When it didn't, there is nothing to wait for — say so rather than leaving
    // the scan indicator pinned on a probe that never began.
    //
    // An earlier version emitted probeFinished() unconditionally right here,
    // which made the whole thing inert: the signal round-tripped through
    // BLEManager synchronously and cleared the in-flight flag before the
    // composite `scanning` property was ever read, so USB never actually
    // contributed to it.
    const bool probeRunning =
#ifdef Q_OS_ANDROID
        m_androidProbeTimer != nullptr;
#else
        m_probePort != nullptr;
#endif
    if (probeRunning) {
        log(QStringLiteral("Probing for USB scale (scan)"));
    } else {
        log(QStringLiteral("Scan: nothing new to probe"));
        finishScanProbe();
    }
}

void UsbScaleManager::finishScanProbe()
{
    if (!m_scanProbePending)
        return;
    m_scanProbePending = false;
    emit probeFinished();
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
        QString deviceInfo = AndroidUsbScaleHelper::deviceInfo();
        info(QStringLiteral("Device found: %1").arg(deviceInfo));
    }

    // Check if connected scale disappeared
    if (m_scale && !devicePresent) {
        warn(QStringLiteral("Scale disconnected — connected scale disappeared"));
        // teardownConnectedScale() emits scaleLost() while m_scale is still
        // valid (so main.cpp can unwire weight signals), then deletes + nulls
        // it, releases the JNI connection, and clears availability.
        teardownConnectedScale();
        return;
    }

    // Available-but-not-connected scale unplugged: drop availability and release
    // the JNI connection held open since the probe confirmed.
    if (!m_scale && m_scaleAvailable && !devicePresent) {
        info(QStringLiteral("Scale unplugged before connect"));
        AndroidUsbScaleHelper::close();
        m_androidPermissionRequested = false;
        m_hasLoggedInitialPorts = false;
        setScaleAvailable(false);
        return;
    }

    // Check if probing device disappeared
    if (m_androidProbing && !devicePresent) {
        log(QStringLiteral("Probing device disappeared"));
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
            log(QStringLiteral("Requesting USB permission..."));
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
    log(QStringLiteral("Probing: %1").arg(info));

    if (!AndroidUsbScaleHelper::open()) {
        QString err = AndroidUsbScaleHelper::lastError();
        warn(QStringLiteral("Failed to open: %1").arg(err));
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
    log(QStringLiteral("Sent init command"));

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
                info(QStringLiteral("Half Decent Scale confirmed (weight packet received)"));

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
    log(QStringLiteral("Probe timeout (%1 bytes: %2)")
            .arg(m_probeBuffer.size())
            .arg(QString::fromLatin1(m_probeBuffer.toHex())));
    cleanupAndroidProbe(true);
}

void UsbScaleManager::cleanupAndroidProbe(bool closeConnection)
{
    // Android's equivalent of cleanupProbe(): the single point a probe pass ends.
    finishScanProbe();

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
                log(QStringLiteral("Found %1 (VID:%2 PID:%3)")
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
        warn(QStringLiteral("Scale disconnected — port lost"));
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
        info(QStringLiteral("Scale unplugged before connect (%1)").arg(m_confirmedPortName));
        m_confirmedPortName.clear();
        m_hasLoggedInitialPorts = false;
        setScaleAvailable(false);
    }

    // Check if probing port disappeared
    if (m_probePort && m_probingPortInfo.portName().isEmpty() == false
        && !currentPorts.contains(m_probingPortInfo.portName())) {
        log(QStringLiteral("Probing port disappeared"));
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

    log(QStringLiteral("Probing %1").arg(portInfo.portName()));

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
        warn(QStringLiteral("Failed to open %1: %2")
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

    // The scale ALWAYS emits a plain-text weight line —
    // "<millis> Weight: <value>" from sendUsbTextWeight(), which is ungated in
    // firmware. The binary packet below is gated on b_usbweight_enabled, which
    // defaults to false and is only turned on by the 0x20 command we send on
    // probe. A scale that is busy serving BLE or WiFi does not act on that
    // command, so binary never arrives and the probe used to time out — the
    // scale was plugged in, streaming, and reported as absent.
    //
    // Accepting the text line fixes that: the scale is listed on all three
    // transports regardless of which one is currently in use, and the user picks
    // one. Binary streaming is (re)requested by connectToScale() when USB is
    // actually selected, which is the point at which it matters.
    if (m_probeBuffer.contains(" Weight: ")) {
        const QString confirmedPort = m_probingPortInfo.portName();
        info(QStringLiteral("Half Decent Scale found on %1 (text weight stream)")
                .arg(confirmedPort));
        cleanupProbe();
        m_confirmedPortName = confirmedPort;
        setScaleAvailable(true);
        return;
    }

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
                info(QStringLiteral("Half Decent Scale found on %1 (binary weight packet)")
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

    log(QStringLiteral("Probe timeout on %1 (received %2 bytes)")
            .arg(m_probingPortInfo.portName())
            .arg(m_probeBuffer.size()));

    // Dump what actually arrived when the port talked but we rejected all of
    // it. "Received N bytes" alone cannot distinguish a device that is not a
    // scale from a scale whose framing we fail to parse — and the latter looks
    // identical to a dead port from the user's side.
    if (!m_probeBuffer.isEmpty()) {
        const QByteArray head = m_probeBuffer.left(64);
        log(QStringLiteral("Rejected %1 bytes; first %2: %3")
                .arg(m_probeBuffer.size())
                .arg(head.size())
                .arg(QString::fromLatin1(head.toHex(' '))));
    }

    cleanupProbe();
}

void UsbScaleManager::cleanupProbe()
{
    // The one place a desktop probe pass ends — success, failure or timeout.
    // A scan is waiting on this, not on the pass merely having been started.
    finishScanProbe();

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
