#include "usb/usbmanager.h"
#include "ble/de1logging.h"
#include "usb/serialtransport.h"

#ifdef Q_OS_ANDROID
#include "usb/androidusbhelper.h"
#endif

#include <QDebug>

// Alias the shared DE1 helpers — never copy a body. Tag "USB" for discovery;
// the link this hands off to logs under "Serial" (see serialtransport.cpp).
#define USB_LOG(msg)  DE1_LOG_TAGGED("USB", msg)
#define USB_INFO(msg) DE1_INFO_TAGGED("USB", msg)
#define USB_WARN(msg) DE1_WARN_TAGGED("USB", msg)

USBManager::USBManager(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(POLL_INTERVAL_MS);
    connect(&m_pollTimer, &QTimer::timeout, this, &USBManager::onPollTimerTick);
}

USBManager::~USBManager()
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

bool USBManager::isDe1Connected() const
{
    return m_transport != nullptr;
}

void USBManager::discardUnopenedTransport(SerialTransport* transport, const QString& portLabel)
{
    USB_WARN(QStringLiteral("Transport failed to open on %1 — discarding and re-arming "
                            "discovery").arg(portLabel));

    // deleteLater, not delete: open() may have emitted queued signals on it.
    transport->deleteLater();

#ifdef Q_OS_ANDROID
    // The probe deliberately left the JNI connection open for the transport to
    // adopt (cleanupAndroidProbe(false)). Nobody adopted it, so release it or
    // the next probe finds it already open and re-probes against a stale handle.
    AndroidUsbHelper::close();
#else
    // Drop the port from the known set so the next poll's candidate filter
    // (!m_knownPorts.contains(port)) treats it as new and re-probes it. Without
    // this the still-plugged-in DE1 is never looked at again.
    m_knownPorts.remove(portLabel);
#endif
}

QString USBManager::portName() const
{
    return m_connectedPortName;
}

QString USBManager::serialNumber() const
{
    return m_connectedSerialNumber;
}

// ---------------------------------------------------------------------------
// Polling control
// ---------------------------------------------------------------------------

void USBManager::startPolling()
{
    if (m_pollTimer.isActive()) {
        return;
    }

    USB_INFO(QStringLiteral("Polling started (every %1 ms)").arg(POLL_INTERVAL_MS));

    // Do an immediate poll, then start the timer
    onPollTimerTick();
    m_pollTimer.start();
}

void USBManager::stopPolling()
{
    m_pollTimer.stop();
#ifdef Q_OS_ANDROID
    cleanupAndroidProbe(true);
#else
    cleanupProbe();
#endif
}

void USBManager::disconnectUsb()
{
    if (!m_transport) return;

    USB_INFO(QStringLiteral("User-initiated USB disconnect"));

    // Prevent auto-reconnect while cable is still physically connected.
    // Flag is cleared when the device physically disappears (cable unplugged).
    m_userDisconnected = true;

    m_connectedPortName.clear();
    m_connectedSerialNumber.clear();

    // de1Lost handler (main.cpp) calls de1Device.disconnect() which calls
    // transport->disconnect() — this closes the USB connection. Safe to call
    // before deleteLater because signals are dispatched synchronously.
    m_transport->deleteLater();
    m_transport = nullptr;

    emit de1Lost();
    emit de1ConnectedChanged();
}

// ---------------------------------------------------------------------------
// Port polling — dispatches to platform-specific implementation
// ---------------------------------------------------------------------------

void USBManager::onPollTimerTick()
{
#ifdef Q_OS_ANDROID
    onPollTimerTickAndroid();
#else
    onPollTimerTickDesktop();
#endif
}

// ===========================================================================
// Android implementation — uses JNI to call Android USB Host API
// ===========================================================================

#ifdef Q_OS_ANDROID

void USBManager::onPollTimerTickAndroid()
{
    bool devicePresent = AndroidUsbHelper::hasDevice();

    // Log when a device first appears
    if (!m_hasLoggedInitialPorts && devicePresent) {
        m_hasLoggedInitialPorts = true;
        QString info = AndroidUsbHelper::deviceInfo();
        USB_INFO(QStringLiteral("Android USB device found: %1").arg(info));
    }

    // Check if connected device disappeared
    if (m_transport && !devicePresent) {
        USB_WARN(QStringLiteral("Connected USB device disappeared"));

        m_connectedPortName.clear();
        m_connectedSerialNumber.clear();
        m_transport = nullptr;
        m_androidPermissionRequested = false;

        emit de1Lost();
        emit de1ConnectedChanged();
        return;
    }

    // Check if probing device disappeared
    if (m_androidProbing && !devicePresent) {
        USB_LOG(QStringLiteral("Probing device disappeared, aborting probe"));
        cleanupAndroidProbe(true);
        return;
    }

    // User disconnected — wait for device to physically disappear before allowing reconnect
    if (m_userDisconnected) {
        if (!devicePresent) {
            m_userDisconnected = false;
            m_androidPermissionRequested = false;
            m_hasLoggedInitialPorts = false;
            USB_INFO(QStringLiteral("Device unplugged after user disconnect — ready to "
                                    "reconnect"));
        }
        return;
    }

    // Already connected — nothing to do
    if (m_transport) return;

    // Already probing — wait for result
    if (m_androidProbing) return;

    // No device — nothing to do
    if (!devicePresent) {
        // Reset flags so we can re-request/re-log when a device appears
        m_androidPermissionRequested = false;
        m_hasLoggedInitialPorts = false;
        return;
    }

    // Check permission
    if (!AndroidUsbHelper::hasPermission()) {
        if (!m_androidPermissionRequested) {
            m_androidPermissionRequested = true;
            USB_INFO(QStringLiteral("Requesting Android USB permission..."));
            AndroidUsbHelper::requestPermission();
        }
        return;
    }

    // Device present with permission — probe it
    probeAndroid();
}

void USBManager::probeAndroid()
{
    m_androidProbing = true;
    m_probeBuffer.clear();

    QString info = AndroidUsbHelper::deviceInfo();
    USB_LOG(QStringLiteral("Probing Android USB device: %1").arg(info));

    if (!AndroidUsbHelper::open()) {
        QString err = AndroidUsbHelper::lastError();
        USB_WARN(QStringLiteral("Failed to open Android USB: %1").arg(err));
        m_androidProbing = false;
        return;
    }

    // Send probe: subscribe to shot sample endpoint
    // If a DE1 is on the other end, it will respond with [M] data
    QByteArray probeCmd = QByteArrayLiteral("<+M>\n");
    int written = AndroidUsbHelper::write(probeCmd);
    USB_LOG(QStringLiteral("Sent probe <+M>, wrote %1 bytes").arg(written));

    // Set up timeout timer
    m_androidProbeTimer = new QTimer(this);
    m_androidProbeTimer->setSingleShot(true);
    m_androidProbeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_androidProbeTimer, &QTimer::timeout, this, &USBManager::onAndroidProbeTimeout);

    // Set up read poll timer (check for probe response every 50ms)
    m_androidReadTimer = new QTimer(this);
    m_androidReadTimer->setInterval(50);
    connect(m_androidReadTimer, &QTimer::timeout, this, &USBManager::onAndroidProbeRead);

    m_androidProbeTimer->start();
    m_androidReadTimer->start();
}

void USBManager::onAndroidProbeRead()
{
    QByteArray data = AndroidUsbHelper::readAvailable();
    if (data.isEmpty()) return;

    m_probeBuffer.append(data);

    USB_LOG(QStringLiteral("Probe received %1 bytes, total: %2 data: %3")
                .arg(data.size()).arg(m_probeBuffer.size())
                .arg(QString::fromLatin1(m_probeBuffer)));

    // Look for [M] in the response — confirms this is a DE1
    if (m_probeBuffer.contains("[M]")) {
        // Parse device info: "vendorId:productId:serialNumber"
        QString info = AndroidUsbHelper::deviceInfo();
        QStringList parts = info.split(QLatin1Char(':'));
        QString sn = (parts.size() > 2) ? parts[2] : QString();

        USB_INFO(QStringLiteral("DE1 found via Android USB (S/N: %1)")
                     .arg(sn.isEmpty() ? QStringLiteral("N/A") : sn));

        // Stop probe timers but DON'T close the connection — SerialTransport will use it
        cleanupAndroidProbe(false);

        // Create SerialTransport backed by the already-open Android USB
        // connection. Built locally and only published once it is actually
        // open — see discardUnopenedTransport() for why a dead m_transport is
        // worse than no transport at all.
        auto* transport = new SerialTransport(QStringLiteral("android-usb"), this);
        transport->setSerialNumber(sn);

        // Open starts the read timer and subscribes (connection already open via JNI)
        transport->open();

        if (!transport->isConnected()) {
            discardUnopenedTransport(transport, QStringLiteral("Android USB"));
            return;
        }

        m_transport = transport;
        m_connectedPortName = QStringLiteral("Android USB");
        m_connectedSerialNumber = sn;

        emit de1ConnectedChanged();
        emit de1Discovered(m_transport);
    }
}

void USBManager::onAndroidProbeTimeout()
{
    // DEBUG, unlike the desktop timeout below: Android has no PID filter — any
    // attached USB serial device gets probed — so "did not answer <+M>" is the
    // ordinary result for everything that is not a DE1, and warning on it would
    // fire on a user's keyboard dongle.
    USB_LOG(QStringLiteral("Android USB probe timeout (received %1 bytes: %2)")
                .arg(m_probeBuffer.size())
                .arg(QString::fromLatin1(m_probeBuffer.toHex())));
    cleanupAndroidProbe(true);
}

void USBManager::cleanupAndroidProbe(bool closeConnection)
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
        AndroidUsbHelper::close();
    }

    m_probeBuffer.clear();
    m_androidProbing = false;
}

#endif // Q_OS_ANDROID

// ===========================================================================
// Desktop implementation — uses QSerialPort / QSerialPortInfo
// ===========================================================================

#ifndef Q_OS_ANDROID

void USBManager::onPollTimerTickDesktop()
{
    const auto ports = QSerialPortInfo::availablePorts();

    // Log all ports on first poll for debugging (once only)
    if (!m_hasLoggedInitialPorts && !ports.isEmpty()) {
        m_hasLoggedInitialPorts = true;
        for (const auto& port : ports) {
            // One line carrying every field: the two this used to print
            // (a rich qDebug, a four-field logMessage) meant the log a user sent
            // in had the short one and the terminal had the long one.
            USB_LOG(QStringLiteral("Found port %1 VID:%2 PID:%3 desc:\"%4\" mfg:\"%5\" "
                                   "serial:%6 sysLoc:%7")
                        .arg(port.portName())
                        .arg(port.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                        .arg(port.productIdentifier(), 4, 16, QLatin1Char('0'))
                        .arg(port.description(), port.manufacturer(), port.serialNumber(),
                             port.systemLocation()));
        }
        // No "no ports found" line here: this block only runs when !ports.isEmpty(),
        // so the `if (ports.isEmpty())` that used to sit here could never be true.
    }

    // Build set of currently-present port names (filtered by VID)
    QSet<QString> currentPorts;
    QList<QSerialPortInfo> candidatePorts;

    for (const auto& port : ports) {
        // Filter by WCH vendor ID + DE1 product ID (CH9102).
        // PID filter prevents claiming the Half Decent Scale (PID 0x7523).
        if (port.vendorIdentifier() == VENDOR_ID_WCH
            && port.productIdentifier() == PRODUCT_ID_DE1) {
            currentPorts.insert(port.portName());

            // If this is a new port we haven't seen, it's a probe candidate
            if (!m_knownPorts.contains(port.portName())
                && !m_probingPorts.contains(port.portName())) {
                candidatePorts.append(port);
            }
        }
    }

    // Check if our connected port disappeared
    if (!m_connectedPortName.isEmpty() && !currentPorts.contains(m_connectedPortName)) {
        USB_WARN(QStringLiteral("Connected port %1 disappeared").arg(m_connectedPortName));

        m_connectedPortName.clear();
        m_connectedSerialNumber.clear();

        // Clear transport pointer but don't delete — DE1Device may own it via setTransport
        m_transport = nullptr;

        emit de1Lost();
        emit de1ConnectedChanged();
    }

    // User disconnected — wait for DE1 device to physically disappear before allowing reconnect
    if (m_userDisconnected) {
        if (currentPorts.isEmpty()) {
            m_userDisconnected = false;
            m_hasLoggedInitialPorts = false;
            USB_INFO(QStringLiteral("Device unplugged after user disconnect — ready to "
                                    "reconnect"));
        }
        m_knownPorts = currentPorts;
        return;
    }

    // Check if a port being probed disappeared
    if (m_probePort && !currentPorts.contains(m_probingPortInfo.portName())) {
        USB_LOG(QStringLiteral("Probing port %1 disappeared, aborting probe")
                    .arg(m_probingPortInfo.portName()));
        cleanupProbe();
    }

    // Update known ports
    m_knownPorts = currentPorts;

    // Probe new candidates (one at a time)
    if (!m_probePort && !candidatePorts.isEmpty() && !m_transport) {
        probePort(candidatePorts.first());
    }
}

void USBManager::probePort(const QSerialPortInfo& portInfo)
{
    if (m_probePort) {
        return;
    }

    if (m_transport) {
        return;
    }

    USB_LOG(QStringLiteral("Probing port %1 VID:%2 PID:%3 sysLoc:%4")
                .arg(portInfo.portName())
                .arg(portInfo.vendorIdentifier(), 4, 16, QLatin1Char('0'))
                .arg(portInfo.productIdentifier(), 4, 16, QLatin1Char('0'))
                .arg(portInfo.systemLocation()));

    m_probingPortInfo = portInfo;
    m_probingPorts.insert(portInfo.portName());
    m_probeBuffer.clear();

    m_probePort = new QSerialPort(this);
    m_probePort->setPortName(portInfo.portName());
    m_probePort->setBaudRate(115200);
    m_probePort->setDataBits(QSerialPort::Data8);
    m_probePort->setStopBits(QSerialPort::OneStop);
    m_probePort->setParity(QSerialPort::NoParity);
    m_probePort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_probePort->open(QIODevice::ReadWrite)) {
        USB_WARN(QStringLiteral("Failed to open %1 for probing: %2")
                     .arg(portInfo.portName(), m_probePort->errorString()));
        cleanupProbe();
        return;
    }

    m_probePort->setDataTerminalReady(false);
    m_probePort->setRequestToSend(false);

    connect(m_probePort, &QSerialPort::readyRead, this, &USBManager::onProbeReadyRead);

    m_probeTimer = new QTimer(this);
    m_probeTimer->setSingleShot(true);
    m_probeTimer->setInterval(PROBE_TIMEOUT_MS);
    connect(m_probeTimer, &QTimer::timeout, this, &USBManager::onProbeTimeout);
    m_probeTimer->start();

    m_probePort->write("<+M>\n");
}

void USBManager::onProbeReadyRead()
{
    if (!m_probePort) {
        return;
    }

    m_probeBuffer.append(m_probePort->readAll());

    if (m_probeBuffer.contains("[M]")) {
        QString confirmedPortName = m_probingPortInfo.portName();
        QString sn = m_probingPortInfo.serialNumber();

        USB_INFO(QStringLiteral("DE1 found on %1 (S/N: %2)")
                     .arg(confirmedPortName, sn.isEmpty() ? QStringLiteral("N/A") : sn));

        cleanupProbe();

        // Built locally and only published once it is actually open — see
        // discardUnopenedTransport() for why a dead m_transport is worse than
        // no transport at all.
        auto* transport = new SerialTransport(confirmedPortName, this);
        transport->setSerialNumber(sn);
        transport->open();

        if (!transport->isConnected()) {
            discardUnopenedTransport(transport, confirmedPortName);
            return;
        }

        m_transport = transport;
        m_connectedPortName = confirmedPortName;
        m_connectedSerialNumber = sn;

        emit de1ConnectedChanged();
        emit de1Discovered(m_transport);
    }
}

void USBManager::onProbeTimeout()
{
    if (!m_probePort) {
        return;
    }

    // WARN, unlike the Android timeout above: this port already passed the
    // VID/PID filter, so it advertises itself as DE1 hardware and then failed to
    // answer <+M>. That is a machine the user expects to be connected and is not.
    USB_WARN(QStringLiteral("Probe timeout on %1 — DE1 hardware did not answer <+M> "
                            "(received %2 bytes: %3)")
                 .arg(m_probingPortInfo.portName())
                 .arg(m_probeBuffer.size())
                 .arg(QString::fromLatin1(m_probeBuffer.toHex())));

    cleanupProbe();
}

void USBManager::cleanupProbe()
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

    m_probingPorts.remove(m_probingPortInfo.portName());
    m_probeBuffer.clear();
}

#endif // !Q_OS_ANDROID
